/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua_web.h"

#include <lauxlib.h>


#define NGX_LUA_WEB_RESPONSE_METATABLE  "ngx_lua_web.Response"
#define NGX_LUA_WEB_HTTP_NO_CONTENT     204
#define NGX_LUA_WEB_HTTP_RESET_CONTENT  205
#define NGX_LUA_WEB_HTTP_NOT_MODIFIED   304


static int ngx_lua_web_response_new(lua_State *L);
static int ngx_lua_web_response_index(lua_State *L);
static int ngx_lua_web_response_gc(lua_State *L);
static void ngx_lua_web_response_init(lua_State *L,
    ngx_lua_web_response_t *response, int init_index, int arg);
static void ngx_lua_web_response_init_status(lua_State *L,
    ngx_lua_web_response_t *response, int init_index, int arg);
static void ngx_lua_web_response_init_body(lua_State *L,
    ngx_lua_web_response_t *response, int init_index, int arg,
    int response_index);
static ngx_uint_t ngx_lua_web_response_has_null_body_status(
    ngx_uint_t status);


static const luaL_Reg  ngx_lua_web_response_global_methods[] = {
    { "new", ngx_lua_web_response_new },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_web_response_methods[] = {
    { NULL, NULL }
};


void
ngx_lua_web_response_register(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_RESPONSE_METATABLE)) {
        lua_pushliteral(L, "Response");
        lua_setfield(L, -2, "__name");
        luaL_setfuncs(L, ngx_lua_web_response_methods, 0);
        lua_pushcfunction(L, ngx_lua_web_response_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushcfunction(L, ngx_lua_web_response_index);
        lua_setfield(L, -2, "__index");
    }

    lua_pop(L, 1);

    lua_newtable(L);
    luaL_setfuncs(L, ngx_lua_web_response_global_methods, 0);
    lua_setglobal(L, "Response");
}


ngx_lua_web_response_t *
ngx_lua_web_response_create(lua_State *L)
{
    int                     response_index;
    ngx_lua_web_response_t *response;

    response = lua_newuserdatauv(L, sizeof(ngx_lua_web_response_t), 2);
    if (response == NULL) {
        return NULL;
    }

    response_index = lua_absindex(L, -1);
    response->headers = NULL;
    response->body = NULL;
    response->status = 200;

    luaL_setmetatable(L, NGX_LUA_WEB_RESPONSE_METATABLE);

    response->headers = ngx_lua_web_headers_create(L);
    if (response->headers == NULL) {
        return NULL;
    }

    lua_setiuservalue(L, response_index, 1);

    return response;
}


ngx_lua_web_response_t *
ngx_lua_web_response_get(lua_State *L, int index)
{
    return luaL_testudata(L, index, NGX_LUA_WEB_RESPONSE_METATABLE);
}


static int
ngx_lua_web_response_new(lua_State *L)
{
    int                     nargs;
    ngx_lua_web_response_t *response;

    nargs = lua_gettop(L);

    if (nargs > 1) {
        return luaL_error(L, "Response.new() takes optional init");
    }

    if (nargs == 1 && !lua_isnil(L, 1) && !lua_istable(L, 1)) {
        return luaL_argerror(L, 1, "table expected");
    }

    response = ngx_lua_web_response_create(L);
    if (response == NULL) {
        return luaL_error(L, "no memory");
    }

    if (nargs == 1 && !lua_isnil(L, 1)) {
        ngx_lua_web_response_init(L, response, 1, 1);
    }

    return 1;
}


static int
ngx_lua_web_response_index(lua_State *L)
{
    size_t                  len;
    const char             *name;
    ngx_lua_web_response_t *response;

    response = luaL_checkudata(L, 1, NGX_LUA_WEB_RESPONSE_METATABLE);
    name = lua_tolstring(L, 2, &len);

    if (name == NULL) {
        lua_pushnil(L);
        return 1;
    }

    if (len == 7 && ngx_strncmp(name, "headers", 7) == 0) {
        lua_getiuservalue(L, 1, 1);
        return 1;
    }

    if (len == 4 && ngx_strncmp(name, "body", 4) == 0) {
        if (response->body == NULL) {
            lua_pushnil(L);
            return 1;
        }

        lua_getiuservalue(L, 1, 2);
        return 1;
    }

    if (len == 8 && ngx_strncmp(name, "bodyUsed", 8) == 0) {
        lua_pushboolean(L, response->body != NULL
                           && ngx_lua_web_stream_body_used(response->body));
        return 1;
    }

    if (len == 6 && ngx_strncmp(name, "status", 6) == 0) {
        lua_pushinteger(L, response->status);
        return 1;
    }

    lua_pushnil(L);
    return 1;
}


static int
ngx_lua_web_response_gc(lua_State *L)
{
    ngx_lua_web_response_t  *response;

    response = luaL_checkudata(L, 1, NGX_LUA_WEB_RESPONSE_METATABLE);

    response->headers = NULL;
    response->body = NULL;

    return 0;
}


static void
ngx_lua_web_response_init(lua_State *L, ngx_lua_web_response_t *response,
    int init_index, int arg)
{
    int  headers_index;
    int  response_index;

    response_index = lua_absindex(L, -1);
    init_index = lua_absindex(L, init_index);

    ngx_lua_web_response_init_status(L, response, init_index, arg);
    ngx_lua_web_response_init_body(L, response, init_index, arg,
                                   response_index);

    lua_getfield(L, init_index, "headers");

    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    headers_index = lua_absindex(L, -1);
    if (response->headers == NULL) {
        (void) luaL_error(L, "Response headers are invalid");
    }

    ngx_lua_web_headers_init(L, response->headers, headers_index, arg);
    lua_pop(L, 1);
}


static void
ngx_lua_web_response_init_status(lua_State *L,
    ngx_lua_web_response_t *response, int init_index, int arg)
{
    lua_Integer  status;

    lua_getfield(L, init_index, "status");

    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    if (!lua_isinteger(L, -1)) {
        luaL_argerror(L, arg, "status must be an integer");
    }

    status = lua_tointeger(L, -1);
    if (status < 200 || status > 599) {
        luaL_argerror(L, arg, "status must be from 200 to 599");
    }

    response->status = (ngx_uint_t) status;
    lua_pop(L, 1);
}


static void
ngx_lua_web_response_init_body(lua_State *L,
    ngx_lua_web_response_t *response, int init_index, int arg,
    int response_index)
{
    ngx_lua_web_stream_t  *body;

    lua_getfield(L, init_index, "body");

    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    body = ngx_lua_web_stream_get(L, -1);
    if (body == NULL) {
        luaL_argerror(L, arg, "body must be a ReadableStream");
    }

    if (ngx_lua_web_response_has_null_body_status(response->status)) {
        luaL_argerror(L, arg, "body is not allowed for this status");
    }

    response->body = body;
    lua_setiuservalue(L, response_index, 2);
}


static ngx_uint_t
ngx_lua_web_response_has_null_body_status(ngx_uint_t status)
{
    return status == NGX_LUA_WEB_HTTP_NO_CONTENT
           || status == NGX_LUA_WEB_HTTP_RESET_CONTENT
           || status == NGX_LUA_WEB_HTTP_NOT_MODIFIED;
}
