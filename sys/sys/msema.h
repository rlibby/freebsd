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

#ifdef _KERNEL
#if 0
#include <sys/types.h>
#include <sys/systm.h>
#endif
#include <machine/atomic.h>

struct msema {
	uint64_t bits;
};

/* Operation flags. */
#define	MSEMA_ANY	0x01 /* Allow short returns, don't pass the limit */
#define	MSEMA_RELAXED	0x02 /* One request may pass the limit by < count */
#define	MSEMA_SIGWAIT	0x04 /* Sleep may be interrupted by signals */
#define	MSEMA_ONESLEEP	0x08 /* Sleep up to once */
#define	MSEMA_PRIV_REWAIT 0x10 /* Skip the fast path after a failed trywait */
#define	MSEMA_API_FLAGS	(MSEMA_ANY | MSEMA_RELAXED | MSEMA_SIGWAIT \
    | MSEMA_ONESLEEP)

/*
 * Macros for interpreting the msema.bits field.  20 bits of sleeper count,
 * 1 sign bit, and 43 bits of resource count.
 *
 * XXX Alternately we could make it [count | sleepers] because we don't need
 * modular arithmetic for sleepers (never less than 0).  We'd need to ensure
 * sign extension when extracting the count field.  I'm not sure whether that
 * would lead to a simpler implementation.
 */
#define	MSEMA_BITS_SLEEPER_SHIFT	44
#define	MSEMA_BITS_SLEEPERS_MAX	((1LL << (64 - MSEMA_BITS_SLEEPER_SHIFT)) - 1)
#define	MSEMA_BITS_SLEEPERS(x)	((x) >> MSEMA_BITS_SLEEPER_SHIFT)
#define	MSEMA_BITS_ONE_SLEEPER	(1LL << MSEMA_BITS_SLEEPER_SHIFT)
#define	MSEMA_BITS_ZERO_COUNT	(1LL << (MSEMA_BITS_SLEEPER_SHIFT - 1))
#define	MSEMA_BITS_COUNT_MASK	((1LL << MSEMA_BITS_SLEEPER_SHIFT) - 1)
#define	MSEMA_BITS_OFFSET_COUNT(x)	((x) & MSEMA_BITS_COUNT_MASK)

/* API */
inline void msema_adjust(struct msema *msema, int count);
inline int msema_count(struct msema *msema);
inline void msema_destroy(struct msema *msema);
inline void msema_init(struct msema *msema, int count);
inline void msema_post(struct msema *msema, int count);
inline void msema_post_one(struct msema *msema);
inline int msema_rewait(struct msema *msema, int count, int flags, int pri,
    const char *wmesg, int *sleeps_out);
inline int msema_rewait_full(struct msema *msema, int count, int *count_out,
    int flags, int pri, const char *wmesg, int timo, int *sleeps_out);
inline int msema_trywait(struct msema *msema, int count, int flags);
inline int msema_trywait_any(struct msema *msema, int count);
inline int msema_trywait_one(struct msema *msema);
inline int msema_trywait_relaxed(struct msema *msema, int count);
inline int msema_wait(struct msema *msema, int count, int flags, int pri,
    const char *wmesg, int *sleeps_out);
inline int msema_wait_any(struct msema *msema, int count, int pri,
    const char *wmesg, int *sleeps_out);
inline int msema_wait_full(struct msema *msema, int count, int *count_out,
    int flags, int pri, const char *wmesg, int timo, int *sleeps_out);
inline void msema_wait_one(struct msema *msema, int pri, const char *wmesg,
    int *sleeps_out);
inline void msema_wait_relaxed(struct msema *msema, int count, int pri,
    const char *wmesg, int *sleeps_out);

extern void _msema_post_noinline(struct msema *msema, int count);
extern int _msema_wait_hard(struct msema *msema, int count, int *count_out,
    int flags, int pri, const char *wmesg, int timo, int *sleeps_out);

/* Inline procedures */

static inline void
_msema_check_count(uint64_t cnt)
{
	if (__predict_false(MSEMA_BITS_SLEEPERS(cnt)) != 0)
		panic("msema count overflow: %#jx", (uintmax_t)cnt);
}

inline void
msema_init(struct msema *msema, int count)
{
	uint64_t bits;

	bits = MSEMA_BITS_ZERO_COUNT + count;
	_msema_check_count(bits);
	atomic_store_64(&msema->bits, bits);
}

inline void
msema_destroy(struct msema *msema)
{
	uint64_t bits;

	bits = atomic_load_64(&msema->bits);
	if (MSEMA_BITS_SLEEPERS(bits) != 0)
		panic("msema_destroy with sleepers: %#jx", (uintmax_t)bits);
}

inline int
msema_count(struct msema *msema)
{
	uint64_t oldcnt;

	oldcnt = MSEMA_BITS_OFFSET_COUNT(atomic_load_64(msema));
	if (oldcnt <= MSEMA_BITS_ZERO_COUNT)
		return (0);
	return oldcnt - MSEMA_BITS_ZERO_COUNT;
}

inline void
msema_post(struct msema *msema, int count)
{
	uint64_t old;

	MPASS(count > 0);

	/*
	 * In the common case we either have no sleepers or
	 * are still over the limit and can just return.
	 */
	old = atomic_fetchadd_64(&msema->bits, count);
	_msema_check_count(MSEMA_BITS_OFFSET_COUNT(old) + count);
	if (__predict_true(MSEMA_BITS_SLEEPERS(old) == 0 ||
	    MSEMA_BITS_OFFSET_COUNT(old) + count <= MSEMA_BITS_ZERO_COUNT))
		return;

	/*
	 * Moderate the rate of wakeups.  Sleepers will continue
	 * to generate wakeups if necessary.
	 */
	wakeup_one(&msema->bits);
}

inline void
msema_post_one(struct msema *msema)
{
	msema_post(msema, 1);
}

inline void
msema_adjust(struct msema *msema, int count)
{
	uint64_t old;

	if (count > 0) {
		msema_post(msema, count);
	} else if (count < 0) {
		old = atomic_fetchadd_64(&msema->bits, count);
		_msema_check_count(MSEMA_BITS_OFFSET_COUNT(old) + count);
	}
}

/* Fast path.  Return value of 0 means that count are temporarily held. */
static inline int
_msema_trywait_common(struct msema *msema, int count, int flags)
{
	uint64_t old, oldcnt, newcnt;

	/*
	 * We expect normal allocations to succeed with a simple
	 * fetchadd.
	 */
	old = atomic_fetchadd_64(&msema->bits, -count);
	KASSERT(old != 0, ("msema uninitialized"));
	oldcnt = MSEMA_BITS_OFFSET_COUNT(old);
	newcnt = oldcnt - count;
	if (__predict_true(newcnt >= MSEMA_BITS_ZERO_COUNT))
		return (count);

	if (oldcnt > MSEMA_BITS_ZERO_COUNT) {
		/*
		 * With MSEMA_RELAXED, we can acquire the requested resource
		 * count as long as there were any at all available.  The
		 * resource count may then be left negative.
		 */
		if ((flags & MSEMA_RELAXED) != 0)
			return (count);
		/*
		 * With MSEMA_ANY, if we had some resource and no sleepers just
		 * return the truncated value.  We have to release the excess
		 * resource though because that may wake sleepers who weren't
		 * woken because we were temporarily over the limit.
		 */
		if ((flags & MSEMA_ANY) != 0) {
			_msema_post_noinline(msema, count -
			    (oldcnt - MSEMA_BITS_ZERO_COUNT));
			return (oldcnt - MSEMA_BITS_ZERO_COUNT);
		}
	}

	_msema_check_count(newcnt);

	return (0);
}

inline int
msema_trywait(struct msema *msema, int count, int flags)
{
	int n;

	MPASS(count > 0);
	MPASS((flags & ~(MSEMA_API_FLAGS)) == 0);

	n = _msema_trywait_common(msema, count, flags);
	if (__predict_false(n == 0))
		_msema_post_noinline(msema, count);
	return (n);
}

inline int
msema_trywait_any(struct msema *msema, int count)
{
	return (msema_trywait(msema, count, MSEMA_ANY));
}

inline int
msema_trywait_one(struct msema *msema)
{
	return (msema_trywait(msema, 1, 0));
}

inline int
msema_trywait_relaxed(struct msema *msema, int count)
{
	return (msema_trywait(msema, count, MSEMA_ANY));
}

inline int
msema_wait_full(struct msema *msema, int count, int *count_out, int flags,
    int pri, const char *wmesg, int timo, int *sleeps_out)
{
	int n;

	MPASS(count > 0);
	MPASS(count == 1 || (flags & MSEMA_ANY | MSEMA_RELAXED) != 0);
	MPASS((flags & ~(MSEMA_API_FLAGS)) == 0);

	n = _msema_trywait_common(msema, count, flags);
	if (__predict_true(n != 0)) {
		*count_out = n;
		return (0);
	}
	return (_msema_wait_hard(msema, count, count_out, flags, pri, wmesg,
	    timo, sleeps_out));
}

inline int
msema_wait(struct msema *msema, int count, int flags, int pri,
    const char *wmesg, int *sleeps_out)
{
	int ret, n;

	MPASS((flags & (MSEMA_ONESLEEP | MSEMA_SIGWAIT)) == 0);

	ret = msema_wait_full(msema, count, &n, flags, pri, wmesg, 0,
	    sleeps_out);
	MPASS(ret == 0);
	MPASS(n > 0);
	MPASS((flags & MSEMA_ANY) != 0 || n == count);
	return (n);
}

inline int
msema_wait_any(struct msema *msema, int count, int pri, const char *wmesg,
    int *sleeps_out)
{
	int ret, n;

	ret = msema_wait_full(msema, count, &n, MSEMA_ANY, pri, wmesg, 0,
	    sleeps_out);
	MPASS(ret == 0);
	MPASS(n > 0);
	return (n);
}

inline void
msema_wait_one(struct msema *msema, int pri, const char *wmesg,
    int *sleeps_out)
{
	int ret, n;

	ret = msema_wait_full(msema, 1, &n, 0, pri, wmesg, 0, sleeps_out);
	MPASS(ret == 0);
	MPASS(n == 1);
}

inline void
msema_wait_relaxed(struct msema *msema, int count, int pri, const char *wmesg,
    int *sleeps_out)
{
	int ret, n;

	ret = msema_wait_full(msema, count, &n, MSEMA_RELAXED, pri, wmesg, 0,
	    sleeps_out);
	MPASS(ret == 0);
	MPASS(n == count);
}

inline int
msema_rewait_full(struct msema *msema, int count, int *count_out,
    int flags, int pri, const char *wmesg, int timo, int *sleeps_out)
{
	MPASS(count > 0);
	MPASS(count == 1 || (flags & MSEMA_ANY | MSEMA_RELAXED) != 0);
	MPASS((flags & ~(MSEMA_API_FLAGS)) == 0);

	return (_msema_wait_hard(msema, count, count_out,
	    flags | MSEMA_PRIV_REWAIT, pri, wmesg, timo, sleeps_out));
}

inline int
msema_rewait(struct msema *msema, int count, int flags, int pri,
    const char *wmesg, int *sleeps_out)
{
	int ret, n;

	MPASS((flags & (MSEMA_ONESLEEP | MSEMA_SIGWAIT)) == 0);

	ret = msema_rewait_full(msema, count, &n, flags, pri, wmesg, 0,
	    sleeps_out);
	MPASS(ret == 0);
	MPASS(n > 0);
	MPASS((flags & MSEMA_ANY) != 0 || n == count);
	return (n);
}
#endif
