#include "lauxlib.h"
#include "lualib.h"

static int magic(lua_State *L) {
    
    luaL_checktype(L, 1, LUA_TNUMBER);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_Integer ix = lua_tointeger(L, 1);
    if (!(1 <= ix && ix <= NFUNCTIONS)) {
        lua_pop(L, 2);
        lua_pushliteral(L, "index out of range");
        lua_error(L);
        return 0; /* never reached */
    }
    
    StkId o = (void *) lua_topointer(L, 2);
    if (ttype(o) != LUA_TLCL) {
        lua_pop(L, 2);
        lua_pushliteral(L, "not a Lua closure");
        lua_error(L);
        return 0; /* never reached */
    }
    
    LClosure *cl = (LClosure *) o;
    cl->p->magic_implementation = zz_magic_functions[ix-1];

    return 0;
}

int ZZ_LUAOPEN_NAME (lua_State *L) {
    lua_pushcfunction(L, magic);
    lua_setglobal(L, "magic");
    return 0;
}
