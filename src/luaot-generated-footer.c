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
    if (ok != LUA_OK) {
        fprintf(stderr, "could not load file\n");
        exit(1);
    }

    LClosure *cl = (void *) lua_topointer(L, -1);

    int next_id = 0;
    bind_magic(cl->p, &next_id);

    lua_call(L, 0, 1);
    return 1;
}
