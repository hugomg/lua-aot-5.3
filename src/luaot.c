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

#include "pretty_printer.h"

static void PrintFunction(const Proto* f);

#define DEFAULT_PROGNAME "luaot"

// Program options:
static const char* progname;        /* actual program name from argv[0] */
static const char* input_filename;  /* path to input Lua module */
static const char* output_filename; /* path to output C library module */
static const char* module_name;     /* name of generated module (for luaopen_XXX) */

// Global variables
static int NFUNCTIONS = 0;  /* ID of magic functions */ 
static PrettyPrinter pp;

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

  PP_writeln(&pp, "#include \"luaot-generated-header.c\"");
  PP_writeln(&pp, "");

  {
    // Generated C implementations
    NFUNCTIONS = 0;
    PrintFunction(f);
  }

  {
    PP_writeln(&pp, "ZZ_MAGIC_FUNC zz_magic_functions[%d] = {", NFUNCTIONS);
    for (int i=0; i < NFUNCTIONS; i++) {
      PP_writeln(&pp, "  zz_magic_function_%d,", i);
    }
    PP_writeln(&pp, "};");
    PP_writeln(&pp, "");
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

    PP_writeln(&pp, "static const char ZZ_ORIGINAL_SOURCE_CODE[] = {"); PP_indent(&pp);
    PP_begin_line(&pp);
    {int c, i=0; while (c = fgetc(infile), c != EOF) {
      PP_write(&pp, "%3d, ", c);
      if (++i >= 16) {
        i = 0;
        PP_end_line(&pp);
        PP_begin_line(&pp);
      }
    }}
    PP_write(&pp, "%3d,", 0);
    PP_end_line(&pp);
    PP_dedent(&pp); PP_writeln(&pp, "};");
    PP_writeln(&pp, "");

    fclose(infile);

  }

  PP_writeln(&pp, "#define ZZ_LUAOPEN_NAME luaopen_%s", module_name);
  PP_writeln(&pp, "");

  PP_writeln(&pp, "#include \"luaot-generated-footer.c\"");

  return 0;
}

int main(int argc, char* argv[])
{
  doargs(argc,argv);

  FILE *outfile = fopen(output_filename, "w");
  if (!outfile) {
    fatal("could not open output file for writing");
  }

  PP_init(&pp, outfile);

  lua_State* L = luaL_newstate();
  if (L==NULL) fatal("cannot create state: not enough memory");
  lua_pushcfunction(L, &pmain);
  if (lua_pcall(L,0,0,0)!=LUA_OK) fatal(lua_tostring(L,-1));
  lua_close(L);

  fclose(outfile);

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
  const char* s = getstr(ts);
  size_t i,n=tsslen(ts);
  PP_write(&pp, "%c",'"');
  for (i=0; i<n; i++)
  {
    int c = (int)(unsigned char)s[i];
    switch (c) {
      case '"' : PP_write(&pp, "\\\""); break;
      case '\\': PP_write(&pp, "\\\\"); break;
      case '\a': PP_write(&pp, "\\a"); break;
      case '\b': PP_write(&pp, "\\b"); break;
      case '\f': PP_write(&pp, "\\f"); break;
      case '\n': PP_write(&pp, "\\n"); break;
      case '\r': PP_write(&pp, "\\r"); break;
      case '\t': PP_write(&pp, "\\t"); break;
      case '\v': PP_write(&pp, "\\v"); break;
      default: {
        if (isprint(c)) {
          PP_write(&pp, "%c",c);
        } else {
          PP_write(&pp, "\\%03d",c);
        }
        break;
      }
    }
  }
  PP_write(&pp, "%c",'"');
}

static void PrintConstant(const Proto* f, int i)
{
  const TValue* o = &f->k[i];
  switch (ttype(o)) {
    case LUA_TNIL:
      PP_write(&pp, "nil");
      break;
    case LUA_TBOOLEAN:
      PP_write(&pp, bvalue(o) ? "true" : "false");
      break;
    case LUA_TNUMFLT: {
      char buff[100];
      sprintf(buff,LUA_NUMBER_FMT,fltvalue(o));
      PP_write(&pp, "%s",buff);
      if (buff[strspn(buff,"-0123456789")]=='\0') PP_write(&pp, ".0");
    } break;
    case LUA_TNUMINT:
      PP_write(&pp, LUA_INTEGER_FMT,ivalue(o));
      break;
    case LUA_TSHRSTR: case LUA_TLNGSTR:
      PrintString(tsvalue(o));
      break;
    default:				/* cannot happen */
      PP_write(&pp, "? type=%d",ttype(o));
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

  PP_begin_line(&pp);

  PP_write(&pp, "// ");
  //PP_write(&pp, "\t%d\t",pc+1);
  if (line>0) PP_write(&pp, "[%d]\t",line); else PP_write(&pp, "[-]\t");
  PP_write(&pp, "%-9s\t",luaP_opnames[o]);
  switch (getOpMode(o)) {
    case iABC:
      PP_write(&pp, "%d",a);
      if (getBMode(o)!=OpArgN) PP_write(&pp, " %d",ISK(b) ? (MYK(INDEXK(b))) : b);
      if (getCMode(o)!=OpArgN) PP_write(&pp, " %d",ISK(c) ? (MYK(INDEXK(c))) : c);
      break;

    case iABx:
      PP_write(&pp, "%d",a);
      if (getBMode(o)==OpArgK) PP_write(&pp, " %d",MYK(bx));
      if (getBMode(o)==OpArgU) PP_write(&pp, " %d",bx);
      break;

    case iAsBx:
      PP_write(&pp, "%d %d",a,sbx);
      break;

    case iAx:
      PP_write(&pp, "%d",MYK(ax));
      break;
  }

  switch (o) {
    case OP_LOADK:
      PP_write(&pp, "\t; "); PrintConstant(f,bx);
      break;

    case OP_GETUPVAL:
    case OP_SETUPVAL:
      PP_write(&pp, "\t; %s",UPVALNAME(b));
      break;

    case OP_GETTABUP:
      PP_write(&pp, "\t; %s",UPVALNAME(b));
      if (ISK(c)) { PP_write(&pp, " "); PrintConstant(f,INDEXK(c)); }
      break;

    case OP_SETTABUP:
      PP_write(&pp, "\t; %s",UPVALNAME(a));
      if (ISK(b)) { PP_write(&pp, " "); PrintConstant(f,INDEXK(b)); }
      if (ISK(c)) { PP_write(&pp, " "); PrintConstant(f,INDEXK(c)); }
      break;

    case OP_GETTABLE:
    case OP_SELF:
      if (ISK(c)) { PP_write(&pp, "\t; "); PrintConstant(f,INDEXK(c)); }
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
      if (ISK(b) || ISK(c)) {
        PP_write(&pp, "\t; ");
        if (ISK(b)) PrintConstant(f,INDEXK(b)); else PP_write(&pp, "-");
        PP_write(&pp, " ");
        if (ISK(c)) PrintConstant(f,INDEXK(c)); else PP_write(&pp, "-");
      }
      break;

    case OP_JMP:
    case OP_FORLOOP:
    case OP_FORPREP:
    case OP_TFORLOOP:
      PP_write(&pp, "\t; to %d",sbx+pc+1);
      break;

    case OP_CLOSURE:
      PP_write(&pp, "\t; %p",VOID(f->p[bx]));
      break;

    case OP_SETLIST:
      if (c==0) PP_write(&pp, "\t; %d",(int)code[++pc]); else PP_write(&pp, "\t; %d",c);
      break;

    case OP_EXTRAARG:
      PP_write(&pp, "\t; "); PrintConstant(f,ax);
      break;

    default:
      break;
  }

  PP_end_line(&pp);
}


static void PrintCode(const Proto* f)
{
  const Instruction* code=f->code;
  int nopcodes=f->sizecode;

  PP_writeln(&pp, "// source = %s", getstr(f->source));
  PP_writeln(&pp, "// linedefined = %d", f->linedefined);
  PP_writeln(&pp, "// lastlinedefined = %d", f->lastlinedefined);
  PP_writeln(&pp, "// what = %s", (f->linedefined == 0) ? "main" : "Lua");

  PP_writeln(&pp, "static int zz_magic_function_%d (lua_State *L, LClosure *cl)", NFUNCTIONS);
  PP_writeln(&pp, "{"); PP_indent(&pp);
  PP_writeln(&pp,   "CallInfo *ci = L->ci;");
  PP_writeln(&pp,   "TValue *k = cl->p->k;");
  PP_writeln(&pp,   "StkId base = ci->u.l.base;");
  PP_writeln(&pp,   "");
  PP_writeln(&pp,   "// Avoid warnings if the function has few opcodes:");
  PP_writeln(&pp,   "(void) ci;");
  PP_writeln(&pp,   "(void) k;");
  PP_writeln(&pp,   "(void) base;");
  PP_writeln(&pp,   "");

  for (int pc=0; pc<nopcodes; pc++) {
    PrintOpcodeComment(f, pc);

    Instruction i = code[pc];
    OpCode o=GET_OPCODE(i);

    PP_writeln(&pp, "label_%d: {", pc); PP_indent(&pp);

    // vmfetch
    //PP_writeln(&pp, "Instruction i = 0x%08x;", i);
    PP_writeln(&pp, "Instruction i = *(ci->u.l.savedpc++);");
   // PP_writeln(&pp, "assert(i ==  0x%08x);", i); //(!)
    PP_writeln(&pp, "if (L->hookmask & (LUA_MASKLINE | LUA_MASKCOUNT))");
    PP_writeln(&pp, "  Protect(luaG_traceexec(L));");
    PP_writeln(&pp, "StkId ra = RA(i); /* WARNING: any stack reallocation invalidates 'ra' */");
    PP_writeln(&pp, "lua_assert(base == ci->u.l.base);");
    PP_writeln(&pp, "lua_assert(base <= L->top && L->top < L->stack + L->stacksize);");
    PP_writeln(&pp, "");

    switch (o) {

      case OP_MOVE: {
        PP_writeln(&pp, "setobjs2s(L, ra, RB(i));");
      } break;

      case OP_LOADK: {
        PP_writeln(&pp, "TValue *rb = k + GETARG_Bx(i);");
        PP_writeln(&pp, "setobj2s(L, ra, rb);");
      } break;

      case OP_LOADKX: {
        assert(pc + 1 < nopcodes);
        PP_writeln(&pp, "TValue *rb;");
        PP_writeln(&pp, "lua_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_EXTRAARG);");
        PP_writeln(&pp, "rb = k + GETARG_Ax(*ci->u.l.savedpc++);");
        PP_writeln(&pp, "setobj2s(L, ra, rb);");
        PP_writeln(&pp, "goto label_%d;", pc+2);
      } break;

      case OP_LOADBOOL: {
        PP_writeln(&pp, "setbvalue(ra, GETARG_B(i));");
        PP_writeln(&pp, "if (GETARG_C(i)) { /* skip next instruction (if C) */");
        PP_writeln(&pp, "  ci->u.l.savedpc++;");
        PP_writeln(&pp, "  goto label_%d;", pc+2);
        PP_writeln(&pp, "}");
      } break;

      case OP_LOADNIL: {
        PP_writeln(&pp, "int b = GETARG_B(i);");
        PP_writeln(&pp, "do {");
        PP_writeln(&pp, "  setnilvalue(ra++);");
        PP_writeln(&pp, "} while (b--);");
      } break;
 
      case OP_GETUPVAL: {
        PP_writeln(&pp, "int b = GETARG_B(i);");
        PP_writeln(&pp, "setobj2s(L, ra, cl->upvals[b]->v);");
      } break;
     
      case OP_GETTABUP: {
        PP_writeln(&pp, "TValue *upval = cl->upvals[GETARG_B(i)]->v;");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "gettableProtected(L, upval, rc, ra);");
      } break;

      case OP_GETTABLE: {
        PP_writeln(&pp, "StkId rb = RB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "gettableProtected(L, rb, rc, ra);");
      } break;

      case OP_SETTABUP: {
        PP_writeln(&pp, "(void) ra;");
        PP_writeln(&pp, "TValue *upval = cl->upvals[GETARG_A(i)]->v;");
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "settableProtected(L, upval, rb, rc);");
      } break;

      case OP_SETUPVAL: {
        PP_writeln(&pp, "UpVal *uv = cl->upvals[GETARG_B(i)];");
        PP_writeln(&pp, "setobj(L, uv->v, ra);");
        PP_writeln(&pp, "luaC_upvalbarrier(L, uv);");
      } break;

      case OP_SETTABLE: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "settableProtected(L, ra, rb, rc);");
      } break;

      case OP_NEWTABLE: {
        PP_writeln(&pp, "int b = GETARG_B(i);");
        PP_writeln(&pp, "int c = GETARG_C(i);");
        PP_writeln(&pp, "Table *t = luaH_new(L);");
        PP_writeln(&pp, "sethvalue(L, ra, t);");
        PP_writeln(&pp, "if (b != 0 || c != 0)");
        PP_writeln(&pp, "  luaH_resize(L, t, luaO_fb2int(b), luaO_fb2int(c));");
        PP_writeln(&pp, "checkGC(L, ra + 1);");
      } break;

      case OP_SELF: {
        PP_writeln(&pp, "const TValue *aux;");
        PP_writeln(&pp, "StkId rb = RB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "TString *key = tsvalue(rc);  /* key must be a string */");
        PP_writeln(&pp, "setobjs2s(L, ra + 1, rb);");
        PP_writeln(&pp, "if (luaV_fastget(L, rb, key, aux, luaH_getstr)) {");
        PP_writeln(&pp, "  setobj2s(L, ra, aux);");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else Protect(luaV_finishget(L, rb, rc, ra, aux));");
      } break;

      case OP_ADD: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Number nb; lua_Number nc;");
        PP_writeln(&pp, "if (ttisinteger(rb) && ttisinteger(rc)) {");
        PP_writeln(&pp, "  lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);");
        PP_writeln(&pp, "  setivalue(ra, intop(+, ib, ic));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {");
        PP_writeln(&pp, "  setfltvalue(ra, luai_numadd(L, nb, nc));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_ADD)); }");
      } break;

      case OP_SUB: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Number nb; lua_Number nc;");
        PP_writeln(&pp, "if (ttisinteger(rb) && ttisinteger(rc)) {");
        PP_writeln(&pp, "  lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);");
        PP_writeln(&pp, "  setivalue(ra, intop(-, ib, ic));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {");
        PP_writeln(&pp, "  setfltvalue(ra, luai_numsub(L, nb, nc));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SUB)); }");
      } break;

      case OP_MUL: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Number nb; lua_Number nc;");
        PP_writeln(&pp, "if (ttisinteger(rb) && ttisinteger(rc)) {");
        PP_writeln(&pp, "  lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);");
        PP_writeln(&pp, "  setivalue(ra, intop(*, ib, ic));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {");
        PP_writeln(&pp, "  setfltvalue(ra, luai_nummul(L, nb, nc));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_MUL)); }");
      } break;

      case OP_DIV: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Number nb; lua_Number nc;");
        PP_writeln(&pp, "if (tonumber(rb, &nb) && tonumber(rc, &nc)) {");
        PP_writeln(&pp, "  setfltvalue(ra, luai_numdiv(L, nb, nc));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_DIV)); }");
      } break;

      case OP_BAND: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Integer ib; lua_Integer ic;");
        PP_writeln(&pp, "if (tointeger(rb, &ib) && tointeger(rc, &ic)) {");
        PP_writeln(&pp, "  setivalue(ra, intop(&, ib, ic));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BAND)); }");
      } break;

      case OP_BOR: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Integer ib; lua_Integer ic;");
        PP_writeln(&pp, "if (tointeger(rb, &ib) && tointeger(rc, &ic)) {");
        PP_writeln(&pp, "  setivalue(ra, intop(|, ib, ic));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BOR)); }");
      } break;

      case OP_BXOR: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Integer ib; lua_Integer ic;");
        PP_writeln(&pp, "if (tointeger(rb, &ib) && tointeger(rc, &ic)) {");
        PP_writeln(&pp, "  setivalue(ra, intop(^, ib, ic));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_BXOR)); }");
      } break;

      case OP_SHL: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Integer ib; lua_Integer ic;");
        PP_writeln(&pp, "if (tointeger(rb, &ib) && tointeger(rc, &ic)) {");
        PP_writeln(&pp, "  setivalue(ra, luaV_shiftl(ib, ic));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SHL)); }");
      } break;

      case OP_SHR: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Integer ib; lua_Integer ic;");
        PP_writeln(&pp, "if (tointeger(rb, &ib) && tointeger(rc, &ic)) {");
        PP_writeln(&pp, "  setivalue(ra, luaV_shiftl(ib, -ic));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_SHR)); }");
      } break;

      case OP_MOD: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Number nb; lua_Number nc;");
        PP_writeln(&pp, "if (ttisinteger(rb) && ttisinteger(rc)) {");
        PP_writeln(&pp, "  lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);");
        PP_writeln(&pp, "  setivalue(ra, luaV_mod(L, ib, ic));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {");
        PP_writeln(&pp, "  lua_Number m;");
        PP_writeln(&pp, "  luai_nummod(L, nb, nc, m);");
        PP_writeln(&pp, "  setfltvalue(ra, m);");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_MOD)); }");
      } break;

      case OP_IDIV: { /* floor division */
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Number nb; lua_Number nc;");
        PP_writeln(&pp, "if (ttisinteger(rb) && ttisinteger(rc)) {");
        PP_writeln(&pp, "  lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);");
        PP_writeln(&pp, "  setivalue(ra, luaV_div(L, ib, ic));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {");
        PP_writeln(&pp, "  setfltvalue(ra, luai_numidiv(L, nb, nc));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_IDIV)); }");
      } break;

      case OP_POW: {
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "lua_Number nb; lua_Number nc;");
        PP_writeln(&pp, "if (tonumber(rb, &nb) && tonumber(rc, &nc)) {");
        PP_writeln(&pp, "  setfltvalue(ra, luai_numpow(L, nb, nc));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else { Protect(luaT_trybinTM(L, rb, rc, ra, TM_POW)); }");
      } break;

      case OP_UNM: {
        PP_writeln(&pp, "TValue *rb = RB(i);");
        PP_writeln(&pp, "lua_Number nb;");
        PP_writeln(&pp, "if (ttisinteger(rb)) {");
        PP_writeln(&pp, "  lua_Integer ib = ivalue(rb);");
        PP_writeln(&pp, "  setivalue(ra, intop(-, 0, ib));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else if (tonumber(rb, &nb)) {");
        PP_writeln(&pp, "  setfltvalue(ra, luai_numunm(L, nb));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else {");
        PP_writeln(&pp, "  Protect(luaT_trybinTM(L, rb, rb, ra, TM_UNM));");
        PP_writeln(&pp, "}");
      } break;

      case OP_BNOT: {
        PP_writeln(&pp, "TValue *rb = RB(i);");
        PP_writeln(&pp, "lua_Integer ib;");
        PP_writeln(&pp, "if (tointeger(rb, &ib)) {");
        PP_writeln(&pp, "  setivalue(ra, intop(^, ~l_castS2U(0), ib));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else {");
        PP_writeln(&pp, "  Protect(luaT_trybinTM(L, rb, rb, ra, TM_BNOT));");
        PP_writeln(&pp, "}");
      } break;

      case OP_NOT: {
        PP_writeln(&pp, "TValue *rb = RB(i);");
        PP_writeln(&pp, "int res = l_isfalse(rb);  /* next assignment may change this value */");
        PP_writeln(&pp, "setbvalue(ra, res);");
      } break;

      case OP_LEN: {
        PP_writeln(&pp, "Protect(luaV_objlen(L, ra, RB(i)));");
      } break;

      case OP_CONCAT: {
        PP_writeln(&pp, "int b = GETARG_B(i);");
        PP_writeln(&pp, "int c = GETARG_C(i);");
        PP_writeln(&pp, "StkId rb;");
        PP_writeln(&pp, "L->top = base + c + 1;  /* mark the end of concat operands */");
        PP_writeln(&pp, "Protect(luaV_concat(L, c - b + 1));");
        PP_writeln(&pp, "ra = RA(i);  /* 'luaV_concat' may invoke TMs and move the stack */");
        PP_writeln(&pp, "rb = base + b;");
        PP_writeln(&pp, "setobjs2s(L, ra, rb);");
        PP_writeln(&pp, "checkGC(L, (ra >= rb ? ra + 1 : rb));");
        PP_writeln(&pp, "L->top = ci->top;  /* restore top */");
      } break;

      case OP_JMP: {
        int target = pc + GETARG_sBx(i) + 1;
        PP_writeln(&pp, "(void) ra;");
        PP_writeln(&pp, "int a = GETARG_A(i);");
        PP_writeln(&pp, "if (a != 0) luaF_close(L, ci->u.l.base + a - 1);");
        PP_writeln(&pp, "ci->u.l.savedpc += GETARG_sBx(i);"); // (!)
        PP_writeln(&pp, "goto label_%d;", target);
      } break;

      case OP_EQ: {
        PP_writeln(&pp, "(void) ra;");
        PP_writeln(&pp, "TValue *rb = RKB(i);");
        PP_writeln(&pp, "TValue *rc = RKC(i);");
        PP_writeln(&pp, "int cmp;");
        PP_writeln(&pp, "Protect(cmp = luaV_equalobj(L, rb, rc));");
        PP_writeln(&pp, "if (cmp != GETARG_A(i)) {");
        PP_writeln(&pp, "  ci->u.l.savedpc++;\n");
        PP_writeln(&pp, "  goto label_%d;", pc+2);
        PP_writeln(&pp, "}");
      } break;

      case OP_LT: {
        PP_writeln(&pp, "(void) ra;");
        PP_writeln(&pp, "int cmp;");
        PP_writeln(&pp, "Protect(cmp = luaV_lessthan(L, RKB(i), RKC(i)));");
        PP_writeln(&pp, "if (cmp != GETARG_A(i)) {");
        PP_writeln(&pp, "  ci->u.l.savedpc++;\n");
        PP_writeln(&pp, "  goto label_%d;", pc+2);
        PP_writeln(&pp, "}");
      } break;

      case OP_LE: {
        PP_writeln(&pp, "(void) ra;");
        PP_writeln(&pp, "int cmp;");
        PP_writeln(&pp, "Protect(cmp = luaV_lessequal(L, RKB(i), RKC(i)));");
        PP_writeln(&pp, "if (cmp != GETARG_A(i)) {");
        PP_writeln(&pp, "  ci->u.l.savedpc++;\n");
        PP_writeln(&pp, "  goto label_%d;", pc+2);
        PP_writeln(&pp, "}");
      } break;

      case OP_TEST: {
        PP_writeln(&pp, "if (GETARG_C(i) ? l_isfalse(ra) : !l_isfalse(ra)) {");
        PP_writeln(&pp, "  ci->u.l.savedpc++;\n");
        PP_writeln(&pp, "  goto label_%d;", pc+2);
        PP_writeln(&pp, "}");
      } break;

      case OP_TESTSET: {
        PP_writeln(&pp, "TValue *rb = RB(i);");
        PP_writeln(&pp, "if (GETARG_C(i) ? l_isfalse(rb) : !l_isfalse(rb)) {");
        PP_writeln(&pp, "  ci->u.l.savedpc++;\n");
        PP_writeln(&pp, "  goto label_%d;", pc+2);
        PP_writeln(&pp, "} else {");
        PP_writeln(&pp, "  setobjs2s(L, ra, rb);");
        PP_writeln(&pp, "}");
      } break;

      case OP_CALL: {
        PP_writeln(&pp, "int b = GETARG_B(i);");
        PP_writeln(&pp, "int nresults = GETARG_C(i) - 1;");
        PP_writeln(&pp, "if (b != 0) L->top = ra+b;  /* else previous instruction set top */");
        PP_writeln(&pp, "if (luaD_precall(L, ra, nresults)) {  /* C function? */");
        PP_writeln(&pp, "  if (nresults >= 0)");
        PP_writeln(&pp, "    L->top = ci->top;  /* adjust results */");
        PP_writeln(&pp, "  Protect((void)0);  /* update 'base' */");
        PP_writeln(&pp, "} else {  /* Lua function */");
        PP_writeln(&pp, "  luaV_execute(L);");                       // (!)
        PP_writeln(&pp, "  Protect((void)0);  /* update 'base' */"); // (!)
        PP_writeln(&pp, "}");
      } break;

      case OP_TAILCALL: {
        PP_writeln(&pp, "int b = GETARG_B(i);");
        PP_writeln(&pp, "if (b != 0) L->top = ra+b;  /* else previous instruction set top */");
        PP_writeln(&pp, "lua_assert(GETARG_C(i) - 1 == LUA_MULTRET);");
        PP_writeln(&pp, "if (luaD_precall(L, ra, LUA_MULTRET)) {  /* C function? */");
        PP_writeln(&pp, "  Protect((void)0);  /* update 'base' */");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else {");
        PP_writeln(&pp, "  luaV_execute(L);");
        PP_writeln(&pp, "  Protect((void)0);  /* update 'base' */");
        PP_writeln(&pp, "}");

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
        PP_writeln(&pp, "int b = GETARG_B(i);");
        PP_writeln(&pp, "if (cl->p->sizep > 0) luaF_close(L, base);");
        PP_writeln(&pp, "int ret = (b != 0 ? b - 1 : cast_int(L->top - ra));");
        PP_writeln(&pp, "luaD_poscall(L, ci, ra, ret);");
        PP_writeln(&pp, "return ret;");
      } break;

      case OP_FORLOOP: {
        int target = pc + GETARG_sBx(i) + 1;
        PP_writeln(&pp, "if (ttisinteger(ra)) {  /* integer loop? */");
        PP_writeln(&pp, "  lua_Integer step = ivalue(ra + 2);");
        PP_writeln(&pp, "  lua_Integer idx = intop(+, ivalue(ra), step); /* increment index */");
        PP_writeln(&pp, "  lua_Integer limit = ivalue(ra + 1);");
        PP_writeln(&pp, "  if ((0 < step) ? (idx <= limit) : (limit <= idx)) {");
        PP_writeln(&pp, "    chgivalue(ra, idx);  /* update internal index... */");
        PP_writeln(&pp, "    setivalue(ra + 3, idx);  /* ...and external index */");
        PP_writeln(&pp, "    ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */");
        PP_writeln(&pp, "    goto label_%d;  /* jump back */", target);
        PP_writeln(&pp, "  }");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else {  /* floating loop */");
        PP_writeln(&pp, "  lua_Number step = fltvalue(ra + 2);");
        PP_writeln(&pp, "  lua_Number idx = luai_numadd(L, fltvalue(ra), step); /* inc. index */");
        PP_writeln(&pp, "  lua_Number limit = fltvalue(ra + 1);");
        PP_writeln(&pp, "  if (luai_numlt(0, step) ? luai_numle(idx, limit)");
        PP_writeln(&pp, "                          : luai_numle(limit, idx)) {");
        PP_writeln(&pp, "    chgfltvalue(ra, idx);  /* update internal index... */");
        PP_writeln(&pp, "    setfltvalue(ra + 3, idx);  /* ...and external index */");
        PP_writeln(&pp, "    ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */");
        PP_writeln(&pp, "    goto label_%d;  /* jump back */", target);
        PP_writeln(&pp, "  }");
        PP_writeln(&pp, "}");
      } break;

      case OP_FORPREP: { 
        int target = pc + GETARG_sBx(i) + 1;
        PP_writeln(&pp, "TValue *init = ra;");
        PP_writeln(&pp, "TValue *plimit = ra + 1;");
        PP_writeln(&pp, "TValue *pstep = ra + 2;");
        PP_writeln(&pp, "lua_Integer ilimit;");
        PP_writeln(&pp, "int stopnow;");
        PP_writeln(&pp, "if (ttisinteger(init) && ttisinteger(pstep) &&");
        PP_writeln(&pp, "    luaV_forlimit(plimit, &ilimit, ivalue(pstep), &stopnow)) {");
        PP_writeln(&pp, "  /* all values are integer */");
        PP_writeln(&pp, "  lua_Integer initv = (stopnow ? 0 : ivalue(init));");
        PP_writeln(&pp, "  setivalue(plimit, ilimit);");
        PP_writeln(&pp, "  setivalue(init, intop(-, initv, ivalue(pstep)));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "else {  /* try making all values floats */");
        PP_writeln(&pp, "  lua_Number ninit; lua_Number nlimit; lua_Number nstep;");
        PP_writeln(&pp, "  if (!tonumber(plimit, &nlimit))");
        PP_writeln(&pp, "    luaG_runerror(L, \"'for' limit must be a number\");");
        PP_writeln(&pp, "  setfltvalue(plimit, nlimit);");
        PP_writeln(&pp, "  if (!tonumber(pstep, &nstep))");
        PP_writeln(&pp, "    luaG_runerror(L, \"'for' step must be a number\");");
        PP_writeln(&pp, "  setfltvalue(pstep, nstep);");
        PP_writeln(&pp, "  if (!tonumber(init, &ninit))");
        PP_writeln(&pp, "    luaG_runerror(L, \"'for' initial value must be a number\");");
        PP_writeln(&pp, "  setfltvalue(init, luai_numsub(L, ninit, nstep));");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "ci->u.l.savedpc += GETARG_sBx(i);");
        PP_writeln(&pp, "goto label_%d;", target);
      } break;

      case OP_TFORCALL: {
        PP_writeln(&pp, "StkId cb = ra + 3;  /* call base */");
        PP_writeln(&pp, "setobjs2s(L, cb+2, ra+2);");
        PP_writeln(&pp, "setobjs2s(L, cb+1, ra+1);");
        PP_writeln(&pp, "setobjs2s(L, cb, ra);");
        PP_writeln(&pp, "L->top = cb + 3;  /* func. + 2 args (state and index) */");
        PP_writeln(&pp, "Protect(luaD_call(L, cb, GETARG_C(i)));");
        PP_writeln(&pp, "L->top = ci->top;");

        assert(pc+1 < nopcodes);
        assert(GET_OPCODE(code[pc+1]) == OP_TFORLOOP);
      } break;

      case OP_TFORLOOP: {
        int target = pc + GETARG_sBx(i) + 1;
        PP_writeln(&pp, "if (!ttisnil(ra + 1)) {  /* continue loop? */");
        PP_writeln(&pp, "  setobjs2s(L, ra, ra + 1);  /* save control variable */");
        PP_writeln(&pp, "  ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */");
        PP_writeln(&pp, "  goto label_%d; /* jump back */", target);
        PP_writeln(&pp, "}");
      } break;

      case OP_SETLIST: {
        assert(pc + 1 < nopcodes);
        Instruction next_i = code[pc+1];

        PP_writeln(&pp, "int n = GETARG_B(i);");
        PP_writeln(&pp, "int c = GETARG_C(i);");
        PP_writeln(&pp, "unsigned int last;");
        PP_writeln(&pp, "Table *h;");
        PP_writeln(&pp, "if (n == 0) n = cast_int(L->top - ra) - 1;");
        PP_writeln(&pp, "if (c == 0) {");
        PP_writeln(&pp, "  lua_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_EXTRAARG);", next_i);
        PP_writeln(&pp, "  c = GETARG_Ax(*ci->u.l.savedpc++);", next_i); //(!)
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "h = hvalue(ra);");
        PP_writeln(&pp, "last = ((c-1)*LFIELDS_PER_FLUSH) + n;");
        PP_writeln(&pp, "if (last > h->sizearray)  /* needs more space? */");
        PP_writeln(&pp, "  luaH_resizearray(L, h, last);  /* preallocate it at once */");
        PP_writeln(&pp, "for (; n > 0; n--) {");
        PP_writeln(&pp, "  TValue *val = ra+n;");
        PP_writeln(&pp, "  luaH_setint(L, h, last--, val);");
        PP_writeln(&pp, "  luaC_barrierback(L, h, val);");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "L->top = ci->top;  /* correct top (in case of previous open call) */");
      } break;

      case OP_CLOSURE: {
        PP_writeln(&pp, "Proto *p = cl->p->p[GETARG_Bx(i)];");
        PP_writeln(&pp, "LClosure *ncl = luaV_getcached(p, cl->upvals, base);  /* cached closure*/");
        PP_writeln(&pp, "if (ncl == NULL)  /* no match? */");
        PP_writeln(&pp, "  luaV_pushclosure(L, p, cl->upvals, base, ra);  /* create a new one */");
        PP_writeln(&pp, "else");
        PP_writeln(&pp, "  setclLvalue(L, ra, ncl);  /* push cashed closure */");
        PP_writeln(&pp, "checkGC(L, ra + 1);");
      } break;

      case OP_VARARG: {
        PP_writeln(&pp, "int b = GETARG_B(i) - 1;  /* required results */");
        PP_writeln(&pp, "int j;");
        PP_writeln(&pp, "int n = cast_int(base - ci->func) - cl->p->numparams - 1;");
        PP_writeln(&pp, "if (n < 0)  /* less arguments than parameters? */");
        PP_writeln(&pp, "  n = 0;  /* no vararg arguments */");
        PP_writeln(&pp, "if (b < 0) {  /* B == 0? */");
        PP_writeln(&pp, "  b = n;  /* get all var. arguments */");
        PP_writeln(&pp, "  Protect(luaD_checkstack(L, n));");
        PP_writeln(&pp, "  ra = RA(i);  /* previous call may change the stack */");
        PP_writeln(&pp, "  L->top = ra + n;");
        PP_writeln(&pp, "}");
        PP_writeln(&pp, "for (j = 0; j < b && j < n; j++)");
        PP_writeln(&pp, "  setobjs2s(L, ra + j, base - n + j);");
        PP_writeln(&pp, "for (; j < b; j++)  /* complete required results with nil */");
        PP_writeln(&pp, "  setnilvalue(ra + j);");
      } break;

      case OP_EXTRAARG: {
        PP_writeln(&pp, "(void) ra;");
        PP_writeln(&pp, "// NO OP");
      } break;

      default: {
        fprintf(stderr, "Uninplemented opcode %s", luaP_opnames[o]);
        fatal("aborting");
      } break;
    }
    PP_dedent(&pp); PP_writeln(&pp, "}");
    PP_writeln(&pp, "");
  }
  PP_dedent(&pp); PP_writeln(&pp, "}");
  PP_writeln(&pp, "");
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
