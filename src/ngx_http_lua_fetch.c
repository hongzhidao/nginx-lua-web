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
    /* Core fetch state. */
    ngx_lua_ctx_t              *ctx;
    ngx_http_request_t         *r;
    ngx_lua_web_request_t      *request;
    ngx_lua_web_response_t     *response;
    ngx_pool_t                 *pool;
    ngx_peer_connection_t       peer;
    ngx_sockaddr_t              sockaddr;
    ngx_str_t                   peer_name;

    /* Buffers and errors. */
    ngx_buf_t                  *write_buf;
    ngx_buf_t                  *read_buf;
    ngx_str_t                   error;

    /* Response body source state. */
    ngx_lua_web_stream_t       *response_body;
    ngx_http_chunked_t          chunked;
    off_t                       content_length_n;
    off_t                       body_read;
    unsigned                    chunked_body:1;
    unsigned                    no_body:1;
    unsigned                    body_done:1;

    /* Request/connection state. */
    unsigned                    request_sent:1;
    unsigned                    request_body_sent:1;
    unsigned                    failed:1;
};


static int ngx_http_lua_fetch(lua_State *L);
static int ngx_http_lua_fetch_continue(lua_State *L, int status,
    lua_KContext ctx);
static void ngx_http_lua_fetch_resume(ngx_http_lua_fetch_t *fetch);
static int ngx_http_lua_fetch_push_result(lua_State *L,
    ngx_http_lua_fetch_t *fetch);

static ngx_lua_web_request_t *ngx_http_lua_fetch_normalize_request(
    lua_State *L);
static ngx_http_lua_fetch_t *ngx_http_lua_fetch_create(lua_State *L,
    ngx_lua_web_request_t *request);

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

static ngx_int_t ngx_http_lua_fetch_init_body(lua_State *L,
    ngx_http_lua_fetch_t *fetch, int response_index,
    ngx_lua_web_response_t *response, u_char *header_end);
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
static void ngx_http_lua_fetch_process_body_handler(ngx_event_t *ev);

static void ngx_http_lua_fetch_fail(ngx_http_lua_fetch_t *fetch,
    const char *message);
static void ngx_http_lua_fetch_free_peer(ngx_http_lua_fetch_t *fetch,
    ngx_uint_t close);
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
    ngx_int_t                rc;
    ngx_http_lua_fetch_t    *fetch;
    ngx_lua_web_request_t   *request;

    request = ngx_http_lua_fetch_normalize_request(L);

    lua_replace(L, 1);
    lua_settop(L, 1);

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


static ngx_lua_web_request_t *
ngx_http_lua_fetch_normalize_request(lua_State *L)
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
    fetch->response = NULL;
    fetch->response_body = NULL;
    fetch->pool = ctx->pool;
    fetch->content_length_n = -1;
    fetch->body_read = 0;
    fetch->request_sent = 0;
    fetch->request_body_sent = 0;
    fetch->chunked_body = 0;
    fetch->no_body = 0;
    fetch->body_done = 0;
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

    if (ngx_http_lua_fetch_create_request(fetch) != NGX_OK) {
        (void) luaL_error(L, "no memory");
        return NULL;
    }

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


static ngx_int_t
ngx_http_lua_fetch_create_request(ngx_http_lua_fetch_t *fetch)
{
    size_t                 len;
    ngx_buf_t             *b;
    ngx_lua_web_request_t *request;

    request = fetch->request;

    len = request->method.len
          + sizeof(" /fetch-upstream HTTP/1.1" CRLF) - 1
          + sizeof("Host: localhost" CRLF) - 1
          + sizeof("Connection: close" CRLF) - 1
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
    b->last = ngx_cpymem(b->last, " /fetch-upstream HTTP/1.1" CRLF,
                         sizeof(" /fetch-upstream HTTP/1.1" CRLF) - 1);
    b->last = ngx_cpymem(b->last, "Host: localhost" CRLF,
                         sizeof("Host: localhost" CRLF) - 1);
    b->last = ngx_cpymem(b->last, "Connection: close" CRLF,
                         sizeof("Connection: close" CRLF) - 1);

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

    if (fetch->failed) {
        return NGX_ERROR;
    }

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

    if (ngx_http_lua_fetch_init_body(L, fetch, response_index, response,
                                     header_end)
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

        p = line_end + 1;
    }

    if (status == NGX_HTTP_NO_CONTENT
        || status == NGX_HTTP_LUA_FETCH_RESET_CONTENT
        || status == NGX_HTTP_NOT_MODIFIED
        || fetch->content_length_n == 0)
    {
        fetch->no_body = 1;
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


static ngx_int_t
ngx_http_lua_fetch_init_body(lua_State *L, ngx_http_lua_fetch_t *fetch,
    int response_index, ngx_lua_web_response_t *response, u_char *header_end)
{
    ngx_lua_web_stream_t         *body;
    ngx_lua_web_stream_source_t  *source;

    fetch->read_buf->pos = header_end;

    if (fetch->no_body) {
        ngx_http_lua_fetch_free_peer(fetch, 1);
        return NGX_OK;
    }

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
    ngx_connection_t      *c;

    fetch = source->data;

    rc = ngx_http_lua_fetch_read_body(fetch, stream);
    if (rc == NGX_DONE) {
        ngx_lua_web_stream_close(stream);
        ngx_http_lua_fetch_free_peer(fetch, 1);
        return NGX_OK;
    }

    if (rc == NGX_ERROR) {
        ngx_lua_web_stream_error(stream);
        ngx_http_lua_fetch_free_peer(fetch, 1);
        return NGX_ERROR;
    }

    if (rc != NGX_AGAIN) {
        return rc;
    }

    c = fetch->peer.connection;
    c->read->handler = ngx_http_lua_fetch_process_body_handler;
    ngx_add_timer(c->read, NGX_HTTP_LUA_FETCH_READ_TIMEOUT);

    if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
        ngx_lua_web_stream_error(stream);
        ngx_http_lua_fetch_free_peer(fetch, 1);
        return NGX_ERROR;
    }

    return NGX_AGAIN;
}


static ngx_int_t
ngx_http_lua_fetch_read_body(ngx_http_lua_fetch_t *fetch,
    ngx_lua_web_stream_t *stream)
{
    if (fetch->body_done) {
        return NGX_DONE;
    }

    if (fetch->no_body) {
        fetch->body_done = 1;
        return NGX_DONE;
    }

    if (fetch->chunked_body) {
        return ngx_http_lua_fetch_read_chunked_body(fetch, stream);
    }

    return ngx_http_lua_fetch_read_plain_body(fetch, stream);
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
            fetch->body_done = 1;
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

            if (ngx_lua_web_stream_enqueue_string(stream, fetch->pool, b->pos,
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
                fetch->body_done = 1;
            }

            return NGX_OK;
        }

        rc = ngx_http_lua_fetch_recv_body(fetch);

        if (rc == NGX_OK) {
            continue;
        }

        if (rc == NGX_DONE) {
            fetch->body_done = 1;
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

            if (ngx_lua_web_stream_enqueue_string(stream, fetch->pool, b->pos,
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
            fetch->body_done = 1;
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
        ngx_http_lua_fetch_free_peer(fetch, 1);
        ngx_lua_web_stream_wake(body);
        return;
    }

    rc = ngx_http_lua_fetch_read_body(fetch, body);
    if (rc == NGX_AGAIN) {
        return;
    }

    if (rc == NGX_DONE) {
        ngx_lua_web_stream_close(body);
        ngx_http_lua_fetch_free_peer(fetch, 1);

    } else if (rc == NGX_ERROR) {
        ngx_lua_web_stream_error(body);
        ngx_http_lua_fetch_free_peer(fetch, 1);
    }

    ngx_lua_web_stream_wake(body);
}


static void
ngx_http_lua_fetch_fail(ngx_http_lua_fetch_t *fetch, const char *message)
{
    fetch->failed = 1;
    fetch->error.data = (u_char *) message;
    fetch->error.len = ngx_strlen(message);
    ngx_http_lua_fetch_free_peer(fetch, 1);
}


static void
ngx_http_lua_fetch_free_peer(ngx_http_lua_fetch_t *fetch, ngx_uint_t close)
{
    ngx_connection_t  *c;

    c = fetch->peer.connection;
    if (c == NULL) {
        return;
    }

    fetch->peer.connection = NULL;

    (void) close;

    ngx_close_connection(c);
}


static void
ngx_http_lua_fetch_cleanup(void *data)
{
    ngx_http_lua_fetch_t  *fetch = data;

    ngx_http_lua_fetch_free_peer(fetch, 1);
}
