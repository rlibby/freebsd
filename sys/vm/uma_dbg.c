/*-
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitset.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/uma.h>
#include <vm/uma_int.h>
#include <vm/uma_dbg.h>

static const uint32_t uma_junk = 0xdeadc0de;

/*
 * Checks an item to make sure it hasn't been overwritten since it was freed,
 * prior to subsequent reallocation.
 *
 * Complies with standard ctor arg/return
 *
 */
int
trash_ctor(void *mem, int size, void *arg, int flags)
{
	int cnt;
	uint32_t *p;

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

/* XXX explain */
struct rwlock g_uma_dbg_nowait_lock;
RW_SYSINIT(uma_dbg_nowait, &g_uma_dbg_nowait_lock, "uma dbg nowait");

#define NOWAIT_FAIL_LIST_BUFSIZE 4096
char malloc_fail_blacklist[NOWAIT_FAIL_LIST_BUFSIZE] = "kobj";
char malloc_fail_whitelist[NOWAIT_FAIL_LIST_BUFSIZE] = "";
char zalloc_fail_blacklist[NOWAIT_FAIL_LIST_BUFSIZE] =
    "BUF TRIE,ata_request,sackhole";
char zalloc_fail_whitelist[NOWAIT_FAIL_LIST_BUFSIZE] = "";

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
                       return true;
               if (e == NULL)
                       break;
               b = e + 1;
       }
       return false;
}

static bool
uma_dbg_nowait_fail_enabled_internal(const char *blacklist,
    const char *whitelist, const char *name)
{
	bool fail;

	/* Protect ourselves from the sysctl handlers. */
	rw_rlock(&g_uma_dbg_nowait_lock);
	if (whitelist[0] == '\0')
		fail = !str_in_list(blacklist, ',', name);
	else
		fail = str_in_list(whitelist, ',', name);
	rw_runlock(&g_uma_dbg_nowait_lock);

	return fail;
}

bool
uma_dbg_nowait_fail_enabled_malloc(const char *name)
{
	return uma_dbg_nowait_fail_enabled_internal(malloc_fail_blacklist,
	    malloc_fail_whitelist, name);
}

bool
uma_dbg_nowait_fail_enabled_zalloc(const char *name)
{
	return uma_dbg_nowait_fail_enabled_internal(zalloc_fail_blacklist,
	    zalloc_fail_whitelist, name);
}

/*
 * XXX provide SYSCTL_STRING_LOCKED / sysctl_string_locked_handler?
 * This is basically just a different sysctl_string_handler.  This one wraps
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

	rw_wlock(&g_uma_dbg_nowait_lock);
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
		rw_wunlock(&g_uma_dbg_nowait_lock);
	free(newbuf, M_TEMP);
	return error;
}

SYSCTL_NODE(_debug, OID_AUTO, mnowait_failure, CTLFLAG_RW, 0,
    "Control of M_NOWAIT memory allocation failure injection.");

SYSCTL_PROC(_debug_mnowait_failure, OID_AUTO, malloc_blacklist,
    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, malloc_fail_blacklist,
    sizeof(malloc_fail_blacklist), sysctl_debug_mnowait_failure_list, "A",
    "With debug.fail_point.malloc and with an empty whitelist, CSV list of "
    "zones which remain unaffected.");

SYSCTL_PROC(_debug_mnowait_failure, OID_AUTO, malloc_whitelist,
    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, malloc_fail_whitelist,
    sizeof(malloc_fail_whitelist), sysctl_debug_mnowait_failure_list, "A",
    "With debug.fail_point.malloc, CSV list of zones exclusively affected.  "
    "With an empty whitelist, all zones but those on the blacklist"
    "are affected.");

SYSCTL_PROC(_debug_mnowait_failure, OID_AUTO, zalloc_blacklist,
    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, zalloc_fail_blacklist,
    sizeof(zalloc_fail_blacklist), sysctl_debug_mnowait_failure_list, "A",
    "With debug.fail_point.uma_zalloc_arg and with an empty whitelist, CSV "
    "list of zones which remain unaffected.");

SYSCTL_PROC(_debug_mnowait_failure, OID_AUTO, zalloc_whitelist,
    CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE, zalloc_fail_whitelist,
    sizeof(zalloc_fail_whitelist), sysctl_debug_mnowait_failure_list, "A",
    "With debug.fail_point.uma_zalloc_arg, CSV list of zones exclusively "
    "affected.  With an empty whitelist, all zones but those on the blacklist"
    "are affected.");
