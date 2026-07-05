/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua_web.h"

#include <lauxlib.h>


#define NGX_LUA_WEB_REQUEST_METATABLE  "ngx_lua_web.Request"


static int ngx_lua_web_request_new(lua_State *L);
static int ngx_lua_web_request_index(lua_State *L);
static int ngx_lua_web_request_gc(lua_State *L);
static void ngx_lua_web_request_copy(lua_State *L,
    ngx_lua_web_request_t *request, ngx_lua_web_request_t *source,
    int request_index, int source_index, int arg);
static void ngx_lua_web_request_reset_headers(lua_State *L,
    ngx_lua_web_request_t *request, int request_index);
static ngx_uint_t ngx_lua_web_request_init_has_field(lua_State *L,
    int init_index, const char *name);
static void ngx_lua_web_request_init_body(lua_State *L,
    ngx_lua_web_request_t *request, int init_index, int arg,
    int request_index);
static ngx_uint_t ngx_lua_web_request_method_forbids_body(ngx_str_t *method);
static void ngx_lua_web_request_init_string_field(lua_State *L,
    ngx_str_t *field, int init_index, const char *name, int arg);
static void ngx_lua_web_request_free_string(lua_State *L, ngx_str_t *field);
static void *ngx_lua_web_request_alloc(lua_State *L, void *ptr,
    size_t osize, size_t nsize);


static const luaL_Reg  ngx_lua_web_request_global_methods[] = {
    { "new", ngx_lua_web_request_new },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_web_request_methods[] = {
    { NULL, NULL }
};


void
ngx_lua_web_request_register(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_REQUEST_METATABLE)) {
        lua_pushliteral(L, "Request");
        lua_setfield(L, -2, "__name");
        luaL_setfuncs(L, ngx_lua_web_request_methods, 0);
        lua_pushcfunction(L, ngx_lua_web_request_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushcfunction(L, ngx_lua_web_request_index);
        lua_setfield(L, -2, "__index");
    }

    lua_pop(L, 1);

    lua_newtable(L);
    luaL_setfuncs(L, ngx_lua_web_request_global_methods, 0);
    lua_setglobal(L, "Request");
}


static int
ngx_lua_web_request_new(lua_State *L)
{
    int                    nargs;
    ngx_lua_web_request_t *request;

    nargs = lua_gettop(L);

    if (nargs > 2) {
        return luaL_error(L, "Request.new() takes input and optional init");
    }

    if (nargs == 2 && !lua_isnil(L, 2) && !lua_istable(L, 2)) {
        return luaL_argerror(L, 2, "table expected");
    }

    if (nargs == 2 && lua_isnil(L, 1)) {
        return luaL_argerror(L, 1, "Request or string expected");
    }

    if (nargs >= 1 && !lua_isnil(L, 1)
        && ngx_lua_web_request_get(L, 1) == NULL
        && lua_type(L, 1) != LUA_TSTRING)
    {
        return luaL_argerror(L, 1, "Request or string expected");
    }

    request = ngx_lua_web_request_create(L);
    if (request == NULL) {
        return luaL_error(L, "no memory");
    }

    if (nargs >= 1 && !lua_isnil(L, 1)) {
        ngx_lua_web_request_init(L, request, -1, 1, nargs == 2 ? 2 : 0, 1);
    }

    return 1;
}


static int
ngx_lua_web_request_index(lua_State *L)
{
    size_t                 len;
    const char            *name;
    ngx_lua_web_request_t *request;

    request = luaL_checkudata(L, 1, NGX_LUA_WEB_REQUEST_METATABLE);
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
        if (request->body == NULL) {
            lua_pushnil(L);
            return 1;
        }

        lua_getiuservalue(L, 1, 2);
        return 1;
    }

    if (len == 3 && ngx_strncmp(name, "url", 3) == 0) {
        lua_pushlstring(L, (const char *) request->url.data,
                        request->url.len);
        return 1;
    }

    if (len == 6 && ngx_strncmp(name, "method", 6) == 0) {
        lua_pushlstring(L, (const char *) request->method.data,
                        request->method.len);
        return 1;
    }

    lua_pushnil(L);
    return 1;
}


ngx_lua_web_request_t *
ngx_lua_web_request_create(lua_State *L)
{
    int                    request_index;
    ngx_lua_web_request_t *request;

    request = lua_newuserdatauv(L, sizeof(ngx_lua_web_request_t), 2);
    if (request == NULL) {
        return NULL;
    }

    request_index = lua_absindex(L, -1);
    request->headers = NULL;
    request->body = NULL;
    request->url.data = NULL;
    request->url.len = 0;
    request->method.data = NULL;
    request->method.len = 0;

    luaL_setmetatable(L, NGX_LUA_WEB_REQUEST_METATABLE);

    if (ngx_lua_web_request_set_string(L, &request->method, "GET", 3)
        != NGX_OK)
    {
        return NULL;
    }

    request->headers = ngx_lua_web_headers_create(L);
    if (request->headers == NULL) {
        return NULL;
    }

    lua_setiuservalue(L, request_index, 1);

    return request;
}


ngx_lua_web_request_t *
ngx_lua_web_request_get(lua_State *L, int index)
{
    return luaL_testudata(L, index, NGX_LUA_WEB_REQUEST_METATABLE);
}


static int
ngx_lua_web_request_gc(lua_State *L)
{
    ngx_lua_web_request_t  *request;

    request = luaL_checkudata(L, 1, NGX_LUA_WEB_REQUEST_METATABLE);

    ngx_lua_web_request_free_string(L, &request->url);
    ngx_lua_web_request_free_string(L, &request->method);

    request->headers = NULL;
    request->body = NULL;

    return 0;
}


void
ngx_lua_web_request_init(lua_State *L, ngx_lua_web_request_t *request,
    int request_index, int input_index, int init_index, int arg)
{
    int                     headers_index;
    size_t                  url_len;
    const char             *url;
    ngx_lua_web_request_t  *source;

    request_index = lua_absindex(L, request_index);
    input_index = lua_absindex(L, input_index);

    if (init_index != 0) {
        init_index = lua_absindex(L, init_index);
    }

    source = ngx_lua_web_request_get(L, input_index);
    if (source != NULL) {
        ngx_lua_web_request_copy(L, request, source, request_index,
                                 input_index, arg);

    } else if (lua_type(L, input_index) == LUA_TSTRING) {
        url = lua_tolstring(L, input_index, &url_len);

        if (ngx_lua_web_request_set_string(L, &request->url, url, url_len)
            != NGX_OK)
        {
            (void) luaL_error(L, "no memory");
        }

    } else {
        luaL_argerror(L, arg, "Request or string expected");
    }

    if (init_index != 0 && !lua_isnil(L, init_index)) {
        if (ngx_lua_web_request_init_has_field(L, init_index, "headers")) {
            ngx_lua_web_request_reset_headers(L, request, request_index);
        }

        ngx_lua_web_request_init_string_field(L, &request->method,
                                              init_index, "method", arg);
        ngx_lua_web_request_init_body(L, request, init_index, arg,
                                      request_index);

        if (request->body != NULL
            && ngx_lua_web_request_method_forbids_body(&request->method))
        {
            (void) luaL_argerror(L, arg,
                                 "GET and HEAD requests cannot have a body");
        }

        lua_getfield(L, init_index, "headers");

        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            return;
        }

        headers_index = lua_absindex(L, -1);
        if (request->headers == NULL) {
            (void) luaL_error(L, "Request headers are invalid");
        }

        ngx_lua_web_headers_init(L, request->headers, headers_index, arg);
        lua_pop(L, 1);
    }
}


static void
ngx_lua_web_request_copy(lua_State *L, ngx_lua_web_request_t *request,
    ngx_lua_web_request_t *source, int request_index, int source_index,
    int arg)
{
    request_index = lua_absindex(L, request_index);
    source_index = lua_absindex(L, source_index);

    if (ngx_lua_web_request_set_string(L, &request->url,
                                       (const char *) source->url.data,
                                       source->url.len)
        != NGX_OK)
    {
        (void) luaL_error(L, "no memory");
    }

    if (ngx_lua_web_request_set_string(L, &request->method,
                                       (const char *) source->method.data,
                                       source->method.len)
        != NGX_OK)
    {
        (void) luaL_error(L, "no memory");
    }

    lua_getiuservalue(L, source_index, 1);
    ngx_lua_web_headers_init(L, request->headers, -1, arg);
    lua_pop(L, 1);

    if (source->body != NULL) {
        request->body = source->body;
        lua_getiuservalue(L, source_index, 2);
        lua_setiuservalue(L, request_index, 2);
    }
}


static void
ngx_lua_web_request_reset_headers(lua_State *L,
    ngx_lua_web_request_t *request, int request_index)
{
    request_index = lua_absindex(L, request_index);

    request->headers = ngx_lua_web_headers_create(L);
    if (request->headers == NULL) {
        (void) luaL_error(L, "no memory");
        return;
    }

    lua_setiuservalue(L, request_index, 1);
}


static ngx_uint_t
ngx_lua_web_request_init_has_field(lua_State *L, int init_index,
    const char *name)
{
    ngx_uint_t  found;

    init_index = lua_absindex(L, init_index);

    lua_getfield(L, init_index, name);
    found = !lua_isnil(L, -1);
    lua_pop(L, 1);

    return found;
}


static void
ngx_lua_web_request_init_body(lua_State *L, ngx_lua_web_request_t *request,
    int init_index, int arg, int request_index)
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

    request->body = body;
    lua_setiuservalue(L, request_index, 2);
}


static ngx_uint_t
ngx_lua_web_request_method_forbids_body(ngx_str_t *method)
{
    if (method->len == 3 && ngx_strncmp(method->data, "GET", 3) == 0) {
        return 1;
    }

    if (method->len == 4 && ngx_strncmp(method->data, "HEAD", 4) == 0) {
        return 1;
    }

    return 0;
}


static void
ngx_lua_web_request_init_string_field(lua_State *L, ngx_str_t *field,
    int init_index, const char *name, int arg)
{
    size_t       len;
    const char  *value;

    lua_getfield(L, init_index, name);

    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    if (lua_type(L, -1) != LUA_TSTRING) {
        luaL_argerror(L, arg, "url and method must be strings");
    }

    value = lua_tolstring(L, -1, &len);
    if (ngx_lua_web_request_set_string(L, field, value, len) != NGX_OK) {
        (void) luaL_error(L, "no memory");
        return;
    }

    lua_pop(L, 1);
}


ngx_int_t
ngx_lua_web_request_set_string(lua_State *L, ngx_str_t *field,
    const char *value, size_t len)
{
    ngx_str_t  copy;

    copy.data = NULL;
    copy.len = len;

    if (len != 0) {
        copy.data = ngx_lua_web_request_alloc(L, NULL, 0, len);
        if (copy.data == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(copy.data, value, len);
    }

    ngx_lua_web_request_free_string(L, field);
    *field = copy;

    return NGX_OK;
}


static void
ngx_lua_web_request_free_string(lua_State *L, ngx_str_t *field)
{
    ngx_lua_web_request_alloc(L, field->data, field->len, 0);

    field->data = NULL;
    field->len = 0;
}


static void *
ngx_lua_web_request_alloc(lua_State *L, void *ptr, size_t osize, size_t nsize)
{
    void       *ud;
    lua_Alloc   alloc;

    alloc = lua_getallocf(L, &ud);

    return alloc(ud, ptr, osize, nsize);
}
