/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua_web.h"

#include <lauxlib.h>


#define NGX_LUA_WEB_URL_METATABLE  "ngx_lua_web.URL"


struct ngx_lua_web_url_s {
    ngx_str_t                      href;
    ngx_str_t                      origin;
    ngx_str_t                      protocol;
    ngx_str_t                      username;
    ngx_str_t                      password;
    ngx_str_t                      host;
    ngx_str_t                      hostname;
    ngx_str_t                      port;
    ngx_str_t                      pathname;
    ngx_str_t                      search;
    ngx_str_t                      hash;
    ngx_lua_web_search_params_t   *search_params;
};


static ngx_lua_web_url_t *ngx_lua_web_url_create(lua_State *L);
static int ngx_lua_web_url_new(lua_State *L);
static int ngx_lua_web_url_to_string_method(lua_State *L);
static int ngx_lua_web_url_index(lua_State *L);
static int ngx_lua_web_url_gc(lua_State *L);
static int ngx_lua_web_url_tostring(lua_State *L);
static ngx_int_t ngx_lua_web_url_init(lua_State *L,
    ngx_lua_web_url_t *url, const char *input, size_t input_len,
    int base_index, int arg);
static ngx_int_t ngx_lua_web_url_init_search_params(lua_State *L,
    ngx_lua_web_url_t *url, int url_index);
static ngx_int_t ngx_lua_web_url_parse_absolute(lua_State *L,
    ngx_lua_web_url_t *url, const char *input, size_t input_len);
static ngx_int_t ngx_lua_web_url_parse_authority(lua_State *L,
    ngx_lua_web_url_t *url, const char *authority, size_t authority_len);
static ngx_int_t ngx_lua_web_url_parse_port(lua_State *L,
    ngx_lua_web_url_t *url, const char *port, size_t port_len);
static ngx_int_t ngx_lua_web_url_resolve(lua_State *L, ngx_str_t *dst,
    const char *input, size_t input_len, ngx_lua_web_url_t *base);
static ngx_int_t ngx_lua_web_url_resolve_path(lua_State *L, ngx_str_t *dst,
    ngx_lua_web_url_t *base, const char *input, size_t input_len);
static ngx_int_t ngx_lua_web_url_normalize_path(lua_State *L, ngx_str_t *dst,
    const char *path, size_t path_len);
static ngx_int_t ngx_lua_web_url_rebuild(lua_State *L,
    ngx_lua_web_url_t *url);
static ngx_int_t ngx_lua_web_url_rebuild_origin(lua_State *L,
    ngx_lua_web_url_t *url);
static ngx_int_t ngx_lua_web_url_rebuild_href(lua_State *L,
    ngx_lua_web_url_t *url);
static ngx_int_t ngx_lua_web_url_set_protocol(lua_State *L,
    ngx_lua_web_url_t *url, const char *scheme, size_t scheme_len);
static ngx_int_t ngx_lua_web_url_set_string(lua_State *L, ngx_str_t *field,
    const char *value, size_t len);
static ngx_int_t ngx_lua_web_url_set_lower_string(lua_State *L,
    ngx_str_t *field, const char *value, size_t len);
static ngx_int_t ngx_lua_web_url_set_search_from_query(lua_State *L,
    ngx_lua_web_url_t *url, const char *query, size_t query_len);
static ngx_int_t ngx_lua_web_url_set_pathname(lua_State *L,
    ngx_lua_web_url_t *url, const char *path, size_t path_len);
static ngx_int_t ngx_lua_web_url_set_empty(lua_State *L, ngx_str_t *field);
static ngx_uint_t ngx_lua_web_url_has_scheme(const char *value, size_t len);
static ngx_uint_t ngx_lua_web_url_default_port(ngx_str_t *protocol,
    const char *port, size_t port_len);
static ngx_uint_t ngx_lua_web_url_is_port(const char *port, size_t port_len);
static const char *ngx_lua_web_url_path_end(const char *p, const char *last);
static const char *ngx_lua_web_url_find_last(const char *p,
    const char *last, u_char ch);
static void ngx_lua_web_url_clear(lua_State *L, ngx_lua_web_url_t *url);
static void ngx_lua_web_url_free_string(lua_State *L, ngx_str_t *field);
static void *ngx_lua_web_url_alloc(lua_State *L, void *ptr, size_t osize,
    size_t nsize);


static const luaL_Reg  ngx_lua_web_url_global_methods[] = {
    { "new", ngx_lua_web_url_new },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_web_url_methods[] = {
    { "toString", ngx_lua_web_url_to_string_method },
    { NULL, NULL }
};


void
ngx_lua_web_url_register(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_URL_METATABLE)) {
        lua_pushliteral(L, "URL");
        lua_setfield(L, -2, "__name");
        luaL_setfuncs(L, ngx_lua_web_url_methods, 0);
        lua_pushcfunction(L, ngx_lua_web_url_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushcfunction(L, ngx_lua_web_url_tostring);
        lua_setfield(L, -2, "__tostring");
        lua_pushcfunction(L, ngx_lua_web_url_index);
        lua_setfield(L, -2, "__index");
    }

    lua_pop(L, 1);

    lua_newtable(L);
    luaL_setfuncs(L, ngx_lua_web_url_global_methods, 0);
    lua_setglobal(L, "URL");
}


static ngx_lua_web_url_t *
ngx_lua_web_url_create(lua_State *L)
{
    ngx_lua_web_url_t  *url;

    url = lua_newuserdatauv(L, sizeof(ngx_lua_web_url_t), 1);
    if (url == NULL) {
        return NULL;
    }

    url->href.data = NULL;
    url->href.len = 0;
    url->origin.data = NULL;
    url->origin.len = 0;
    url->protocol.data = NULL;
    url->protocol.len = 0;
    url->username.data = NULL;
    url->username.len = 0;
    url->password.data = NULL;
    url->password.len = 0;
    url->host.data = NULL;
    url->host.len = 0;
    url->hostname.data = NULL;
    url->hostname.len = 0;
    url->port.data = NULL;
    url->port.len = 0;
    url->pathname.data = NULL;
    url->pathname.len = 0;
    url->search.data = NULL;
    url->search.len = 0;
    url->hash.data = NULL;
    url->hash.len = 0;
    url->search_params = NULL;

    luaL_setmetatable(L, NGX_LUA_WEB_URL_METATABLE);

    return url;
}


static ngx_lua_web_url_t *
ngx_lua_web_url_get(lua_State *L, int index)
{
    return luaL_testudata(L, index, NGX_LUA_WEB_URL_METATABLE);
}


static int
ngx_lua_web_url_new(lua_State *L)
{
    int                 nargs, url_index;
    size_t              input_len;
    const char         *input;
    ngx_lua_web_url_t  *url;

    nargs = lua_gettop(L);

    if (nargs < 1 || nargs > 2) {
        return luaL_error(L, "URL.new() takes input and optional base");
    }

    input = luaL_checklstring(L, 1, &input_len);

    if (nargs == 2 && !lua_isnil(L, 2) && lua_type(L, 2) != LUA_TSTRING
        && ngx_lua_web_url_get(L, 2) == NULL)
    {
        return luaL_argerror(L, 2, "base must be a string or URL");
    }

    url = ngx_lua_web_url_create(L);
    if (url == NULL) {
        return luaL_error(L, "no memory");
    }

    url_index = lua_absindex(L, -1);

    if (ngx_lua_web_url_init(L, url, input, input_len,
                             nargs == 2 ? 2 : 0, 1)
        != NGX_OK)
    {
        return luaL_argerror(L, 1, "URL is invalid");
    }

    if (ngx_lua_web_url_init_search_params(L, url, url_index) != NGX_OK) {
        return luaL_error(L, "no memory");
    }

    return 1;
}


static int
ngx_lua_web_url_to_string_method(lua_State *L)
{
    ngx_lua_web_url_t  *url;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "URL:toString() takes no arguments");
    }

    url = luaL_checkudata(L, 1, NGX_LUA_WEB_URL_METATABLE);

    lua_pushlstring(L, (const char *) url->href.data, url->href.len);

    return 1;
}


static int
ngx_lua_web_url_index(lua_State *L)
{
    size_t              len;
    const char         *name;
    ngx_lua_web_url_t  *url;
    ngx_str_t          *value;

    url = luaL_checkudata(L, 1, NGX_LUA_WEB_URL_METATABLE);
    name = lua_tolstring(L, 2, &len);

    if (name == NULL) {
        lua_pushnil(L);
        return 1;
    }

    value = NULL;

    if (len == 4 && ngx_strncmp(name, "href", 4) == 0) {
        value = &url->href;

    } else if (len == 6 && ngx_strncmp(name, "origin", 6) == 0) {
        value = &url->origin;

    } else if (len == 8 && ngx_strncmp(name, "protocol", 8) == 0) {
        value = &url->protocol;

    } else if (len == 8 && ngx_strncmp(name, "username", 8) == 0) {
        value = &url->username;

    } else if (len == 8 && ngx_strncmp(name, "password", 8) == 0) {
        value = &url->password;

    } else if (len == 4 && ngx_strncmp(name, "host", 4) == 0) {
        value = &url->host;

    } else if (len == 8 && ngx_strncmp(name, "hostname", 8) == 0) {
        value = &url->hostname;

    } else if (len == 4 && ngx_strncmp(name, "port", 4) == 0) {
        value = &url->port;

    } else if (len == 8 && ngx_strncmp(name, "pathname", 8) == 0) {
        value = &url->pathname;

    } else if (len == 6 && ngx_strncmp(name, "search", 6) == 0) {
        value = &url->search;

    } else if (len == 4 && ngx_strncmp(name, "hash", 4) == 0) {
        value = &url->hash;

    } else if (len == 12 && ngx_strncmp(name, "searchParams", 12) == 0) {
        lua_getiuservalue(L, 1, 1);
        return 1;
    }

    if (value != NULL) {
        lua_pushlstring(L, value->data == NULL ? "" : (const char *) value->data,
                        value->len);
        return 1;
    }

    luaL_getmetatable(L, NGX_LUA_WEB_URL_METATABLE);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);

    return 1;
}


static int
ngx_lua_web_url_gc(lua_State *L)
{
    ngx_lua_web_url_t  *url;

    url = luaL_checkudata(L, 1, NGX_LUA_WEB_URL_METATABLE);

    ngx_lua_web_url_clear(L, url);

    return 0;
}


static int
ngx_lua_web_url_tostring(lua_State *L)
{
    return ngx_lua_web_url_to_string_method(L);
}


static ngx_int_t
ngx_lua_web_url_init(lua_State *L, ngx_lua_web_url_t *url,
    const char *input, size_t input_len, int base_index, int arg)
{
    size_t              base_len;
    const char         *base_string;
    ngx_str_t           resolved;
    ngx_lua_web_url_t  *base, parsed_base;

    if (ngx_lua_web_url_has_scheme(input, input_len)) {
        return ngx_lua_web_url_parse_absolute(L, url, input, input_len);
    }

    if (base_index == 0 || lua_isnil(L, base_index)) {
        return NGX_ERROR;
    }

    base = ngx_lua_web_url_get(L, base_index);
    if (base == NULL) {
        base_string = lua_tolstring(L, base_index, &base_len);

        ngx_memzero(&parsed_base, sizeof(ngx_lua_web_url_t));

        if (ngx_lua_web_url_parse_absolute(L, &parsed_base, base_string,
                                           base_len)
            != NGX_OK)
        {
            return luaL_argerror(L, 2, "base URL is invalid");
        }

        base = &parsed_base;

    } else {
        ngx_memzero(&parsed_base, sizeof(ngx_lua_web_url_t));
    }

    resolved.data = NULL;
    resolved.len = 0;

    if (ngx_lua_web_url_resolve(L, &resolved, input, input_len, base)
        != NGX_OK)
    {
        ngx_lua_web_url_clear(L, &parsed_base);
        return NGX_ERROR;
    }

    if (ngx_lua_web_url_parse_absolute(L, url, (const char *) resolved.data,
                                       resolved.len)
        != NGX_OK)
    {
        ngx_lua_web_url_free_string(L, &resolved);
        ngx_lua_web_url_clear(L, &parsed_base);
        return NGX_ERROR;
    }

    ngx_lua_web_url_free_string(L, &resolved);
    ngx_lua_web_url_clear(L, &parsed_base);

    (void) arg;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_init_search_params(lua_State *L, ngx_lua_web_url_t *url,
    int url_index)
{
    ngx_lua_web_search_params_t  *params;

    params = ngx_lua_web_search_params_create(L);
    if (params == NULL) {
        return NGX_ERROR;
    }

    if (url->search.len > 1
        && ngx_lua_web_search_params_init_query(L, params,
                                                (const char *) url->search.data + 1,
                                                url->search.len - 1)
           != NGX_OK)
    {
        return NGX_ERROR;
    }

    url->search_params = params;
    ngx_lua_web_search_params_set_owner(L, params, -1, url, url_index);
    lua_setiuservalue(L, url_index, 1);

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_parse_absolute(lua_State *L, ngx_lua_web_url_t *url,
    const char *input, size_t input_len)
{
    size_t       scheme_len, query_len, path_len, hash_len;
    const char  *p, *last, *scheme_end, *authority, *authority_end;
    const char  *path, *query, *hash, *query_end;

    p = input;
    last = input + input_len;
    scheme_end = NULL;

    while (p < last) {
        if (*p == ':') {
            scheme_end = p;
            break;
        }

        p++;
    }

    if (scheme_end == NULL || scheme_end + 2 >= last
        || scheme_end[1] != '/' || scheme_end[2] != '/')
    {
        return NGX_ERROR;
    }

    scheme_len = scheme_end - input;

    if (!((scheme_len == sizeof("http") - 1
           && ngx_strncasecmp((u_char *) input, (u_char *) "http",
                              sizeof("http") - 1)
              == 0)
          || (scheme_len == sizeof("https") - 1
              && ngx_strncasecmp((u_char *) input, (u_char *) "https",
                                 sizeof("https") - 1)
                 == 0)))
    {
        return NGX_ERROR;
    }

    authority = scheme_end + sizeof("://") - 1;
    authority_end = ngx_lua_web_url_path_end(authority, last);

    if (authority == authority_end) {
        return NGX_ERROR;
    }

    ngx_lua_web_url_clear(L, url);

    if (ngx_lua_web_url_set_protocol(L, url, input, scheme_len)
        != NGX_OK)
    {
        return luaL_error(L, "no memory");
    }

    if (ngx_lua_web_url_parse_authority(L, url, authority,
                                        authority_end - authority)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    path = authority_end;
    hash = (const char *) ngx_strlchr((u_char *) path, (u_char *) last, '#');
    query_end = hash == NULL ? last : hash;
    query = (const char *) ngx_strlchr((u_char *) path,
                                       (u_char *) query_end, '?');

    if (query != NULL) {
        path_len = query - path;
        query_len = query_end - query - 1;

    } else {
        path_len = query_end - path;
        query_len = 0;
    }

    hash_len = hash == NULL ? 0 : last - hash - 1;

    if (ngx_lua_web_url_set_pathname(L, url, path, path_len) != NGX_OK) {
        return luaL_error(L, "no memory");
    }

    if (query != NULL) {
        if (ngx_lua_web_url_set_search_from_query(L, url, query + 1,
                                                  query_len)
            != NGX_OK)
        {
            return luaL_error(L, "no memory");
        }

    } else if (ngx_lua_web_url_set_empty(L, &url->search) != NGX_OK) {
        return luaL_error(L, "no memory");
    }

    if (hash != NULL && hash_len > 0) {
        if (ngx_lua_web_url_set_string(L, &url->hash, hash,
                                       hash_len + 1)
            != NGX_OK)
        {
            return luaL_error(L, "no memory");
        }

    } else if (ngx_lua_web_url_set_empty(L, &url->hash) != NGX_OK) {
        return luaL_error(L, "no memory");
    }

    if (ngx_lua_web_url_rebuild(L, url) != NGX_OK) {
        return luaL_error(L, "no memory");
    }

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_parse_authority(lua_State *L, ngx_lua_web_url_t *url,
    const char *authority, size_t authority_len)
{
    size_t       hostname_len, userinfo_len, host_len;
    const char  *last, *userinfo_end, *host_start, *host_end, *port;
    const char  *colon, *bracket, *p;
    ngx_str_t    normalized_port;

    last = authority + authority_len;
    userinfo_end = ngx_lua_web_url_find_last(authority, last, '@');
    host_start = authority;

    if (userinfo_end != NULL) {
        userinfo_len = userinfo_end - authority;
        colon = (const char *) ngx_strlchr((u_char *) authority,
                                           (u_char *) userinfo_end, ':');

        if (colon == NULL) {
            if (ngx_lua_web_url_set_string(L, &url->username, authority,
                                           userinfo_len)
                != NGX_OK)
            {
                return luaL_error(L, "no memory");
            }

        } else {
            if (ngx_lua_web_url_set_string(L, &url->username, authority,
                                           colon - authority)
                != NGX_OK)
            {
                return luaL_error(L, "no memory");
            }

            if (ngx_lua_web_url_set_string(L, &url->password, colon + 1,
                                           userinfo_end - colon - 1)
                != NGX_OK)
            {
                return luaL_error(L, "no memory");
            }
        }

        host_start = userinfo_end + 1;
    }

    if (host_start == last) {
        return NGX_ERROR;
    }

    port = NULL;

    if (*host_start == '[') {
        bracket = (const char *) ngx_strlchr((u_char *) host_start,
                                             (u_char *) last, ']');
        if (bracket == NULL) {
            return NGX_ERROR;
        }

        host_end = bracket + 1;

        if (host_end < last) {
            if (*host_end != ':') {
                return NGX_ERROR;
            }

            port = host_end + 1;
        }

    } else {
        colon = NULL;

        for (p = host_start; p < last; p++) {
            if (*p == ':') {
                if (colon != NULL) {
                    return NGX_ERROR;
                }

                colon = p;
            }
        }

        if (colon != NULL) {
            host_end = colon;
            port = colon + 1;

        } else {
            host_end = last;
        }
    }

    hostname_len = host_end - host_start;
    if (hostname_len == 0) {
        return NGX_ERROR;
    }

    if (ngx_lua_web_url_set_lower_string(L, &url->hostname, host_start,
                                         hostname_len)
        != NGX_OK)
    {
        return luaL_error(L, "no memory");
    }

    if (port != NULL) {
        if (ngx_lua_web_url_parse_port(L, url, port, last - port)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

    } else if (ngx_lua_web_url_set_empty(L, &url->port) != NGX_OK) {
        return luaL_error(L, "no memory");
    }

    host_len = url->hostname.len
               + (url->port.len == 0 ? 0 : sizeof(":") - 1
                                            + url->port.len);

    if (host_len == 0) {
        return NGX_ERROR;
    }

    normalized_port.data = NULL;
    normalized_port.len = 0;

    if (url->port.len != 0) {
        normalized_port = url->port;
    }

    ngx_lua_web_url_free_string(L, &url->host);
    url->host.data = ngx_lua_web_url_alloc(L, NULL, 0, host_len);
    if (url->host.data == NULL) {
        return luaL_error(L, "no memory");
    }

    p = (const char *) ngx_cpymem(url->host.data, url->hostname.data,
                                  url->hostname.len);

    if (normalized_port.len != 0) {
        *((u_char *) p) = ':';
        p++;
        p = (const char *) ngx_cpymem((u_char *) p, normalized_port.data,
                                      normalized_port.len);
    }

    url->host.len = p - (const char *) url->host.data;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_parse_port(lua_State *L, ngx_lua_web_url_t *url,
    const char *port, size_t port_len)
{
    if (port_len == 0) {
        return ngx_lua_web_url_set_empty(L, &url->port);
    }

    if (!ngx_lua_web_url_is_port(port, port_len)) {
        return NGX_ERROR;
    }

    if (ngx_lua_web_url_default_port(&url->protocol, port, port_len)) {
        return ngx_lua_web_url_set_empty(L, &url->port);
    }

    return ngx_lua_web_url_set_string(L, &url->port, port, port_len);
}


static ngx_int_t
ngx_lua_web_url_resolve(lua_State *L, ngx_str_t *dst, const char *input,
    size_t input_len, ngx_lua_web_url_t *base)
{
    size_t     len;
    u_char    *p;
    ngx_str_t  path;

    if (input_len == 0) {
        len = base->origin.len + base->pathname.len + base->search.len;
        dst->data = ngx_lua_web_url_alloc(L, NULL, 0, len);
        if (dst->data == NULL) {
            return NGX_ERROR;
        }

        p = ngx_cpymem(dst->data, base->origin.data, base->origin.len);
        p = ngx_cpymem(p, base->pathname.data, base->pathname.len);
        p = ngx_cpymem(p, base->search.data, base->search.len);
        dst->len = p - dst->data;

        return NGX_OK;
    }

    if (input_len >= 2 && input[0] == '/' && input[1] == '/') {
        len = base->protocol.len + input_len;
        dst->data = ngx_lua_web_url_alloc(L, NULL, 0, len);
        if (dst->data == NULL) {
            return NGX_ERROR;
        }

        p = ngx_cpymem(dst->data, base->protocol.data, base->protocol.len);
        p = ngx_cpymem(p, input, input_len);
        dst->len = p - dst->data;

        return NGX_OK;
    }

    if (input_len > 0 && input[0] == '#') {
        len = base->origin.len + base->pathname.len + base->search.len
              + input_len;
        dst->data = ngx_lua_web_url_alloc(L, NULL, 0, len);
        if (dst->data == NULL) {
            return NGX_ERROR;
        }

        p = ngx_cpymem(dst->data, base->origin.data, base->origin.len);
        p = ngx_cpymem(p, base->pathname.data, base->pathname.len);
        p = ngx_cpymem(p, base->search.data, base->search.len);
        p = ngx_cpymem(p, input, input_len);
        dst->len = p - dst->data;

        return NGX_OK;
    }

    if (input_len > 0 && input[0] == '?') {
        len = base->origin.len + base->pathname.len + input_len;
        dst->data = ngx_lua_web_url_alloc(L, NULL, 0, len);
        if (dst->data == NULL) {
            return NGX_ERROR;
        }

        p = ngx_cpymem(dst->data, base->origin.data, base->origin.len);
        p = ngx_cpymem(p, base->pathname.data, base->pathname.len);
        p = ngx_cpymem(p, input, input_len);
        dst->len = p - dst->data;

        return NGX_OK;
    }

    path.data = NULL;
    path.len = 0;

    if (ngx_lua_web_url_resolve_path(L, &path, base, input, input_len)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    len = base->origin.len + path.len;
    dst->data = ngx_lua_web_url_alloc(L, NULL, 0, len);
    if (dst->data == NULL) {
        ngx_lua_web_url_free_string(L, &path);
        return NGX_ERROR;
    }

    p = ngx_cpymem(dst->data, base->origin.data, base->origin.len);
    p = ngx_cpymem(p, path.data, path.len);
    dst->len = p - dst->data;

    ngx_lua_web_url_free_string(L, &path);

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_resolve_path(lua_State *L, ngx_str_t *dst,
    ngx_lua_web_url_t *base, const char *input, size_t input_len)
{
    size_t       dir_len, temp_len, normalized_len, suffix_len;
    u_char     *path, *p;
    const char *input_path_end, *slash;

    input_path_end = ngx_lua_web_url_path_end(input, input + input_len);
    suffix_len = input_len - (input_path_end - input);

    if (input_len > 0 && input[0] == '/') {
        if (ngx_lua_web_url_normalize_path(L, dst, input,
                                           input_path_end - input)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        goto append_suffix;
    }

    slash = ngx_lua_web_url_find_last((const char *) base->pathname.data,
                                      (const char *) base->pathname.data
                                      + base->pathname.len,
                                      '/');

    if (slash == NULL) {
        dir_len = sizeof("/") - 1;

    } else {
        dir_len = slash - (const char *) base->pathname.data + 1;
    }

    temp_len = dir_len + (input_path_end - input);
    path = ngx_lua_web_url_alloc(L, NULL, 0, temp_len);
    if (path == NULL) {
        return NGX_ERROR;
    }

    if (dir_len == sizeof("/") - 1 && slash == NULL) {
        p = ngx_cpymem(path, "/", sizeof("/") - 1);

    } else {
        p = ngx_cpymem(path, base->pathname.data, dir_len);
    }

    p = ngx_cpymem(p, input, input_path_end - input);

    if (ngx_lua_web_url_normalize_path(L, dst, (const char *) path,
                                       p - path)
        != NGX_OK)
    {
        ngx_lua_web_url_alloc(L, path, temp_len, 0);
        return NGX_ERROR;
    }

    ngx_lua_web_url_alloc(L, path, temp_len, 0);

append_suffix:

    if (suffix_len != 0) {
        normalized_len = dst->len + suffix_len;
        p = ngx_lua_web_url_alloc(L, dst->data, dst->len, normalized_len);
        if (p == NULL) {
            ngx_lua_web_url_free_string(L, dst);
            return NGX_ERROR;
        }

        dst->data = p;
        ngx_memcpy(dst->data + dst->len, input_path_end, suffix_len);
        dst->len = normalized_len;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_normalize_path(lua_State *L, ngx_str_t *dst,
    const char *path, size_t path_len)
{
    size_t       seg_len, out_len;
    u_char      *out;
    const char  *p, *last, *seg, *prev;

    out = ngx_lua_web_url_alloc(L, NULL, 0, path_len + 1);
    if (out == NULL) {
        return NGX_ERROR;
    }

    out_len = 0;
    p = path;
    last = path + path_len;

    if (path_len == 0 || *p != '/') {
        out[out_len++] = '/';
    }

    while (p < last) {
        while (p < last && *p == '/') {
            p++;
        }

        seg = p;

        while (p < last && *p != '/') {
            p++;
        }

        seg_len = p - seg;

        if (seg_len == 0 || (seg_len == 1 && seg[0] == '.')) {
            continue;
        }

        if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
            if (out_len > 1) {
                prev = (const char *) out + out_len - 1;
                if (*prev == '/') {
                    prev--;
                }

                while (prev > (const char *) out && *prev != '/') {
                    prev--;
                }

                out_len = prev - (const char *) out + 1;
            }

            continue;
        }

        if (out_len == 0 || out[out_len - 1] != '/') {
            out[out_len++] = '/';
        }

        ngx_memcpy(out + out_len, seg, seg_len);
        out_len += seg_len;
    }

    if (out_len == 0) {
        out[out_len++] = '/';
    }

    if (path_len > 0 && path[path_len - 1] == '/'
        && out[out_len - 1] != '/')
    {
        out[out_len++] = '/';
    }

    dst->data = out;
    dst->len = out_len;

    return NGX_OK;
}


void
ngx_lua_web_url_sync_search_params(lua_State *L, ngx_lua_web_url_t *url)
{
    ngx_str_t  value;

    if (url->search_params == NULL) {
        return;
    }

    if (ngx_lua_web_search_params_to_string(L, url->search_params, &value)
        != NGX_OK)
    {
        (void) luaL_error(L, "no memory");
        return;
    }

    if (value.len == 0) {
        if (ngx_lua_web_url_set_empty(L, &url->search) != NGX_OK) {
            ngx_lua_web_search_params_free_string(L, &value);
            (void) luaL_error(L, "no memory");
            return;
        }

    } else if (ngx_lua_web_url_set_search_from_query(L, url,
                                                     (const char *) value.data,
                                                     value.len)
               != NGX_OK)
    {
        ngx_lua_web_search_params_free_string(L, &value);
        (void) luaL_error(L, "no memory");
        return;
    }

    ngx_lua_web_search_params_free_string(L, &value);

    if (ngx_lua_web_url_rebuild_href(L, url) != NGX_OK) {
        (void) luaL_error(L, "no memory");
    }
}


static ngx_int_t
ngx_lua_web_url_rebuild(lua_State *L, ngx_lua_web_url_t *url)
{
    if (ngx_lua_web_url_rebuild_origin(L, url) != NGX_OK) {
        return NGX_ERROR;
    }

    return ngx_lua_web_url_rebuild_href(L, url);
}


static ngx_int_t
ngx_lua_web_url_rebuild_origin(lua_State *L, ngx_lua_web_url_t *url)
{
    size_t  len;
    u_char  *p;

    len = url->protocol.len + sizeof("//") - 1 + url->host.len;

    ngx_lua_web_url_free_string(L, &url->origin);

    url->origin.data = ngx_lua_web_url_alloc(L, NULL, 0, len);
    if (url->origin.data == NULL) {
        return NGX_ERROR;
    }

    p = ngx_cpymem(url->origin.data, url->protocol.data, url->protocol.len);
    p = ngx_cpymem(p, "//", sizeof("//") - 1);
    p = ngx_cpymem(p, url->host.data, url->host.len);
    url->origin.len = p - url->origin.data;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_rebuild_href(lua_State *L, ngx_lua_web_url_t *url)
{
    size_t  len;
    u_char  *p;

    len = url->protocol.len + sizeof("//") - 1
          + url->username.len
          + (url->password.len == 0 ? 0 : sizeof(":") - 1
                                           + url->password.len)
          + (url->username.len == 0 && url->password.len == 0
             ? 0 : sizeof("@") - 1)
          + url->host.len + url->pathname.len + url->search.len
          + url->hash.len;

    ngx_lua_web_url_free_string(L, &url->href);

    url->href.data = ngx_lua_web_url_alloc(L, NULL, 0, len);
    if (url->href.data == NULL) {
        return NGX_ERROR;
    }

    p = ngx_cpymem(url->href.data, url->protocol.data, url->protocol.len);
    p = ngx_cpymem(p, "//", sizeof("//") - 1);

    if (url->username.len != 0 || url->password.len != 0) {
        p = ngx_cpymem(p, url->username.data, url->username.len);

        if (url->password.len != 0) {
            *p++ = ':';
            p = ngx_cpymem(p, url->password.data, url->password.len);
        }

        *p++ = '@';
    }

    p = ngx_cpymem(p, url->host.data, url->host.len);
    p = ngx_cpymem(p, url->pathname.data, url->pathname.len);
    p = ngx_cpymem(p, url->search.data, url->search.len);
    p = ngx_cpymem(p, url->hash.data, url->hash.len);
    url->href.len = p - url->href.data;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_set_protocol(lua_State *L, ngx_lua_web_url_t *url,
    const char *scheme, size_t scheme_len)
{
    size_t     i;
    ngx_str_t  protocol;

    protocol.len = scheme_len + sizeof(":") - 1;
    protocol.data = ngx_lua_web_url_alloc(L, NULL, 0, protocol.len);
    if (protocol.data == NULL) {
        return NGX_ERROR;
    }

    for (i = 0; i < scheme_len; i++) {
        if (scheme[i] >= 'A' && scheme[i] <= 'Z') {
            protocol.data[i] = (u_char) (scheme[i] + ('a' - 'A'));

        } else {
            protocol.data[i] = (u_char) scheme[i];
        }
    }

    protocol.data[scheme_len] = ':';

    ngx_lua_web_url_free_string(L, &url->protocol);
    url->protocol = protocol;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_set_string(lua_State *L, ngx_str_t *field,
    const char *value, size_t len)
{
    ngx_str_t  copy;

    copy.data = NULL;
    copy.len = len;

    if (len != 0) {
        copy.data = ngx_lua_web_url_alloc(L, NULL, 0, len);
        if (copy.data == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(copy.data, value, len);
    }

    ngx_lua_web_url_free_string(L, field);
    *field = copy;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_set_lower_string(lua_State *L, ngx_str_t *field,
    const char *value, size_t len)
{
    size_t     i;
    ngx_str_t  copy;

    copy.data = NULL;
    copy.len = len;

    if (len != 0) {
        copy.data = ngx_lua_web_url_alloc(L, NULL, 0, len);
        if (copy.data == NULL) {
            return NGX_ERROR;
        }

        for (i = 0; i < len; i++) {
            if (value[i] >= 'A' && value[i] <= 'Z') {
                copy.data[i] = (u_char) (value[i] + ('a' - 'A'));

            } else {
                copy.data[i] = (u_char) value[i];
            }
        }
    }

    ngx_lua_web_url_free_string(L, field);
    *field = copy;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_set_search_from_query(lua_State *L, ngx_lua_web_url_t *url,
    const char *query, size_t query_len)
{
    ngx_str_t  search;

    search.data = NULL;
    search.len = 0;

    if (query_len == 0) {
        return ngx_lua_web_url_set_empty(L, &url->search);
    }

    search.len = query_len + sizeof("?") - 1;
    search.data = ngx_lua_web_url_alloc(L, NULL, 0, search.len);
    if (search.data == NULL) {
        return NGX_ERROR;
    }

    search.data[0] = '?';
    ngx_memcpy(search.data + 1, query, query_len);

    ngx_lua_web_url_free_string(L, &url->search);
    url->search = search;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_set_pathname(lua_State *L, ngx_lua_web_url_t *url,
    const char *path, size_t path_len)
{
    ngx_str_t  pathname;

    if (path_len == 0) {
        return ngx_lua_web_url_set_string(L, &url->pathname, "/",
                                          sizeof("/") - 1);
    }

    if (path[0] != '/') {
        return NGX_ERROR;
    }

    pathname.data = NULL;
    pathname.len = 0;

    if (ngx_lua_web_url_normalize_path(L, &pathname, path, path_len)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    ngx_lua_web_url_free_string(L, &url->pathname);
    url->pathname = pathname;

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_url_set_empty(lua_State *L, ngx_str_t *field)
{
    return ngx_lua_web_url_set_string(L, field, "", 0);
}


static ngx_uint_t
ngx_lua_web_url_has_scheme(const char *value, size_t len)
{
    size_t  i;

    if (len == 0 || !((value[0] >= 'A' && value[0] <= 'Z')
                      || (value[0] >= 'a' && value[0] <= 'z')))
    {
        return 0;
    }

    for (i = 1; i < len; i++) {
        if (value[i] == ':') {
            return 1;
        }

        if (!((value[i] >= 'A' && value[i] <= 'Z')
              || (value[i] >= 'a' && value[i] <= 'z')
              || (value[i] >= '0' && value[i] <= '9')
              || value[i] == '+'
              || value[i] == '-'
              || value[i] == '.'))
        {
            return 0;
        }
    }

    return 0;
}


static ngx_uint_t
ngx_lua_web_url_default_port(ngx_str_t *protocol, const char *port,
    size_t port_len)
{
    if (protocol->len == sizeof("http:") - 1
        && ngx_strncmp(protocol->data, "http:", sizeof("http:") - 1) == 0)
    {
        return port_len == 2 && ngx_strncmp(port, "80", 2) == 0;
    }

    if (protocol->len == sizeof("https:") - 1
        && ngx_strncmp(protocol->data, "https:", sizeof("https:") - 1) == 0)
    {
        return port_len == 3 && ngx_strncmp(port, "443", 3) == 0;
    }

    return 0;
}


static ngx_uint_t
ngx_lua_web_url_is_port(const char *port, size_t port_len)
{
    size_t  i;

    for (i = 0; i < port_len; i++) {
        if (port[i] < '0' || port[i] > '9') {
            return 0;
        }
    }

    return 1;
}


static const char *
ngx_lua_web_url_path_end(const char *p, const char *last)
{
    while (p < last) {
        if (*p == '/' || *p == '?' || *p == '#') {
            return p;
        }

        p++;
    }

    return last;
}


static const char *
ngx_lua_web_url_find_last(const char *p, const char *last, u_char ch)
{
    const char  *found;

    found = NULL;

    while (p < last) {
        if (*p == ch) {
            found = p;
        }

        p++;
    }

    return found;
}


static void
ngx_lua_web_url_clear(lua_State *L, ngx_lua_web_url_t *url)
{
    ngx_lua_web_url_free_string(L, &url->href);
    ngx_lua_web_url_free_string(L, &url->origin);
    ngx_lua_web_url_free_string(L, &url->protocol);
    ngx_lua_web_url_free_string(L, &url->username);
    ngx_lua_web_url_free_string(L, &url->password);
    ngx_lua_web_url_free_string(L, &url->host);
    ngx_lua_web_url_free_string(L, &url->hostname);
    ngx_lua_web_url_free_string(L, &url->port);
    ngx_lua_web_url_free_string(L, &url->pathname);
    ngx_lua_web_url_free_string(L, &url->search);
    ngx_lua_web_url_free_string(L, &url->hash);
    url->search_params = NULL;
}


static void
ngx_lua_web_url_free_string(lua_State *L, ngx_str_t *field)
{
    ngx_lua_web_url_alloc(L, field->data, field->len, 0);

    field->data = NULL;
    field->len = 0;
}


static void *
ngx_lua_web_url_alloc(lua_State *L, void *ptr, size_t osize, size_t nsize)
{
    void       *ud;
    lua_Alloc   alloc;

    alloc = lua_getallocf(L, &ud);

    return alloc(ud, ptr, osize, nsize);
}
