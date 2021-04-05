/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019-2020 Jeffrey Roberson <jeff@FreeBSD.org>
 * Copyright (c) 2021 Ryan Libby <rlibby@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/msema.h>
#include <sys/sleepqueue.h>
#include <machine/atomic.h>

/* External linkage for header inlines. */
extern inline void msema_adjust(struct msema *msema, int count);
extern inline int msema_count(struct msema *msema);
extern inline void msema_destroy(struct msema *msema);
extern inline void msema_init(struct msema *msema, int count);
extern inline void msema_post(struct msema *msema, int count);
extern inline void msema_post_one(struct msema *msema);
extern inline int msema_rewait(struct msema *msema, int count, int flags,
    int pri, const char *wmesg, int *sleeps_out);
extern inline int msema_rewait_full(struct msema *msema, int count,
    int *count_out, int flags, int pri, const char *wmesg, int timo,
    int *sleeps_out);
extern inline int msema_trywait(struct msema *msema, int count, int flags);
extern inline int msema_trywait_any(struct msema *msema, int count);
extern inline int msema_trywait_one(struct msema *msema);
extern inline int msema_trywait_relaxed(struct msema *msema, int count);
extern inline int msema_wait(struct msema *msema, int count, int flags,
    int pri, const char *wmesg, int *sleeps_out);
extern inline int msema_wait_any(struct msema *msema, int count, int pri,
    const char *wmesg, int *sleeps_out);
extern inline int msema_wait_full(struct msema *msema, int count,
    int *count_out, int flags, int pri, const char *wmesg, int timo,
    int *sleeps_out);
extern inline void msema_wait_one(struct msema *msema, int pri,
    const char *wmesg, int *sleeps_out);
extern inline void msema_wait_relaxed(struct msema *msema, int count, int pri,
    const char *wmesg, int *sleeps_out);

int
_msema_wait_hard(struct msema *msema, int count, int *count_out,
    int flags, int pri, const char *wmesg, int timo, int *sleeps_out)
{
	uint64_t old, new, oldcnt, newcnt;
	int ret;

	MPASS(count > 0);
	MPASS(count == 1 || (flags & MSEMA_ANY | MSEMA_RELAXED) != 0);

	*count_out = 0;

	/*
	 * The hard case.  We're going to sleep because there were existing
	 * sleepers or because we ran out of items.  This routine enforces
	 * fairness by keeping fifo order.
	 *
	 * First release our ill gotten gains.
	 */
	if ((flags & MSEMA_PRIV_REWAIT) == 0)
		msema_post(msema, count);
	for (;;) {
		/*
		 * We need to allocate an item or set ourself as a sleeper
		 * while the sleepq lock is held to avoid wakeup races.
		 */
		sleepq_lock(&msema->bits);
		old = atomic_load_64(&msema->bits);
		do {
			MPASS(MSEMA_BITS_SLEEPERS(old) <
			    MSEMA_BITS_SLEEPERS_MAX);
			if (MSEMA_BITS_SLEEPERS(old) != 0 ||
			    MSEMA_BITS_OFFSET_COUNT(old) <=
			    MSEMA_BITS_ZERO_COUNT) {
				/* Sleepers or no resources.  Get in line. */
				new = old + MSEMA_BITS_ONE_SLEEPER;
			} else if (MSEMA_BITS_OFFSET_COUNT(old) >=
			    MSEMA_BITS_ZERO_COUNT + count ||
			    (flags & MSEMA_RELAXED) != 0) {
				/* Enough resources, or any and relaxed. */
				new = old - count;
			} else {
				/* Any resources. */
				MPASS((flags & MSEMA_ANY) != 0);
				new = old - (MSEMA_BITS_OFFSET_COUNT(old) -
				    MSEMA_BITS_ZERO_COUNT);
			}
		} while (atomic_fcmpset_64(&msema->bits, &old, new) == 0);

		/* We may have successfully allocated under the sleepq lock. */
		if (MSEMA_BITS_SLEEPERS(new) == 0) {
			sleepq_release(&msema->bits);
			*count_out = new - old;
			return (0);
		}

		if (sleeps_out != NULL)
			(*sleeps_out)++;

		/*
		 * We have added ourselves as a sleeper.  The sleepq lock
		 * protects us from wakeup races.  Sleep now and then retry.
		 */
		sleepq_add(&msema->bits, NULL, wmesg, SLEEPQ_SLEEP,
		    (flags & MSEMA_SIGWAIT) != 0 ? SLEEPQ_INTERRUPTIBLE : 0);
		if (timo != 0) {
			sleepq_set_timeout(&msema->bits, timo);
			if ((flags & MSEMA_SIGWAIT) != 0) {
				ret = sleepq_timedwait_sig(&msema->bits, pri);
			} else {
				ret = sleepq_timedwait(&msema->bits, pri);
			}
		} else {
			if ((flags & MSEMA_SIGWAIT) != 0) {
				ret = sleepq_wait_sig(&msema->bits, pri);
			} else {
				sleepq_wait(&msema->bits, pri);
				ret = 0;
			}
		}
		if (ret != 0)
			return (ret);

		/*
		 * After wakeup, remove ourselves as a sleeper and try
		 * again.  We no longer have the sleepq lock for protection.
		 *
		 * Subract ourselves as a sleeper while attempting to add
		 * our count.
		 */
		old = atomic_fetchadd_64(&msema->bits,
		    -(MSEMA_BITS_ONE_SLEEPER + count));
		/* We're no longer a sleeper. */
		old -= MSEMA_BITS_ONE_SLEEPER;

		/*
		 * If we're still at the limit, restart.  Notably do not
		 * block on other sleepers.
		 */
		oldcnt = MSEMA_BITS_OFFSET_COUNT(old);
		newcnt = oldcnt - count;
		_msema_check_count(newcnt);
		if (oldcnt <= MSEMA_BITS_ZERO_COUNT ||
		    ((flags & (MSEMA_ANY | MSEMA_RELAXED)) == 0 &&
		     newcnt < MSEMA_BITS_ZERO_COUNT)) {
			msema_post(msema, count);
			if ((flags & MSEMA_ONESLEEP) != 0)
				return (EWOULDBLOCK);
			continue;
		}

		if (newcnt > MSEMA_BITS_ZERO_COUNT) {
			/* There are still more resources. */
			if (MSEMA_BITS_SLEEPERS(old) != 0)
				wakeup_one(&msema->bits);
			*count_out = count;
		} else if (newcnt == MSEMA_BITS_ZERO_COUNT ||
		    (flags & MSEMA_RELAXED) != 0) {
			/* Exactly enough, or we are relaxed and a bit over. */
			*count_out = count;
		} else {
			/* Truncate to available and release the overage. */
			MPASS((flags & MSEMA_ANY) != 0);
			*count_out = oldcnt - MSEMA_BITS_ZERO_COUNT;
			msema_post(msema, count - *count_out);
		}
		return (0);
	}
}

void
_msema_post_noinline(struct msema *msema, int count)
{
	msema_post(msema, count);
}
