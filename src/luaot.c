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

#define DEFAULT_PROGNAME "luaot"

// Program options:
static const char* progname;        /* actual program name from argv[0] */
static const char* input_filename;  /* path to input Lua module */
static const char* output_filename; /* path to output C library module */
static const char* module_name;     /* name of generated module (for luaopen_XXX) */

// Global variables
static int NFUNCTIONS = 0;  /* ID of magic functions */ 
static FILE * OUTFILE;



static void fatal(const char* message)
{
    fprintf(stderr,"%s: %s\n",progname,message);
    exit(EXIT_FAILURE);
}

static void usage()
{
    fprintf(stderr,"usage: %s INPUT -o OUTPUT\n", progname);
    exit(EXIT_FAILURE);
}

// Throw away the directory of a filename
char * basename (const char *path)
{
    const char *start = path;
    for (const char *s = path; *s != '\0'; s++){
        if (*s == '/') {
            start = s + 1;
        }
    }

    return strdup(start);
}

// Split a filename into a name and extension.
void split_ext(const char *path, char **noext_out, char **ext_out)
{
    char * pathcopy = strdup(path);

    char * dot = strrchr(pathcopy, '.');
    if (dot) {
        *dot = '\0';
        *noext_out = strdup(pathcopy);
        *ext_out = strdup(dot+1);
    } else {
        *noext_out = NULL;
        *ext_out = NULL;
    }

    free (pathcopy);
}

static void doargs(int argc, char* argv[])
{
    progname = DEFAULT_PROGNAME;
    input_filename = NULL;
    output_filename = NULL;
    module_name = NULL;

    if (argv[0] !=NULL && argv[0][0] != '\0') {
        progname=argv[0];
    }

    int npos = 0;
    int reading_options = 1;
    for (int i=1; i < argc; i++) {
        const char *arg = argv[i];

        if (reading_options && arg[0] == '-') {
            
            if (0 == strcmp(arg, "--")) {
                reading_options = 0;
            } else if (0 == strcmp(arg, "-h") || 0 == strcmp(arg, "--help")){
                usage();
            } else if (0 == strcmp(arg, "-o")) {
                i += 1;
                if (i >= argc ) {
                    fprintf(stderr, "%s: Missing argument for -o\n", progname);
                    usage();
                }
                output_filename = argv[i];
            } else {
                fprintf(stderr,"%s: Unrecognized option %s\n", progname, arg);
                usage();
            }

        } else {

            if (npos == 0) {
                input_filename = arg;
            } else {
                fprintf(stderr,"%s: Too many positional parameters\n", progname);
                usage();
            }

            npos++;
        }
    }

    if (npos < 1) {
        fprintf(stderr, "%s: Too few positional parameters\n", progname);
        usage();
    }

    if (!output_filename) {
        fprintf(stderr, "%s, -o option is required\n", progname);
        usage();
    }

    char * input_basename = NULL;
    char * input_basename_noext = NULL;
    char * input_basename_ext = NULL;

    char * output_basename = NULL;
    char * output_basename_noext = NULL;
    char * output_basename_ext = NULL;

    input_basename = basename(input_filename);
    output_basename = basename(output_filename);

    split_ext(input_basename, &input_basename_noext, &input_basename_ext);
    if (input_basename_ext == NULL || 0 != strcmp(input_basename_ext, "lua")) {
        fatal("input file must have a .lua extension");
    }
    
    split_ext(output_basename, &output_basename_noext, &output_basename_ext);
    if (output_basename_ext == NULL || 0 != strcmp(output_basename_ext, "c")) {
        fatal("output file must have a .c extension");
    }

    if (0 != strcmp(input_basename_noext, output_basename_noext)) {
        fatal("the names of the input and output files must match");
        // We do this because the C module needs to know its Lua module name
        // because of the luaopen_ interface and it is easier for everyone
        // if I force the ".lua", ".c" and ".so" filenames to match.
    }

    module_name = strdup(input_basename_noext);

    for (const char *s = module_name; *s != '\0'; s++) {
        if (! (isalnum(*s) || *s == '_')) {
            fatal("the name of the input module contains invalid characters (only letters, numbers and underscores are allowed).");
        }
    }

    free(input_basename);
    free(input_basename_noext);
    free(input_basename_ext);

    free(output_basename);
    free(output_basename_noext);
    free(output_basename_ext);
}

#define toproto(L,i) getproto(L->top+(i))

static int pmain(lua_State* L)
{
    if (luaL_loadfile(L, input_filename) != LUA_OK) fatal(lua_tostring(L,-1));

    const Proto* f = toproto(L, -1);

    fprintf(OUTFILE, "#include \"luaot-generated-header.c\"\n");
    fprintf(OUTFILE, "\n");

    {
        // Generated C implementations
        NFUNCTIONS = 0;
        PrintFunction(f);
    }
    
    {
        fprintf(OUTFILE, "ZZ_MAGIC_FUNC zz_magic_functions[%d] = {\n", NFUNCTIONS);
        for (int i=0; i < NFUNCTIONS; i++) {
            fprintf(OUTFILE, "  zz_magic_function_%d,\n", i);
        }
        fprintf(OUTFILE, "};\n");
        fprintf(OUTFILE, "\n");
    }

    {
        // The original Lua code
        //
        // We need this right now because our code works by taking an existing
        // Proto* and patching it by setting the magic_implementation field.
        //
        // Right now I am serializing the Lua source code (as a char array because
        // trying to serialize as a string blew the C99 maximum string length).
        // I would prefer to only serialize as bytecode or perhaps even only serialize 
        // the parts of the Proto* that we need but I couldn't figure out how to do 
        // this yet.
        
        FILE *infile = fopen(input_filename, "r");
        if (!infile) fatal("could not open input file");

        fprintf(OUTFILE, "const char ZZ_ORIGINAL_SOURCE_CODE[] = {\n");
        {int c; while (c = fgetc(infile), c != EOF) {
            fprintf(OUTFILE, "%d, ", c);
        }}
        fprintf(OUTFILE, "};\n");
        fprintf(OUTFILE, "\n");

        fclose(infile);
 
    }

    fprintf(OUTFILE, "#define ZZ_LUAOPEN_NAME luaopen_%s\n", module_name);
    fprintf(OUTFILE, "\n");

    fprintf(OUTFILE, "#include \"luaot-generated-footer.c\"\n");

    return 0;
}

int main(int argc, char* argv[])
{
    doargs(argc,argv);

    OUTFILE = fopen(output_filename, "w");
    if (!OUTFILE) {
        fatal("could not open output file for writing");
    }

    lua_State* L = luaL_newstate();
    if (L==NULL) fatal("cannot create state: not enough memory");
    lua_pushcfunction(L, &pmain);
    if (lua_pcall(L,0,0,0)!=LUA_OK) fatal(lua_tostring(L,-1));
    lua_close(L);

    fclose(OUTFILE);

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
 fprintf(OUTFILE, "%c",'"');
 for (i=0; i<n; i++)
 {
  int c=(int)(unsigned char)s[i];
  switch (c)
  {
   case '"':  fprintf(OUTFILE, "\\\""); break;
   case '\\': fprintf(OUTFILE, "\\\\"); break;
   case '\a': fprintf(OUTFILE, "\\a"); break;
   case '\b': fprintf(OUTFILE, "\\b"); break;
   case '\f': fprintf(OUTFILE, "\\f"); break;
   case '\n': fprintf(OUTFILE, "\\n"); break;
   case '\r': fprintf(OUTFILE, "\\r"); break;
   case '\t': fprintf(OUTFILE, "\\t"); break;
   case '\v': fprintf(OUTFILE, "\\v"); break;
   default:	if (isprint(c))
   			fprintf(OUTFILE, "%c",c);
		else
			fprintf(OUTFILE, "\\%03d",c);
  }
 }
 fprintf(OUTFILE, "%c",'"');
}

static void PrintConstant(const Proto* f, int i)
{
 const TValue* o=&f->k[i];
 switch (ttype(o))
 {
  case LUA_TNIL:
	fprintf(OUTFILE, "nil");
	break;
  case LUA_TBOOLEAN:
	fprintf(OUTFILE, bvalue(o) ? "true" : "false");
	break;
  case LUA_TNUMFLT:
	{
	char buff[100];
	sprintf(buff,LUA_NUMBER_FMT,fltvalue(o));
	fprintf(OUTFILE, "%s",buff);
	if (buff[strspn(buff,"-0123456789")]=='\0') fprintf(OUTFILE, ".0");
	break;
	}
  case LUA_TNUMINT:
	fprintf(OUTFILE, LUA_INTEGER_FMT,ivalue(o));
	break;
  case LUA_TSHRSTR: case LUA_TLNGSTR:
	PrintString(tsvalue(o));
	break;
  default:				/* cannot happen */
	fprintf(OUTFILE, "? type=%d",ttype(o));
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

  fprintf(OUTFILE, "  // ");
  //fprintf(OUTFILE, "\t%d\t",pc+1);
  if (line>0) fprintf(OUTFILE, "[%d]\t",line); else fprintf(OUTFILE, "[-]\t");
  fprintf(OUTFILE, "%-9s\t",luaP_opnames[o]);
  switch (getOpMode(o))
  {
    case iABC:
      fprintf(OUTFILE, "%d",a);
      if (getBMode(o)!=OpArgN) fprintf(OUTFILE, " %d",ISK(b) ? (MYK(INDEXK(b))) : b);
      if (getCMode(o)!=OpArgN) fprintf(OUTFILE, " %d",ISK(c) ? (MYK(INDEXK(c))) : c);
    break;

    case iABx:
      fprintf(OUTFILE, "%d",a);
      if (getBMode(o)==OpArgK) fprintf(OUTFILE, " %d",MYK(bx));
      if (getBMode(o)==OpArgU) fprintf(OUTFILE, " %d",bx);
    break;

    case iAsBx:
      fprintf(OUTFILE, "%d %d",a,sbx);
    break;

    case iAx:
      fprintf(OUTFILE, "%d",MYK(ax));
    break;
  }

  switch (o)
  {
    case OP_LOADK:
      fprintf(OUTFILE, "\t; "); PrintConstant(f,bx);
    break;

    case OP_GETUPVAL:
    case OP_SETUPVAL:
      fprintf(OUTFILE, "\t; %s",UPVALNAME(b));
    break;

    case OP_GETTABUP:
      fprintf(OUTFILE, "\t; %s",UPVALNAME(b));
      if (ISK(c)) { fprintf(OUTFILE, " "); PrintConstant(f,INDEXK(c)); }
    break;

    case OP_SETTABUP:
      fprintf(OUTFILE, "\t; %s",UPVALNAME(a));
      if (ISK(b)) { fprintf(OUTFILE, " "); PrintConstant(f,INDEXK(b)); }
      if (ISK(c)) { fprintf(OUTFILE, " "); PrintConstant(f,INDEXK(c)); }
    break;

    case OP_GETTABLE:
    case OP_SELF:
      if (ISK(c)) { fprintf(OUTFILE, "\t; "); PrintConstant(f,INDEXK(c)); }
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
        fprintf(OUTFILE, "\t; ");
        if (ISK(b)) PrintConstant(f,INDEXK(b)); else fprintf(OUTFILE, "-");
        fprintf(OUTFILE, " ");
        if (ISK(c)) PrintConstant(f,INDEXK(c)); else fprintf(OUTFILE, "-");
      }
    break;

    case OP_JMP:
    case OP_FORLOOP:
    case OP_FORPREP:
    case OP_TFORLOOP:
      fprintf(OUTFILE, "\t; to %d",sbx+pc+1);
    break;

    case OP_CLOSURE:
      fprintf(OUTFILE, "\t; %p",VOID(f->p[bx]));
    break;

    case OP_SETLIST:
      if (c==0) fprintf(OUTFILE, "\t; %d",(int)code[++pc]); else fprintf(OUTFILE, "\t; %d",c);
    break;

    case OP_EXTRAARG:
      fprintf(OUTFILE, "\t; "); PrintConstant(f,ax);
    break;

    default:
    break;
  }

  fprintf(OUTFILE, "\n");
}

static void PrintCode(const Proto* f)
{
  const Instruction* code=f->code;
  int nopcodes=f->sizecode;

  fprintf(OUTFILE, "// source = %s\n", getstr(f->source));
  fprintf(OUTFILE, "// linedefined = %d\n", f->linedefined);
  fprintf(OUTFILE, "// lastlinedefined = %d\n", f->lastlinedefined);
  fprintf(OUTFILE, "// what = %s\n", (f->linedefined == 0) ? "main" : "Lua");

  fprintf(OUTFILE, "static int zz_magic_function_%d (lua_State *L, LClosure *cl)\n", NFUNCTIONS);
  fprintf(OUTFILE, "{\n");
  fprintf(OUTFILE, "  CallInfo *ci = L->ci;\n");
  fprintf(OUTFILE, "  TValue *k = cl->p->k;\n");
  fprintf(OUTFILE, "  StkId base = ci->u.l.base;\n");
  fprintf(OUTFILE, "  \n");
  fprintf(OUTFILE, "  // Avoid warnings if the function has few opcodes:\n");
  fprintf(OUTFILE, "  UNUSED(ci);\n");
  fprintf(OUTFILE, "  UNUSED(k);\n");
  fprintf(OUTFILE, "  UNUSED(base);\n");
  fprintf(OUTFILE, "  \n");
 

  for (int pc=0; pc<nopcodes; pc++)
  {
    PrintOpcodeComment(f, pc);

    Instruction i=code[pc];
    OpCode o=GET_OPCODE(i);

    fprintf(OUTFILE, "  label_%d: {\n", pc);
    fprintf(OUTFILE, "    Instruction i = 0x%08x;\n", i);
    fprintf(OUTFILE, "    StkId ra = RA(i);\n");
    switch (o) {
      
      case OP_MOVE: {
        fprintf(OUTFILE, "    setobjs2s(L, ra, RB(i));\n");
      } break;
      
      case OP_LOADK: {
        fprintf(OUTFILE, "    TValue *rb = k + GETARG_Bx(i);\n");
        fprintf(OUTFILE, "    setobj2s(L, ra, rb);\n");
      } break;

      case OP_LOADKX: {
        assert(pc + 1 < nopcodes);
        Instruction next_i = code[pc+1];
        fprintf(OUTFILE, "    TValue *rb;\n");
        fprintf(OUTFILE, "    lua_assert(GET_OPCODE(%d) == OP_EXTRAARG);\n", next_i);
        fprintf(OUTFILE, "    rb = k + GETARG_Ax(%d);\n", next_i);
        fprintf(OUTFILE, "    setobj2s(L, ra, rb);\n");
      } break;
      
      case OP_LOADBOOL: {
        fprintf(OUTFILE, "    setbvalue(ra, GETARG_B(i));\n");
        fprintf(OUTFILE, "    if (GETARG_C(i)) {\n");
        fprintf(OUTFILE, "      goto label_%d; /* skip next instruction (if C) */\n", pc+2);
        fprintf(OUTFILE, "    }\n");
      } break;
      
      case OP_LOADNIL: {
        fprintf(OUTFILE, "    int b = GETARG_B(i);\n");
        fprintf(OUTFILE, "    do {\n");
        fprintf(OUTFILE, "      setnilvalue(ra++);\n");
        fprintf(OUTFILE, "    } while (b--);\n");
      } break;
 
      case OP_GETUPVAL: {
        fprintf(OUTFILE, "    int b = GETARG_B(i);\n");
        fprintf(OUTFILE, "    setobj2s(L, ra, cl->upvals[b]->v);\n");
      } break;
     
      case OP_GETTABUP: {
        fprintf(OUTFILE, "    TValue *upval = cl->upvals[GETARG_B(i)]->v;\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    gettableProtected(L, upval, rc, ra);\n");
      } break;
      
      case OP_GETTABLE: {
        fprintf(OUTFILE, "    StkId rb = RB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    gettableProtected(L, rb, rc, ra);\n");
      } break;

      case OP_SETTABUP: {
        fprintf(OUTFILE, "    UNUSED(ra);\n");
        fprintf(OUTFILE, "    TValue *upval = cl->upvals[GETARG_A(i)]->v;\n");
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    settableProtected(L, upval, rb, rc);\n");
      } break;

      case OP_SETUPVAL: {
        fprintf(OUTFILE, "    UpVal *uv = cl->upvals[GETARG_B(i)];\n");
        fprintf(OUTFILE, "    setobj(L, uv->v, ra);\n");
        fprintf(OUTFILE, "    luaC_upvalbarrier(L, uv);\n");
      } break;
 
      case OP_SETTABLE: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    settableProtected(L, ra, rb, rc);\n");
      } break;

      case OP_NEWTABLE: {
        fprintf(OUTFILE, "    int b = GETARG_B(i);\n");
        fprintf(OUTFILE, "    int c = GETARG_C(i);\n");
        fprintf(OUTFILE, "    Table *t = luaH_new(L);\n");
        fprintf(OUTFILE, "    sethvalue(L, ra, t);\n");
        fprintf(OUTFILE, "    if (b != 0 || c != 0)\n");
        fprintf(OUTFILE, "      luaH_resize(L, t, luaO_fb2int(b), luaO_fb2int(c));\n");
        fprintf(OUTFILE, "    checkGC(L, ra + 1);\n");
      } break;

      case OP_SELF: {
        fprintf(OUTFILE, "    const TValue *aux;\n");
        fprintf(OUTFILE, "    StkId rb = RB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    TString *key = tsvalue(rc);  /* key must be a string */\n");
        fprintf(OUTFILE, "    setobjs2s(L, ra + 1, rb);\n");
        fprintf(OUTFILE, "    if (luaV_fastget(L, rb, key, aux, luaH_getstr)) {\n");
        fprintf(OUTFILE, "      setobj2s(L, ra, aux);\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else Protect(luaV_finishget(L, rb, rc, ra, aux));\n");
      } break;

      case OP_ADD: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Number nb; lua_Number nc;\n");
        fprintf(OUTFILE, "    if (ttisinteger(rb) && ttisinteger(rc)) {\n");
        fprintf(OUTFILE, "      lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);\n");
        fprintf(OUTFILE, "      setivalue(ra, intop(+, ib, ic));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        fprintf(OUTFILE, "      setfltvalue(ra, luai_numadd(L, nb, nc));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_ADD)); }\n");
      } break;

      case OP_SUB: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Number nb; lua_Number nc;\n");
        fprintf(OUTFILE, "    if (ttisinteger(rb) && ttisinteger(rc)) {\n");
        fprintf(OUTFILE, "      lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);\n");
        fprintf(OUTFILE, "      setivalue(ra, intop(-, ib, ic));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        fprintf(OUTFILE, "      setfltvalue(ra, luai_numsub(L, nb, nc));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SUB)); }\n");
      } break;
      
      case OP_MUL: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Number nb; lua_Number nc;\n");
        fprintf(OUTFILE, "    if (ttisinteger(rb) && ttisinteger(rc)) {\n");
        fprintf(OUTFILE, "      lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);\n");
        fprintf(OUTFILE, "      setivalue(ra, intop(*, ib, ic));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        fprintf(OUTFILE, "      setfltvalue(ra, luai_nummul(L, nb, nc));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_MUL)); }\n");
      } break;

      case OP_DIV: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Number nb; lua_Number nc;\n");
        fprintf(OUTFILE, "    if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        fprintf(OUTFILE, "      setfltvalue(ra, luai_numdiv(L, nb, nc));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_DIV)); }\n");
      } break;

      case OP_BAND: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Integer ib; lua_Integer ic;\n");
        fprintf(OUTFILE, "    if (tointeger(rb, &ib) && tointeger(rc, &ic)) {\n");
        fprintf(OUTFILE, "      setivalue(ra, intop(&, ib, ic));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BAND)); }\n");
      } break;

      case OP_BOR: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Integer ib; lua_Integer ic;\n");
        fprintf(OUTFILE, "    if (tointeger(rb, &ib) && tointeger(rc, &ic)) {\n");
        fprintf(OUTFILE, "      setivalue(ra, intop(|, ib, ic));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BOR)); }\n");
      } break;

      case OP_BXOR: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Integer ib; lua_Integer ic;\n");
        fprintf(OUTFILE, "    if (tointeger(rb, &ib) && tointeger(rc, &ic)) {\n");
        fprintf(OUTFILE, "      setivalue(ra, intop(^, ib, ic));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BXOR)); }\n");
      } break;

      case OP_SHL: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Integer ib; lua_Integer ic;\n");
        fprintf(OUTFILE, "    if (tointeger(rb, &ib) && tointeger(rc, &ic)) {\n");
        fprintf(OUTFILE, "      setivalue(ra, luaV_shiftl(ib, ic));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SHL)); }\n");
      } break;

      case OP_SHR: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Integer ib; lua_Integer ic;\n");
        fprintf(OUTFILE, "    if (tointeger(rb, &ib) && tointeger(rc, &ic)) {\n");
        fprintf(OUTFILE, "      setivalue(ra, luaV_shiftl(ib, -ic));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SHR)); }\n");
      } break;

      case OP_MOD: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Number nb; lua_Number nc;\n");
        fprintf(OUTFILE, "    if (ttisinteger(rb) && ttisinteger(rc)) {\n");
        fprintf(OUTFILE, "      lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);\n");
        fprintf(OUTFILE, "      setivalue(ra, luaV_mod(L, ib, ic));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        fprintf(OUTFILE, "      lua_Number m;\n");
        fprintf(OUTFILE, "      luai_nummod(L, nb, nc, m);\n");
        fprintf(OUTFILE, "      setfltvalue(ra, m);\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_MOD)); }\n");
      } break;

      case OP_IDIV: { /* floor division */
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Number nb; lua_Number nc;\n");
        fprintf(OUTFILE, "    if (ttisinteger(rb) && ttisinteger(rc)) {\n");
        fprintf(OUTFILE, "      lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);\n");
        fprintf(OUTFILE, "      setivalue(ra, luaV_div(L, ib, ic));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        fprintf(OUTFILE, "      setfltvalue(ra, luai_numidiv(L, nb, nc));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_IDIV)); }\n");
      } break;
      
      case OP_POW: {
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    lua_Number nb; lua_Number nc;\n");
        fprintf(OUTFILE, "    if (tonumber(rb, &nb) && tonumber(rc, &nc)) {\n");
        fprintf(OUTFILE, "      setfltvalue(ra, luai_numpow(L, nb, nc));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_POW)); }\n");
      } break;

      case OP_UNM: {
        fprintf(OUTFILE, "    TValue *rb = RB(i);\n");
        fprintf(OUTFILE, "    lua_Number nb;\n");
        fprintf(OUTFILE, "    if (ttisinteger(rb)) {\n");
        fprintf(OUTFILE, "      lua_Integer ib = ivalue(rb);\n");
        fprintf(OUTFILE, "      setivalue(ra, intop(-, 0, ib));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else if (tonumber(rb, &nb)) {\n");
        fprintf(OUTFILE, "      setfltvalue(ra, luai_numunm(L, nb));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else {\n");
        fprintf(OUTFILE, "      Protect(luaT_trybinTM(L, rb, rb, ra, TM_UNM));\n");
        fprintf(OUTFILE, "    }\n");
      } break;

      case OP_BNOT: {
        fprintf(OUTFILE, "    TValue *rb = RB(i);\n");
        fprintf(OUTFILE, "    lua_Integer ib;\n");
        fprintf(OUTFILE, "    if (tointeger(rb, &ib)) {\n");
        fprintf(OUTFILE, "      setivalue(ra, intop(^, ~l_castS2U(0), ib));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else {\n");
        fprintf(OUTFILE, "      Protect(luaT_trybinTM(L, rb, rb, ra, TM_BNOT));\n");
        fprintf(OUTFILE, "    }\n");
      } break;

      case OP_NOT: {
        fprintf(OUTFILE, "    TValue *rb = RB(i);\n");
        fprintf(OUTFILE, "    int res = l_isfalse(rb);  /* next assignment may change this value */\n");
        fprintf(OUTFILE, "    setbvalue(ra, res);\n");
      } break;

      case OP_LEN: {
        fprintf(OUTFILE, "    Protect(luaV_objlen(L, ra, RB(i)));\n");
      } break;

      case OP_CONCAT: {
        fprintf(OUTFILE, "    int b = GETARG_B(i);\n");
        fprintf(OUTFILE, "    int c = GETARG_C(i);\n");
        fprintf(OUTFILE, "    StkId rb;\n");
        fprintf(OUTFILE, "    L->top = base + c + 1;  /* mark the end of concat operands */\n");
        fprintf(OUTFILE, "    Protect(luaV_concat(L, c - b + 1));\n");
        fprintf(OUTFILE, "    ra = RA(i);  /* 'luaV_concat' may invoke TMs and move the stack */\n");
        fprintf(OUTFILE, "    rb = base + b;\n");
        fprintf(OUTFILE, "    setobjs2s(L, ra, rb);\n");
        fprintf(OUTFILE, "    checkGC(L, (ra >= rb ? ra + 1 : rb));\n");
        fprintf(OUTFILE, "    L->top = ci->top;  /* restore top */\n");
      } break;

      case OP_JMP: {
        int target = pc + GETARG_sBx(i) + 1;
        fprintf(OUTFILE, "    UNUSED(ra);\n");
        fprintf(OUTFILE, "    int a = GETARG_A(i);\n");
        fprintf(OUTFILE, "    if (a != 0) luaF_close(L, ci->u.l.base + a - 1);\n");
        fprintf(OUTFILE, "    goto label_%d;\n", target);
      } break;
     
      case OP_EQ: {
        fprintf(OUTFILE, "    UNUSED(ra);\n");
        fprintf(OUTFILE, "    TValue *rb = RKB(i);\n");
        fprintf(OUTFILE, "    TValue *rc = RKC(i);\n");
        fprintf(OUTFILE, "    int cmp;\n");
        fprintf(OUTFILE, "    Protect(cmp = luaV_equalobj(L, rb, rc));\n");
        fprintf(OUTFILE, "    if (cmp != GETARG_A(i)) {\n");
        fprintf(OUTFILE, "      goto label_%d;\n", pc+2);
        fprintf(OUTFILE, "    } else {\n");
        fprintf(OUTFILE, "      goto label_%d;\n", pc+1);
        fprintf(OUTFILE, "    }\n");
      } break;
 
      case OP_LT: {
        fprintf(OUTFILE, "    UNUSED(ra);\n");
        fprintf(OUTFILE, "    int cmp;\n");
        fprintf(OUTFILE, "    Protect(cmp = luaV_lessthan(L, RKB(i), RKC(i)));\n");
        fprintf(OUTFILE, "    if (cmp != GETARG_A(i)) {\n");
        fprintf(OUTFILE, "      goto label_%d;\n", pc+2);
        fprintf(OUTFILE, "    } else {\n");
        fprintf(OUTFILE, "      goto label_%d;\n", pc+1);
        fprintf(OUTFILE, "    }\n");
      } break;

      case OP_LE: {
        fprintf(OUTFILE, "    UNUSED(ra);\n");
        fprintf(OUTFILE, "    int cmp;\n");
        fprintf(OUTFILE, "    Protect(cmp = luaV_lessequal(L, RKB(i), RKC(i)));\n");
        fprintf(OUTFILE, "    if (cmp != GETARG_A(i)) {\n");
        fprintf(OUTFILE, "      goto label_%d;\n", pc+2);
        fprintf(OUTFILE, "    } else {\n");
        fprintf(OUTFILE, "      goto label_%d;\n", pc+1);
        fprintf(OUTFILE, "    }\n");
      } break;
     
      case OP_TEST: {
        fprintf(OUTFILE, "    if (GETARG_C(i) ? l_isfalse(ra) : !l_isfalse(ra)) {\n");
        fprintf(OUTFILE, "      goto label_%d;\n", pc+2);
        fprintf(OUTFILE, "    } else {\n");
        fprintf(OUTFILE, "      goto label_%d;\n", pc+1);
        fprintf(OUTFILE, "    }\n");
      } break;

      case OP_TESTSET: {
        fprintf(OUTFILE, "    TValue *rb = RB(i);\n");
        fprintf(OUTFILE, "    if (GETARG_C(i) ? l_isfalse(rb) : !l_isfalse(rb)) {\n");
        fprintf(OUTFILE, "      goto label_%d;\n", pc+2);
        fprintf(OUTFILE, "    } else {\n");
        fprintf(OUTFILE, "      setobjs2s(L, ra, rb);\n");
        fprintf(OUTFILE, "      goto label_%d;\n", pc+1);
        fprintf(OUTFILE, "    }\n");
      } break;

      case OP_CALL: {
        fprintf(OUTFILE, "    int b = GETARG_B(i);\n");
        fprintf(OUTFILE, "    int nresults = GETARG_C(i) - 1;\n");
        fprintf(OUTFILE, "    if (b != 0) L->top = ra+b;  /* else previous instruction set top */\n");
        fprintf(OUTFILE, "    if (luaD_precall(L, ra, nresults)) {  /* C function? */\n");
        fprintf(OUTFILE, "      if (nresults >= 0)\n");
        fprintf(OUTFILE, "        L->top = ci->top;  /* adjust results */\n");
        fprintf(OUTFILE, "      Protect((void)0);  /* update 'base' */\n");
        fprintf(OUTFILE, "    } else {  /* Lua function */\n");
        fprintf(OUTFILE, "      luaV_execute(L);\n");
        fprintf(OUTFILE, "      Protect((void)0);  /* update 'base' */\n");
        fprintf(OUTFILE, "    }\n");
      } break;

      case OP_TAILCALL: {
        fprintf(OUTFILE, "    int b = GETARG_B(i);\n");
        fprintf(OUTFILE, "    if (b != 0) L->top = ra+b;  /* else previous instruction set top */\n");
        fprintf(OUTFILE, "    lua_assert(GETARG_C(i) - 1 == LUA_MULTRET);\n");
        fprintf(OUTFILE, "    if (luaD_precall(L, ra, LUA_MULTRET)) {  /* C function? */\n");
        fprintf(OUTFILE, "      Protect((void)0);  /* update 'base' */\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else {\n");
        fprintf(OUTFILE, "      luaV_execute(L);\n");
        fprintf(OUTFILE, "      Protect((void)0);  /* update 'base' */\n");
        fprintf(OUTFILE, "    }\n");

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
        fprintf(OUTFILE, "    int b = GETARG_B(i);\n");
        fprintf(OUTFILE, "    if (cl->p->sizep > 0) luaF_close(L, base);\n");
        fprintf(OUTFILE, "    int ret = (b != 0 ? b - 1 : cast_int(L->top - ra));\n");
        fprintf(OUTFILE, "    luaD_poscall(L, ci, ra, ret);\n");
        fprintf(OUTFILE, "    return ret;\n");
      } break;
     
      case OP_FORLOOP: {
        int target = pc + GETARG_sBx(i) + 1;
        fprintf(OUTFILE, "    if (ttisinteger(ra)) {  /* integer loop? */\n");
        fprintf(OUTFILE, "      lua_Integer step = ivalue(ra + 2);\n");
        fprintf(OUTFILE, "      lua_Integer idx = intop(+, ivalue(ra), step); /* increment index */\n");
        fprintf(OUTFILE, "      lua_Integer limit = ivalue(ra + 1);\n");
        fprintf(OUTFILE, "      if ((0 < step) ? (idx <= limit) : (limit <= idx)) {\n");
        fprintf(OUTFILE, "        chgivalue(ra, idx);  /* update internal index... */\n");
        fprintf(OUTFILE, "        setivalue(ra + 3, idx);  /* ...and external index */\n");
        fprintf(OUTFILE, "        goto label_%d;  /* jump back */\n", target);
        fprintf(OUTFILE, "      }\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else {  /* floating loop */\n");
        fprintf(OUTFILE, "      lua_Number step = fltvalue(ra + 2);\n");
        fprintf(OUTFILE, "      lua_Number idx = luai_numadd(L, fltvalue(ra), step); /* inc. index */\n");
        fprintf(OUTFILE, "      lua_Number limit = fltvalue(ra + 1);\n");
        fprintf(OUTFILE, "      if (luai_numlt(0, step) ? luai_numle(idx, limit)\n");
        fprintf(OUTFILE, "                              : luai_numle(limit, idx)) {\n");
        fprintf(OUTFILE, "        chgfltvalue(ra, idx);  /* update internal index... */\n");
        fprintf(OUTFILE, "        setfltvalue(ra + 3, idx);  /* ...and external index */\n");
        fprintf(OUTFILE, "        goto label_%d;  /* jump back */\n", target);
        fprintf(OUTFILE, "      }\n");
        fprintf(OUTFILE, "    }\n");
      } break;

      case OP_FORPREP: { 
        int target = pc + GETARG_sBx(i) + 1;
        fprintf(OUTFILE, "    TValue *init = ra;\n");
        fprintf(OUTFILE, "    TValue *plimit = ra + 1;\n");
        fprintf(OUTFILE, "    TValue *pstep = ra + 2;\n");
        fprintf(OUTFILE, "    lua_Integer ilimit;\n");
        fprintf(OUTFILE, "    int stopnow;\n");
        fprintf(OUTFILE, "    if (ttisinteger(init) && ttisinteger(pstep) &&\n");
        fprintf(OUTFILE, "        luaV_forlimit(plimit, &ilimit, ivalue(pstep), &stopnow)) {\n");
        fprintf(OUTFILE, "      /* all values are integer */\n");
        fprintf(OUTFILE, "      lua_Integer initv = (stopnow ? 0 : ivalue(init));\n");
        fprintf(OUTFILE, "      setivalue(plimit, ilimit);\n");
        fprintf(OUTFILE, "      setivalue(init, intop(-, initv, ivalue(pstep)));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    else {  /* try making all values floats */\n");
        fprintf(OUTFILE, "      lua_Number ninit; lua_Number nlimit; lua_Number nstep;\n");
        fprintf(OUTFILE, "      if (!tonumber(plimit, &nlimit))\n");
        fprintf(OUTFILE, "        luaG_runerror(L, \"'for' limit must be a number\");\n");
        fprintf(OUTFILE, "      setfltvalue(plimit, nlimit);\n");
        fprintf(OUTFILE, "      if (!tonumber(pstep, &nstep))\n");
        fprintf(OUTFILE, "        luaG_runerror(L, \"'for' step must be a number\");\n");
        fprintf(OUTFILE, "      setfltvalue(pstep, nstep);\n");
        fprintf(OUTFILE, "      if (!tonumber(init, &ninit))\n");
        fprintf(OUTFILE, "        luaG_runerror(L, \"'for' initial value must be a number\");\n");
        fprintf(OUTFILE, "      setfltvalue(init, luai_numsub(L, ninit, nstep));\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    goto label_%d;\n", target);
      } break;

      case OP_TFORCALL: {
        fprintf(OUTFILE, "    StkId cb = ra + 3;  /* call base */\n");
        fprintf(OUTFILE, "    setobjs2s(L, cb+2, ra+2);\n");
        fprintf(OUTFILE, "    setobjs2s(L, cb+1, ra+1);\n");
        fprintf(OUTFILE, "    setobjs2s(L, cb, ra);\n");
        fprintf(OUTFILE, "    L->top = cb + 3;  /* func. + 2 args (state and index) */\n");
        fprintf(OUTFILE, "    Protect(luaD_call(L, cb, GETARG_C(i)));\n");
        fprintf(OUTFILE, "    L->top = ci->top;\n");

        assert(pc+1 < nopcodes);
        assert(GET_OPCODE(code[pc+1]) == OP_TFORLOOP);
      } break;
 
      case OP_TFORLOOP: {
        int target = pc + GETARG_sBx(i) + 1;
        fprintf(OUTFILE, "    if (!ttisnil(ra + 1)) {  /* continue loop? */\n");
        fprintf(OUTFILE, "      setobjs2s(L, ra, ra + 1);  /* save control variable */\n");
        fprintf(OUTFILE, "      goto label_%d; /* jump back */\n", target);
        fprintf(OUTFILE, "    }\n");
      } break;

      case OP_SETLIST: {
        assert(pc + 1 < nopcodes);
        Instruction next_i = code[pc+1];

        fprintf(OUTFILE, "    int n = GETARG_B(i);\n");
        fprintf(OUTFILE, "    int c = GETARG_C(i);\n");
        fprintf(OUTFILE, "    unsigned int last;\n");
        fprintf(OUTFILE, "    Table *h;\n");
        fprintf(OUTFILE, "    if (n == 0) n = cast_int(L->top - ra) - 1;\n");
        fprintf(OUTFILE, "    if (c == 0) {\n");
        fprintf(OUTFILE, "      lua_assert(GET_OPCODE(%d) == OP_EXTRAARG);\n", next_i);
        fprintf(OUTFILE, "      c = GETARG_Ax(%d);\n", next_i);
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    h = hvalue(ra);\n");
        fprintf(OUTFILE, "    last = ((c-1)*LFIELDS_PER_FLUSH) + n;\n");
        fprintf(OUTFILE, "    if (last > h->sizearray)  /* needs more space? */\n");
        fprintf(OUTFILE, "      luaH_resizearray(L, h, last);  /* preallocate it at once */\n");
        fprintf(OUTFILE, "    for (; n > 0; n--) {\n");
        fprintf(OUTFILE, "      TValue *val = ra+n;\n");
        fprintf(OUTFILE, "      luaH_setint(L, h, last--, val);\n");
        fprintf(OUTFILE, "      luaC_barrierback(L, h, val);\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    L->top = ci->top;  /* correct top (in case of previous open call) */\n");
      } break;

      case OP_CLOSURE: {
        fprintf(OUTFILE, "    Proto *p = cl->p->p[GETARG_Bx(i)];\n");
        fprintf(OUTFILE, "    LClosure *ncl = luaV_getcached(p, cl->upvals, base);  /* cached closure*/\n");
        fprintf(OUTFILE, "    if (ncl == NULL)  /* no match? */\n");
        fprintf(OUTFILE, "      luaV_pushclosure(L, p, cl->upvals, base, ra);  /* create a new one */\n");
        fprintf(OUTFILE, "    else\n");
        fprintf(OUTFILE, "      setclLvalue(L, ra, ncl);  /* push cashed closure */\n");
        fprintf(OUTFILE, "    checkGC(L, ra + 1);\n");
      } break;

      case OP_VARARG: {
        fprintf(OUTFILE, "    int b = GETARG_B(i) - 1;  /* required results */\n");
        fprintf(OUTFILE, "    int j;\n");
        fprintf(OUTFILE, "    int n = cast_int(base - ci->func) - cl->p->numparams - 1;\n");
        fprintf(OUTFILE, "    if (n < 0)  /* less arguments than parameters? */\n");
        fprintf(OUTFILE, "      n = 0;  /* no vararg arguments */\n");
        fprintf(OUTFILE, "    if (b < 0) {  /* B == 0? */\n");
        fprintf(OUTFILE, "      b = n;  /* get all var. arguments */\n");
        fprintf(OUTFILE, "      Protect(luaD_checkstack(L, n));\n");
        fprintf(OUTFILE, "      ra = RA(i);  /* previous call may change the stack */\n");
        fprintf(OUTFILE, "      L->top = ra + n;\n");
        fprintf(OUTFILE, "    }\n");
        fprintf(OUTFILE, "    for (j = 0; j < b && j < n; j++)\n");
        fprintf(OUTFILE, "      setobjs2s(L, ra + j, base - n + j);\n");
        fprintf(OUTFILE, "    for (; j < b; j++)  /* complete required results with nil */\n");
        fprintf(OUTFILE, "      setnilvalue(ra + j);\n");
      } break;

      case OP_EXTRAARG: {
        fprintf(OUTFILE, "    UNUSED(ra);\n");
        fprintf(OUTFILE, "    // NO OP\n");
      } break;

      default: {
        fprintf(stderr, "Uninplemented opcode %s", luaP_opnames[o]);
        fatal("aborting");
      } break;
    }
    fprintf(OUTFILE, "  }\n");
    fprintf(OUTFILE, "\n");
  }
  fprintf(OUTFILE, "}\n");
  fprintf(OUTFILE, "\n");
}

#define SS(x)	((x==1)?"":"s")
#define S(x)	(int)(x),SS(x)

static void PrintFunction(const Proto* f)
{
 int n=f->sizep;
 PrintCode(f);
 NFUNCTIONS++;
 for (int i=0; i<n; i++) {
     PrintFunction(f->p[i]);
 }
}
