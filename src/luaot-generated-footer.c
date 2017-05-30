#include "lauxlib.h"
#include "lualib.h"

static void bind_magic(Proto *p, int *next_id)
{
    p->magic_implementation = zz_magic_functions[*next_id];
    *next_id = *next_id + 1;

    for(int i=0; i < p->sizep; i++) {
        bind_magic(p->p[i], next_id);
    }
}

int ZZ_LUAOPEN_NAME (lua_State *L) {
    
    int ok = luaL_loadstring(L, ZZ_ORIGINAL_SOURCE_CODE);
    switch (ok) {
      case LUA_OK:
        /* No errors */
        break;
      case LUA_ERRSYNTAX:
        fprintf(stderr, "syntax error in bundled source code.\n");
        exit(1);
        break;
      case LUA_ERRMEM:
        fprintf(stderr, "memory allocation (out-of-memory) error in bundled source code.\n");
        exit(1);
        break;
      case LUA_ERRGCMM:
        fprintf(stderr, "error while running a __gc metamethod.\n");
        exit(1);
        break;
      default:
        assert(0);
    }

    LClosure *cl = (void *) lua_topointer(L, -1);

    int next_id = 0;
    bind_magic(cl->p, &next_id);

    lua_call(L, 0, 1);
    return 1;
}
