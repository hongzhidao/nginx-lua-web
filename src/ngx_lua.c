/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua.h"


void
ngx_lua_disable_coroutine(lua_State *L)
{
    lua_pushnil(L);
    lua_setglobal(L, "coroutine");

    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "loaded");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "coroutine");
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "preload");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "coroutine");
    }
    lua_pop(L, 1);

    lua_pop(L, 1);
}
