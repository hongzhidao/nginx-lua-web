/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua_web.h"

#include <lauxlib.h>


#define NGX_LUA_WEB_SEARCH_PARAMS_METATABLE  "ngx_lua_web.URLSearchParams"


typedef struct {
    ngx_str_t  name;
    ngx_str_t  value;
} ngx_lua_web_search_param_t;


struct ngx_lua_web_search_params_s {
    ngx_lua_web_search_param_t  *elts;
    size_t                       nelts;
    size_t                       cap;
    ngx_lua_web_url_t           *owner;
};


static int ngx_lua_web_search_params_new(lua_State *L);
static int ngx_lua_web_search_params_append_method(lua_State *L);
static int ngx_lua_web_search_params_delete_method(lua_State *L);
static int ngx_lua_web_search_params_get_method(lua_State *L);
static int ngx_lua_web_search_params_get_all_method(lua_State *L);
static int ngx_lua_web_search_params_has_method(lua_State *L);
static int ngx_lua_web_search_params_set_method(lua_State *L);
static int ngx_lua_web_search_params_sort_method(lua_State *L);
static int ngx_lua_web_search_params_to_string_method(lua_State *L);
static int ngx_lua_web_search_params_index(lua_State *L);
static int ngx_lua_web_search_params_gc(lua_State *L);
static int ngx_lua_web_search_params_tostring(lua_State *L);
static ngx_lua_web_search_params_t *ngx_lua_web_search_params_get(
    lua_State *L, int index);
static void ngx_lua_web_search_params_init(lua_State *L,
    ngx_lua_web_search_params_t *params, int init_index, int arg);
static void ngx_lua_web_search_params_copy_from_params(lua_State *L,
    ngx_lua_web_search_params_t *params,
    ngx_lua_web_search_params_t *source, int arg);
static void ngx_lua_web_search_params_copy_from_table(lua_State *L,
    ngx_lua_web_search_params_t *params, int table, int arg);
static void ngx_lua_web_search_params_copy_table_pair(lua_State *L,
    ngx_lua_web_search_params_t *params, int key, int value, int arg);
static void ngx_lua_web_search_params_copy_sequence_pair(lua_State *L,
    ngx_lua_web_search_params_t *params, int value, int arg);
static ngx_int_t ngx_lua_web_search_params_append(lua_State *L,
    ngx_lua_web_search_params_t *params, const char *name, size_t name_len,
    const char *value, size_t value_len);
static ngx_int_t ngx_lua_web_search_params_append_owned(lua_State *L,
    ngx_lua_web_search_params_t *params, ngx_str_t *name,
    ngx_str_t *value);
static void ngx_lua_web_search_params_remove_at(lua_State *L,
    ngx_lua_web_search_params_t *params, size_t index);
static ngx_int_t ngx_lua_web_search_params_reserve(lua_State *L,
    ngx_lua_web_search_params_t *params, size_t n);
static ngx_int_t ngx_lua_web_search_params_dup(lua_State *L, ngx_str_t *dst,
    const char *src, size_t len);
static ngx_int_t ngx_lua_web_search_params_decode(lua_State *L,
    ngx_str_t *dst, const char *src, size_t len);
static size_t ngx_lua_web_search_params_decode_len(const char *src,
    size_t len);
static ngx_int_t ngx_lua_web_search_params_encode(lua_State *L,
    ngx_str_t *dst, ngx_str_t *src);
static size_t ngx_lua_web_search_params_encode_len(ngx_str_t *src);
static ngx_uint_t ngx_lua_web_search_params_is_unescaped(u_char ch);
static ngx_int_t ngx_lua_web_search_params_hex(u_char ch);
static ngx_uint_t ngx_lua_web_search_params_name_eq(
    ngx_lua_web_search_param_t *param, const char *name, size_t len);
static ngx_uint_t ngx_lua_web_search_params_value_eq(
    ngx_lua_web_search_param_t *param, const char *value, size_t len);
static ngx_int_t ngx_lua_web_search_params_name_cmp(ngx_str_t *left,
    ngx_str_t *right);
static void ngx_lua_web_search_params_sync_owner(lua_State *L,
    ngx_lua_web_search_params_t *params);
static void ngx_lua_web_search_params_free_entry(lua_State *L,
    ngx_lua_web_search_param_t *param);
static void *ngx_lua_web_search_params_alloc(lua_State *L, void *ptr,
    size_t osize, size_t nsize);


static const luaL_Reg  ngx_lua_web_search_params_global_methods[] = {
    { "new", ngx_lua_web_search_params_new },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_web_search_params_methods[] = {
    { "append", ngx_lua_web_search_params_append_method },
    { "delete", ngx_lua_web_search_params_delete_method },
    { "get", ngx_lua_web_search_params_get_method },
    { "getAll", ngx_lua_web_search_params_get_all_method },
    { "has", ngx_lua_web_search_params_has_method },
    { "set", ngx_lua_web_search_params_set_method },
    { "sort", ngx_lua_web_search_params_sort_method },
    { "toString", ngx_lua_web_search_params_to_string_method },
    { NULL, NULL }
};


ngx_lua_web_search_params_t *
ngx_lua_web_search_params_create(lua_State *L)
{
    ngx_lua_web_search_params_t  *params;

    params = lua_newuserdatauv(L, sizeof(ngx_lua_web_search_params_t), 1);
    if (params == NULL) {
        return NULL;
    }

    params->elts = NULL;
    params->nelts = 0;
    params->cap = 0;
    params->owner = NULL;

    luaL_setmetatable(L, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);

    return params;
}


static ngx_lua_web_search_params_t *
ngx_lua_web_search_params_get(lua_State *L, int index)
{
    return luaL_testudata(L, index, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);
}


void
ngx_lua_web_search_params_set_owner(lua_State *L,
    ngx_lua_web_search_params_t *params, int params_index,
    ngx_lua_web_url_t *url, int url_index)
{
    params_index = lua_absindex(L, params_index);

    params->owner = url;

    if (url == NULL) {
        lua_pushnil(L);

    } else {
        lua_pushvalue(L, url_index);
    }

    lua_setiuservalue(L, params_index, 1);
}


void
ngx_lua_web_search_params_register(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE)) {
        lua_pushliteral(L, "URLSearchParams");
        lua_setfield(L, -2, "__name");
        luaL_setfuncs(L, ngx_lua_web_search_params_methods, 0);
        lua_pushcfunction(L, ngx_lua_web_search_params_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushcfunction(L, ngx_lua_web_search_params_tostring);
        lua_setfield(L, -2, "__tostring");
        lua_pushcfunction(L, ngx_lua_web_search_params_index);
        lua_setfield(L, -2, "__index");
    }

    lua_pop(L, 1);

    lua_newtable(L);
    luaL_setfuncs(L, ngx_lua_web_search_params_global_methods, 0);
    lua_setglobal(L, "URLSearchParams");
}


static int
ngx_lua_web_search_params_new(lua_State *L)
{
    int                          nargs;
    ngx_lua_web_search_params_t *params;

    nargs = lua_gettop(L);

    if (nargs > 1) {
        return luaL_error(L, "URLSearchParams.new() takes optional init");
    }

    params = ngx_lua_web_search_params_create(L);
    if (params == NULL) {
        return luaL_error(L, "no memory");
    }

    if (nargs == 1 && !lua_isnil(L, 1)) {
        ngx_lua_web_search_params_init(L, params, 1, 1);
    }

    return 1;
}


static int
ngx_lua_web_search_params_append_method(lua_State *L)
{
    size_t                        name_len, value_len;
    const char                   *name, *value;
    ngx_lua_web_search_params_t  *params;

    if (lua_gettop(L) != 3) {
        return luaL_error(L, "URLSearchParams:append() takes name and value");
    }

    params = luaL_checkudata(L, 1, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);
    name = luaL_checklstring(L, 2, &name_len);
    value = luaL_checklstring(L, 3, &value_len);

    if (ngx_lua_web_search_params_append(L, params, name, name_len,
                                         value, value_len)
        != NGX_OK)
    {
        return luaL_error(L, "no memory");
    }

    ngx_lua_web_search_params_sync_owner(L, params);

    return 0;
}


static int
ngx_lua_web_search_params_delete_method(lua_State *L)
{
    size_t                        i, name_len, value_len;
    const char                   *name, *value;
    ngx_lua_web_search_params_t  *params;

    if (lua_gettop(L) != 2 && lua_gettop(L) != 3) {
        return luaL_error(L,
                          "URLSearchParams:delete() takes name and optional value");
    }

    params = luaL_checkudata(L, 1, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);
    name = luaL_checklstring(L, 2, &name_len);

    value = NULL;
    value_len = 0;

    if (lua_gettop(L) == 3) {
        value = luaL_checklstring(L, 3, &value_len);
    }

    for (i = 0; i < params->nelts; /* void */) {
        if (ngx_lua_web_search_params_name_eq(&params->elts[i], name,
                                              name_len)
            && (value == NULL
                || ngx_lua_web_search_params_value_eq(&params->elts[i],
                                                      value, value_len)))
        {
            ngx_lua_web_search_params_remove_at(L, params, i);
            continue;
        }

        i++;
    }

    ngx_lua_web_search_params_sync_owner(L, params);

    return 0;
}


static int
ngx_lua_web_search_params_get_method(lua_State *L)
{
    size_t                        i, name_len;
    const char                   *name;
    ngx_lua_web_search_params_t  *params;

    if (lua_gettop(L) != 2) {
        return luaL_error(L, "URLSearchParams:get() takes name");
    }

    params = luaL_checkudata(L, 1, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);
    name = luaL_checklstring(L, 2, &name_len);

    for (i = 0; i < params->nelts; i++) {
        if (ngx_lua_web_search_params_name_eq(&params->elts[i], name,
                                              name_len))
        {
            lua_pushlstring(L, params->elts[i].value.data == NULL
                            ? ""
                            : (const char *) params->elts[i].value.data,
                            params->elts[i].value.len);
            return 1;
        }
    }

    lua_pushnil(L);
    return 1;
}


static int
ngx_lua_web_search_params_get_all_method(lua_State *L)
{
    size_t                        i, n, name_len;
    const char                   *name;
    ngx_lua_web_search_params_t  *params;

    if (lua_gettop(L) != 2) {
        return luaL_error(L, "URLSearchParams:getAll() takes name");
    }

    params = luaL_checkudata(L, 1, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);
    name = luaL_checklstring(L, 2, &name_len);

    lua_newtable(L);
    n = 1;

    for (i = 0; i < params->nelts; i++) {
        if (ngx_lua_web_search_params_name_eq(&params->elts[i], name,
                                              name_len))
        {
            lua_pushlstring(L, params->elts[i].value.data == NULL
                            ? ""
                            : (const char *) params->elts[i].value.data,
                            params->elts[i].value.len);
            lua_rawseti(L, -2, n++);
        }
    }

    return 1;
}


static int
ngx_lua_web_search_params_has_method(lua_State *L)
{
    size_t                        i, name_len, value_len;
    const char                   *name, *value;
    ngx_lua_web_search_params_t  *params;

    if (lua_gettop(L) != 2 && lua_gettop(L) != 3) {
        return luaL_error(L,
                          "URLSearchParams:has() takes name and optional value");
    }

    params = luaL_checkudata(L, 1, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);
    name = luaL_checklstring(L, 2, &name_len);

    value = NULL;
    value_len = 0;

    if (lua_gettop(L) == 3) {
        value = luaL_checklstring(L, 3, &value_len);
    }

    for (i = 0; i < params->nelts; i++) {
        if (ngx_lua_web_search_params_name_eq(&params->elts[i], name,
                                              name_len)
            && (value == NULL
                || ngx_lua_web_search_params_value_eq(&params->elts[i],
                                                      value, value_len)))
        {
            lua_pushboolean(L, 1);
            return 1;
        }
    }

    lua_pushboolean(L, 0);
    return 1;
}


static int
ngx_lua_web_search_params_set_method(lua_State *L)
{
    size_t                        i, first, name_len, value_len;
    const char                   *name, *value;
    ngx_str_t                     new_value;
    ngx_lua_web_search_params_t  *params;

    if (lua_gettop(L) != 3) {
        return luaL_error(L, "URLSearchParams:set() takes name and value");
    }

    params = luaL_checkudata(L, 1, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);
    name = luaL_checklstring(L, 2, &name_len);
    value = luaL_checklstring(L, 3, &value_len);

    first = params->nelts;

    for (i = 0; i < params->nelts; i++) {
        if (ngx_lua_web_search_params_name_eq(&params->elts[i], name,
                                              name_len))
        {
            first = i;
            break;
        }
    }

    if (first == params->nelts) {
        if (ngx_lua_web_search_params_append(L, params, name, name_len,
                                             value, value_len)
            != NGX_OK)
        {
            return luaL_error(L, "no memory");
        }

        ngx_lua_web_search_params_sync_owner(L, params);
        return 0;
    }

    if (ngx_lua_web_search_params_dup(L, &new_value, value, value_len)
        != NGX_OK)
    {
        return luaL_error(L, "no memory");
    }

    ngx_lua_web_search_params_alloc(L, params->elts[first].value.data,
                                    params->elts[first].value.len, 0);
    params->elts[first].value = new_value;

    i = first + 1;
    while (i < params->nelts) {
        if (ngx_lua_web_search_params_name_eq(&params->elts[i], name,
                                              name_len))
        {
            ngx_lua_web_search_params_remove_at(L, params, i);
            continue;
        }

        i++;
    }

    ngx_lua_web_search_params_sync_owner(L, params);

    return 0;
}


static int
ngx_lua_web_search_params_sort_method(lua_State *L)
{
    size_t                        i, j;
    ngx_lua_web_search_param_t    value;
    ngx_lua_web_search_params_t  *params;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "URLSearchParams:sort() takes no arguments");
    }

    params = luaL_checkudata(L, 1, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);

    for (i = 1; i < params->nelts; i++) {
        value = params->elts[i];
        j = i;

        while (j > 0
               && ngx_lua_web_search_params_name_cmp(
                      &params->elts[j - 1].name, &value.name)
                  > 0)
        {
            params->elts[j] = params->elts[j - 1];
            j--;
        }

        params->elts[j] = value;
    }

    ngx_lua_web_search_params_sync_owner(L, params);

    return 0;
}


static int
ngx_lua_web_search_params_to_string_method(lua_State *L)
{
    ngx_str_t                     value;
    ngx_lua_web_search_params_t  *params;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "URLSearchParams:toString() takes no arguments");
    }

    params = luaL_checkudata(L, 1, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);

    if (ngx_lua_web_search_params_to_string(L, params, &value) != NGX_OK) {
        return luaL_error(L, "no memory");
    }

    lua_pushlstring(L, value.data == NULL ? "" : (const char *) value.data,
                    value.len);
    ngx_lua_web_search_params_free_string(L, &value);

    return 1;
}


static int
ngx_lua_web_search_params_index(lua_State *L)
{
    size_t       len;
    const char  *name;

    name = lua_tolstring(L, 2, &len);

    if (name != NULL && len == 4 && ngx_strncmp(name, "size", 4) == 0) {
        ngx_lua_web_search_params_t  *params;

        params = luaL_checkudata(L, 1, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);
        lua_pushinteger(L, params->nelts);
        return 1;
    }

    luaL_getmetatable(L, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);

    return 1;
}


static int
ngx_lua_web_search_params_gc(lua_State *L)
{
    size_t                        i;
    ngx_lua_web_search_params_t  *params;

    params = luaL_checkudata(L, 1, NGX_LUA_WEB_SEARCH_PARAMS_METATABLE);

    for (i = 0; i < params->nelts; i++) {
        ngx_lua_web_search_params_free_entry(L, &params->elts[i]);
    }

    ngx_lua_web_search_params_alloc(L, params->elts,
                                    params->cap
                                    * sizeof(ngx_lua_web_search_param_t),
                                    0);

    params->elts = NULL;
    params->nelts = 0;
    params->cap = 0;
    params->owner = NULL;

    return 0;
}


static int
ngx_lua_web_search_params_tostring(lua_State *L)
{
    return ngx_lua_web_search_params_to_string_method(L);
}


static void
ngx_lua_web_search_params_init(lua_State *L,
    ngx_lua_web_search_params_t *params, int init_index, int arg)
{
    size_t                         len;
    const char                    *value;
    ngx_lua_web_search_params_t   *source_params;

    if (lua_isnil(L, init_index)) {
        return;
    }

    source_params = ngx_lua_web_search_params_get(L, init_index);
    if (source_params != NULL) {
        ngx_lua_web_search_params_copy_from_params(L, params, source_params,
                                                   arg);
        return;
    }

    if (lua_type(L, init_index) == LUA_TSTRING) {
        value = lua_tolstring(L, init_index, &len);

        if (len > 0 && value[0] == '?') {
            value++;
            len--;
        }

        if (ngx_lua_web_search_params_init_query(L, params, value, len)
            != NGX_OK)
        {
            (void) luaL_error(L, "no memory");
        }

        return;
    }

    if (lua_istable(L, init_index)) {
        ngx_lua_web_search_params_copy_from_table(L, params, init_index, arg);
        return;
    }

    luaL_argerror(L, arg, "string, table, or URLSearchParams expected");
}


ngx_int_t
ngx_lua_web_search_params_init_query(lua_State *L,
    ngx_lua_web_search_params_t *params, const char *query, size_t len)
{
    size_t     name_len, value_len;
    const char  *p, *last, *end, *eq, *name, *value;
    ngx_str_t   decoded_name, decoded_value;

    p = query;
    last = query + len;

    while (p < last) {
        end = p;

        while (end < last && *end != '&') {
            end++;
        }

        if (end == p) {
            p = end + 1;
            continue;
        }

        eq = p;
        while (eq < end && *eq != '=') {
            eq++;
        }

        name = p;
        name_len = eq - p;

        if (eq < end) {
            value = eq + 1;
            value_len = end - value;

        } else {
            value = end;
            value_len = 0;
        }

        if (ngx_lua_web_search_params_decode(L, &decoded_name, name,
                                             name_len)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        if (ngx_lua_web_search_params_decode(L, &decoded_value, value,
                                             value_len)
            != NGX_OK)
        {
            ngx_lua_web_search_params_free_string(L, &decoded_name);
            return NGX_ERROR;
        }

        if (ngx_lua_web_search_params_append_owned(L, params, &decoded_name,
                                                   &decoded_value)
            != NGX_OK)
        {
            ngx_lua_web_search_params_free_string(L, &decoded_name);
            ngx_lua_web_search_params_free_string(L, &decoded_value);
            return NGX_ERROR;
        }

        p = end + 1;
    }

    return NGX_OK;
}


static void
ngx_lua_web_search_params_copy_from_params(lua_State *L,
    ngx_lua_web_search_params_t *params,
    ngx_lua_web_search_params_t *source, int arg)
{
    size_t  i;

    for (i = 0; i < source->nelts; i++) {
        if (ngx_lua_web_search_params_append(L, params,
                                             (const char *) source->elts[i].name.data,
                                             source->elts[i].name.len,
                                             (const char *) source->elts[i].value.data,
                                             source->elts[i].value.len)
            != NGX_OK)
        {
            (void) luaL_error(L, "no memory");
            return;
        }
    }

    (void) arg;
}


static void
ngx_lua_web_search_params_copy_from_table(lua_State *L,
    ngx_lua_web_search_params_t *params, int table, int arg)
{
    lua_Integer  i;

    table = lua_absindex(L, table);

    lua_rawgeti(L, table, 1);
    if (lua_istable(L, -1)) {
        lua_pop(L, 1);

        for (i = 1; ; i++) {
            lua_rawgeti(L, table, i);

            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                break;
            }

            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                luaL_argerror(L, arg,
                              "URLSearchParams sequence entries must be tables");
            }

            ngx_lua_web_search_params_copy_sequence_pair(L, params, -1, arg);
            lua_pop(L, 1);
        }

        return;
    }

    lua_pop(L, 1);

    lua_pushnil(L);
    while (lua_next(L, table) != 0) {
        ngx_lua_web_search_params_copy_table_pair(L, params, -2, -1, arg);
        lua_pop(L, 1);
    }
}


static void
ngx_lua_web_search_params_copy_table_pair(lua_State *L,
    ngx_lua_web_search_params_t *params, int key, int value, int arg)
{
    size_t       name_len, value_len;
    const char  *name, *param_value;

    if (lua_isinteger(L, key) && lua_istable(L, value)) {
        ngx_lua_web_search_params_copy_sequence_pair(L, params, value, arg);
        return;
    }

    if (lua_type(L, key) != LUA_TSTRING) {
        luaL_argerror(L, arg, "URLSearchParams names must be strings");
    }

    if (lua_type(L, value) != LUA_TSTRING) {
        luaL_argerror(L, arg, "URLSearchParams values must be strings");
    }

    name = lua_tolstring(L, key, &name_len);
    param_value = lua_tolstring(L, value, &value_len);

    if (ngx_lua_web_search_params_append(L, params, name, name_len,
                                         param_value, value_len)
        != NGX_OK)
    {
        (void) luaL_error(L, "no memory");
    }
}


static void
ngx_lua_web_search_params_copy_sequence_pair(lua_State *L,
    ngx_lua_web_search_params_t *params, int value, int arg)
{
    size_t       name_len, value_len;
    const char  *name, *param_value;

    value = lua_absindex(L, value);

    lua_rawgeti(L, value, 1);
    lua_rawgeti(L, value, 2);

    if (lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 2);
        luaL_argerror(L, arg,
                      "URLSearchParams sequence pairs must contain strings");
    }

    name = lua_tolstring(L, -2, &name_len);
    param_value = lua_tolstring(L, -1, &value_len);

    if (ngx_lua_web_search_params_append(L, params, name, name_len,
                                         param_value, value_len)
        != NGX_OK)
    {
        lua_pop(L, 2);
        (void) luaL_error(L, "no memory");
        return;
    }

    lua_pop(L, 2);
}


static ngx_int_t
ngx_lua_web_search_params_append(lua_State *L,
    ngx_lua_web_search_params_t *params, const char *name, size_t name_len,
    const char *value, size_t value_len)
{
    ngx_str_t  new_name, new_value;

    if (ngx_lua_web_search_params_dup(L, &new_name, name, name_len)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_lua_web_search_params_dup(L, &new_value, value, value_len)
        != NGX_OK)
    {
        ngx_lua_web_search_params_free_string(L, &new_name);
        return NGX_ERROR;
    }

    if (ngx_lua_web_search_params_append_owned(L, params, &new_name,
                                               &new_value)
        != NGX_OK)
    {
        ngx_lua_web_search_params_free_string(L, &new_name);
        ngx_lua_web_search_params_free_string(L, &new_value);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_search_params_append_owned(lua_State *L,
    ngx_lua_web_search_params_t *params, ngx_str_t *name,
    ngx_str_t *value)
{
    ngx_lua_web_search_param_t  *param;

    if (ngx_lua_web_search_params_reserve(L, params, params->nelts + 1)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    param = &params->elts[params->nelts++];
    param->name = *name;
    param->value = *value;

    name->data = NULL;
    name->len = 0;
    value->data = NULL;
    value->len = 0;

    return NGX_OK;
}


static void
ngx_lua_web_search_params_remove_at(lua_State *L,
    ngx_lua_web_search_params_t *params, size_t index)
{
    ngx_lua_web_search_params_free_entry(L, &params->elts[index]);

    if (index + 1 < params->nelts) {
        ngx_memmove(&params->elts[index], &params->elts[index + 1],
                    (params->nelts - index - 1)
                    * sizeof(ngx_lua_web_search_param_t));
    }

    params->nelts--;
}


static ngx_int_t
ngx_lua_web_search_params_reserve(lua_State *L,
    ngx_lua_web_search_params_t *params, size_t n)
{
    size_t                       cap;
    ngx_lua_web_search_param_t  *elts;

    if (n <= params->cap) {
        return NGX_OK;
    }

    cap = (params->cap == 0) ? 4 : params->cap * 2;
    while (cap < n) {
        cap *= 2;
    }

    elts = ngx_lua_web_search_params_alloc(L, params->elts,
                                           params->cap
                                           * sizeof(ngx_lua_web_search_param_t),
                                           cap
                                           * sizeof(ngx_lua_web_search_param_t));
    if (elts == NULL) {
        return NGX_ERROR;
    }

    params->elts = elts;
    params->cap = cap;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_search_params_dup(lua_State *L, ngx_str_t *dst,
    const char *src, size_t len)
{
    dst->len = len;
    dst->data = NULL;

    if (len == 0) {
        return NGX_OK;
    }

    dst->data = ngx_lua_web_search_params_alloc(L, NULL, 0, len);
    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(dst->data, src, len);

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_search_params_decode(lua_State *L, ngx_str_t *dst,
    const char *src, size_t len)
{
    size_t     i, n;
    ngx_int_t  hi, lo;

    dst->len = ngx_lua_web_search_params_decode_len(src, len);
    dst->data = NULL;

    if (dst->len == 0) {
        return NGX_OK;
    }

    dst->data = ngx_lua_web_search_params_alloc(L, NULL, 0, dst->len);
    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    n = 0;

    for (i = 0; i < len; i++) {
        if (src[i] == '+') {
            dst->data[n++] = ' ';
            continue;
        }

        if (src[i] == '%' && i + 2 < len) {
            hi = ngx_lua_web_search_params_hex((u_char) src[i + 1]);
            lo = ngx_lua_web_search_params_hex((u_char) src[i + 2]);

            if (hi >= 0 && lo >= 0) {
                dst->data[n++] = (u_char) ((hi << 4) | lo);
                i += 2;
                continue;
            }
        }

        dst->data[n++] = (u_char) src[i];
    }

    return NGX_OK;
}


static size_t
ngx_lua_web_search_params_decode_len(const char *src, size_t len)
{
    size_t     i, n;
    ngx_int_t  hi, lo;

    n = 0;

    for (i = 0; i < len; i++) {
        if (src[i] == '%' && i + 2 < len) {
            hi = ngx_lua_web_search_params_hex((u_char) src[i + 1]);
            lo = ngx_lua_web_search_params_hex((u_char) src[i + 2]);

            if (hi >= 0 && lo >= 0) {
                i += 2;
            }
        }

        n++;
    }

    return n;
}


ngx_int_t
ngx_lua_web_search_params_to_string(lua_State *L,
    ngx_lua_web_search_params_t *params, ngx_str_t *dst)
{
    size_t     i, len;
    u_char    *p;
    ngx_str_t  encoded_name, encoded_value;

    dst->len = 0;
    dst->data = NULL;

    for (i = 0; i < params->nelts; i++) {
        dst->len += ngx_lua_web_search_params_encode_len(
            &params->elts[i].name);
        dst->len += sizeof("=") - 1;
        dst->len += ngx_lua_web_search_params_encode_len(
            &params->elts[i].value);

        if (i > 0) {
            dst->len += sizeof("&") - 1;
        }
    }

    if (dst->len == 0) {
        return NGX_OK;
    }

    dst->data = ngx_lua_web_search_params_alloc(L, NULL, 0, dst->len);
    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    p = dst->data;

    for (i = 0; i < params->nelts; i++) {
        if (i > 0) {
            *p++ = '&';
        }

        if (ngx_lua_web_search_params_encode(L, &encoded_name,
                                             &params->elts[i].name)
            != NGX_OK)
        {
            ngx_lua_web_search_params_free_string(L, dst);
            return NGX_ERROR;
        }

        len = encoded_name.len;
        if (len > 0) {
            p = ngx_cpymem(p, encoded_name.data, len);
        }
        ngx_lua_web_search_params_free_string(L, &encoded_name);

        *p++ = '=';

        if (ngx_lua_web_search_params_encode(L, &encoded_value,
                                             &params->elts[i].value)
            != NGX_OK)
        {
            ngx_lua_web_search_params_free_string(L, dst);
            return NGX_ERROR;
        }

        len = encoded_value.len;
        if (len > 0) {
            p = ngx_cpymem(p, encoded_value.data, len);
        }
        ngx_lua_web_search_params_free_string(L, &encoded_value);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_search_params_encode(lua_State *L, ngx_str_t *dst,
    ngx_str_t *src)
{
    static u_char  hex[] = "0123456789ABCDEF";

    size_t  i;
    u_char  ch, *p;

    dst->len = ngx_lua_web_search_params_encode_len(src);
    dst->data = NULL;

    if (dst->len == 0) {
        return NGX_OK;
    }

    dst->data = ngx_lua_web_search_params_alloc(L, NULL, 0, dst->len);
    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    p = dst->data;

    for (i = 0; i < src->len; i++) {
        ch = src->data[i];

        if (ngx_lua_web_search_params_is_unescaped(ch)) {
            *p++ = ch;

        } else if (ch == ' ') {
            *p++ = '+';

        } else {
            *p++ = '%';
            *p++ = hex[ch >> 4];
            *p++ = hex[ch & 0xf];
        }
    }

    return NGX_OK;
}


static size_t
ngx_lua_web_search_params_encode_len(ngx_str_t *src)
{
    size_t  i, len;
    u_char  ch;

    len = 0;

    for (i = 0; i < src->len; i++) {
        ch = src->data[i];
        len += (ngx_lua_web_search_params_is_unescaped(ch) || ch == ' ')
               ? 1 : 3;
    }

    return len;
}


static ngx_uint_t
ngx_lua_web_search_params_is_unescaped(u_char ch)
{
    return (ch >= 'A' && ch <= 'Z')
           || (ch >= 'a' && ch <= 'z')
           || (ch >= '0' && ch <= '9')
           || ch == '*'
           || ch == '-'
           || ch == '.'
           || ch == '_';
}


static ngx_int_t
ngx_lua_web_search_params_hex(u_char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }

    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }

    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }

    return NGX_ERROR;
}


static ngx_uint_t
ngx_lua_web_search_params_name_eq(ngx_lua_web_search_param_t *param,
    const char *name, size_t len)
{
    return param->name.len == len
           && (len == 0 || ngx_memcmp(param->name.data, name, len) == 0);
}


static ngx_uint_t
ngx_lua_web_search_params_value_eq(ngx_lua_web_search_param_t *param,
    const char *value, size_t len)
{
    return param->value.len == len
           && (len == 0 || ngx_memcmp(param->value.data, value, len) == 0);
}


static ngx_int_t
ngx_lua_web_search_params_name_cmp(ngx_str_t *left, ngx_str_t *right)
{
    size_t     len;
    ngx_int_t  rc;

    len = ngx_min(left->len, right->len);

    if (len != 0) {
        rc = ngx_memcmp(left->data, right->data, len);
        if (rc != 0) {
            return rc;
        }
    }

    if (left->len == right->len) {
        return 0;
    }

    return left->len < right->len ? -1 : 1;
}


static void
ngx_lua_web_search_params_sync_owner(lua_State *L,
    ngx_lua_web_search_params_t *params)
{
    if (params->owner != NULL) {
        ngx_lua_web_url_sync_search_params(L, params->owner);
    }
}


void
ngx_lua_web_search_params_free_string(lua_State *L, ngx_str_t *value)
{
    ngx_lua_web_search_params_alloc(L, value->data, value->len, 0);
    value->data = NULL;
    value->len = 0;
}


static void
ngx_lua_web_search_params_free_entry(lua_State *L,
    ngx_lua_web_search_param_t *param)
{
    ngx_lua_web_search_params_free_string(L, &param->name);
    ngx_lua_web_search_params_free_string(L, &param->value);
}


static void *
ngx_lua_web_search_params_alloc(lua_State *L, void *ptr, size_t osize,
    size_t nsize)
{
    void       *ud;
    lua_Alloc   alloc;

    alloc = lua_getallocf(L, &ud);

    return alloc(ud, ptr, osize, nsize);
}
