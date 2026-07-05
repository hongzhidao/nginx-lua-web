/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua.h"

#include <lauxlib.h>
#include <string.h>


#define NGX_LUA_APP_METATABLE  "ngx_lua_app"


typedef enum {
    NGX_LUA_APP_MATCH_EXACT = 0,
    NGX_LUA_APP_MATCH_PREFIX,
    NGX_LUA_APP_MATCH_ALL,
    NGX_LUA_APP_MATCH_PARAMS
} ngx_lua_app_match_e;


typedef struct {
    const char          *method;
    size_t               method_len;
    char                *pattern;
    size_t               pattern_len;
    ngx_lua_app_match_e  match;
    int                  handler_ref;
} ngx_lua_app_route_t;


struct ngx_lua_app_s {
    ngx_lua_app_route_t  *routes;
    size_t                nroutes;
    size_t                routes_cap;
};


static int ngx_lua_app_new(lua_State *L);
static int ngx_lua_app_all(lua_State *L);
static int ngx_lua_app_get_method(lua_State *L);
static int ngx_lua_app_post(lua_State *L);
static int ngx_lua_app_route(lua_State *L, const char *name,
    const char *method, size_t method_len);
static int ngx_lua_app_gc(lua_State *L);
static int ngx_lua_app_reserve(lua_State *L, ngx_lua_app_t *app, size_t n);
static ngx_uint_t ngx_lua_app_pattern_has_colon(const char *pattern,
    size_t len);
static ngx_uint_t ngx_lua_app_is_param_start(const char *pattern, size_t pos);
static ngx_uint_t ngx_lua_app_is_param_name_first(u_char ch);
static ngx_uint_t ngx_lua_app_is_param_name(u_char ch);
static int ngx_lua_app_validate_param_pattern(lua_State *L,
    const char *pattern, size_t len);
static int ngx_lua_app_match_params(lua_State *L,
    ngx_lua_app_route_t *route, const char *path, size_t len);
static void *ngx_lua_app_alloc(lua_State *L, void *ptr, size_t osize,
    size_t nsize);


static const luaL_Reg  ngx_lua_app_global_methods[] = {
    { "new", ngx_lua_app_new },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_app_methods[] = {
    { "all", ngx_lua_app_all },
    { "get", ngx_lua_app_get_method },
    { "post", ngx_lua_app_post },
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
ngx_lua_app_find_handler(lua_State *L, ngx_lua_app_t *app, const char *method,
    size_t method_len, const char *path, size_t len)
{
    size_t  i;

    for (i = 0; i < app->nroutes; i++) {
        if (app->routes[i].method != NULL
            && (app->routes[i].method_len != method_len
                || memcmp(app->routes[i].method, method, method_len) != 0))
        {
            continue;
        }

        if (app->routes[i].match == NGX_LUA_APP_MATCH_ALL) {
            lua_newtable(L);
            return app->routes[i].handler_ref;
        }

        if (app->routes[i].match == NGX_LUA_APP_MATCH_EXACT
            && app->routes[i].pattern_len == len
            && memcmp(app->routes[i].pattern, path, len) == 0)
        {
            lua_newtable(L);
            return app->routes[i].handler_ref;
        }

        if (app->routes[i].match == NGX_LUA_APP_MATCH_PREFIX
            && app->routes[i].pattern_len <= len
            && memcmp(app->routes[i].pattern, path,
                      app->routes[i].pattern_len) == 0)
        {
            lua_newtable(L);
            return app->routes[i].handler_ref;
        }

        if (app->routes[i].match == NGX_LUA_APP_MATCH_PARAMS
            && ngx_lua_app_match_params(L, &app->routes[i], path, len))
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
    return ngx_lua_app_route(L, "all", NULL, 0);
}


static int
ngx_lua_app_get_method(lua_State *L)
{
    return ngx_lua_app_route(L, "get", "GET", 3);
}


static int
ngx_lua_app_post(lua_State *L)
{
    return ngx_lua_app_route(L, "post", "POST", 4);
}


static int
ngx_lua_app_route(lua_State *L, const char *name, const char *method,
    size_t method_len)
{
    size_t                len;
    size_t                pattern_len;
    const char           *pattern;
    ngx_lua_app_t        *app;
    ngx_uint_t            has_colon;
    ngx_lua_app_match_e   match;
    ngx_lua_app_route_t  *route;

    if (lua_gettop(L) != 3) {
        return luaL_error(L, "App:%s() takes pattern and handler", name);
    }

    app = luaL_checkudata(L, 1, NGX_LUA_APP_METATABLE);
    pattern = luaL_checklstring(L, 2, &len);

    if (!lua_isfunction(L, 3)) {
        return luaL_argerror(L, 3, "function expected");
    }

    match = NGX_LUA_APP_MATCH_EXACT;
    pattern_len = len;
    has_colon = ngx_lua_app_pattern_has_colon(pattern, len);

    if (len == 1 && pattern[0] == '*') {
        match = NGX_LUA_APP_MATCH_ALL;

    } else if (len == 0 || pattern[0] != '/') {
        return luaL_argerror(L, 2, "route pattern must start with / or be *");

    } else if (has_colon) {
        if (ngx_lua_app_validate_param_pattern(L, pattern, len) != 0) {
            return luaL_error(L, "invalid route parameter pattern");
        }

        match = NGX_LUA_APP_MATCH_PARAMS;

    } else if (len > 1 && pattern[len - 1] == '*') {
        match = NGX_LUA_APP_MATCH_PREFIX;
        pattern_len = len - 1;
    }

    if (ngx_lua_app_reserve(L, app, app->nroutes + 1) != 0) {
        return luaL_error(L, "no memory");
    }

    route = &app->routes[app->nroutes++];

    route->method = method;
    route->method_len = method_len;
    route->match = match;

    route->pattern = ngx_lua_app_alloc(L, NULL, 0, pattern_len + 1);
    if (route->pattern == NULL) {
        app->nroutes--;
        return luaL_error(L, "no memory");
    }

    memcpy(route->pattern, pattern, pattern_len);
    route->pattern[pattern_len] = '\0';
    route->pattern_len = pattern_len;

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


static ngx_uint_t
ngx_lua_app_pattern_has_colon(const char *pattern, size_t len)
{
    size_t  i;

    for (i = 0; i < len; i++) {
        if (pattern[i] == ':') {
            return 1;
        }
    }

    return 0;
}


static ngx_uint_t
ngx_lua_app_is_param_start(const char *pattern, size_t pos)
{
    return pattern[pos] == ':' && (pos == 0 || pattern[pos - 1] == '/');
}


static ngx_uint_t
ngx_lua_app_is_param_name_first(u_char ch)
{
    return (ch >= 'A' && ch <= 'Z')
           || (ch >= 'a' && ch <= 'z')
           || ch == '_';
}


static ngx_uint_t
ngx_lua_app_is_param_name(u_char ch)
{
    return ngx_lua_app_is_param_name_first(ch)
           || (ch >= '0' && ch <= '9');
}


static int
ngx_lua_app_validate_param_pattern(lua_State *L, const char *pattern,
    size_t len)
{
    size_t  i, name_start, name_len;

    lua_newtable(L);

    for (i = 0; i < len; i++) {
        if (pattern[i] != ':') {
            continue;
        }

        if (!ngx_lua_app_is_param_start(pattern, i)) {
            lua_pop(L, 1);
            return luaL_argerror(
                L, 2, "route parameter must start a path segment");
        }

        name_start = i + 1;
        if (name_start == len
            || pattern[name_start] == '/'
            || !ngx_lua_app_is_param_name_first((u_char) pattern[name_start]))
        {
            lua_pop(L, 1);
            return luaL_argerror(
                L, 2, "route parameter name must be a Lua identifier");
        }

        i = name_start + 1;
        while (i < len && pattern[i] != '/') {
            if (!ngx_lua_app_is_param_name((u_char) pattern[i])) {
                lua_pop(L, 1);
                return luaL_argerror(
                    L, 2, "route parameter name must be a Lua identifier");
            }

            i++;
        }

        name_len = i - name_start;

        lua_pushlstring(L, pattern + name_start, name_len);
        lua_rawget(L, -2);

        if (!lua_isnil(L, -1)) {
            lua_pop(L, 2);
            return luaL_argerror(L, 2,
                                 "duplicate route parameter name");
        }

        lua_pop(L, 1);
        lua_pushlstring(L, pattern + name_start, name_len);
        lua_pushboolean(L, 1);
        lua_rawset(L, -3);
    }

    lua_pop(L, 1);

    return 0;
}


static int
ngx_lua_app_match_params(lua_State *L, ngx_lua_app_route_t *route,
    const char *path, size_t len)
{
    size_t       i, j, name_start, name_len, value_start, value_len;
    const char  *pattern;

    pattern = route->pattern;
    i = 0;
    j = 0;

    lua_newtable(L);

    while (i < route->pattern_len) {
        if (ngx_lua_app_is_param_start(pattern, i)) {
            name_start = i + 1;
            i = name_start;

            while (i < route->pattern_len && pattern[i] != '/') {
                i++;
            }

            name_len = i - name_start;
            value_start = j;

            while (j < len && path[j] != '/') {
                j++;
            }

            value_len = j - value_start;
            if (value_len == 0) {
                lua_pop(L, 1);
                return 0;
            }

            lua_pushlstring(L, pattern + name_start, name_len);
            lua_pushlstring(L, path + value_start, value_len);
            lua_rawset(L, -3);

            continue;
        }

        if (j >= len || pattern[i] != path[j]) {
            lua_pop(L, 1);
            return 0;
        }

        i++;
        j++;
    }

    if (j != len) {
        lua_pop(L, 1);
        return 0;
    }

    return 1;
}


static void *
ngx_lua_app_alloc(lua_State *L, void *ptr, size_t osize, size_t nsize)
{
    void       *ud;
    lua_Alloc   alloc;

    alloc = lua_getallocf(L, &ud);

    return alloc(ud, ptr, osize, nsize);
}
