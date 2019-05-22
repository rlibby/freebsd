/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2003, 2004, 2005 Jeffrey Roberson <jeff@FreeBSD.org>
 * Copyright (c) 2004, 2005 Bosko Milekic <bmilekic@FreeBSD.org>
 * All rights reserved.
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

/*
 * uma_dbg.c	Debugging features for UMA users
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_stack.h"
#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitset.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#include <sys/stack.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/uma.h>
#include <vm/uma_int.h>
#include <vm/uma_dbg.h>
#include <vm/memguard.h>

static const uint32_t uma_junk = 0xdeadc0de;

/*
 * Checks an item to make sure it hasn't been overwritten since it was freed,
 * prior to subsequent reallocation.
 *
 * Complies with standard ctor arg/return
 */
int
trash_ctor(void *mem, int size, void *arg, int flags)
{
	int cnt;
	uint32_t *p;

#ifdef DEBUG_MEMGUARD
	if (is_memguard_addr(mem))
		return (0);
#endif

	cnt = size / sizeof(uma_junk);

	for (p = mem; cnt > 0; cnt--, p++)
		if (*p != uma_junk) {
#ifdef INVARIANTS
			panic("Memory modified after free %p(%d) val=%x @ %p\n",
			    mem, size, *p, p);
#else
			printf("Memory modified after free %p(%d) val=%x @ %p\n",
			    mem, size, *p, p);
#endif
			return (0);
		}
	return (0);
}

/*
 * Fills an item with predictable garbage
 *
 * Complies with standard dtor arg/return
 *
 */
void
trash_dtor(void *mem, int size, void *arg)
{
	int cnt;
	uint32_t *p;

#ifdef DEBUG_MEMGUARD
	if (is_memguard_addr(mem))
		return;
#endif

	cnt = size / sizeof(uma_junk);

	for (p = mem; cnt > 0; cnt--, p++)
		*p = uma_junk;
}

/*
 * Fills an item with predictable garbage
 *
 * Complies with standard init arg/return
 *
 */
int
trash_init(void *mem, int size, int flags)
{
	trash_dtor(mem, size, NULL);
	return (0);
}

/*
 * Checks an item to make sure it hasn't been overwritten since it was freed.
 *
 * Complies with standard fini arg/return
 *
 */
void
trash_fini(void *mem, int size)
{
	(void)trash_ctor(mem, size, NULL, 0);
}

int
mtrash_ctor(void *mem, int size, void *arg, int flags)
{
	struct malloc_type **ksp;
	uint32_t *p = mem;
	int cnt;

#ifdef DEBUG_MEMGUARD
	if (is_memguard_addr(mem))
		return (0);
#endif

	size -= sizeof(struct malloc_type *);
	ksp = (struct malloc_type **)mem;
	ksp += size / sizeof(struct malloc_type *);
	cnt = size / sizeof(uma_junk);

	for (p = mem; cnt > 0; cnt--, p++)
		if (*p != uma_junk) {
			printf("Memory modified after free %p(%d) val=%x @ %p\n",
			    mem, size, *p, p);
			panic("Most recently used by %s\n", (*ksp == NULL)?
			    "none" : (*ksp)->ks_shortdesc);
		}
	return (0);
}

/*
 * Fills an item with predictable garbage
 *
 * Complies with standard dtor arg/return
 *
 */
void
mtrash_dtor(void *mem, int size, void *arg)
{
	int cnt;
	uint32_t *p;

#ifdef DEBUG_MEMGUARD
	if (is_memguard_addr(mem))
		return;
#endif

	size -= sizeof(struct malloc_type *);
	cnt = size / sizeof(uma_junk);

	for (p = mem; cnt > 0; cnt--, p++)
		*p = uma_junk;
}

/*
 * Fills an item with predictable garbage
 *
 * Complies with standard init arg/return
 *
 */
int
mtrash_init(void *mem, int size, int flags)
{
	struct malloc_type **ksp;

#ifdef DEBUG_MEMGUARD
	if (is_memguard_addr(mem))
		return (0);
#endif

	mtrash_dtor(mem, size, NULL);

	ksp = (struct malloc_type **)mem;
	ksp += (size / sizeof(struct malloc_type *)) - 1;
	*ksp = NULL;
	return (0);
}

/*
 * Checks an item to make sure it hasn't been overwritten since it was freed,
 * prior to freeing it back to available memory.
 *
 * Complies with standard fini arg/return
 *
 */
void
mtrash_fini(void *mem, int size)
{
	(void)mtrash_ctor(mem, size, NULL, 0);
}

#ifdef MALLOC_MAKE_FAILURES
/*
 * Debugging and failure injection for UMA and malloc M_NOWAIT memory
 * allocations.  This code and the hooks in UMA and malloc allow for
 * injection of failures for specific UMA zones and malloc types and for
 * tracking of the last failure injected.
 *
 * Configuration is done via the sysctls under debug.mnowait_failure.
 * There is a whitelist and a blacklist containing UMA zone names (see
 * vmstat -z) and malloc type names (see vmstat -m).  If any entries are
 * present in the whitelist, failure injection will be enabled for only
 * the zones and malloc types matching the whitelist entries.  If the
 * whitelist is empty, then only blacklist matches will be excluded.
 * Certain zones and malloc types may be known not to behave well with
 * with failure injection, and they may be present in the default
 * blacklist.
 *
 * Enabling failure injection is done via the fail point configurable by
 * sysctl debug.fail_point.mnowait.  See fail(9).
 *
 * By default, the zalloc failure injection hooks ignore allocations
 * done for malloc.
 *
 * TODO: move above to a man page.
 */

#if defined(DDB) || defined(STACK)
#define	HAVE_STACK
#endif

/* Uma Dbg Nowait Failure Globals -> g_udnf_ */

/* Configuration. */
bool g_uma_dbg_nowait_fail_zalloc_ignore_malloc = true;
#define	NOWAIT_FAIL_LIST_BUFSIZE	1024
static char g_udnf_whitelist[NOWAIT_FAIL_LIST_BUFSIZE];
static char g_udnf_blacklist[NOWAIT_FAIL_LIST_BUFSIZE] =
    "ata_request,"
    "BUF TRIE,"
    "ifaddr,"
    "kobj,"
    "linker,"
    "pcb,"
    "sackhole,"
    "sctp_ifa,"
    "sctp_ifn,"
    "sctp_vrf";

static struct rwlock g_udnf_conf_lock;
RW_SYSINIT(uma_dbg_nowait_conf, &g_udnf_conf_lock, "uma dbg nowait conf");

/* Tracking. */
#define	NOWAIT_FAIL_NAME_BUFSIZE	80
static char g_udnf_last_name[NOWAIT_FAIL_NAME_BUFSIZE];
static char g_udnf_last_comm[MAXCOMLEN + 1];
static pid_t g_udnf_last_pid;
static lwpid_t g_udnf_last_tid;
static int g_udnf_last_ticks;

#ifdef HAVE_STACK
static struct stack g_udnf_last_stack;
#endif

static struct mtx g_udnf_track_lock;
MTX_SYSINIT(uma_dbg_nowait_track, &g_udnf_track_lock, "uma dbg nowait track",
    0);

void
uma_dbg_nowait_fail_record(const char *name)
{
#ifdef HAVE_STACK
	struct stack st = {};
#endif
	struct thread *td;

#ifdef HAVE_STACK
	stack_save(&st);
#endif
	td = curthread;

	mtx_lock(&g_udnf_track_lock);
#ifdef HAVE_STACK
	stack_copy(&st, &g_udnf_last_stack);
#endif
	strlcpy(g_udnf_last_name, name, sizeof(g_udnf_last_name));
	g_udnf_last_tid = td->td_tid;
	g_udnf_last_pid = td->td_proc->p_pid;
	strlcpy(g_udnf_last_comm, td->td_proc->p_comm,
	    sizeof(g_udnf_last_comm));
	g_udnf_last_ticks = ticks;
	mtx_unlock(&g_udnf_track_lock);
}

static int
sysctl_debug_mnowait_failure_last_injection(SYSCTL_HANDLER_ARGS)
{
	char last_name[NOWAIT_FAIL_NAME_BUFSIZE];
	char last_comm[MAXCOMLEN + 1];
	struct sbuf sbuf;
#ifdef HAVE_STACK
	struct stack last_stack;
#endif
	pid_t last_pid;
	lwpid_t last_tid;
	u_int delta;
	int error;
	int last_ticks;

	mtx_lock(&g_udnf_track_lock);
#ifdef HAVE_STACK
	stack_copy(&g_udnf_last_stack, &last_stack);
#endif
	strlcpy(last_name, g_udnf_last_name, sizeof(last_name));
	last_tid = g_udnf_last_tid;
	last_pid = g_udnf_last_pid;
	strlcpy(last_comm, g_udnf_last_comm, sizeof(last_comm));
	last_ticks = g_udnf_last_ticks;
	mtx_unlock(&g_udnf_track_lock);

	if (last_tid == 0)
		return (0);

	delta = ticks - last_ticks;

	sbuf_new_for_sysctl(&sbuf, NULL, 128, req);
	sbuf_printf(&sbuf, "%s[%d] tid %d alloc %s %u.%03u s ago",
	    last_comm, last_pid, last_tid,
	    last_name, delta / hz, (delta % hz) * 1000 / hz);
#ifdef HAVE_STACK
	sbuf_putc(&sbuf, '\n');
	stack_sbuf_print(&sbuf, &last_stack);
#endif
	error = sbuf_finish(&sbuf);
	sbuf_delete(&sbuf);

	return (error);
}

static bool
str_in_list(const char *list, char delim, const char *str)
{
       const char *b, *e;
       size_t blen, slen;

       b = list;
       slen = strlen(str);
       for (;;) {
               e = strchr(b, delim);
               blen = e == NULL ? strlen(b) : e - b;
               if (blen == slen && strncmp(b, str, slen) == 0)
                       return (true);
               if (e == NULL)
                       break;
               b = e + 1;
       }
       return (false);
}

bool
uma_dbg_nowait_fail_enabled(const char *name)
{
	bool fail;

	/* Protect ourselves from the sysctl handlers. */
	rw_rlock(&g_udnf_conf_lock);
	if (g_udnf_whitelist[0] == '\0')
		fail = !str_in_list(g_udnf_blacklist, ',', name);
	else
		fail = str_in_list(g_udnf_whitelist, ',', name);
	rw_runlock(&g_udnf_conf_lock);

	return (fail);
}

/*
 * XXX provide SYSCTL_STRING_LOCKED / sysctl_handle_string_locked?
 * This is basically just a different sysctl_handle_string.  This one wraps
 * the string manipulation in a lock and in a way that will not cause a sleep
 * under that lock.
 */
static int
sysctl_debug_mnowait_failure_list(SYSCTL_HANDLER_ARGS)
{
	char *newbuf = NULL;
	int error, newlen;
	bool have_lock = false;

	if (req->newptr != NULL) {
		newlen = req->newlen - req->newidx;
		if (newlen >= arg2) {
			error = EINVAL;
			goto out;
		}
		newbuf = malloc(newlen, M_TEMP, M_WAITOK);
		error = SYSCTL_IN(req, newbuf, newlen);
		if (error != 0)
			goto out;
	}

	error = sysctl_wire_old_buffer(req, arg2);
	if (error != 0)
		goto out;

	rw_wlock(&g_udnf_conf_lock);
	have_lock = true;

	error = SYSCTL_OUT(req, arg1, strnlen(arg1, arg2 - 1) + 1);
	if (error != 0)
		goto out;

	if (newbuf == NULL)
		goto out;

	bcopy(newbuf, arg1, newlen);
	((char *)arg1)[newlen] = '\0';
 out:
	if (have_lock)
		rw_wunlock(&g_udnf_conf_lock);
	free(newbuf, M_TEMP);
	return (error);
}

SYSCTL_NODE(_debug, OID_AUTO, mnowait_failure, CTLFLAG_RW, 0,
    "Control of M_NOWAIT memory allocation failure injection.");

KFAIL_POINT_DEFINE(DEBUG_FP, mnowait, 0);

SYSCTL_PROC(_debug_mnowait_failure, OID_AUTO, blacklist,
    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, g_udnf_blacklist,
    sizeof(g_udnf_blacklist), sysctl_debug_mnowait_failure_list, "A",
    "With debug.fail_point.mnowait and with an empty whitelist, CSV list of "
    "zones which remain unaffected.");

SYSCTL_PROC(_debug_mnowait_failure, OID_AUTO, whitelist,
    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, g_udnf_whitelist,
    sizeof(g_udnf_whitelist), sysctl_debug_mnowait_failure_list, "A",
    "With debug.fail_point.mnowait, CSV list of zones exclusively affected.  "
    "With an empty whitelist, all zones but those on the blacklist"
    "are affected.");

SYSCTL_BOOL(_debug_mnowait_failure, OID_AUTO, zalloc_ignore_malloc,
    CTLFLAG_RW, &g_uma_dbg_nowait_fail_zalloc_ignore_malloc, 0,
    "Whether zalloc failure injection ignores (does not inject) malloc "
    "zones.");

SYSCTL_PROC(_debug_mnowait_failure, OID_AUTO, last_injection,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, 0,
    sysctl_debug_mnowait_failure_last_injection, "A",
    "The last allocation for which a failure was injected.");
#endif /* MALLOC_MAKE_FAILURES */
