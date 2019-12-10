/*
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/_bitset.h>
#include <sys/bitset.h>

#include <stdbool.h>
#include <string.h>

#include <atf-c.h>

#define BS1_BITS	((int)(BITSET_SIZE(1) * NBBY))
#define BS1S_BITS	((int)(BITSET_SIZE(1) * NBBY - 4))
#define BS2S_BITS	((int)(BITSET_SIZE(1) * NBBY + 4))
#define BS2_BITS	((int)(BITSET_SIZE(2) * NBBY))

BITSET_DEFINE(bs1, BS1_BITS);
BITSET_DEFINE(bs1s, BS1S_BITS);
BITSET_DEFINE(bs2s, BS2S_BITS);
BITSET_DEFINE(bs2, BS2_BITS);

#define BITSET_CHECK_IDENTITIES(bits, p)	do {			\
	int count;							\
	bool empty, full;						\
	ATF_CHECK(!BIT_CMP(bits, p, p));				\
	ATF_CHECK(BIT_SUBSET(bits, p, p));				\
	empty = BIT_EMPTY(bits, p);					\
	full = BIT_ISFULLSET(bits, p);					\
	count = BIT_COUNT(bits, p);					\
	ATF_CHECK(!(empty && full));					\
	ATF_CHECK_EQ(count == bits, full);				\
	ATF_CHECK_EQ(count == 0, empty);				\
} while (0)

#define	BITSET_TEST_BASIC(name, bits)					\
BITSET_DEFINE(bs_##name, bits);						\
ATF_TC_WITHOUT_HEAD(bitset_test_basic_##name);				\
ATF_TC_BODY(bitset_test_basic_##name, tc)				\
{									\
	struct bs_##name bs, bs2;					\
	int i;								\
									\
	memset(&bs, -1, sizeof(bs));					\
	BIT_ZERO(bits, &bs);						\
	BITSET_CHECK_IDENTITIES(bits, &bs);				\
	ATF_CHECK(BIT_EMPTY(bits, &bs));				\
	ATF_CHECK(!BIT_ISFULLSET(bits, &bs));				\
	ATF_CHECK_EQ(BIT_COUNT(bits, &bs), 0);				\
	ATF_CHECK_EQ(BIT_FFS(bits, &bs), 0);				\
	ATF_CHECK_EQ(BIT_FLS(bits, &bs), 0);				\
									\
	memset(&bs, 0, sizeof(bs));					\
	BIT_FILL(bits, &bs);						\
	BITSET_CHECK_IDENTITIES(bits, &bs);				\
	ATF_CHECK(!BIT_EMPTY(bits, &bs));				\
	ATF_CHECK(BIT_ISFULLSET(bits, &bs));				\
	ATF_CHECK_EQ(BIT_COUNT(bits, &bs), bits);			\
	ATF_CHECK_EQ(BIT_FFS(bits, &bs), 1);				\
	ATF_CHECK_EQ(BIT_FLS(bits, &bs), bits);				\
									\
	BIT_ZERO(bits, &bs);						\
	BIT_FILL(bits, &bs2);						\
	for (i = 0; i < bits; i++) {					\
		BIT_CLR(bits, i, &bs2);					\
		BITSET_CHECK_IDENTITIES(bits, &bs2);			\
	}								\
	ATF_CHECK(!BIT_CMP(bits, &bs, &bs2));				\
									\
	BIT_ZERO(bits, &bs);						\
	BIT_FILL(bits, &bs2);						\
	for (i = 0; i < bits; i++) {					\
		BIT_SET(bits, i, &bs);					\
		BITSET_CHECK_IDENTITIES(bits, &bs);			\
	}								\
	ATF_CHECK(!BIT_CMP(bits, &bs, &bs2));				\
									\
	BIT_ZERO(bits, &bs);						\
	for (i = 0; i < bits; i++) {					\
		BIT_SET(bits, i, &bs);					\
		BITSET_CHECK_IDENTITIES(bits, &bs);			\
		ATF_CHECK_EQ(BIT_FFS(bits, &bs), 1);			\
		ATF_CHECK_EQ(BIT_FLS(bits, &bs), i + 1);		\
		ATF_CHECK_EQ(BIT_COUNT(bits, &bs), i + 1);		\
	}								\
	for (i = 0; i < bits; i++) {					\
		ATF_CHECK_EQ(BIT_FFS(bits, &bs), i + 1);		\
		ATF_CHECK_EQ(BIT_FLS(bits, &bs), bits);			\
		ATF_CHECK_EQ(BIT_COUNT(bits, &bs), bits - i);		\
		BIT_CLR(bits, i, &bs);					\
		BITSET_CHECK_IDENTITIES(bits, &bs);			\
	}								\
}

BITSET_TEST_BASIC(bs1, BS1_BITS);
BITSET_TEST_BASIC(bs1s, BS1S_BITS);
BITSET_TEST_BASIC(bs2s, BS2S_BITS);

/* Truth tables, inputs are index values (0b00, 0b01, 0b10, 0b11). */
static const char bitset_tt2_and[] =	{ 0, 0, 0, 1 };
static const char bitset_tt2_andnot[] =	{ 0, 0, 1, 0 };
static const char bitset_tt2_or[] =	{ 0, 1, 1, 1 };
static const char bitset_tt2_xor[] =	{ 0, 1, 1, 0 };

#define	BITSET_TEST_INIT(bits, v, p)	do {				\
	if (v)								\
		BIT_FILL(bits, p);					\
	else								\
		BIT_ZERO(bits, p);					\
} while (0)

#define BITSET_TEST_OP2_NORMAL(op, b0, b1, o)	do {			\
	struct bs2 dst, exp, src, src_cpy;				\
									\
	BITSET_TEST_INIT(BS2_BITS, b0, &dst);				\
	BITSET_TEST_INIT(BS2_BITS, b1, &src);				\
	memcpy(&src_cpy, &src, sizeof(src_cpy));			\
	BITSET_TEST_INIT(BS2_BITS, o, &exp);				\
	op(BS2_BITS, &dst, &src);					\
	ATF_CHECK_MSG(!BIT_CMP(BS2_BITS, &dst, &exp),			\
	    #op "(%d, %d) -> %d expected", b0, b1, o);			\
	ATF_CHECK_MSG(memcmp(&src, &src_cpy, sizeof(src)) == 0,		\
	    #op "modified const src set");				\
} while (0)

#define BITSET_TEST_OP2_ALIAS(op, b, o)	do {				\
	struct bs2 dst, exp;						\
									\
	BITSET_TEST_INIT(BS2_BITS, b, &dst);				\
	BITSET_TEST_INIT(BS2_BITS, o, &exp);				\
	op(BS2_BITS, &dst, &dst);					\
	ATF_CHECK_MSG(!BIT_CMP(BS2_BITS, &dst, &exp),			\
	    "Aliased " #op "(%d, %d) -> %d expected", b, b, o);		\
} while (0)

#define BITSET_TEST_OP2(op, tt)						\
ATF_TC_WITHOUT_HEAD(bitset_test_##op);					\
ATF_TC_BODY(bitset_test_##op, tc)					\
{									\
	int i, j;							\
									\
	for (i = 0; i < 2; i++)						\
		for (j = 0; j < 2; j++)					\
			BITSET_TEST_OP2_NORMAL(op, i, j,		\
			    tt[(i << 1) | j]);				\
	for (i = 0; i < 2; i++)						\
		BITSET_TEST_OP2_ALIAS(op, i, tt[(i << 1) | i]);		\
}

BITSET_TEST_OP2(BIT_AND, bitset_tt2_and);
BITSET_TEST_OP2(BIT_NAND, bitset_tt2_andnot);
BITSET_TEST_OP2(BIT_OR, bitset_tt2_or);
BITSET_TEST_OP2(BIT_XOR, bitset_tt2_xor);

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, bitset_test_basic_bs1);
	ATF_TP_ADD_TC(tp, bitset_test_basic_bs1s);
	ATF_TP_ADD_TC(tp, bitset_test_basic_bs2s);

	ATF_TP_ADD_TC(tp, bitset_test_BIT_AND);
	ATF_TP_ADD_TC(tp, bitset_test_BIT_NAND);
	ATF_TP_ADD_TC(tp, bitset_test_BIT_OR);
	ATF_TP_ADD_TC(tp, bitset_test_BIT_XOR);

	return (atf_no_error());
}
