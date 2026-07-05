/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua_web.h"

#include <lauxlib.h>


#define NGX_LUA_WEB_HEADERS_METATABLE  "ngx_lua_web.Headers"


typedef struct {
    ngx_str_t  name;
    ngx_str_t  value;
} ngx_lua_web_header_t;


struct ngx_lua_web_headers_s {
    ngx_lua_web_header_t  *elts;
    size_t                 nelts;
    size_t                 cap;
};


static int ngx_lua_web_headers_new(lua_State *L);
static int ngx_lua_web_headers_get_method(lua_State *L);
static int ngx_lua_web_headers_gc(lua_State *L);
static void ngx_lua_web_headers_copy_from_headers(lua_State *L,
    ngx_lua_web_headers_t *headers, ngx_lua_web_headers_t *source, int arg);
static void ngx_lua_web_headers_copy_from_table(lua_State *L,
    ngx_lua_web_headers_t *headers, int table, int arg);
static void ngx_lua_web_headers_copy_entry(lua_State *L,
    ngx_lua_web_headers_t *headers, int key, int value, int arg);
static ngx_int_t ngx_lua_web_headers_reserve(lua_State *L,
    ngx_lua_web_headers_t *headers, size_t n);
static ngx_int_t ngx_lua_web_headers_dup_lower(lua_State *L, ngx_str_t *dst,
    const char *src, size_t len);
static ngx_int_t ngx_lua_web_headers_dup(lua_State *L, ngx_str_t *dst,
    const char *src, size_t len);
static ngx_uint_t ngx_lua_web_headers_name_eq(ngx_str_t *stored,
    const char *name, size_t len);
static void ngx_lua_web_headers_free_entry(lua_State *L,
    ngx_lua_web_header_t *header);
static void *ngx_lua_web_headers_alloc(lua_State *L, void *ptr,
    size_t osize, size_t nsize);


static const luaL_Reg  ngx_lua_web_headers_global_methods[] = {
    { "new", ngx_lua_web_headers_new },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_web_headers_methods[] = {
    { "get", ngx_lua_web_headers_get_method },
    { NULL, NULL }
};


ngx_lua_web_headers_t *
ngx_lua_web_headers_create(lua_State *L)
{
    ngx_lua_web_headers_t  *headers;

    headers = lua_newuserdatauv(L, sizeof(ngx_lua_web_headers_t), 0);
    if (headers == NULL) {
        return NULL;
    }

    headers->elts = NULL;
    headers->nelts = 0;
    headers->cap = 0;

    luaL_setmetatable(L, NGX_LUA_WEB_HEADERS_METATABLE);

    return headers;
}


ngx_lua_web_headers_t *
ngx_lua_web_headers_get(lua_State *L, int index)
{
    return luaL_testudata(L, index, NGX_LUA_WEB_HEADERS_METATABLE);
}


size_t
ngx_lua_web_headers_count(ngx_lua_web_headers_t *headers)
{
    return headers->nelts;
}


ngx_int_t
ngx_lua_web_headers_get_entry(ngx_lua_web_headers_t *headers, size_t index,
    ngx_str_t *name, ngx_str_t *value)
{
    if (index >= headers->nelts) {
        return NGX_ERROR;
    }

    *name = headers->elts[index].name;
    *value = headers->elts[index].value;

    return NGX_OK;
}


void
ngx_lua_web_headers_register(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_HEADERS_METATABLE)) {
        lua_pushliteral(L, "Headers");
        lua_setfield(L, -2, "__name");
        luaL_setfuncs(L, ngx_lua_web_headers_methods, 0);
        lua_pushcfunction(L, ngx_lua_web_headers_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }

    lua_pop(L, 1);

    lua_newtable(L);
    luaL_setfuncs(L, ngx_lua_web_headers_global_methods, 0);
    lua_setglobal(L, "Headers");
}


static int
ngx_lua_web_headers_new(lua_State *L)
{
    int                    nargs;
    ngx_lua_web_headers_t *headers;

    nargs = lua_gettop(L);

    if (nargs > 1) {
        return luaL_error(L, "Headers.new() takes optional init");
    }

    headers = ngx_lua_web_headers_create(L);
    if (headers == NULL) {
        return luaL_error(L, "no memory");
    }

    if (nargs == 1 && !lua_isnil(L, 1)) {
        ngx_lua_web_headers_init(L, headers, 1, 1);
    }

    return 1;
}


static int
ngx_lua_web_headers_get_method(lua_State *L)
{
    size_t                 i, name_len;
    const char            *name;
    ngx_lua_web_header_t  *header;
    ngx_lua_web_headers_t *headers;

    if (lua_gettop(L) != 2) {
        return luaL_error(L, "Headers:get() takes name");
    }

    headers = luaL_checkudata(L, 1, NGX_LUA_WEB_HEADERS_METATABLE);

    if (lua_type(L, 2) != LUA_TSTRING) {
        return luaL_argerror(L, 2, "header name must be a string");
    }

    name = lua_tolstring(L, 2, &name_len);

    for (i = 0; i < headers->nelts; i++) {
        header = &headers->elts[i];

        if (ngx_lua_web_headers_name_eq(&header->name, name, name_len)) {
            lua_pushlstring(L, (const char *) header->value.data,
                            header->value.len);
            return 1;
        }
    }

    lua_pushnil(L);
    return 1;
}


static int
ngx_lua_web_headers_gc(lua_State *L)
{
    size_t                 i;
    ngx_lua_web_headers_t *headers;

    headers = luaL_checkudata(L, 1, NGX_LUA_WEB_HEADERS_METATABLE);

    for (i = 0; i < headers->nelts; i++) {
        ngx_lua_web_headers_free_entry(L, &headers->elts[i]);
    }

    ngx_lua_web_headers_alloc(L, headers->elts,
                              headers->cap * sizeof(ngx_lua_web_header_t),
                              0);

    headers->elts = NULL;
    headers->nelts = 0;
    headers->cap = 0;

    return 0;
}


void
ngx_lua_web_headers_init(lua_State *L, ngx_lua_web_headers_t *headers,
    int init_index, int arg)
{
    ngx_lua_web_headers_t *source_headers;

    if (lua_isnil(L, init_index)) {
        return;
    }

    source_headers = ngx_lua_web_headers_get(L, init_index);
    if (source_headers != NULL) {
        ngx_lua_web_headers_copy_from_headers(L, headers, source_headers, arg);
        return;
    }

    if (lua_istable(L, init_index)) {
        ngx_lua_web_headers_copy_from_table(L, headers, init_index, arg);
        return;
    }

    luaL_argerror(L, arg, "table or Headers expected");
}


static void
ngx_lua_web_headers_copy_from_headers(lua_State *L,
    ngx_lua_web_headers_t *headers, ngx_lua_web_headers_t *source, int arg)
{
    size_t  i;

    for (i = 0; i < source->nelts; i++) {
        ngx_lua_web_headers_set(L, headers,
                                (const char *) source->elts[i].name.data,
                                source->elts[i].name.len,
                                (const char *) source->elts[i].value.data,
                                source->elts[i].value.len);
    }

    (void) arg;
}


static void
ngx_lua_web_headers_copy_from_table(lua_State *L,
    ngx_lua_web_headers_t *headers, int table, int arg)
{
    table = lua_absindex(L, table);

    lua_pushnil(L);
    while (lua_next(L, table) != 0) {
        ngx_lua_web_headers_copy_entry(L, headers, -2, -1, arg);
        lua_pop(L, 1);
    }
}


static void
ngx_lua_web_headers_copy_entry(lua_State *L, ngx_lua_web_headers_t *headers,
    int key, int value, int arg)
{
    size_t       name_len, value_len;
    const char  *name, *header_value;

    if (lua_type(L, key) != LUA_TSTRING) {
        luaL_argerror(L, arg, "header names must be strings");
    }

    if (lua_type(L, value) != LUA_TSTRING) {
        luaL_argerror(L, arg, "header values must be strings");
    }

    name = lua_tolstring(L, key, &name_len);
    header_value = lua_tolstring(L, value, &value_len);

    ngx_lua_web_headers_set(L, headers, name, name_len,
                            header_value, value_len);
}


void
ngx_lua_web_headers_set(lua_State *L, ngx_lua_web_headers_t *headers,
    const char *name, size_t name_len, const char *value, size_t value_len)
{
    size_t                 i;
    ngx_str_t              new_name, new_value;
    ngx_lua_web_header_t  *header;

    for (i = 0; i < headers->nelts; i++) {
        header = &headers->elts[i];

        if (ngx_lua_web_headers_name_eq(&header->name, name, name_len)) {
            if (ngx_lua_web_headers_dup(L, &new_value, value, value_len)
                != NGX_OK)
            {
                (void) luaL_error(L, "no memory");
                return;
            }

            ngx_lua_web_headers_alloc(L, header->value.data,
                                      header->value.len, 0);
            header->value = new_value;
            return;
        }
    }

    if (ngx_lua_web_headers_reserve(L, headers, headers->nelts + 1)
        != NGX_OK)
    {
        (void) luaL_error(L, "no memory");
        return;
    }

    if (ngx_lua_web_headers_dup_lower(L, &new_name, name, name_len) != NGX_OK)
    {
        (void) luaL_error(L, "no memory");
        return;
    }

    if (ngx_lua_web_headers_dup(L, &new_value, value, value_len) != NGX_OK) {
        ngx_lua_web_headers_alloc(L, new_name.data, new_name.len, 0);
        (void) luaL_error(L, "no memory");
        return;
    }

    header = &headers->elts[headers->nelts++];
    header->name = new_name;
    header->value = new_value;
}


static ngx_int_t
ngx_lua_web_headers_reserve(lua_State *L, ngx_lua_web_headers_t *headers,
    size_t n)
{
    size_t                 cap;
    ngx_lua_web_header_t  *elts;

    if (n <= headers->cap) {
        return NGX_OK;
    }

    cap = (headers->cap == 0) ? 4 : headers->cap * 2;
    while (cap < n) {
        cap *= 2;
    }

    elts = ngx_lua_web_headers_alloc(L, headers->elts,
                                     headers->cap
                                     * sizeof(ngx_lua_web_header_t),
                                     cap * sizeof(ngx_lua_web_header_t));
    if (elts == NULL) {
        return NGX_ERROR;
    }

    headers->elts = elts;
    headers->cap = cap;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_headers_dup_lower(lua_State *L, ngx_str_t *dst, const char *src,
    size_t len)
{
    size_t  i;

    dst->len = len;
    dst->data = NULL;

    if (len == 0) {
        return NGX_OK;
    }

    dst->data = ngx_lua_web_headers_alloc(L, NULL, 0, len);
    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    for (i = 0; i < len; i++) {
        if (src[i] >= 'A' && src[i] <= 'Z') {
            dst->data[i] = (u_char) (src[i] + ('a' - 'A'));

        } else {
            dst->data[i] = (u_char) src[i];
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_headers_dup(lua_State *L, ngx_str_t *dst, const char *src,
    size_t len)
{
    dst->len = len;
    dst->data = NULL;

    if (len == 0) {
        return NGX_OK;
    }

    dst->data = ngx_lua_web_headers_alloc(L, NULL, 0, len);
    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(dst->data, src, len);

    return NGX_OK;
}


static ngx_uint_t
ngx_lua_web_headers_name_eq(ngx_str_t *stored, const char *name, size_t len)
{
    size_t  i;
    u_char  ch;

    if (stored->len != len) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        if (name[i] >= 'A' && name[i] <= 'Z') {
            ch = (u_char) (name[i] + ('a' - 'A'));

        } else {
            ch = (u_char) name[i];
        }

        if (stored->data[i] != ch) {
            return 0;
        }
    }

    return 1;
}


static void
ngx_lua_web_headers_free_entry(lua_State *L, ngx_lua_web_header_t *header)
{
    ngx_lua_web_headers_alloc(L, header->name.data, header->name.len, 0);
    ngx_lua_web_headers_alloc(L, header->value.data, header->value.len, 0);

    header->name.data = NULL;
    header->name.len = 0;
    header->value.data = NULL;
    header->value.len = 0;
}


static void *
ngx_lua_web_headers_alloc(lua_State *L, void *ptr, size_t osize, size_t nsize)
{
    void       *ud;
    lua_Alloc   alloc;

    alloc = lua_getallocf(L, &ud);

    return alloc(ud, ptr, osize, nsize);
}
