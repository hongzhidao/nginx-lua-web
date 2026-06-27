/*
 * Copyright (C) Zhidao HONG
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <lua.h>

#include "ngx_lua.h"
#include "ngx_lua_web.h"
#include "ngx_http_lua.h"


static ngx_int_t ngx_http_lua_request_body_source_start(
    ngx_lua_web_stream_t *stream, void *data);
static ngx_int_t ngx_http_lua_request_body_source_pull(
    ngx_lua_web_stream_t *stream, void *data);
static void ngx_http_lua_request_read_handler(ngx_http_request_t *r);
static ngx_uint_t ngx_http_lua_request_body_source_enqueue(
    ngx_http_request_t *r, ngx_lua_web_stream_t *stream);


ngx_lua_web_stream_source_t *
ngx_http_lua_request_body_source_create(lua_State *L, ngx_http_request_t *r)
{
    ngx_lua_ctx_t                *ctx;
    ngx_lua_web_stream_source_t  *source;

    if (L == NULL || r == NULL) {
        return NULL;
    }

    ctx = ngx_lua_get_ctx(L);
    if (ctx == NULL) {
        return NULL;
    }

    source = ngx_lua_pcalloc(ctx->pool, sizeof(ngx_lua_web_stream_source_t));
    if (source == NULL) {
        return NULL;
    }

    source->data = r;
    source->start = ngx_http_lua_request_body_source_start;
    source->pull = ngx_http_lua_request_body_source_pull;

    return source;
}


static ngx_int_t
ngx_http_lua_request_body_source_start(ngx_lua_web_stream_t *stream,
    void *data)
{
    ngx_http_request_t  *r;

    r = data;

    ngx_http_lua_request_body_source_enqueue(r, stream);

    if (!r->reading_body) {
        ngx_lua_web_stream_close(stream);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_request_body_source_pull(ngx_lua_web_stream_t *stream,
    void *data)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r;

    r = data;

    if (ngx_http_lua_request_body_source_enqueue(r, stream)) {
        return NGX_OK;
    }

    if (!r->reading_body) {
        ngx_lua_web_stream_close(stream);
        return NGX_OK;
    }

    rc = ngx_http_read_unbuffered_request_body(r);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    if (ngx_http_lua_request_body_source_enqueue(r, stream)) {
        return NGX_OK;
    }

    if (rc == NGX_OK && !r->reading_body) {
        ngx_lua_web_stream_close(stream);
    }

    if (rc == NGX_AGAIN) {
        r->read_event_handler = ngx_http_lua_request_read_handler;
    }

    return rc;
}


static void
ngx_http_lua_request_read_handler(ngx_http_request_t *r)
{
    ngx_int_t              rc;
    ngx_http_lua_ctx_t   *ctx;
    ngx_lua_web_stream_t  *stream;

    if (r->connection->read->timedout) {
        r->connection->timedout = 1;
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    stream = ctx->request_body;

    rc = ngx_lua_web_stream_pull_source(stream);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ngx_http_finalize_request(r, rc);
        return;
    }

    if (ngx_lua_web_stream_has_pending(stream)
        || ngx_lua_web_stream_is_closed(stream))
    {
        ngx_lua_web_stream_wake(stream);
    }
}


static ngx_uint_t
ngx_http_lua_request_body_source_enqueue(ngx_http_request_t *r,
    ngx_lua_web_stream_t *stream)
{
    ngx_chain_t  *in;

    if (r == NULL || r->request_body == NULL || r->request_body->bufs == NULL) {
        return 0;
    }

    in = r->request_body->bufs;
    r->request_body->bufs = NULL;

    ngx_lua_web_stream_enqueue(stream, in);

    return 1;
}
