#include "lprefix.h"

#include <assert.h>

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"


/* limit for table tag-method chains (to avoid loops) */
#define MAXTAGLOOP	2000

/*
** 'l_intfitsf' checks whether a given integer can be converted to a
** float without rounding. Used in comparisons. Left undefined if
** all integers fit in a float precisely.
*/
#if !defined(l_intfitsf)

/* number of bits in the mantissa of a float */
#define NBM		(l_mathlim(MANT_DIG))

/*
** Check whether some integers may not fit in a float, that is, whether
** (maxinteger >> NBM) > 0 (that implies (1 << NBM) <= maxinteger).
** (The shifts are done in parts to avoid shifting by more than the size
** of an integer. In a worst case, NBM == 113 for long double and
** sizeof(integer) == 32.)
*/
#if ((((LUA_MAXINTEGER >> (NBM / 4)) >> (NBM / 4)) >> (NBM / 4)) \
	>> (NBM - (3 * (NBM / 4))))  >  0

#define l_intfitsf(i)  \
  (-((lua_Integer)1 << NBM) <= (i) && (i) <= ((lua_Integer)1 << NBM))

#endif

#endif




/*
** some macros for common tasks in 'luaV_execute'
*/


#define RA(i)	(base+GETARG_A(i))
#define RB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))
#define RC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))
#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))


#define Protect(x)	{ {x;}; base = ci->u.l.base; }

#define checkGC(L,c)  \
	{ luaC_condGC(L, L->top = (c),  /* limit of live values */ \
                         Protect(L->top = ci->top));  /* restore top */ \
           luai_threadyield(L); }


/*
** copy of 'luaV_gettable', but protecting the call to potential
** metamethod (which can reallocate the stack)
*/
#define gettableProtected(L,t,k,v)  { const TValue *slot; \
  if (luaV_fastget(L,t,k,slot,luaH_get)) { setobj2s(L, v, slot); } \
  else Protect(luaV_finishget(L,t,k,v,slot)); }


/* same for 'luaV_settable' */
#define settableProtected(L,t,k,v) { const TValue *slot; \
  if (!luaV_fastset(L,t,k,slot,luaH_get,v)) \
    Protect(luaV_finishset(L,t,k,v,slot)); }
