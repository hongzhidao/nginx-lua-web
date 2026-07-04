/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_http_lua.h"
#include "ngx_lua.h"

#include <ngx_event_connect.h>

#include <lauxlib.h>
#include <stdint.h>
#include <stdio.h>


#define NGX_HTTP_LUA_FETCH_CONNECT_TIMEOUT  5000
#define NGX_HTTP_LUA_FETCH_SEND_TIMEOUT     5000
#define NGX_HTTP_LUA_FETCH_READ_TIMEOUT     5000
#define NGX_HTTP_LUA_FETCH_RESPONSE_BUFFER  4096


#define NGX_HTTP_LUA_FETCH_REQUEST                                           \
    "GET /fetch-upstream HTTP/1.1\r\n"                                      \
    "Host: localhost\r\n"                                                   \
    "Connection: close\r\n"                                                 \
    "\r\n"


typedef struct ngx_http_lua_fetch_s  ngx_http_lua_fetch_t;
typedef void (*ngx_http_lua_fetch_event_pt)(ngx_http_lua_fetch_t *fetch);


struct ngx_http_lua_fetch_s {
    ngx_lua_ctx_t              *ctx;
    ngx_http_request_t         *r;
    ngx_lua_web_request_t      *request;
    ngx_pool_t                 *pool;
    ngx_peer_connection_t       peer;
    ngx_sockaddr_t              sockaddr;
    ngx_buf_t                  *write_buf;
    ngx_buf_t                  *read_buf;
    ngx_str_t                   peer_name;
    ngx_str_t                   error;
    const char                 *timeout_error;
    ngx_http_lua_fetch_event_pt read_event_handler;
    ngx_http_lua_fetch_event_pt write_event_handler;
    unsigned                    failed:1;
};


static int ngx_http_lua_fetch(lua_State *L);
static int ngx_http_lua_fetch_continue(lua_State *L, int status,
    lua_KContext ctx);
static ngx_lua_web_request_t *ngx_http_lua_fetch_push_request(lua_State *L);
static ngx_http_lua_fetch_t *ngx_http_lua_fetch_create(lua_State *L,
    ngx_lua_web_request_t *request);
static int ngx_http_lua_fetch_start(lua_State *L);
static ngx_int_t ngx_http_lua_fetch_connect(ngx_http_lua_fetch_t *fetch);
static void ngx_http_lua_fetch_event_handler(ngx_event_t *ev);
static void ngx_http_lua_fetch_connect_handler(ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_connected(ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_send_request(ngx_http_lua_fetch_t *fetch);
static void ngx_http_lua_fetch_send_request_handler(
    ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_process_header(
    ngx_http_lua_fetch_t *fetch);
static void ngx_http_lua_fetch_process_header_handler(
    ngx_http_lua_fetch_t *fetch);
static void ngx_http_lua_fetch_dummy_handler(ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_finish(ngx_http_lua_fetch_t *fetch);
static ngx_int_t ngx_http_lua_fetch_test_connect(ngx_connection_t *c);
static u_char *ngx_http_lua_fetch_header_end(ngx_buf_t *b);
static int ngx_http_lua_fetch_push_result(lua_State *L,
    ngx_http_lua_fetch_t *fetch);
static void ngx_http_lua_fetch_fail(ngx_http_lua_fetch_t *fetch,
    const char *message);
static void ngx_http_lua_fetch_resume(ngx_http_lua_fetch_t *fetch);
static void ngx_http_lua_fetch_close_connection(ngx_http_lua_fetch_t *fetch);
static void ngx_http_lua_fetch_cleanup(void *data);
static void ngx_http_lua_fetch_print_request(ngx_lua_web_request_t *request);
static void ngx_http_lua_fetch_print_string(const char *data, size_t len);


void
ngx_http_lua_fetch_register(lua_State *L)
{
    lua_pushcfunction(L, ngx_http_lua_fetch);
    lua_setglobal(L, "fetch");
}


static int
ngx_http_lua_fetch(lua_State *L)
{
    (void) ngx_http_lua_fetch_push_request(L);

    lua_replace(L, 1);
    lua_settop(L, 1);

    return ngx_http_lua_fetch_start(L);
}


static int
ngx_http_lua_fetch_continue(lua_State *L, int status, lua_KContext ctx)
{
    ngx_http_lua_fetch_t  *fetch;

    fetch = (ngx_http_lua_fetch_t *) (intptr_t) ctx;

    (void) status;

    return ngx_http_lua_fetch_push_result(L, fetch);
}


static ngx_lua_web_request_t *
ngx_http_lua_fetch_push_request(lua_State *L)
{
    int                     nargs;
    size_t                  url_len;
    const char             *url;
    ngx_lua_web_request_t  *request;

    nargs = lua_gettop(L);

    if (nargs < 1 || nargs > 2) {
        (void) luaL_error(L, "fetch() takes input and optional init");
        return NULL;
    }

    if (nargs == 2 && !lua_isnil(L, 2) && !lua_istable(L, 2)) {
        (void) luaL_argerror(L, 2, "table expected");
        return NULL;
    }

    request = ngx_lua_web_request_get(L, 1);
    if (request != NULL) {
        if (nargs == 1 || lua_isnil(L, 2)) {
            lua_pushvalue(L, 1);
            return request;
        }

        (void) luaL_error(L, "fetch(Request, init) is not supported yet");
        return NULL;
    }

    if (lua_type(L, 1) == LUA_TSTRING) {
        url = lua_tolstring(L, 1, &url_len);

        request = ngx_lua_web_request_create(L);
        if (request == NULL) {
            (void) luaL_error(L, "no memory");
            return NULL;
        }

        if (nargs == 2 && !lua_isnil(L, 2)) {
            ngx_lua_web_request_init(L, request, 2, 2);
        }

        if (ngx_lua_web_request_set_string(L, &request->url, url, url_len)
            != NGX_OK)
        {
            (void) luaL_error(L, "no memory");
            return NULL;
        }

        return request;
    }

    if (lua_istable(L, 1)) {
        request = ngx_lua_web_request_create(L);
        if (request == NULL) {
            (void) luaL_error(L, "no memory");
            return NULL;
        }

        ngx_lua_web_request_init(L, request, 1, 1);

        if (nargs == 2 && !lua_isnil(L, 2)) {
            ngx_lua_web_request_init(L, request, 2, 2);
        }

        return request;
    }

    (void) luaL_argerror(L, 1, "Request, string, or table expected");
    return NULL;
}


static ngx_http_lua_fetch_t *
ngx_http_lua_fetch_create(lua_State *L, ngx_lua_web_request_t *request)
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
    fetch->request = request;
    fetch->pool = ctx->pool;
    fetch->failed = 0;
    fetch->error.len = 0;
    fetch->error.data = NULL;

    fetch->write_buf = ngx_create_temp_buf(ctx->pool,
                                           sizeof(NGX_HTTP_LUA_FETCH_REQUEST)
                                           - 1);
    if (fetch->write_buf == NULL) {
        (void) luaL_error(L, "no memory");
        return NULL;
    }

    fetch->write_buf->last = ngx_cpymem(fetch->write_buf->last,
                                        NGX_HTTP_LUA_FETCH_REQUEST,
                                        sizeof(NGX_HTTP_LUA_FETCH_REQUEST)
                                        - 1);

    fetch->read_buf = ngx_create_temp_buf(ctx->pool,
                                          NGX_HTTP_LUA_FETCH_RESPONSE_BUFFER);
    if (fetch->read_buf == NULL) {
        (void) luaL_error(L, "no memory");
        return NULL;
    }

    ngx_str_set(&fetch->peer_name, "fetch upstream");

    if (ngx_connection_local_sockaddr(r->connection, NULL, 0) != NGX_OK) {
        ngx_http_lua_fetch_fail(fetch, "fetch connect target unavailable");
        return fetch;
    }

    if (r->connection->local_socklen > (socklen_t) sizeof(ngx_sockaddr_t)) {
        ngx_http_lua_fetch_fail(fetch, "fetch connect target is too large");
        return fetch;
    }

    ngx_memcpy(&fetch->sockaddr, r->connection->local_sockaddr,
               r->connection->local_socklen);

    fetch->peer.sockaddr = &fetch->sockaddr.sockaddr;
    fetch->peer.socklen = r->connection->local_socklen;
    fetch->peer.name = &fetch->peer_name;
    fetch->peer.get = ngx_event_get_peer;
    fetch->peer.log = r->connection->log;
    fetch->peer.log_error = NGX_ERROR_ERR;
    fetch->peer.tries = 1;

    cln = ngx_pool_cleanup_add(ctx->pool, 0);
    if (cln == NULL) {
        (void) luaL_error(L, "no memory");
        return NULL;
    }

    cln->handler = ngx_http_lua_fetch_cleanup;
    cln->data = fetch;

    return fetch;
}


static int
ngx_http_lua_fetch_start(lua_State *L)
{
    ngx_int_t                rc;
    ngx_http_lua_fetch_t    *fetch;
    ngx_lua_web_request_t   *request;

    request = ngx_lua_web_request_get(L, 1);
    if (request == NULL) {
        return luaL_error(L, "fetch request state is invalid");
    }

    fetch = ngx_http_lua_fetch_create(L, request);
    if (fetch == NULL) {
        return lua_error(L);
    }

    rc = ngx_http_lua_fetch_connect(fetch);

    if (rc == NGX_AGAIN) {
        return lua_yieldk(L, 0, (lua_KContext) (intptr_t) fetch,
                          ngx_http_lua_fetch_continue);
    }

    return ngx_http_lua_fetch_push_result(L, fetch);
}


static ngx_int_t
ngx_http_lua_fetch_connect(ngx_http_lua_fetch_t *fetch)
{
    ngx_int_t          rc;
    ngx_connection_t  *c;

    if (fetch->failed) {
        return NGX_ERROR;
    }

    rc = ngx_event_connect_peer(&fetch->peer);

    if (rc == NGX_ERROR || rc == NGX_DECLINED || rc == NGX_BUSY) {
        ngx_http_lua_fetch_fail(fetch, "fetch connect failed");
        return NGX_ERROR;
    }

    c = fetch->peer.connection;
    if (c == NULL) {
        ngx_http_lua_fetch_fail(fetch, "fetch connect failed");
        return NGX_ERROR;
    }

    c->data = fetch;
    c->write->handler = ngx_http_lua_fetch_event_handler;
    c->read->handler = ngx_http_lua_fetch_event_handler;

    fetch->write_event_handler = ngx_http_lua_fetch_connect_handler;
    fetch->read_event_handler = ngx_http_lua_fetch_connect_handler;

    if (rc == NGX_AGAIN) {
        fetch->timeout_error = "fetch connect timed out";
        ngx_add_timer(c->write, NGX_HTTP_LUA_FETCH_CONNECT_TIMEOUT);
        return NGX_AGAIN;
    }

    return ngx_http_lua_fetch_connected(fetch);
}


static void
ngx_http_lua_fetch_event_handler(ngx_event_t *ev)
{
    ngx_connection_t       *c;
    ngx_http_lua_fetch_t   *fetch;

    c = ev->data;
    fetch = c->data;

    if (ev->timedout) {
        ev->timedout = 0;
        ngx_http_lua_fetch_fail(fetch, fetch->timeout_error != NULL
                                ? fetch->timeout_error
                                : "fetch timed out");
        ngx_http_lua_fetch_close_connection(fetch);
        ngx_http_lua_fetch_resume(fetch);
        return;
    }

    if (ev->write) {
        fetch->write_event_handler(fetch);

    } else {
        fetch->read_event_handler(fetch);
    }
}


static void
ngx_http_lua_fetch_connect_handler(ngx_http_lua_fetch_t *fetch)
{
    ngx_int_t          rc;
    ngx_connection_t  *c;

    c = fetch->peer.connection;
    if (c == NULL) {
        ngx_http_lua_fetch_fail(fetch, "fetch connect failed");
        ngx_http_lua_fetch_resume(fetch);
        return;
    }

    if (c->write->timer_set) {
        ngx_del_timer(c->write);
    }

    if (ngx_http_lua_fetch_test_connect(c) != NGX_OK) {
        ngx_http_lua_fetch_fail(fetch, "fetch connect failed");
        ngx_http_lua_fetch_close_connection(fetch);
        ngx_http_lua_fetch_resume(fetch);
        return;
    }

    rc = ngx_http_lua_fetch_connected(fetch);
    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_lua_fetch_resume(fetch);
}


static ngx_int_t
ngx_http_lua_fetch_connected(ngx_http_lua_fetch_t *fetch)
{
    ngx_log_error(NGX_LOG_NOTICE, fetch->r->connection->log, 0,
                  "fetch connected");

    return ngx_http_lua_fetch_send_request(fetch);
}


static ngx_int_t
ngx_http_lua_fetch_send_request(ngx_http_lua_fetch_t *fetch)
{
    size_t             size;
    ssize_t            n;
    ngx_connection_t  *c;

    c = fetch->peer.connection;
    if (c == NULL) {
        ngx_http_lua_fetch_fail(fetch, "fetch request send failed");
        return NGX_ERROR;
    }

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
            fetch->write_event_handler =
                                      ngx_http_lua_fetch_send_request_handler;
            fetch->timeout_error = "fetch request send timed out";
            ngx_add_timer(c->write, NGX_HTTP_LUA_FETCH_SEND_TIMEOUT);

            if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
                ngx_http_lua_fetch_fail(fetch, "fetch request send failed");
                ngx_http_lua_fetch_close_connection(fetch);
                return NGX_ERROR;
            }

            return NGX_AGAIN;
        }

        ngx_http_lua_fetch_fail(fetch, "fetch request send failed");
        ngx_http_lua_fetch_close_connection(fetch);
        return NGX_ERROR;
    }

    if (c->write->timer_set) {
        ngx_del_timer(c->write);
    }

    fetch->write_event_handler = ngx_http_lua_fetch_dummy_handler;
    fetch->read_event_handler = ngx_http_lua_fetch_process_header_handler;
    fetch->timeout_error = "fetch response header read timed out";
    ngx_add_timer(c->read, NGX_HTTP_LUA_FETCH_READ_TIMEOUT);

    if (c->read->ready) {
        return ngx_http_lua_fetch_process_header(fetch);
    }

    if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
        ngx_http_lua_fetch_fail(fetch, "fetch response header read failed");
        ngx_http_lua_fetch_close_connection(fetch);
        return NGX_ERROR;
    }

    return NGX_AGAIN;
}


static void
ngx_http_lua_fetch_send_request_handler(ngx_http_lua_fetch_t *fetch)
{
    ngx_int_t  rc;

    rc = ngx_http_lua_fetch_send_request(fetch);
    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_lua_fetch_resume(fetch);
}


static ngx_int_t
ngx_http_lua_fetch_process_header(ngx_http_lua_fetch_t *fetch)
{
    ssize_t            n;
    ngx_buf_t         *b;
    ngx_connection_t  *c;

    c = fetch->peer.connection;
    if (c == NULL) {
        ngx_http_lua_fetch_fail(fetch, "fetch response header read failed");
        return NGX_ERROR;
    }

    b = fetch->read_buf;

    for ( ;; ) {
        if (ngx_http_lua_fetch_header_end(b) != NULL) {
            ngx_log_error(NGX_LOG_NOTICE, fetch->r->connection->log, 0,
                          "fetch response header received");

            ngx_http_lua_fetch_close_connection(fetch);
            return ngx_http_lua_fetch_finish(fetch);
        }

        if (b->last == b->end) {
            ngx_http_lua_fetch_fail(fetch, "fetch response header too large");
            ngx_http_lua_fetch_close_connection(fetch);
            return NGX_ERROR;
        }

        n = c->recv(c, b->last, b->end - b->last);

        if (n == NGX_AGAIN) {
            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                ngx_http_lua_fetch_fail(fetch,
                                        "fetch response header read failed");
                ngx_http_lua_fetch_close_connection(fetch);
                return NGX_ERROR;
            }

            return NGX_AGAIN;
        }

        if (n == 0) {
            ngx_http_lua_fetch_fail(fetch,
                                    "fetch upstream closed before response");
            ngx_http_lua_fetch_close_connection(fetch);
            return NGX_ERROR;
        }

        if (n == NGX_ERROR) {
            ngx_http_lua_fetch_fail(fetch, "fetch response header read failed");
            ngx_http_lua_fetch_close_connection(fetch);
            return NGX_ERROR;
        }

        b->last += n;
    }
}


static void
ngx_http_lua_fetch_process_header_handler(ngx_http_lua_fetch_t *fetch)
{
    ngx_int_t  rc;

    rc = ngx_http_lua_fetch_process_header(fetch);
    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_lua_fetch_resume(fetch);
}


static void
ngx_http_lua_fetch_dummy_handler(ngx_http_lua_fetch_t *fetch)
{
    (void) fetch;
}


static ngx_int_t
ngx_http_lua_fetch_finish(ngx_http_lua_fetch_t *fetch)
{
    ngx_connection_t  *c;

    c = fetch->peer.connection;
    if (c != NULL) {
        ngx_http_lua_fetch_close_connection(fetch);
    }

    ngx_http_lua_fetch_print_request(fetch->request);

    if (fetch->request->body == NULL) {
        fprintf(stderr, "  body = nil\n");
    }

    fprintf(stderr, "}\n");
    fflush(stderr);

    return NGX_OK;
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


static int
ngx_http_lua_fetch_push_result(lua_State *L, ngx_http_lua_fetch_t *fetch)
{
    if (fetch->failed) {
        lua_pushnil(L);
        lua_pushlstring(L, (const char *) fetch->error.data,
                        fetch->error.len);
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}


static void
ngx_http_lua_fetch_fail(ngx_http_lua_fetch_t *fetch, const char *message)
{
    fetch->failed = 1;
    fetch->error.data = (u_char *) message;
    fetch->error.len = ngx_strlen(message);
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


static void
ngx_http_lua_fetch_close_connection(ngx_http_lua_fetch_t *fetch)
{
    ngx_connection_t  *c;

    c = fetch->peer.connection;
    if (c == NULL) {
        return;
    }

    fetch->peer.connection = NULL;
    ngx_close_connection(c);
}


static void
ngx_http_lua_fetch_cleanup(void *data)
{
    ngx_http_lua_fetch_t  *fetch = data;

    ngx_http_lua_fetch_close_connection(fetch);
}


static void
ngx_http_lua_fetch_print_request(ngx_lua_web_request_t *request)
{
    size_t                 i, n;
    ngx_str_t              name, value;
    ngx_lua_web_headers_t *headers;

    fprintf(stderr, "fetch Request {\n");

    fprintf(stderr, "  url = ");
    ngx_http_lua_fetch_print_string((const char *) request->url.data,
                                    request->url.len);
    fprintf(stderr, "\n");

    fprintf(stderr, "  method = ");
    ngx_http_lua_fetch_print_string((const char *) request->method.data,
                                    request->method.len);
    fprintf(stderr, "\n");

    fprintf(stderr, "  headers = {\n");

    headers = request->headers;
    n = ngx_lua_web_headers_count(headers);

    for (i = 0; i < n; i++) {
        if (ngx_lua_web_headers_get_entry(headers, i, &name, &value)
            != NGX_OK)
        {
            continue;
        }

        fprintf(stderr, "    ");
        ngx_http_lua_fetch_print_string((const char *) name.data, name.len);
        fprintf(stderr, " = ");
        ngx_http_lua_fetch_print_string((const char *) value.data, value.len);
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "  }\n");

    if (request->body != NULL) {
        fprintf(stderr, "  body = ReadableStream\n");
    }

    fflush(stderr);
}


static void
ngx_http_lua_fetch_print_string(const char *data, size_t len)
{
    size_t         i;
    unsigned char  ch;

    fprintf(stderr, "\"");

    for (i = 0; i < len; i++) {
        ch = (unsigned char) data[i];

        switch (ch) {
        case '\\':
            fprintf(stderr, "\\\\");
            break;

        case '"':
            fprintf(stderr, "\\\"");
            break;

        case '\n':
            fprintf(stderr, "\\n");
            break;

        case '\r':
            fprintf(stderr, "\\r");
            break;

        case '\t':
            fprintf(stderr, "\\t");
            break;

        default:
            if (ch >= 0x20 && ch < 0x7f) {
                fputc(ch, stderr);

            } else {
                fprintf(stderr, "\\x%02X", ch);
            }
        }
    }

    fprintf(stderr, "\"");
}
