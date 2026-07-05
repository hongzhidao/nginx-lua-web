/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_http_lua.h"
#include "ngx_lua.h"

#include <ngx_event_connect.h>

#include <lauxlib.h>
#include <stdint.h>


#define NGX_HTTP_LUA_FETCH_CONNECT_TIMEOUT  5000
#define NGX_HTTP_LUA_FETCH_SEND_TIMEOUT     5000
#define NGX_HTTP_LUA_FETCH_READ_TIMEOUT     5000
#define NGX_HTTP_LUA_FETCH_RESPONSE_BUFFER  4096
#define NGX_HTTP_LUA_FETCH_RESET_CONTENT    205


typedef struct ngx_http_lua_fetch_s  ngx_http_lua_fetch_t;


struct ngx_http_lua_fetch_s {
    ngx_lua_ctx_t              *ctx;
    ngx_http_request_t         *r;
    ngx_lua_web_request_t      *request;
    ngx_lua_web_response_t     *response;

    ngx_pool_t                 *pool;
    ngx_peer_connection_t       peer;
    ngx_resolver_ctx_t         *resolver;
    ngx_sockaddr_t              sockaddr;
    ngx_str_t                   scheme;
    ngx_str_t                   host;
    ngx_str_t                   authority;
    ngx_str_t                   uri;
    ngx_str_t                   target;
    in_port_t                   port;
    ngx_str_t                   peer_name;
    ngx_buf_t                  *write_buf;
    ngx_buf_t                  *read_buf;
    ngx_str_t                   error;

    ngx_lua_web_stream_t       *response_body;
    ngx_http_chunked_t          chunked;
    off_t                       content_length_n;
    off_t                       body_read;
    unsigned                    chunked_body:1;

    unsigned                    request_sent:1;
    unsigned                    request_body_sent:1;
    unsigned                    header_only:1;
    unsigned                    keepalive:1;
    unsigned                    ssl:1;
    unsigned                    has_target:1;
    unsigned                    failed:1;
};


static int ngx_http_lua_fetch(lua_State *L);
static int ngx_http_lua_fetch_continue(lua_State *L, int status,
    lua_KContext ctx);
static void ngx_http_lua_fetch_resume(ngx_http_lua_fetch_t *fetch);
static int ngx_http_lua_fetch_push_result(lua_State *L,
    ngx_http_lua_fetch_t *fetch);

static ngx_int_t ngx_http_lua_fetch_parse_args(lua_State *L,
    ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_normalize_request(lua_State *L,
    ngx_http_lua_fetch_t *fetch, int nargs);
static ngx_int_t ngx_http_lua_fetch_parse_options(lua_State *L,
    ngx_http_lua_fetch_t *fetch, int index, int arg);
static ngx_http_lua_fetch_t *ngx_http_lua_fetch_create(lua_State *L);
static ngx_int_t ngx_http_lua_fetch_parse_uri(ngx_http_lua_fetch_t *fetch,
    ngx_str_t *url);
static ngx_int_t ngx_http_lua_fetch_parse_origin(
    ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_resolve(ngx_http_lua_fetch_t *fetch);
static void ngx_http_lua_fetch_resolve_handler(ngx_resolver_ctx_t *ctx);
static ngx_int_t ngx_http_lua_fetch_set_peer(ngx_http_lua_fetch_t *fetch,
    struct sockaddr *sockaddr, socklen_t socklen);

static ngx_int_t ngx_http_lua_fetch_create_request(
    ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_connect(ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_test_connect(ngx_connection_t *c);
static void ngx_http_lua_fetch_send_request_handler(ngx_event_t *ev);
static ngx_int_t ngx_http_lua_fetch_send_request(ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_send_buffer(ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_next_body_buffer(
    ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_create_body_chunk(
    ngx_http_lua_fetch_t *fetch, ngx_str_t *chunk);
static ngx_int_t ngx_http_lua_fetch_create_last_body_chunk(
    ngx_http_lua_fetch_t *fetch);
static void ngx_http_lua_fetch_request_body_wake(void *data);

static void ngx_http_lua_fetch_process_header_handler(ngx_event_t *ev);
static ngx_int_t ngx_http_lua_fetch_process_header(
    ngx_http_lua_fetch_t *fetch);
static u_char *ngx_http_lua_fetch_header_end(ngx_buf_t *b);
static ngx_int_t ngx_http_lua_fetch_parse_status(ngx_http_lua_fetch_t *fetch,
    ngx_uint_t *status);
static ngx_int_t ngx_http_lua_fetch_create_response(
    ngx_http_lua_fetch_t *fetch, u_char *header_end, ngx_uint_t status);
static ngx_int_t ngx_http_lua_fetch_init_response(lua_State *L,
    ngx_http_lua_fetch_t *fetch, ngx_lua_web_response_t *response,
    u_char *header_end, ngx_uint_t status);
static ngx_int_t ngx_http_lua_fetch_parse_response_headers(lua_State *L,
    ngx_http_lua_fetch_t *fetch, ngx_lua_web_response_t *response,
    u_char *header_end, ngx_uint_t status);
static ngx_uint_t ngx_http_lua_fetch_header_is(u_char *name, size_t len,
    const char *value, size_t value_len);
static ngx_uint_t ngx_http_lua_fetch_header_value_has_token(u_char *value,
    u_char *end, const char *token, size_t token_len);

static ngx_int_t ngx_http_lua_fetch_init_body(lua_State *L,
    ngx_http_lua_fetch_t *fetch, int response_index,
    ngx_lua_web_response_t *response);
static void ngx_http_lua_fetch_wait_body_pull(ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_body_source_pull(
    ngx_lua_web_stream_t *stream, ngx_lua_web_stream_source_t *source);
static ngx_int_t ngx_http_lua_fetch_read_body(
    ngx_http_lua_fetch_t *fetch, ngx_lua_web_stream_t *stream);
static ngx_int_t ngx_http_lua_fetch_read_plain_body(
    ngx_http_lua_fetch_t *fetch, ngx_lua_web_stream_t *stream);
static ngx_int_t ngx_http_lua_fetch_read_chunked_body(
    ngx_http_lua_fetch_t *fetch, ngx_lua_web_stream_t *stream);
static ngx_int_t ngx_http_lua_fetch_recv_body(
    ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_enqueue_body_buffer(
    ngx_http_lua_fetch_t *fetch, ngx_lua_web_stream_t *stream,
    u_char *data, size_t len);
static void ngx_http_lua_fetch_process_body_handler(ngx_event_t *ev);

static void ngx_http_lua_fetch_fail(ngx_http_lua_fetch_t *fetch,
    const char *message);
static void ngx_http_lua_fetch_free_peer(ngx_http_lua_fetch_t *fetch,
    ngx_uint_t keepalive);
static void ngx_http_lua_fetch_cleanup(void *data);


void
ngx_http_lua_fetch_register(lua_State *L)
{
    lua_pushcfunction(L, ngx_http_lua_fetch);
    lua_setglobal(L, "fetch");
}


static int
ngx_http_lua_fetch(lua_State *L)
{
    ngx_int_t              rc;
    ngx_http_lua_fetch_t  *fetch;

    fetch = ngx_http_lua_fetch_create(L);
    if (fetch == NULL) {
        return lua_error(L);
    }

    if (ngx_http_lua_fetch_parse_args(L, fetch) != NGX_OK) {
        return luaL_error(L, "fetch request state is invalid");
    }

    lua_replace(L, 1);
    lua_settop(L, 1);

    rc = ngx_http_lua_fetch_parse_origin(fetch);
    if (rc == NGX_ERROR) {
        return ngx_http_lua_fetch_push_result(L, fetch);
    }

    if (ngx_http_lua_fetch_create_request(fetch) != NGX_OK) {
        if (fetch->resolver != NULL) {
            ngx_resolve_name_done(fetch->resolver);
            fetch->resolver = NULL;
        }

        ngx_http_lua_fetch_fail(fetch, "no memory");
        return ngx_http_lua_fetch_push_result(L, fetch);
    }

    if (rc == NGX_OK) {
        rc = ngx_http_lua_fetch_connect(fetch);
    }

    if (rc == NGX_AGAIN) {
        return lua_yieldk(L, 0, (lua_KContext) (intptr_t) fetch,
                          ngx_http_lua_fetch_continue);
    }

    return ngx_http_lua_fetch_push_result(L, fetch);
}


static int
ngx_http_lua_fetch_continue(lua_State *L, int status, lua_KContext ctx)
{
    ngx_http_lua_fetch_t  *fetch;

    fetch = (ngx_http_lua_fetch_t *) (intptr_t) ctx;

    (void) status;

    return ngx_http_lua_fetch_push_result(L, fetch);
}


static void
ngx_http_lua_fetch_resume(ngx_http_lua_fetch_t *fetch)
{
    void              *resume_data;
    ngx_lua_ctx_t     *ctx;
    ngx_lua_resume_pt  resume;

    ctx = fetch->ctx;
    resume = ctx->resume;
    resume_data = ctx->data;
    ctx->resume = NULL;
    ctx->data = NULL;

    resume(resume_data);
}


static int
ngx_http_lua_fetch_push_result(lua_State *L, ngx_http_lua_fetch_t *fetch)
{
    if (fetch->failed) {
        lua_pushnil(L);
        lua_pushlstring(L, (const char *) fetch->error.data,
                        fetch->error.len);
        return 2;
    }

    if (fetch->response == NULL) {
        return luaL_error(L, "fetch response is invalid");
    }

    return 1;
}


static ngx_int_t
ngx_http_lua_fetch_parse_args(lua_State *L, ngx_http_lua_fetch_t *fetch)
{
    int  nargs;

    nargs = lua_gettop(L);

    if (nargs < 1 || nargs > 3) {
        (void) luaL_error(L,
                          "fetch() takes input, optional init, and options");
        return NGX_ERROR;
    }

    if (nargs >= 2 && !lua_isnil(L, 2) && !lua_istable(L, 2)) {
        (void) luaL_argerror(L, 2, "table expected");
        return NGX_ERROR;
    }

    if (nargs == 3 && !lua_isnil(L, 3) && !lua_istable(L, 3)) {
        (void) luaL_argerror(L, 3, "table expected");
        return NGX_ERROR;
    }

    if (ngx_http_lua_fetch_normalize_request(L, fetch, nargs) != NGX_OK) {
        return NGX_ERROR;
    }

    if (nargs < 3) {
        return NGX_OK;
    }

    return ngx_http_lua_fetch_parse_options(L, fetch, 3, 3);
}


static ngx_int_t
ngx_http_lua_fetch_normalize_request(lua_State *L,
    ngx_http_lua_fetch_t *fetch, int nargs)
{
    size_t                  url_len;
    const char             *url;
    ngx_lua_web_request_t  *request;

    request = ngx_lua_web_request_get(L, 1);
    if (request != NULL) {
        if (nargs >= 2 && !lua_isnil(L, 2)) {
            (void) luaL_error(L, "fetch(Request, init) is not supported yet");
            return NGX_ERROR;
        }

        lua_pushvalue(L, 1);
        fetch->request = request;
        return NGX_OK;
    }

    if (lua_type(L, 1) == LUA_TSTRING) {
        url = lua_tolstring(L, 1, &url_len);

        request = ngx_lua_web_request_create(L);
        if (request == NULL) {
            (void) luaL_error(L, "no memory");
            return NGX_ERROR;
        }

        if (nargs >= 2 && !lua_isnil(L, 2)) {
            ngx_lua_web_request_init(L, request, 2, 2);
        }

        if (ngx_lua_web_request_set_string(L, &request->url, url, url_len)
            != NGX_OK)
        {
            (void) luaL_error(L, "no memory");
            return NGX_ERROR;
        }

        fetch->request = request;
        return NGX_OK;
    }

    if (lua_istable(L, 1)) {
        request = ngx_lua_web_request_create(L);
        if (request == NULL) {
            (void) luaL_error(L, "no memory");
            return NGX_ERROR;
        }

        ngx_lua_web_request_init(L, request, 1, 1);

        if (nargs >= 2 && !lua_isnil(L, 2)) {
            ngx_lua_web_request_init(L, request, 2, 2);
        }

        fetch->request = request;
        return NGX_OK;
    }

    (void) luaL_argerror(L, 1, "Request, string, or table expected");
    return NGX_ERROR;
}


static ngx_int_t
ngx_http_lua_fetch_parse_options(lua_State *L, ngx_http_lua_fetch_t *fetch,
    int index, int arg)
{
    size_t       len;
    const char  *key, *value;

    if (index > lua_gettop(L) || lua_isnil(L, index)) {
        return NGX_OK;
    }

    index = lua_absindex(L, index);

    lua_pushnil(L);

    while (lua_next(L, index) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 2);
            luaL_argerror(L, arg, "fetch option keys must be strings");
            return NGX_ERROR;
        }

        key = lua_tolstring(L, -2, &len);
        if (len != sizeof("target") - 1
            || ngx_strncmp(key, "target", sizeof("target") - 1) != 0)
        {
            lua_pop(L, 2);
            luaL_argerror(L, arg, "unsupported fetch option");
            return NGX_ERROR;
        }

        if (lua_type(L, -1) != LUA_TSTRING) {
            lua_pop(L, 2);
            luaL_argerror(L, arg, "target must be a string");
            return NGX_ERROR;
        }

        value = lua_tolstring(L, -1, &len);
        fetch->target.len = len;

        if (len == 0) {
            fetch->target.data = (u_char *) "";

        } else {
            fetch->target.data = ngx_pnalloc(fetch->pool, len);
            if (fetch->target.data == NULL) {
                lua_pop(L, 2);
                luaL_error(L, "no memory");
                return NGX_ERROR;
            }

            ngx_memcpy(fetch->target.data, value, len);
        }

        fetch->has_target = 1;

        lua_pop(L, 1);
    }

    return NGX_OK;
}


static ngx_http_lua_fetch_t *
ngx_http_lua_fetch_create(lua_State *L)
{
    ngx_lua_ctx_t          *ctx;
    ngx_http_request_t     *r;
    ngx_pool_cleanup_t     *cln;
    ngx_http_lua_fetch_t   *fetch;

    ctx = ngx_lua_get_ctx(L);
    if (ctx == NULL || ctx->pool == NULL) {
        (void) luaL_error(L, "fetch() requires a request context");
        return NULL;
    }

    r = ngx_http_lua_get_request(L);
    if (r == NULL) {
        (void) luaL_error(L, "fetch() requires a HTTP request context");
        return NULL;
    }

    fetch = ngx_pcalloc(ctx->pool, sizeof(ngx_http_lua_fetch_t));
    if (fetch == NULL) {
        (void) luaL_error(L, "no memory");
        return NULL;
    }

    fetch->ctx = ctx;
    fetch->r = r;
    fetch->request = NULL;
    fetch->response = NULL;
    fetch->response_body = NULL;
    fetch->pool = ctx->pool;
    fetch->resolver = NULL;
    fetch->content_length_n = -1;
    fetch->body_read = 0;
    fetch->request_sent = 0;
    fetch->request_body_sent = 0;
    fetch->keepalive = 1;
    fetch->ssl = 0;
    fetch->has_target = 0;
    fetch->chunked_body = 0;
    fetch->header_only = 0;
    fetch->failed = 0;
    fetch->error.len = 0;
    fetch->error.data = NULL;

    fetch->read_buf = ngx_create_temp_buf(ctx->pool,
                                          NGX_HTTP_LUA_FETCH_RESPONSE_BUFFER);
    if (fetch->read_buf == NULL) {
        (void) luaL_error(L, "no memory");
        return NULL;
    }

    ngx_str_set(&fetch->peer_name, "fetch upstream");

    cln = ngx_pool_cleanup_add(ctx->pool, 0);
    if (cln == NULL) {
        (void) luaL_error(L, "no memory");
        return NULL;
    }

    cln->handler = ngx_http_lua_fetch_cleanup;
    cln->data = fetch;

    return fetch;
}


static ngx_int_t
ngx_http_lua_fetch_parse_uri(ngx_http_lua_fetch_t *fetch, ngx_str_t *url)
{
    u_char  *p, *last, *authority, *path, *query, *fragment, *uri;

    p = url->data;
    last = p + url->len;

    if (url->len >= sizeof("http://") - 1
        && ngx_strncasecmp(p, (u_char *) "http://", sizeof("http://") - 1)
           == 0)
    {
        authority = p + sizeof("http://") - 1;

    } else if (url->len >= sizeof("https://") - 1
               && ngx_strncasecmp(p, (u_char *) "https://",
                                  sizeof("https://") - 1)
                  == 0)
    {
        authority = p + sizeof("https://") - 1;

    } else {
        ngx_http_lua_fetch_fail(fetch, "fetch URL must be absolute");
        return NGX_ERROR;
    }

    fragment = ngx_strlchr(authority, last, '#');
    if (fragment != NULL) {
        last = fragment;
    }

    path = ngx_strlchr(authority, last, '/');
    query = ngx_strlchr(authority, last, '?');

    if (path == NULL || (query != NULL && query < path)) {
        path = query;
    }

    if (path == NULL) {
        fetch->uri.data = (u_char *) "/";
        fetch->uri.len = sizeof("/") - 1;

    } else if (*path == '?') {
        fetch->uri.len = sizeof("/") - 1 + (last - path);
        uri = ngx_pnalloc(fetch->pool, fetch->uri.len);
        if (uri == NULL) {
            ngx_http_lua_fetch_fail(fetch, "no memory");
            return NGX_ERROR;
        }

        uri[0] = '/';
        ngx_memcpy(uri + 1, path, last - path);
        fetch->uri.data = uri;
        last = path;

    } else {
        fetch->uri.data = path;
        fetch->uri.len = last - path;
        last = path;
    }

    if (authority == last) {
        ngx_http_lua_fetch_fail(fetch, "fetch URL host is invalid");
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_fetch_parse_origin(ngx_http_lua_fetch_t *fetch)
{
    u_char              *p, *last, *authority, *path, *query, *fragment;
    ngx_url_t            parsed;
    ngx_str_t           *url;
    ngx_uint_t           target;

    if (ngx_http_lua_fetch_parse_uri(fetch, &fetch->request->url) != NGX_OK) {
        return NGX_ERROR;
    }

    target = fetch->has_target;
    url = target ? &fetch->target : &fetch->request->url;

    p = url->data;
    last = p + url->len;

    if (url->len < sizeof("http://") - 1) {
        ngx_http_lua_fetch_fail(fetch, target
                                ? "fetch target must be an absolute origin"
                                : "fetch URL must be absolute");
        return NGX_ERROR;
    }

    if (url->len >= sizeof("https://") - 1
        && ngx_strncasecmp(p, (u_char *) "https://",
                           sizeof("https://") - 1)
           == 0)
    {
        fetch->scheme.data = p;
        fetch->scheme.len = sizeof("https") - 1;
        fetch->ssl = 1;
        ngx_http_lua_fetch_fail(fetch, "fetch https is not supported yet");
        return NGX_ERROR;
    }

    if (ngx_strncasecmp(p, (u_char *) "http://", sizeof("http://") - 1)
        != 0)
    {
        ngx_http_lua_fetch_fail(fetch, target
                                ? "fetch target scheme is not supported"
                                : "fetch URL scheme is not supported");
        return NGX_ERROR;
    }

    fetch->scheme.data = p;
    fetch->scheme.len = sizeof("http") - 1;
    fetch->ssl = 0;

    authority = p + sizeof("http://") - 1;

    fragment = ngx_strlchr(authority, last, '#');
    if (fragment != NULL) {
        if (target) {
            ngx_http_lua_fetch_fail(fetch,
                                    "fetch target must be an origin");
            return NGX_ERROR;
        }

        last = fragment;
    }

    path = ngx_strlchr(authority, last, '/');
    query = ngx_strlchr(authority, last, '?');

    if (target && query != NULL) {
        ngx_http_lua_fetch_fail(fetch, "fetch target must be an origin");
        return NGX_ERROR;
    }

    if (path == NULL || (query != NULL && query < path)) {
        path = query;
    }

    if (target && path != NULL && path + 1 != last) {
        ngx_http_lua_fetch_fail(fetch, "fetch target must be an origin");
        return NGX_ERROR;
    }

    if (path != NULL) {
        last = path;
    }

    if (authority == last) {
        ngx_http_lua_fetch_fail(fetch, target
                                ? "fetch target host is invalid"
                                : "fetch URL host is invalid");
        return NGX_ERROR;
    }

    fetch->authority.data = authority;
    fetch->authority.len = last - authority;

    ngx_memzero(&parsed, sizeof(ngx_url_t));
    parsed.url = fetch->authority;
    parsed.default_port = 80;
    parsed.no_resolve = 1;

    if (ngx_parse_url(fetch->pool, &parsed) != NGX_OK) {
        ngx_http_lua_fetch_fail(fetch, target
                                ? "fetch target host is invalid"
                                : "fetch URL host is invalid");
        return NGX_ERROR;
    }

    fetch->host = parsed.host;
    fetch->port = parsed.port;
    fetch->peer_name = fetch->authority;

    if (parsed.naddrs == 0) {
        return ngx_http_lua_fetch_resolve(fetch);
    }

    return ngx_http_lua_fetch_set_peer(fetch, parsed.addrs[0].sockaddr,
                                       parsed.addrs[0].socklen);
}


static ngx_int_t
ngx_http_lua_fetch_resolve(ngx_http_lua_fetch_t *fetch)
{
    ngx_int_t                  rc;
    ngx_resolver_ctx_t        *ctx;
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(fetch->r, ngx_http_core_module);

    ctx = ngx_resolve_start(clcf->resolver, NULL);
    if (ctx == NULL) {
        ngx_http_lua_fetch_fail(fetch, "no memory");
        return NGX_ERROR;
    }

    if (ctx == NGX_NO_RESOLVER) {
        ngx_http_lua_fetch_fail(fetch,
                                "no resolver defined to resolve fetch host");
        return NGX_ERROR;
    }

    ctx->name = fetch->host;
    ctx->handler = ngx_http_lua_fetch_resolve_handler;
    ctx->data = fetch;
    ctx->timeout = clcf->resolver_timeout;

    fetch->resolver = ctx;

    rc = ngx_resolve_name(ctx);
    if (rc != NGX_OK) {
        fetch->resolver = NULL;
        ngx_http_lua_fetch_fail(fetch, "fetch DNS resolve failed");
        return NGX_ERROR;
    }

    if (fetch->resolver == NULL) {
        return fetch->failed ? NGX_ERROR : NGX_OK;
    }

    return NGX_AGAIN;
}


static void
ngx_http_lua_fetch_resolve_handler(ngx_resolver_ctx_t *ctx)
{
    ngx_int_t              rc;
    ngx_uint_t             resume;
    ngx_connection_t      *c;
    ngx_http_lua_fetch_t  *fetch;

    resume = ctx->async;
    fetch = ctx->data;

    if (ctx->state) {
        ngx_http_lua_fetch_fail(fetch, "fetch DNS resolve failed");

    } else if (ctx->naddrs == 0) {
        ngx_http_lua_fetch_fail(fetch, "fetch DNS resolved no addresses");

    } else if (ngx_http_lua_fetch_set_peer(fetch, ctx->addrs[0].sockaddr,
                                           ctx->addrs[0].socklen)
               != NGX_OK)
    {
        ngx_http_lua_fetch_fail(fetch, "fetch connect target is too large");
    }

    fetch->resolver = NULL;
    ngx_resolve_name_done(ctx);

    if (!resume) {
        return;
    }

    if (!fetch->failed) {
        rc = ngx_http_lua_fetch_connect(fetch);
        if (rc == NGX_AGAIN) {
            goto done;
        }
    }

    ngx_http_lua_fetch_resume(fetch);

done:

    c = fetch->r->connection;
    ngx_http_run_posted_requests(c);
}


static ngx_int_t
ngx_http_lua_fetch_set_peer(ngx_http_lua_fetch_t *fetch,
    struct sockaddr *sockaddr, socklen_t socklen)
{
    ngx_http_request_t  *r;

    if (socklen > (socklen_t) sizeof(ngx_sockaddr_t)) {
        return NGX_ERROR;
    }

    ngx_memcpy(&fetch->sockaddr, sockaddr, socklen);
    ngx_inet_set_port(&fetch->sockaddr.sockaddr, fetch->port);

    r = fetch->r;

    fetch->peer.sockaddr = &fetch->sockaddr.sockaddr;
    fetch->peer.socklen = socklen;
    fetch->peer.name = &fetch->peer_name;
    fetch->peer.get = ngx_event_get_peer;
    fetch->peer.log = r->connection->log;
    fetch->peer.log_error = NGX_ERROR_ERR;
    fetch->peer.tries = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_fetch_create_request(ngx_http_lua_fetch_t *fetch)
{
    size_t                 len;
    ngx_buf_t             *b;
    ngx_lua_web_request_t *request;

    request = fetch->request;

    len = request->method.len
          + sizeof(" ") - 1 + fetch->uri.len
          + sizeof(" HTTP/1.1" CRLF) - 1
          + sizeof("Host: ") - 1 + fetch->authority.len
          + sizeof(CRLF) - 1
          + sizeof("Connection: keep-alive" CRLF) - 1
          + sizeof(CRLF) - 1;

    if (request->body != NULL) {
        len += sizeof("Transfer-Encoding: chunked" CRLF) - 1;
    }

    b = ngx_create_temp_buf(fetch->pool, len);
    if (b == NULL) {
        return NGX_ERROR;
    }

    b->last = ngx_cpymem(b->last, request->method.data,
                         request->method.len);
    b->last = ngx_cpymem(b->last, " ", sizeof(" ") - 1);
    b->last = ngx_cpymem(b->last, fetch->uri.data, fetch->uri.len);
    b->last = ngx_cpymem(b->last, " HTTP/1.1" CRLF,
                         sizeof(" HTTP/1.1" CRLF) - 1);
    b->last = ngx_cpymem(b->last, "Host: ", sizeof("Host: ") - 1);
    b->last = ngx_cpymem(b->last, fetch->authority.data,
                         fetch->authority.len);
    b->last = ngx_cpymem(b->last, CRLF, sizeof(CRLF) - 1);
    b->last = ngx_cpymem(b->last, "Connection: keep-alive" CRLF,
                         sizeof("Connection: keep-alive" CRLF) - 1);

    if (request->body != NULL) {
        b->last = ngx_cpymem(b->last, "Transfer-Encoding: chunked" CRLF,
                             sizeof("Transfer-Encoding: chunked" CRLF) - 1);
    }

    b->last = ngx_cpymem(b->last, CRLF, sizeof(CRLF) - 1);

    fetch->write_buf = b;

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_fetch_connect(ngx_http_lua_fetch_t *fetch)
{
    ngx_int_t          rc;
    ngx_connection_t  *c;

    rc = ngx_event_connect_peer(&fetch->peer);

    if (rc == NGX_ERROR || rc == NGX_DECLINED || rc == NGX_BUSY) {
        ngx_http_lua_fetch_fail(fetch, "fetch connect failed");
        return NGX_ERROR;
    }

    c = fetch->peer.connection;

    c->data = fetch;
    c->write->handler = ngx_http_lua_fetch_send_request_handler;
    c->read->handler = ngx_http_lua_fetch_process_header_handler;

    if (rc == NGX_AGAIN) {
        ngx_add_timer(c->write, NGX_HTTP_LUA_FETCH_CONNECT_TIMEOUT);
        return NGX_AGAIN;
    }

    return ngx_http_lua_fetch_send_request(fetch);
}


static ngx_int_t
ngx_http_lua_fetch_test_connect(ngx_connection_t *c)
{
    int        err;
    socklen_t  len;

#if (NGX_HAVE_KQUEUE)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        if (c->write->pending_eof || c->read->pending_eof) {
            if (c->write->pending_eof) {
                err = c->write->kq_errno;

            } else {
                err = c->read->kq_errno;
            }

            c->log->action = "connecting to fetch upstream";
            (void) ngx_connection_error(c, err,
                                    "kevent() reported that connect() failed");
            return NGX_ERROR;
        }

    } else
#endif
    {
        err = 0;
        len = sizeof(int);

        if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (void *) &err, &len)
            == -1)
        {
            err = ngx_socket_errno;
        }

        if (err) {
            c->log->action = "connecting to fetch upstream";
            (void) ngx_connection_error(c, err, "connect() failed");
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static void
ngx_http_lua_fetch_send_request_handler(ngx_event_t *ev)
{
    ngx_connection_t       *c;
    ngx_http_lua_fetch_t   *fetch;

    c = ev->data;
    fetch = c->data;

    if (ev->timedout) {
        ev->timedout = 0;
        ngx_http_lua_fetch_fail(fetch, fetch->request_sent
                                ? "fetch request send timed out"
                                : "fetch connect timed out");
        ngx_http_lua_fetch_resume(fetch);
        return;
    }

    if (ngx_http_lua_fetch_send_request(fetch) == NGX_AGAIN) {
        return;
    }

    ngx_http_lua_fetch_resume(fetch);
}


static ngx_int_t
ngx_http_lua_fetch_send_request(ngx_http_lua_fetch_t *fetch)
{
    ngx_int_t          rc;
    ngx_connection_t  *c;

    c = fetch->peer.connection;

    if (!fetch->request_sent) {
        if (ngx_http_lua_fetch_test_connect(c) != NGX_OK) {
            ngx_http_lua_fetch_fail(fetch, "fetch connect failed");
            return NGX_ERROR;
        }

        fetch->request_sent = 1;

        ngx_log_error(NGX_LOG_NOTICE, fetch->r->connection->log, 0,
                      "fetch connected");
    }

    for ( ;; ) {
        rc = ngx_http_lua_fetch_send_buffer(fetch);
        if (rc != NGX_OK) {
            return rc;
        }

        if (fetch->request->body == NULL || fetch->request_body_sent) {
            break;
        }

        rc = ngx_http_lua_fetch_next_body_buffer(fetch);
        if (rc != NGX_OK) {
            return rc;
        }
    }

    c->write->handler = ngx_http_empty_handler;
    c->read->handler = ngx_http_lua_fetch_process_header_handler;

    if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
        ngx_http_lua_fetch_fail(fetch, "fetch request send failed");
        return NGX_ERROR;
    }

    ngx_add_timer(c->read, NGX_HTTP_LUA_FETCH_READ_TIMEOUT);

    if (c->read->ready) {
        return ngx_http_lua_fetch_process_header(fetch);
    }

    if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
        ngx_http_lua_fetch_fail(fetch, "fetch response header read failed");
        return NGX_ERROR;
    }

    return NGX_AGAIN;
}


static ngx_int_t
ngx_http_lua_fetch_send_buffer(ngx_http_lua_fetch_t *fetch)
{
    size_t             size;
    ssize_t            n;
    ngx_connection_t  *c;

    c = fetch->peer.connection;

    for ( ;; ) {
        size = fetch->write_buf->last - fetch->write_buf->pos;

        if (size == 0) {
            break;
        }

        n = c->send(c, fetch->write_buf->pos, size);

        if (n > 0) {
            fetch->write_buf->pos += n;
            continue;
        }

        if (n == NGX_AGAIN) {
            ngx_add_timer(c->write, NGX_HTTP_LUA_FETCH_SEND_TIMEOUT);

            if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
                ngx_http_lua_fetch_fail(fetch, "fetch request send failed");
                return NGX_ERROR;
            }

            return NGX_AGAIN;
        }

        ngx_http_lua_fetch_fail(fetch, "fetch request send failed");
        return NGX_ERROR;
    }

    if (c->write->timer_set) {
        ngx_del_timer(c->write);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_fetch_next_body_buffer(ngx_http_lua_fetch_t *fetch)
{
    ngx_int_t  rc;
    ngx_str_t  chunk;

    rc = ngx_lua_web_stream_read(fetch->request->body, fetch->pool, &chunk);

    if (rc == NGX_OK) {
        return ngx_http_lua_fetch_create_body_chunk(fetch, &chunk);
    }

    if (rc == NGX_DONE) {
        fetch->request_body_sent = 1;
        return ngx_http_lua_fetch_create_last_body_chunk(fetch);
    }

    if (rc == NGX_AGAIN) {
        ngx_lua_web_stream_wait(fetch->request->body,
                                ngx_http_lua_fetch_request_body_wake, fetch);
        return NGX_AGAIN;
    }

    ngx_http_lua_fetch_fail(fetch, "fetch request body read failed");

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_lua_fetch_create_body_chunk(ngx_http_lua_fetch_t *fetch,
    ngx_str_t *chunk)
{
    ngx_buf_t  *b;

    b = ngx_create_temp_buf(fetch->pool,
                            NGX_SIZE_T_LEN + sizeof(CRLF) - 1
                            + chunk->len + sizeof(CRLF) - 1);
    if (b == NULL) {
        ngx_http_lua_fetch_fail(fetch, "fetch request body send failed");
        return NGX_ERROR;
    }

    b->last = ngx_sprintf(b->last, "%xz" CRLF, chunk->len);
    b->last = ngx_cpymem(b->last, chunk->data, chunk->len);
    b->last = ngx_cpymem(b->last, CRLF, sizeof(CRLF) - 1);

    fetch->write_buf = b;

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_fetch_create_last_body_chunk(ngx_http_lua_fetch_t *fetch)
{
    ngx_buf_t  *b;

    b = ngx_create_temp_buf(fetch->pool, sizeof("0" CRLF CRLF) - 1);
    if (b == NULL) {
        ngx_http_lua_fetch_fail(fetch, "fetch request body send failed");
        return NGX_ERROR;
    }

    b->last = ngx_cpymem(b->last, "0" CRLF CRLF,
                         sizeof("0" CRLF CRLF) - 1);

    fetch->write_buf = b;

    return NGX_OK;
}


static void
ngx_http_lua_fetch_request_body_wake(void *data)
{
    ngx_http_lua_fetch_t  *fetch;

    fetch = data;

    if (ngx_http_lua_fetch_send_request(fetch) == NGX_AGAIN) {
        return;
    }

    ngx_http_lua_fetch_resume(fetch);
}


static void
ngx_http_lua_fetch_process_header_handler(ngx_event_t *ev)
{
    ngx_http_lua_fetch_t  *fetch;
    ngx_connection_t      *c;

    c = ev->data;
    fetch = c->data;

    if (ev->timedout) {
        ev->timedout = 0;
        ngx_http_lua_fetch_fail(fetch,
                                "fetch response header read timed out");
        ngx_http_lua_fetch_resume(fetch);
        return;
    }

    if (ngx_http_lua_fetch_process_header(fetch) == NGX_AGAIN) {
        return;
    }

    ngx_http_lua_fetch_resume(fetch);
}


static ngx_int_t
ngx_http_lua_fetch_process_header(ngx_http_lua_fetch_t *fetch)
{
    ngx_uint_t         status;
    u_char            *header_end;
    ssize_t            n;
    ngx_buf_t         *b;
    ngx_connection_t  *c;

    c = fetch->peer.connection;

    b = fetch->read_buf;

    if (!fetch->request_sent && ngx_http_lua_fetch_test_connect(c) != NGX_OK) {
        ngx_http_lua_fetch_fail(fetch, "fetch connect failed");
        return NGX_ERROR;
    }

    for ( ;; ) {
        header_end = ngx_http_lua_fetch_header_end(b);
        if (header_end != NULL) {
            if (ngx_http_lua_fetch_parse_status(fetch, &status) != NGX_OK) {
                ngx_http_lua_fetch_fail(fetch,
                                        "fetch response header invalid");
                return NGX_ERROR;
            }

            ngx_log_error(NGX_LOG_NOTICE, fetch->r->connection->log, 0,
                          "fetch response header received");
            ngx_log_error(NGX_LOG_NOTICE, fetch->r->connection->log, 0,
                          "fetch response status: %ui", status);

            return ngx_http_lua_fetch_create_response(fetch, header_end,
                                                      status);
        }

        if (b->last == b->end) {
            ngx_http_lua_fetch_fail(fetch, "fetch response header too large");
            return NGX_ERROR;
        }

        n = c->recv(c, b->last, b->end - b->last);

        if (n == NGX_AGAIN) {
            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                ngx_http_lua_fetch_fail(fetch,
                                        "fetch response header read failed");
                return NGX_ERROR;
            }

            return NGX_AGAIN;
        }

        if (n == 0) {
            ngx_http_lua_fetch_fail(fetch,
                                    "fetch upstream closed before response");
            return NGX_ERROR;
        }

        if (n == NGX_ERROR) {
            ngx_http_lua_fetch_fail(fetch,
                                    "fetch response header read failed");
            return NGX_ERROR;
        }

        b->last += n;
    }
}


static u_char *
ngx_http_lua_fetch_header_end(ngx_buf_t *b)
{
    u_char  *p;

    for (p = b->pos; p + 3 < b->last; p++) {
        if (p[0] == '\r' && p[1] == '\n'
            && p[2] == '\r' && p[3] == '\n')
        {
            return p + 4;
        }
    }

    return NULL;
}


static ngx_int_t
ngx_http_lua_fetch_parse_status(ngx_http_lua_fetch_t *fetch,
    ngx_uint_t *status)
{
    u_char     *p, *last;
    ngx_int_t   n;

    p = fetch->read_buf->pos;
    last = fetch->read_buf->last;

    if (last - p < (off_t) (sizeof("HTTP/1.1 000") - 1)) {
        return NGX_ERROR;
    }

    if (ngx_strncmp(p, "HTTP/", sizeof("HTTP/") - 1) != 0) {
        return NGX_ERROR;
    }

    p = ngx_strlchr(p, last, ' ');
    if (p == NULL || p + 4 > last) {
        return NGX_ERROR;
    }

    n = ngx_atoi(p + 1, 3);
    if (n == NGX_ERROR) {
        return NGX_ERROR;
    }

    *status = (ngx_uint_t) n;

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_fetch_create_response(ngx_http_lua_fetch_t *fetch,
    u_char *header_end, ngx_uint_t status)
{
    int                     top;
    lua_State              *L;
    ngx_lua_web_response_t *response;

    L = fetch->ctx->thread;
    top = lua_gettop(L);

    response = ngx_lua_web_response_create(L);
    if (response == NULL) {
        lua_settop(L, top);
        ngx_http_lua_fetch_fail(fetch, "no memory");
        return NGX_ERROR;
    }

    if (ngx_http_lua_fetch_init_response(L, fetch, response, header_end,
                                         status)
        != NGX_OK)
    {
        lua_settop(L, top);

        if (!fetch->failed) {
            ngx_http_lua_fetch_fail(fetch, "no memory");
        }

        return NGX_ERROR;
    }

    fetch->response = response;

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_fetch_init_response(lua_State *L, ngx_http_lua_fetch_t *fetch,
    ngx_lua_web_response_t *response, u_char *header_end, ngx_uint_t status)
{
    int  response_index;

    response_index = lua_absindex(L, -1);

    response->status = status;

    if (ngx_http_lua_fetch_parse_response_headers(L, fetch, response,
                                                  header_end, status)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    fetch->read_buf->pos = header_end;

    if (fetch->header_only) {
        ngx_http_lua_fetch_free_peer(fetch, fetch->keepalive);
        return NGX_OK;
    }

    if (ngx_http_lua_fetch_init_body(L, fetch, response_index, response)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_fetch_parse_response_headers(lua_State *L,
    ngx_http_lua_fetch_t *fetch, ngx_lua_web_response_t *response,
    u_char *header_end, ngx_uint_t status)
{
    u_char     *p, *end, *line_end, *colon, *value, *value_end;
    ngx_int_t   content_length;

    p = fetch->read_buf->pos;
    end = header_end - (sizeof(CRLF) - 1);

    line_end = ngx_strlchr(p, end, LF);
    if (line_end == NULL || line_end == p || line_end[-1] != CR) {
        ngx_http_lua_fetch_fail(fetch, "fetch response header invalid");
        return NGX_ERROR;
    }

    p = line_end + 1;

    while (p < end) {
        line_end = ngx_strlchr(p, end, LF);
        if (line_end == NULL || line_end == p || line_end[-1] != CR) {
            ngx_http_lua_fetch_fail(fetch, "fetch response header invalid");
            return NGX_ERROR;
        }

        if (line_end == p + 1 && p[0] == CR) {
            break;
        }

        colon = ngx_strlchr(p, line_end - 1, ':');
        if (colon == NULL || colon == p) {
            ngx_http_lua_fetch_fail(fetch, "fetch response header invalid");
            return NGX_ERROR;
        }

        value = colon + 1;
        while (value < line_end - 1 && (*value == ' ' || *value == '\t')) {
            value++;
        }

        value_end = line_end - 1;
        while (value_end > value
               && (value_end[-1] == ' ' || value_end[-1] == '\t'))
        {
            value_end--;
        }

        ngx_lua_web_headers_set(L, response->headers, (const char *) p,
                                colon - p, (const char *) value,
                                value_end - value);

        if (ngx_http_lua_fetch_header_is(p, colon - p, "content-length",
                                         sizeof("content-length") - 1))
        {
            content_length = ngx_atoi(value, value_end - value);
            if (content_length == NGX_ERROR) {
                ngx_http_lua_fetch_fail(fetch,
                                        "fetch response header invalid");
                return NGX_ERROR;
            }

            fetch->content_length_n = content_length;
        }

        if (ngx_http_lua_fetch_header_is(p, colon - p, "transfer-encoding",
                                         sizeof("transfer-encoding") - 1)
            && value_end - value == (off_t) (sizeof("chunked") - 1)
            && ngx_strncasecmp(value, (u_char *) "chunked",
                               sizeof("chunked") - 1)
               == 0)
        {
            fetch->chunked_body = 1;
            fetch->content_length_n = -1;
            ngx_memzero(&fetch->chunked, sizeof(ngx_http_chunked_t));
        }

        if (ngx_http_lua_fetch_header_is(p, colon - p, "connection",
                                         sizeof("connection") - 1)
            && ngx_http_lua_fetch_header_value_has_token(value, value_end,
                                                        "close",
                                                        sizeof("close") - 1))
        {
            fetch->keepalive = 0;
        }

        p = line_end + 1;
    }

    if (status == NGX_HTTP_NO_CONTENT
        || status == NGX_HTTP_LUA_FETCH_RESET_CONTENT
        || status == NGX_HTTP_NOT_MODIFIED
        || (fetch->request->method.len == sizeof("HEAD") - 1
            && ngx_strncmp(fetch->request->method.data, "HEAD",
                           sizeof("HEAD") - 1)
               == 0)
        || fetch->content_length_n == 0)
    {
        fetch->header_only = 1;
    }

    if (!fetch->header_only && !fetch->chunked_body
        && fetch->content_length_n < 0)
    {
        fetch->keepalive = 0;
    }

    return NGX_OK;
}


static ngx_uint_t
ngx_http_lua_fetch_header_is(u_char *name, size_t len, const char *value,
    size_t value_len)
{
    if (len != value_len) {
        return 0;
    }

    return ngx_strncasecmp(name, (u_char *) value, len) == 0;
}


static ngx_uint_t
ngx_http_lua_fetch_header_value_has_token(u_char *value, u_char *end,
    const char *token, size_t token_len)
{
    u_char  *p, *start, *last;

    p = value;

    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) {
            p++;
        }

        start = p;

        while (p < end && *p != ',') {
            p++;
        }

        last = p;

        while (last > start
               && (last[-1] == ' ' || last[-1] == '\t'))
        {
            last--;
        }

        if ((size_t) (last - start) == token_len
            && ngx_strncasecmp(start, (u_char *) token, token_len) == 0)
        {
            return 1;
        }
    }

    return 0;
}


static ngx_int_t
ngx_http_lua_fetch_init_body(lua_State *L, ngx_http_lua_fetch_t *fetch,
    int response_index, ngx_lua_web_response_t *response)
{
    ngx_lua_web_stream_t         *body;
    ngx_lua_web_stream_source_t  *source;

    body = ngx_lua_web_stream_create(L, fetch->pool);
    if (body == NULL) {
        return NGX_ERROR;
    }

    source = ngx_pcalloc(fetch->pool, sizeof(ngx_lua_web_stream_source_t));
    if (source == NULL) {
        lua_pop(L, 1);
        return NGX_ERROR;
    }

    source->pull = ngx_http_lua_fetch_body_source_pull;
    source->data = fetch;
    ngx_lua_web_stream_set_source(body, source);

    response->body = body;
    fetch->response_body = body;

    lua_setiuservalue(L, response_index, 2);
    ngx_http_lua_fetch_wait_body_pull(fetch);

    return NGX_OK;
}


static void
ngx_http_lua_fetch_wait_body_pull(ngx_http_lua_fetch_t *fetch)
{
    ngx_connection_t  *c;

    c = fetch->peer.connection;

    if (c->read->timer_set) {
        ngx_del_timer(c->read);
    }

    c->read->handler = ngx_http_empty_handler;
}


static ngx_int_t
ngx_http_lua_fetch_body_source_pull(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source)
{
    ngx_int_t              rc;
    ngx_http_lua_fetch_t  *fetch;

    fetch = source->data;

    rc = ngx_http_lua_fetch_read_body(fetch, stream);
    if (rc == NGX_DONE) {
        ngx_lua_web_stream_close(stream);
        ngx_http_lua_fetch_free_peer(fetch, fetch->keepalive);
        return NGX_OK;
    }

    if (rc == NGX_ERROR) {
        ngx_lua_web_stream_error(stream);
        ngx_http_lua_fetch_free_peer(fetch, 0);
    }

    return rc;
}


static ngx_int_t
ngx_http_lua_fetch_read_body(ngx_http_lua_fetch_t *fetch,
    ngx_lua_web_stream_t *stream)
{
    ngx_int_t          rc;
    ngx_connection_t  *c;

    if (fetch->chunked_body) {
        rc = ngx_http_lua_fetch_read_chunked_body(fetch, stream);

    } else {
        rc = ngx_http_lua_fetch_read_plain_body(fetch, stream);
    }

    if (rc != NGX_AGAIN) {
        return rc;
    }

    c = fetch->peer.connection;
    c->read->handler = ngx_http_lua_fetch_process_body_handler;
    ngx_add_timer(c->read, NGX_HTTP_LUA_FETCH_READ_TIMEOUT);

    if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_AGAIN;
}


static ngx_int_t
ngx_http_lua_fetch_read_plain_body(ngx_http_lua_fetch_t *fetch,
    ngx_lua_web_stream_t *stream)
{
    size_t     size;
    ngx_int_t  rc;
    ngx_buf_t *b;

    b = fetch->read_buf;

    for ( ;; ) {
        if (fetch->content_length_n >= 0
            && fetch->body_read >= fetch->content_length_n)
        {
            return NGX_DONE;
        }

        size = b->last - b->pos;

        if (size != 0) {
            if (fetch->content_length_n >= 0
                && fetch->body_read + (off_t) size
                   > fetch->content_length_n)
            {
                size = (size_t) (fetch->content_length_n
                                 - fetch->body_read);
            }

            if (ngx_http_lua_fetch_enqueue_body_buffer(fetch, stream, b->pos,
                                                       size)
                != NGX_OK)
            {
                return NGX_ERROR;
            }

            b->pos += size;
            fetch->body_read += size;

            if (fetch->content_length_n >= 0
                && fetch->body_read >= fetch->content_length_n)
            {
                return NGX_DONE;
            }

            return NGX_OK;
        }

        rc = ngx_http_lua_fetch_recv_body(fetch);

        if (rc == NGX_OK) {
            continue;
        }

        if (rc == NGX_DONE) {
            return NGX_DONE;
        }

        if (rc == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        return NGX_ERROR;
    }
}


static ngx_int_t
ngx_http_lua_fetch_read_chunked_body(ngx_http_lua_fetch_t *fetch,
    ngx_lua_web_stream_t *stream)
{
    size_t     size;
    ngx_int_t  rc;
    ngx_buf_t *b;

    b = fetch->read_buf;

    for ( ;; ) {
        rc = ngx_http_parse_chunked(fetch->r, b, &fetch->chunked, 0);

        if (rc == NGX_OK) {
            size = ngx_min((off_t) (b->last - b->pos), fetch->chunked.size);

            if (size == 0) {
                rc = ngx_http_lua_fetch_recv_body(fetch);
                if (rc == NGX_OK) {
                    continue;
                }

                if (rc == NGX_DONE) {
                    return NGX_ERROR;
                }

                if (rc == NGX_ERROR) {
                    return NGX_ERROR;
                }

                return rc;
            }

            if (ngx_http_lua_fetch_enqueue_body_buffer(fetch, stream, b->pos,
                                                       size)
                != NGX_OK)
            {
                return NGX_ERROR;
            }

            b->pos += size;
            fetch->chunked.size -= size;

            return NGX_OK;
        }

        if (rc == NGX_DONE) {
            return NGX_DONE;
        }

        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }

        rc = ngx_http_lua_fetch_recv_body(fetch);

        if (rc == NGX_OK) {
            continue;
        }

        if (rc == NGX_DONE) {
            return NGX_ERROR;
        }

        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }

        return rc;
    }
}


static ngx_int_t
ngx_http_lua_fetch_recv_body(ngx_http_lua_fetch_t *fetch)
{
    ssize_t            n;
    ngx_buf_t         *b;
    ngx_connection_t  *c;

    c = fetch->peer.connection;
    if (c == NULL) {
        return NGX_ERROR;
    }

    if (c->read->timedout) {
        return NGX_ERROR;
    }

    b = fetch->read_buf;

    if (b->pos == b->last) {
        b->pos = b->start;
        b->last = b->start;

    } else if (b->last == b->end && b->pos > b->start) {
        ngx_memmove(b->start, b->pos, b->last - b->pos);
        b->last = b->start + (b->last - b->pos);
        b->pos = b->start;
    }

    if (b->last == b->end) {
        return NGX_ERROR;
    }

    n = c->recv(c, b->last, b->end - b->last);

    if (n > 0) {
        b->last += n;
        return NGX_OK;
    }

    if (n == 0) {
        return NGX_DONE;
    }

    if (n == NGX_AGAIN) {
        return NGX_AGAIN;
    }

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_lua_fetch_enqueue_body_buffer(ngx_http_lua_fetch_t *fetch,
    ngx_lua_web_stream_t *stream, u_char *data, size_t len)
{
    ngx_buf_t    *b;
    ngx_chain_t  *cl;

    if (len == 0) {
        return NGX_OK;
    }

    cl = ngx_alloc_chain_link(fetch->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    b = ngx_calloc_buf(fetch->pool);
    if (b == NULL) {
        ngx_free_chain(fetch->pool, cl);
        return NGX_ERROR;
    }

    b->start = data;
    b->pos = data;
    b->last = data + len;
    b->end = data + len;
    b->memory = 1;

    cl->buf = b;
    cl->next = NULL;

    ngx_lua_web_stream_enqueue_bufs(stream, cl);

    return NGX_OK;
}


static void
ngx_http_lua_fetch_process_body_handler(ngx_event_t *ev)
{
    ngx_int_t                 rc;
    ngx_lua_web_stream_t     *body;
    ngx_http_lua_fetch_t     *fetch;
    ngx_connection_t         *c;

    c = ev->data;
    fetch = c->data;

    body = fetch->response_body;

    if (ev->timedout) {
        ev->timedout = 0;
        ngx_lua_web_stream_error(body);
        ngx_http_lua_fetch_free_peer(fetch, 0);
        ngx_lua_web_stream_wake(body);
        return;
    }

    rc = ngx_http_lua_fetch_read_body(fetch, body);
    if (rc == NGX_AGAIN) {
        return;
    }

    if (rc == NGX_DONE) {
        ngx_lua_web_stream_close(body);
        ngx_http_lua_fetch_free_peer(fetch, fetch->keepalive);

    } else if (rc == NGX_ERROR) {
        ngx_lua_web_stream_error(body);
        ngx_http_lua_fetch_free_peer(fetch, 0);
    }

    ngx_lua_web_stream_wake(body);
}


static void
ngx_http_lua_fetch_fail(ngx_http_lua_fetch_t *fetch, const char *message)
{
    fetch->failed = 1;
    fetch->error.data = (u_char *) message;
    fetch->error.len = ngx_strlen(message);
    ngx_http_lua_fetch_free_peer(fetch, 0);
}


static void
ngx_http_lua_fetch_free_peer(ngx_http_lua_fetch_t *fetch, ngx_uint_t keepalive)
{
    ngx_connection_t  *c;

    c = fetch->peer.connection;
    if (c == NULL) {
        return;
    }

    fetch->peer.connection = NULL;

    (void) keepalive;

    ngx_close_connection(c);
}


static void
ngx_http_lua_fetch_cleanup(void *data)
{
    ngx_http_lua_fetch_t  *fetch = data;

    if (fetch->resolver != NULL) {
        ngx_resolve_name_done(fetch->resolver);
        fetch->resolver = NULL;
    }

    ngx_http_lua_fetch_free_peer(fetch, 0);
}
