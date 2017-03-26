/*
** Generate C code from a C source file
**
** This is based on the code from luac.h
** See Copyright Notice in lua.h
*/

#define luac_c
#define LUA_CORE

#include "lprefix.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "lobject.h"
#include "lstate.h"
#include "lundump.h"

static void PrintFunction(const Proto* f);

#define PROGNAME	"luaot"         /* default program name */

static const char* progname=PROGNAME;	/* actual program name */
static const char* modulename=NULL;     /* name of generated module*/ 

static int nfunctions = 0;             /* ID of magic functions */ 

static void fatal(const char* message)
{
 fprintf(stderr,"%s: %s\n",progname,message);
 exit(EXIT_FAILURE);
}

static void usage(const char* message)
{
 if (*message=='-')
  fprintf(stderr,"%s: unrecognized option '%s'\n",progname,message);
 else
  fprintf(stderr,"%s: %s\n",progname,message);
 fprintf(stderr,
  "usage: %s [options] modulename [filenames]\n"
  "Available options are:\n"
  "  -v       show version information\n"
  "  -        stop handling options and process stdin\n"
  ,progname);
 exit(EXIT_FAILURE);
}

#define IS(s)	(strcmp(argv[i],s)==0)

static int doargs(int argc, char* argv[])
{
 int i;
 int version=0;
 if (argv[0]!=NULL && *argv[0]!=0) progname=argv[0];
 for (i=1; i<argc; i++)
 {
  if (*argv[i]!='-')			/* end of options; keep it */
   break;
  else if (IS("--"))			/* end of options; skip it */
  {
   ++i;
   if (version) ++version;
   break;
  }
  else if (IS("-"))			/* end of options; use stdin */
   break;
  else if (IS("-v"))			/* show version */
   ++version;
  else					/* unknown option */
   usage(argv[i]);
 }

 if (version)
 {
  printf("%s\n",LUA_COPYRIGHT);
  if (version==argc-1) exit(EXIT_SUCCESS);
 }

 if (i >= argc)
 {
     usage("missing module name");
 }

 modulename = argv[i++];
 return i;
}

#define FUNCTION "(function()end)();"

static const char* reader(lua_State *L, void *ud, size_t *size)
{
 UNUSED(L);
 if ((*(int*)ud)--)
 {
  *size=sizeof(FUNCTION)-1;
  return FUNCTION;
 }
 else
 {
  *size=0;
  return NULL;
 }
}

#define toproto(L,i) getproto(L->top+(i))

static const Proto* combine(lua_State* L, int n)
{
 if (n==1)
  return toproto(L,-1);
 else
 {
  Proto* f;
  int i=n;
  if (lua_load(L,reader,&i,"=(" PROGNAME ")",NULL)!=LUA_OK) fatal(lua_tostring(L,-1));
  f=toproto(L,-1);
  for (i=0; i<n; i++)
  {
   f->p[i]=toproto(L,i-n-1);
   if (f->p[i]->sizeupvalues>0) f->p[i]->upvalues[0].instack=0;
  }
  f->sizelineinfo=0;
  return f;
 }
}

static int pmain(lua_State* L)
{
 int argc=(int)lua_tointeger(L,1);
 char** argv=(char**)lua_touserdata(L,2);
 const Proto* f;
 int i;
 if (!lua_checkstack(L,argc)) fatal("too many input files");
 for (i=0; i<argc; i++)
 {
  const char* filename=IS("-") ? NULL : argv[i];
  if (luaL_loadfile(L,filename)!=LUA_OK) fatal(lua_tostring(L,-1));
 }
 f=combine(L,argc);

 printf("#include \"luaot-generated-header.c\"\n");
 printf("\n");

 // Ignore the MAIN:
 for (int i=0; i<f->sizep; i++) {
  PrintFunction(f->p[i]);
 }

 printf("#define NFUNCTIONS %d\n", nfunctions);

 printf("ZZ_MAGIC_FUNC zz_magic_functions[%d] = {\n", nfunctions);
 for (int i=0; i < nfunctions; i++) {
   printf("  zz_magic_function_%d,\n", i);
 }
 printf("};\n");
 printf("\n");

 printf("#define ZZ_LUAOPEN_NAME luaopen_%s\n", modulename);
 printf("\n");

 printf("#include \"luaot-generated-footer.c\"\n");

 return 0;
}

int main(int argc, char* argv[])
{
 lua_State* L;
 int i=doargs(argc,argv);
 argc-=i; argv+=i;
 if (argc<=0) usage("no input files given");
 L=luaL_newstate();
 if (L==NULL) fatal("cannot create state: not enough memory");
 lua_pushcfunction(L,&pmain);
 lua_pushinteger(L,argc);
 lua_pushlightuserdata(L,argv);
 if (lua_pcall(L,2,0,0)!=LUA_OK) fatal(lua_tostring(L,-1));
 lua_close(L);
 return EXIT_SUCCESS;
}

/*
** $Id: luac.c,v 1.75 2015/03/12 01:58:27 lhf Exp $
** print bytecodes
** See Copyright Notice in lua.h
*/

#include <ctype.h>
#include <stdio.h>

#define luac_c
#define LUA_CORE

#include "ldebug.h"
#include "lobject.h"
#include "lopcodes.h"

#define VOID(p)		((const void*)(p))

static void PrintString(const TString* ts)
{
 const char* s=getstr(ts);
 size_t i,n=tsslen(ts);
 printf("%c",'"');
 for (i=0; i<n; i++)
 {
  int c=(int)(unsigned char)s[i];
  switch (c)
  {
   case '"':  printf("\\\""); break;
   case '\\': printf("\\\\"); break;
   case '\a': printf("\\a"); break;
   case '\b': printf("\\b"); break;
   case '\f': printf("\\f"); break;
   case '\n': printf("\\n"); break;
   case '\r': printf("\\r"); break;
   case '\t': printf("\\t"); break;
   case '\v': printf("\\v"); break;
   default:	if (isprint(c))
   			printf("%c",c);
		else
			printf("\\%03d",c);
  }
 }
 printf("%c",'"');
}

static void PrintConstant(const Proto* f, int i)
{
 const TValue* o=&f->k[i];
 switch (ttype(o))
 {
  case LUA_TNIL:
	printf("nil");
	break;
  case LUA_TBOOLEAN:
	printf(bvalue(o) ? "true" : "false");
	break;
  case LUA_TNUMFLT:
	{
	char buff[100];
	sprintf(buff,LUA_NUMBER_FMT,fltvalue(o));
	printf("%s",buff);
	if (buff[strspn(buff,"-0123456789")]=='\0') printf(".0");
	break;
	}
  case LUA_TNUMINT:
	printf(LUA_INTEGER_FMT,ivalue(o));
	break;
  case LUA_TSHRSTR: case LUA_TLNGSTR:
	PrintString(tsvalue(o));
	break;
  default:				/* cannot happen */
	printf("? type=%d",ttype(o));
	break;
 }
}

#define UPVALNAME(x) ((f->upvalues[x].name) ? getstr(f->upvalues[x].name) : "-")
#define MYK(x)		(-1-(x))

static void PrintOpcodeComment(const Proto *f, int pc)
{
  const Instruction* code=f->code;
  Instruction i=code[pc];
  OpCode o=GET_OPCODE(i);
  int a=GETARG_A(i);
  int b=GETARG_B(i);
  int c=GETARG_C(i);
  int ax=GETARG_Ax(i);
  int bx=GETARG_Bx(i);
  int sbx=GETARG_sBx(i);
  int line=getfuncline(f,pc);

  printf("  // ");
  //printf("\t%d\t",pc+1);
  if (line>0) printf("[%d]\t",line); else printf("[-]\t");
  printf("%-9s\t",luaP_opnames[o]);
  switch (getOpMode(o))
  {
    case iABC:
      printf("%d",a);
      if (getBMode(o)!=OpArgN) printf(" %d",ISK(b) ? (MYK(INDEXK(b))) : b);
      if (getCMode(o)!=OpArgN) printf(" %d",ISK(c) ? (MYK(INDEXK(c))) : c);
    break;

    case iABx:
      printf("%d",a);
      if (getBMode(o)==OpArgK) printf(" %d",MYK(bx));
      if (getBMode(o)==OpArgU) printf(" %d",bx);
    break;

    case iAsBx:
      printf("%d %d",a,sbx);
    break;

    case iAx:
      printf("%d",MYK(ax));
    break;
  }

  switch (o)
  {
    case OP_LOADK:
      printf("\t; "); PrintConstant(f,bx);
    break;

    case OP_GETUPVAL:
    case OP_SETUPVAL:
      printf("\t; %s",UPVALNAME(b));
    break;

    case OP_GETTABUP:
      printf("\t; %s",UPVALNAME(b));
      if (ISK(c)) { printf(" "); PrintConstant(f,INDEXK(c)); }
    break;

    case OP_SETTABUP:
      printf("\t; %s",UPVALNAME(a));
      if (ISK(b)) { printf(" "); PrintConstant(f,INDEXK(b)); }
      if (ISK(c)) { printf(" "); PrintConstant(f,INDEXK(c)); }
    break;

    case OP_GETTABLE:
    case OP_SELF:
      if (ISK(c)) { printf("\t; "); PrintConstant(f,INDEXK(c)); }
    break;

    case OP_SETTABLE:
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_POW:
    case OP_DIV:
    case OP_IDIV:
    case OP_BAND:
    case OP_BOR:
    case OP_BXOR:
    case OP_SHL:
    case OP_SHR:
    case OP_EQ:
    case OP_LT:
    case OP_LE:
      if (ISK(b) || ISK(c))
      {
        printf("\t; ");
        if (ISK(b)) PrintConstant(f,INDEXK(b)); else printf("-");
        printf(" ");
        if (ISK(c)) PrintConstant(f,INDEXK(c)); else printf("-");
      }
    break;

    case OP_JMP:
    case OP_FORLOOP:
    case OP_FORPREP:
    case OP_TFORLOOP:
      printf("\t; to %d",sbx+pc+1);
    break;

    case OP_CLOSURE:
      printf("\t; %p",VOID(f->p[bx]));
    break;

    case OP_SETLIST:
      if (c==0) printf("\t; %d",(int)code[++pc]); else printf("\t; %d",c);
    break;

    case OP_EXTRAARG:
      printf("\t; "); PrintConstant(f,ax);
    break;

    default:
    break;
  }

  printf("\n");
}

static void PrintCode(const Proto* f)
{
  const Instruction* code=f->code;
  int nopcodes=f->sizecode;

  printf("// source = %s\n", getstr(f->source));
  printf("// linedefined = %d\n", f->linedefined);
  printf("// lastlinedefined = %d\n", f->lastlinedefined);
  printf("// what = %s\n", (f->linedefined == 0) ? "main" : "Lua");

  printf("static int zz_magic_function_%d (lua_State *L, LClosure *cl)\n", nfunctions++);
  printf("{\n");
  printf("  CallInfo *ci = L->ci;\n");
  printf("  TValue *k = cl->p->k;\n");
  printf("  StkId base = ci->u.l.base;\n");
  printf("  \n");
  printf("  // Avoid warnings if the function has few opcodes:\n");
  printf("  UNUSED(ci);\n");
  printf("  UNUSED(k);\n");
  printf("  UNUSED(base);\n");
  printf("  \n");
 

  for (int pc=0; pc<nopcodes; pc++)
  {
    PrintOpcodeComment(f, pc);

    Instruction i=code[pc];
    OpCode o=GET_OPCODE(i);

    printf("  label_%d: {\n", pc);
    printf("    Instruction i = 0x%08x;\n", i);
    printf("    StkId ra = RA(i);\n");
    switch (o) {
      
      case OP_MOVE: {
        printf("    setobjs2s(L, ra, RB(i));\n");
      } break;
      
      case OP_LOADK: {
        printf("    TValue *rb = k + GETARG_Bx(i);\n");
        printf("    setobj2s(L, ra, rb);\n");
      } break;

      // case OP_LOADKX: {
      // } break;
      
      case OP_LOADBOOL: {
        printf("    setbvalue(ra, GETARG_B(i));\n");
        printf("    if (GETARG_C(i)) {\n");
        printf("      goto label_%d; /* skip next instruction (if C) */\n", pc+2);
        printf("    }\n");
      } break;
      
      case OP_LOADNIL: {
        printf("    int b = GETARG_B(i);\n");
        printf("    do {\n");
        printf("      setnilvalue(ra++);\n");
        printf("    } while (b--);\n");
      } break;
 
      case OP_GETUPVAL: {
        printf("    int b = GETARG_B(i);\n");
        printf("    setobj2s(L, ra, cl->upvals[b]->v);\n");
      } break;
     
      case OP_GETTABUP: {
        printf("    TValue *upval = cl->upvals[GETARG_B(i)]->v;\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    gettableProtected(L, upval, rc, ra);\n");
      } break;
      
      case OP_GETTABLE: {
        printf("    StkId rb = RB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    gettableProtected(L, rb, rc, ra);\n");
      } break;

      case OP_SETTABUP: {
        printf("    UNUSED(ra);\n");
        printf("    TValue *upval = cl->upvals[GETARG_A(i)]->v;\n");
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    settableProtected(L, upval, rb, rc);\n");
      } break;

      case OP_SETUPVAL: {
        printf("    UpVal *uv = cl->upvals[GETARG_B(i)];\n");
        printf("    setobj(L, uv->v, ra);\n");
        printf("    luaC_upvalbarrier(L, uv);\n");
      } break;
 
      case OP_SETTABLE: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    settableProtected(L, ra, rb, rc);\n");
      } break;

      case OP_NEWTABLE: {
        printf("    int b = GETARG_B(i);\n");
        printf("    int c = GETARG_C(i);\n");
        printf("    Table *t = luaH_new(L);\n");
        printf("    sethvalue(L, ra, t);\n");
        printf("    if (b != 0 || c != 0)\n");
        printf("      luaH_resize(L, t, luaO_fb2int(b), luaO_fb2int(c));\n");
        printf("    checkGC(L, ra + 1);\n");
      } break;

      case OP_SELF: {
        printf("    const TValue *aux;\n");
        printf("    StkId rb = RB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    TString *key = tsvalue(rc);  /* key must be a string */\n");
        printf("    setobjs2s(L, ra + 1, rb);\n");
        printf("    if (luaV_fastget(L, rb, key, aux, luaH_getstr)) {\n");
        printf("      setobj2s(L, ra, aux);\n");
        printf("    }\n");
        printf("    else Protect(luaV_finishget(L, rb, rc, ra, aux));\n");
      } break;

      case OP_ADD: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Number nb; lua_Number nc;\n");
        printf("    if (ttisinteger(rb) && ttisinteger(rc)) {\n");
        printf("      lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);\n");
        printf("      setivalue(ra, intop(+, ib, ic));\n");
        printf("    }\n");
        printf("    else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        printf("      setfltvalue(ra, luai_numadd(L, nb, nc));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_ADD)); }\n");
      } break;

      case OP_SUB: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Number nb; lua_Number nc;\n");
        printf("    if (ttisinteger(rb) && ttisinteger(rc)) {\n");
        printf("      lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);\n");
        printf("      setivalue(ra, intop(-, ib, ic));\n");
        printf("    }\n");
        printf("    else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        printf("      setfltvalue(ra, luai_numsub(L, nb, nc));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SUB)); }\n");
      } break;
      
      case OP_MUL: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Number nb; lua_Number nc;\n");
        printf("    if (ttisinteger(rb) && ttisinteger(rc)) {\n");
        printf("      lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);\n");
        printf("      setivalue(ra, intop(*, ib, ic));\n");
        printf("    }\n");
        printf("    else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        printf("      setfltvalue(ra, luai_nummul(L, nb, nc));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_MUL)); }\n");
      } break;

      case OP_DIV: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Number nb; lua_Number nc;\n");
        printf("    if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        printf("      setfltvalue(ra, luai_numdiv(L, nb, nc));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_DIV)); }\n");
      } break;

      case OP_BAND: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Integer ib; lua_Integer ic;\n");
        printf("    if (tointeger(rb, &ib) && tointeger(rc, &ic)) {\n");
        printf("      setivalue(ra, intop(&, ib, ic));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BAND)); }\n");
      } break;

      case OP_BOR: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Integer ib; lua_Integer ic;\n");
        printf("    if (tointeger(rb, &ib) && tointeger(rc, &ic)) {\n");
        printf("      setivalue(ra, intop(|, ib, ic));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BOR)); }\n");
      } break;

      case OP_BXOR: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Integer ib; lua_Integer ic;\n");
        printf("    if (tointeger(rb, &ib) && tointeger(rc, &ic)) {\n");
        printf("      setivalue(ra, intop(^, ib, ic));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BXOR)); }\n");
      } break;

      case OP_SHL: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Integer ib; lua_Integer ic;\n");
        printf("    if (tointeger(rb, &ib) && tointeger(rc, &ic)) {\n");
        printf("      setivalue(ra, luaV_shiftl(ib, ic));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SHL)); }\n");
      } break;

      case OP_SHR: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Integer ib; lua_Integer ic;\n");
        printf("    if (tointeger(rb, &ib) && tointeger(rc, &ic)) {\n");
        printf("      setivalue(ra, luaV_shiftl(ib, -ic));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SHR)); }\n");
      } break;

      case OP_MOD: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Number nb; lua_Number nc;\n");
        printf("    if (ttisinteger(rb) && ttisinteger(rc)) {\n");
        printf("      lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);\n");
        printf("      setivalue(ra, luaV_mod(L, ib, ic));\n");
        printf("    }\n");
        printf("    else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        printf("      lua_Number m;\n");
        printf("      luai_nummod(L, nb, nc, m);\n");
        printf("      setfltvalue(ra, m);\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_MOD)); }\n");
      } break;

      case OP_IDIV: { /* floor division */
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Number nb; lua_Number nc;\n");
        printf("    if (ttisinteger(rb) && ttisinteger(rc)) {\n");
        printf("      lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);\n");
        printf("      setivalue(ra, luaV_div(L, ib, ic));\n");
        printf("    }\n");
        printf("    else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        printf("      setfltvalue(ra, luai_numidiv(L, nb, nc));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_IDIV)); }\n");
      } break;
      
      case OP_POW: {
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    lua_Number nb; lua_Number nc;\n");
        printf("    if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        printf("      setfltvalue(ra, luai_numpow(L, nb, nc));\n");
        printf("    }\n");
        printf("    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_POW)); }\n");
      } break;

      case OP_UNM: {
        printf("    TValue *rb = RB(i);\n");
        printf("    lua_Number nb;\n");
        printf("    if (ttisinteger(rb)) {\n");
        printf("      lua_Integer ib = ivalue(rb);\n");
        printf("      setivalue(ra, intop(-, 0, ib));\n");
        printf("    }\n");
        printf("    else if (tonumber(rb, &nb)) {\n");
        printf("      setfltvalue(ra, luai_numunm(L, nb));\n");
        printf("    }\n");
        printf("    else {\n");
        printf("      Protect(luaT_trybinTM(L, rb, rb, ra, TM_UNM));\n");
        printf("    }\n");
      } break;

      case OP_BNOT: {
        printf("    TValue *rb = RB(i);\n");
        printf("    lua_Integer ib;\n");
        printf("    if (tointeger(rb, &ib)) {\n");
        printf("      setivalue(ra, intop(^, ~l_castS2U(0), ib));\n");
        printf("    }\n");
        printf("    else {\n");
        printf("      Protect(luaT_trybinTM(L, rb, rb, ra, TM_BNOT));\n");
        printf("    }\n");
      } break;

      case OP_NOT: {
        printf("    TValue *rb = RB(i);\n");
        printf("    int res = l_isfalse(rb);  /* next assignment may change this value */\n");
        printf("    setbvalue(ra, res);\n");
      } break;

      case OP_LEN: {
        printf("    Protect(luaV_objlen(L, ra, RB(i)));\n");
      } break;

      case OP_CONCAT: {
        printf("    int b = GETARG_B(i);\n");
        printf("    int c = GETARG_C(i);\n");
        printf("    StkId rb;\n");
        printf("    L->top = base + c + 1;  /* mark the end of concat operands */\n");
        printf("    Protect(luaV_concat(L, c - b + 1));\n");
        printf("    ra = RA(i);  /* 'luaV_concat' may invoke TMs and move the stack */\n");
        printf("    rb = base + b;\n");
        printf("    setobjs2s(L, ra, rb);\n");
        printf("    checkGC(L, (ra >= rb ? ra + 1 : rb));\n");
        printf("    L->top = ci->top;  /* restore top */\n");
      } break;

      case OP_JMP: {
        int sbx = GETARG_sBx(i);
        int target = pc + sbx + 1;
        printf("    UNUSED(ra);\n");
        printf("    int a = GETARG_A(i);\n");
        printf("    if (a != 0) luaF_close(L, ci->u.l.base + a - 1);\n");
        printf("    goto label_%d;\n", target);
      } break;
     
      case OP_EQ: {
        printf("    UNUSED(ra);\n");
        printf("    TValue *rb = RKB(i);\n");
        printf("    TValue *rc = RKC(i);\n");
        printf("    int cmp;\n");
        printf("    Protect(cmp = luaV_equalobj(L, rb, rc));\n");
        printf("    if (cmp != GETARG_A(i)) {\n");
        printf("      goto label_%d;\n", pc+2);
        printf("    } else {\n");
        printf("      goto label_%d;\n", pc+1);
        printf("    }\n");
      } break;
 
      case OP_LT: {
        printf("    UNUSED(ra);\n");
        printf("    int cmp;\n");
        printf("    Protect(cmp = luaV_lessthan(L, RKB(i), RKC(i)));\n");
        printf("    if (cmp != GETARG_A(i)) {\n");
        printf("      goto label_%d;\n", pc+2);
        printf("    } else {\n");
        printf("      goto label_%d;\n", pc+1);
        printf("    }\n");
      } break;

      case OP_LE: {
        printf("    UNUSED(ra);\n");
        printf("    int cmp;\n");
        printf("    Protect(cmp = luaV_lessequal(L, RKB(i), RKC(i)));\n");
        printf("    if (cmp != GETARG_A(i)) {\n");
        printf("      goto label_%d;\n", pc+2);
        printf("    } else {\n");
        printf("      goto label_%d;\n", pc+1);
        printf("    }\n");
      } break;
     
      case OP_TEST: {
        printf("    if (GETARG_C(i) ? l_isfalse(ra) : !l_isfalse(ra)) {\n");
        printf("      goto label_%d;\n", pc+2);
        printf("    } else {\n");
        printf("      goto label_%d;\n", pc+1);
        printf("    }\n");
      } break;

      case OP_TESTSET: {
        printf("    TValue *rb = RB(i);\n");
        printf("    if (GETARG_C(i) ? l_isfalse(rb) : !l_isfalse(rb)) {\n");
        printf("      goto label_%d;\n", pc+2);
        printf("    } else {\n");
        printf("      setobjs2s(L, ra, rb);\n");
        printf("      goto label_%d;\n", pc+1);
        printf("    }\n");
      } break;

      case OP_CALL: {
        printf("    int b = GETARG_B(i);\n");
        printf("    int nresults = GETARG_C(i) - 1;\n");
        printf("    if (b != 0) L->top = ra+b;  /* else previous instruction set top */\n");
        printf("    if (luaD_precall(L, ra, nresults)) {  /* C function? */\n");
        printf("      if (nresults >= 0)\n");
        printf("        L->top = ci->top;  /* adjust results */\n");
        printf("      Protect((void)0);  /* update 'base' */\n");
        printf("    } else {  /* Lua function */\n");
        printf("      luaV_execute(L);\n");
        printf("      Protect((void)0);  /* update 'base' */\n");
        printf("    }\n");
      } break;

      case OP_TAILCALL: {
        printf("    int b = GETARG_B(i);\n");
        printf("    if (b != 0) L->top = ra+b;  /* else previous instruction set top */\n");
        printf("    lua_assert(GETARG_C(i) - 1 == LUA_MULTRET);\n");
        printf("    if (luaD_precall(L, ra, LUA_MULTRET)) {  /* C function? */\n");
        printf("      Protect((void)0);  /* update 'base' */\n");
        printf("    }\n");
        printf("    else {\n");
        printf("      luaV_execute(L);\n");
        printf("      Protect((void)0);  /* update 'base' */\n");
        printf("    }\n");

        // I think the "tailcall a c function" path assumes that the next instruction
        // is a return statement and that I can use this on the regular calls as well
        // since we are throwing away the idea of tail calls anyway.
        //
        // But lets check that just in case
        assert(pc+1 < nopcodes);
        Instruction next = code[pc+1];
        assert(GET_OPCODE(next) == OP_RETURN);
        assert(GETARG_B(next) == 0);
      } break;
 
      case OP_RETURN: {
        printf("    int b = GETARG_B(i);\n");
        printf("    if (cl->p->sizep > 0) luaF_close(L, base);\n");
        printf("    int ret = (b != 0 ? b - 1 : cast_int(L->top - ra));\n");
        printf("    luaD_poscall(L, ci, ra, ret);\n");
        printf("    return ret;\n");
      } break;
     
      case OP_FORLOOP: {
        int sbx = GETARG_sBx(i);
        int target = pc + sbx + 1;
        printf("    if (ttisinteger(ra)) {  /* integer loop? */\n");
        printf("      lua_Integer step = ivalue(ra + 2);\n");
        printf("      lua_Integer idx = intop(+, ivalue(ra), step); /* increment index */\n");
        printf("      lua_Integer limit = ivalue(ra + 1);\n");
        printf("      if ((0 < step) ? (idx <= limit) : (limit <= idx)) {\n");
        printf("        chgivalue(ra, idx);  /* update internal index... */\n");
        printf("        setivalue(ra + 3, idx);  /* ...and external index */\n");
        printf("        goto label_%d;  /* jump back */\n", target);
        printf("      }\n");
        printf("    }\n");
        printf("    else {  /* floating loop */\n");
        printf("      lua_Number step = fltvalue(ra + 2);\n");
        printf("      lua_Number idx = luai_numadd(L, fltvalue(ra), step); /* inc. index */\n");
        printf("      lua_Number limit = fltvalue(ra + 1);\n");
        printf("      if (luai_numlt(0, step) ? luai_numle(idx, limit)\n");
        printf("                              : luai_numle(limit, idx)) {\n");
        printf("        chgfltvalue(ra, idx);  /* update internal index... */\n");
        printf("        setfltvalue(ra + 3, idx);  /* ...and external index */\n");
        printf("        goto label_%d;  /* jump back */\n", target);
        printf("      }\n");
        printf("    }\n");
      } break;

      case OP_FORPREP: { 
        int sbx = GETARG_sBx(i);
        int target = pc + sbx + 1;
        printf("    TValue *init = ra;\n");
        printf("    TValue *plimit = ra + 1;\n");
        printf("    TValue *pstep = ra + 2;\n");
        printf("    lua_Integer ilimit;\n");
        printf("    int stopnow;\n");
        printf("    if (ttisinteger(init) && ttisinteger(pstep) &&\n");
        printf("        luaV_forlimit(plimit, &ilimit, ivalue(pstep), &stopnow)) {\n");
        printf("      /* all values are integer */\n");
        printf("      lua_Integer initv = (stopnow ? 0 : ivalue(init));\n");
        printf("      setivalue(plimit, ilimit);\n");
        printf("      setivalue(init, intop(-, initv, ivalue(pstep)));\n");
        printf("    }\n");
        printf("    else {  /* try making all values floats */\n");
        printf("      lua_Number ninit; lua_Number nlimit; lua_Number nstep;\n");
        printf("      if (!tonumber(plimit, &nlimit))\n");
        printf("        luaG_runerror(L, \"'for' limit must be a number\");\n");
        printf("      setfltvalue(plimit, nlimit);\n");
        printf("      if (!tonumber(pstep, &nstep))\n");
        printf("        luaG_runerror(L, \"'for' step must be a number\");\n");
        printf("      setfltvalue(pstep, nstep);\n");
        printf("      if (!tonumber(init, &ninit))\n");
        printf("        luaG_runerror(L, \"'for' initial value must be a number\");\n");
        printf("      setfltvalue(init, luai_numsub(L, ninit, nstep));\n");
        printf("    }\n");
        printf("    goto label_%d;\n", target);
      } break;

      // case OP_TFORCALL: {
      // } break;
 
      // case OP_TFORLOOP: {
      // } break;

      // case OP_SETLIST: {
      // } break;

      case OP_CLOSURE: {
        printf("    Proto *p = cl->p->p[GETARG_Bx(i)];\n");
        printf("    LClosure *ncl = luaV_getcached(p, cl->upvals, base);  /* cached closure*/\n");
        printf("    if (ncl == NULL)  /* no match? */\n");
        printf("      luaV_pushclosure(L, p, cl->upvals, base, ra);  /* create a new one */\n");
        printf("    else\n");
        printf("      setclLvalue(L, ra, ncl);  /* push cashed closure */\n");
        printf("    checkGC(L, ra + 1);\n");
      } break;

      // case OP_VARARG: {
      // } break;

      // case OP_EXTRAARG: {
      //   assert(0);
      // } break;

      default: {
        fprintf(stderr, "Uninplemented opcode %s", luaP_opnames[o]);
        fatal("aborting");
        //printf("    //\n");
        //printf("    // NOT IMPLEMENTED\n");
        //printf("    //\n");
      } break;
    }
    printf("  }\n");
    printf("\n");
  }
  printf("}\n");
  printf("\n");
}

#define SS(x)	((x==1)?"":"s")
#define S(x)	(int)(x),SS(x)

static void PrintFunction(const Proto* f)
{
 int n=f->sizep;
 PrintCode(f);
 for (int i=0; i<n; i++) PrintFunction(f->p[i]);
}
