/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua.h"

#include <lauxlib.h>
#include <string.h>


#define NGX_LUA_APP_METATABLE  "ngx_lua_app"


typedef struct {
    char    *pattern;
    size_t   pattern_len;
    int      handler_ref;
} ngx_lua_app_route_t;


struct ngx_lua_app_s {
    ngx_lua_app_route_t  *routes;
    size_t                nroutes;
    size_t                routes_cap;
};


static int ngx_lua_app_new(lua_State *L);
static int ngx_lua_app_all(lua_State *L);
static int ngx_lua_app_gc(lua_State *L);
static int ngx_lua_app_reserve(lua_State *L, ngx_lua_app_t *app, size_t n);
static void *ngx_lua_app_alloc(lua_State *L, void *ptr, size_t osize,
    size_t nsize);


static const luaL_Reg  ngx_lua_app_global_methods[] = {
    { "new", ngx_lua_app_new },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_app_methods[] = {
    { "all", ngx_lua_app_all },
    { NULL, NULL }
};


void
ngx_lua_app_register(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_APP_METATABLE)) {
        luaL_setfuncs(L, ngx_lua_app_methods, 0);
        lua_pushcfunction(L, ngx_lua_app_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }

    lua_pop(L, 1);

    lua_newtable(L);
    luaL_setfuncs(L, ngx_lua_app_global_methods, 0);
    lua_setglobal(L, "App");
}


ngx_lua_app_t *
ngx_lua_app_get(lua_State *L, int index)
{
    return luaL_testudata(L, index, NGX_LUA_APP_METATABLE);
}


int
ngx_lua_app_find_handler(ngx_lua_app_t *app, const char *path, size_t len)
{
    size_t  i;

    for (i = 0; i < app->nroutes; i++) {
        if (app->routes[i].pattern_len == 1
            && app->routes[i].pattern[0] == '*')
        {
            return app->routes[i].handler_ref;
        }

        if (app->routes[i].pattern_len == len
            && memcmp(app->routes[i].pattern, path, len) == 0)
        {
            return app->routes[i].handler_ref;
        }
    }

    return LUA_NOREF;
}


static int
ngx_lua_app_new(lua_State *L)
{
    ngx_lua_app_t  *app;

    if (lua_gettop(L) != 0) {
        return luaL_error(L, "App.new() takes no arguments");
    }

    app = lua_newuserdatauv(L, sizeof(ngx_lua_app_t), 0);
    app->routes = NULL;
    app->nroutes = 0;
    app->routes_cap = 0;

    luaL_setmetatable(L, NGX_LUA_APP_METATABLE);

    return 1;
}


static int
ngx_lua_app_all(lua_State *L)
{
    size_t                len;
    const char           *pattern;
    ngx_lua_app_t        *app;
    ngx_lua_app_route_t  *route;

    if (lua_gettop(L) != 3) {
        return luaL_error(L, "App:all() takes pattern and handler");
    }

    app = luaL_checkudata(L, 1, NGX_LUA_APP_METATABLE);
    pattern = luaL_checklstring(L, 2, &len);

    if (!lua_isfunction(L, 3)) {
        return luaL_argerror(L, 3, "function expected");
    }

    if (ngx_lua_app_reserve(L, app, app->nroutes + 1) != 0) {
        return luaL_error(L, "no memory");
    }

    route = &app->routes[app->nroutes++];

    route->pattern = ngx_lua_app_alloc(L, NULL, 0, len + 1);
    if (route->pattern == NULL) {
        app->nroutes--;
        return luaL_error(L, "no memory");
    }

    memcpy(route->pattern, pattern, len);
    route->pattern[len] = '\0';
    route->pattern_len = len;

    lua_pushvalue(L, 3);
    route->handler_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_settop(L, 1);

    return 1;
}


static int
ngx_lua_app_gc(lua_State *L)
{
    size_t          i;
    ngx_lua_app_t  *app;

    app = luaL_checkudata(L, 1, NGX_LUA_APP_METATABLE);

    for (i = 0; i < app->nroutes; i++) {
        luaL_unref(L, LUA_REGISTRYINDEX, app->routes[i].handler_ref);
        ngx_lua_app_alloc(L, app->routes[i].pattern,
                          app->routes[i].pattern_len + 1, 0);
    }

    ngx_lua_app_alloc(L, app->routes,
                      app->routes_cap * sizeof(ngx_lua_app_route_t), 0);

    return 0;
}


static int
ngx_lua_app_reserve(lua_State *L, ngx_lua_app_t *app, size_t n)
{
    size_t                cap;
    ngx_lua_app_route_t  *routes;

    if (n <= app->routes_cap) {
        return 0;
    }

    cap = (app->routes_cap == 0) ? 4 : app->routes_cap * 2;

    while (cap < n) {
        cap *= 2;
    }

    routes = ngx_lua_app_alloc(L, app->routes,
                               app->routes_cap
                               * sizeof(ngx_lua_app_route_t),
                               cap * sizeof(ngx_lua_app_route_t));
    if (routes == NULL) {
        return -1;
    }

    app->routes = routes;
    app->routes_cap = cap;

    return 0;
}


static void *
ngx_lua_app_alloc(lua_State *L, void *ptr, size_t osize, size_t nsize)
{
    void       *ud;
    lua_Alloc   alloc;

    alloc = lua_getallocf(L, &ud);

    return alloc(ud, ptr, osize, nsize);
}
