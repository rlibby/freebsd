/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Copyright (c) 2008 Nokia Corporation
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_BITSET_H_
#define	_SYS_BITSET_H_

/*
 * Whether expr is both constant and true.  Result is itself constant.
 * Used to enable optimizations for sets with a known small size.
 */
#define	__constexpr_cond(expr)	(__builtin_constant_p((expr)) && (expr))

#define	__bitset_bitno(_s, n)						\
	(__constexpr_cond(__bitset_words((_s)) == 1) ?			\
	    (__size_t)(n) : ((n) % _BITSET_BITS))

#define	__bitset_mask(_s, n)						\
	(1UL << __bitset_bitno(_s, n))

#define	__bitset_word(_s, n)						\
	(__constexpr_cond(__bitset_words((_s)) == 1) ?			\
	 0 : ((n) / _BITSET_BITS))

/*
 * Bitset iteration macros.  The suffix ([123]) indicates the number of
 * sets being iterated.
 *
 * The _BIT_CMP[12] macros evaluate to true if the given condition is
 * true for all words or coordinate sets of words.  The condition is
 * itself a macro or function taking as arguments a mask of bits in
 * range and the value of each word or coordinate set of words.
 *
 * The _BIT_OP[123] macros apply an operation to all words or coordinate
 * sets of words.
 */

#define	_BIT_CMP1(_s, s1, cond)	__extension__ ({			\
	__size_t __i = 0, __s = (_s);					\
	__size_t __n = __bitset_word(_s, __s);				\
	__size_t __r = __bitset_bitno(_s, __s);				\
	__typeof((s1)->__bits[0]) *__s1_bits = (s1)->__bits;		\
	for (; __i < __n; __i++)					\
		if (!(cond(-1L, __s1_bits[__i])))			\
			break;						\
	(__i == __n && (__r == 0 ||					\
	    (cond((1L << __r) - 1, __s1_bits[__n]))));			\
})

#define	_BIT_CMP2(_s, s1, s2, cond)	__extension__ ({		\
	__size_t __i = 0, __s = (_s);					\
	__size_t __n = __bitset_word(_s, __s);				\
	__size_t __r = __bitset_bitno(_s, __s);				\
	__typeof((s1)->__bits[0]) *__s1_bits = (s1)->__bits;		\
	__typeof((s2)->__bits[0]) *__s2_bits = (s2)->__bits;		\
	for (; __i < __n; __i++)					\
		if (!(cond(-1L, __s1_bits[__i], __s2_bits[__i])))	\
			break;						\
	(__i == __n && (__r == 0 ||					\
	    (cond((1L << __r) - 1, __s1_bits[__n], __s2_bits[__n]))));	\
})

#define	_BIT_EQ_MASK(m, x, y)	(((m) & (x)) == ((m) & (y)))

#define	_BIT_OP1(_s, s1, op)	do {					\
	__size_t __i = 0, __n = __bitset_words(_s);			\
	__typeof((s1)->__bits[0]) *__s1_bits = (s1)->__bits;		\
	for (; __i < __n; __i++)					\
		op(__s1_bits[__i]);					\
} while (0)

#define	_BIT_OP2(_s, s1, s2, op)	do {				\
	__size_t __i = 0, __n = __bitset_words(_s);			\
	__typeof((s1)->__bits[0]) *__s1_bits = (s1)->__bits;		\
	__typeof((s2)->__bits[0]) *__s2_bits = (s2)->__bits;		\
	for (; __i < __n; __i++)					\
		op(__s1_bits[__i], __s2_bits[__i]);			\
} while (0)

#define	_BIT_OP3(_s, s1, s2, s3, op)	do {				\
	__size_t __i = 0, __n = __bitset_words(_s);			\
	__typeof((s1)->__bits[0]) *__s1_bits = (s1)->__bits;		\
	__typeof((s2)->__bits[0]) *__s2_bits = (s2)->__bits;		\
	__typeof((s3)->__bits[0]) *__s3_bits = (s3)->__bits;		\
	for (; __i < __n; __i++)					\
		op(__s1_bits[__i], __s2_bits[__i], __s3_bits[__i]);	\
} while (0)

/*
 * Bitset API.
 */

#define	BIT_CLR(_s, n, p)	do {					\
	__size_t __n = (n);						\
	((p)->__bits[__bitset_word(_s, __n)] &=				\
	    ~__bitset_mask(_s, __n));					\
} while (0)

#define	BIT_COPY(_s, f, t)	(void)(*(t) = *(f))

#define	BIT_ISSET(_s, n, p)	__extension__ ({			\
	__size_t __n = (n);						\
	(((p)->__bits[__bitset_word(_s, __n)] &				\
	    __bitset_mask(_s, __n)) != 0);				\
})

#define	BIT_SET(_s, n, p)	do {					\
	__size_t __n = (n);						\
	((p)->__bits[__bitset_word(_s, __n)] |=				\
	    __bitset_mask(_s, __n));					\
} while (0)

/* Is p empty. */
#define	_BIT_EMPTY_COND(m, x)	_BIT_EQ_MASK(m, x, 0)
#define	BIT_EMPTY(_s, p)	_BIT_CMP1(_s, p, _BIT_EMPTY_COND)

/* Is p full set. */
#define	_BIT_ISFULLSET_COND(m, x)	_BIT_EQ_MASK(m, x, -1L)
#define	BIT_ISFULLSET(_s, p)	_BIT_CMP1(_s, p, _BIT_ISFULLSET_COND)

/* Is c a subset of p. */
#define	_BIT_SUBSET_COND(m, p, c)	_BIT_EQ_MASK(m, (p) & (c), c)
#define	BIT_SUBSET(_s, p, c)	_BIT_CMP2(_s, p, c, _BIT_SUBSET_COND)

/* Are there any common bits between b & c? */
#define	_BIT_OVERLAP_COND(m, p, c)	_BIT_EQ_MASK(m, (p) & (c), 0)
#define	BIT_OVERLAP(_s, p, c)	(!_BIT_CMP2(_s, p, c, _BIT_OVERLAP_COND))

/* Compare two sets, returns 0 if equal 1 otherwise. */
#define	_BIT_CMP_COND(m, p, c)	_BIT_EQ_MASK(m, p, c)
#define	BIT_CMP(_s, p, c)	(!_BIT_CMP2(_s, p, c, _BIT_CMP_COND))

#define	_BIT_ZERO_OP(d)			((d) = 0)
#define	_BIT_FILL_OP(d)			((d) = -1L)
#define	_BIT_AND_OP(d, s)		((d) &= (s))
#define	_BIT_AND2_OP(d, s1, s2)		((d) = (s1) & (s2))
#define	_BIT_ANDNOT_OP(d, s)		((d) &= ~(s))
#define	_BIT_ANDNOT2_OP(d, s1, s2)	((d) = (s1) & ~(s2))
#define	_BIT_OR_OP(d, s)		((d) |= (s))
#define	_BIT_OR2_OP(d, s1, s2)		((d) = (s1) | (s2))
#define	_BIT_XOR_OP(d, s)		((d) ^= (s))
#define	_BIT_XOR2_OP(d, s1, s2)		((d) = (s1) ^ (s2))

#define	BIT_ZERO(_s, d)			_BIT_OP1(_s, d, _BIT_ZERO_OP)
#define	BIT_FILL(_s, d)			_BIT_OP1(_s, d, _BIT_FILL_OP)
#define	BIT_AND(_s, d, s)		_BIT_OP2(_s, d, s, _BIT_AND_OP)
#define	BIT_AND2(_s, d, s1, s2)		_BIT_OP3(_s, d, s1, s2, _BIT_AND2_OP)
#define	BIT_ANDNOT(_s, d, s)		_BIT_OP2(_s, d, s, _BIT_ANDNOT_OP)
#define	BIT_ANDNOT2(_s, d, s1, s2)	_BIT_OP3(_s, d, s, _BIT_ANDNOT2_OP)
#define	BIT_OR(_s, d, s)		_BIT_OP2(_s, d, s, _BIT_OR_OP)
#define	BIT_OR2(_s, d, s1, s2)		_BIT_OP3(_s, d, s1, s2, _BIT_OR2_OP)
#define	BIT_XOR(_s, d, s)		_BIT_OP2(_s, d, s, _BIT_XOR_OP)
#define	BIT_XOR2(_s, d, s1, s2)		_BIT_OP3(_s, d, s1, s2, _BIT_XOR2_OP)

#define	BIT_SETOF(_s, n, p) do {					\
	BIT_ZERO(_s, p);						\
	BIT_SET(_s, n, p);						\
} while (0)

/*
 * Note, the atomic(9) API is not consistent between clear/set and
 * testandclear/testandset in whether the value argument is a mask
 * or a bit index.
 */

#define	BIT_CLR_ATOMIC(_s, n, p)					\
	atomic_clear_long(&(p)->__bits[__bitset_word(_s, n)],		\
	    __bitset_mask((_s), n))

#define	BIT_SET_ATOMIC(_s, n, p)					\
	atomic_set_long(&(p)->__bits[__bitset_word(_s, n)],		\
	    __bitset_mask((_s), n))

#define	BIT_SET_ATOMIC_ACQ(_s, n, p)					\
	atomic_set_acq_long(&(p)->__bits[__bitset_word(_s, n)],		\
	    __bitset_mask((_s), n))

#define	BIT_TEST_CLR_ATOMIC(_s, n, p)					\
	(atomic_testandclear_long(					\
	    &(p)->__bits[__bitset_word((_s), (n))], (n)) != 0)

#define	BIT_TEST_SET_ATOMIC(_s, n, p)					\
	(atomic_testandset_long(					\
	    &(p)->__bits[__bitset_word((_s), (n))], (n)) != 0)

/* Convenience functions catering special cases. */
#define	_BIT_AND_ATOMIC_OP(d, s)					\
	atomic_clear_long(&(d), ~(s))
#define	BIT_AND_ATOMIC(_s, d, s)					\
	_BIT_OP2(_s, d, s, _BIT_AND_ATOMIC_OP)

#define	_BIT_OR_ATOMIC_OP(d, s)						\
	atomic_set_long(&(d), (s))
#define	BIT_OR_ATOMIC(_s, d, s)						\
	_BIT_OP2(_s, d, s, _BIT_OR_ATOMIC_OP)

#define	_BIT_COPY_STORE_REL_OP(s, d)					\
	atomic_store_rel_long(&(d), (s))
#define	BIT_COPY_STORE_REL(_s, s, d)					\
	_BIT_OP2(_s, s, d, _BIT_COPY_STORE_REL_OP)

/*
 * Note that `start` and the returned value from BIT_FFS_AT are
 * 1-based bit indices.
 */
#define	BIT_FFS_AT(_s, p, start) __extension__ ({			\
	__size_t __i, __s = (_s);					\
	__size_t __n = __bitset_word(_s, __s);				\
	__size_t __r = __bitset_bitno(_s, __s);				\
	__typeof((p)->__bits[0]) *__bits = (p)->__bits;			\
	long __bit;							\
									\
	__bit = 0;							\
	do {								\
		if ((start) == 0) {					\
			__i = 0;    					\
		} else {						\
			long __mask;					\
			__i = bitstet_word(_s, (start));		\
			__mask = ~0UL << __bitset_bitno(_s, (start));	\
			if (__r != 0 && __i == __n)			\
				__mask &= (1UL << __r) - 1;		\
			__bit = ffsl(__bits[__i] & __mask);		\
			if (__bit != 0) {				\
				__bit += __i * _BITSET_BITS;		\
				break;					\
			}						\
			__i++;						\
		}							\
		for (; __i < __n; __i++) {				\
			if (__bits[__i] != 0) {				\
				__bit = ffsl(__bits[__i]);		\
				__bit += __i * _BITSET_BITS;		\
				break;					\
			}						\
		}							\
		if (__r == 0 || __bit != 0)				\
			break;						\
		__bit = ffsl(__bits[__n] & ((1UL << __r) - 1));		\
		if (__bit)						\
			__bit += __n * _BITSET_BITS;			\
	} while (0);							\
	__bit;								\
})

#define	BIT_FFS(_s, p) BIT_FFS_AT((_s), (p), 0)

#define	BIT_FLS(_s, p) __extension__ ({					\
	__size_t __i, __s = (_s);					\
	__size_t __n = __bitset_word(_s, __s);				\
	__size_t __r = __bitset_bitno(_s, __s);				\
	__typeof((p)->__bits[0]) *__bits = (p)->__bits;			\
	long __bit;							\
									\
	__bit = 0;							\
	if (__r != 0) {							\
		__bit = flsl(((1L << __r) - 1) & __bits[__n]);		\
		if (__bit)						\
			__bit += __n * _BITSET_BITS;			\
	}								\
	if (__bit == 0) {						\
		for (__i = __n; __i > 0; ) {				\
			__i--;						\
			if (__bits[__i] != 0) {				\
				__bit = flsl(__bits[__i]);		\
				__bit += __i * _BITSET_BITS;		\
				break;					\
			}						\
		}							\
	}								\
	__bit;								\
})

#define	BIT_COUNT(_s, p) __extension__ ({				\
	__size_t __i = 0, __s = (_s);					\
	__size_t __n = __bitset_word(_s, __s);				\
	__size_t __r = __bitset_bitno(_s, __s);				\
	__typeof((p)->__bits[0]) *__bits = (p)->__bits;			\
	long __count;							\
									\
	__count = 0;							\
	for (__i = 0; __i < __n; __i++)					\
		__count += __bitcountl(__bits[__i]);			\
	if (__r != 0)							\
		__count += __bitcountl(((1L << __r) - 1) & __bits[__n]);\
	__count;							\
})

#define	BITSET_T_INITIALIZER(x)						\
	{ .__bits = { x } }

#define	BITSET_FSET(n)							\
	[ 0 ... ((n) - 1) ] = (-1L)

#define	BITSET_SIZE(_s)	(__bitset_words((_s)) * sizeof(long))

/*
 * Dynamically allocate a bitset.
 */
#define BITSET_ALLOC(_s, mt, mf)	malloc(BITSET_SIZE((_s)), mt, (mf))

#endif /* !_SYS_BITSET_H_ */
