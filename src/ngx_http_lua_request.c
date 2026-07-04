/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_http_lua.h"


static ngx_int_t ngx_http_lua_request_body_source_pull(
    ngx_lua_web_stream_t *stream, ngx_lua_web_stream_source_t *source);
static ngx_int_t ngx_http_lua_request_body_read(ngx_http_request_t *r,
    ngx_lua_web_stream_t *stream);
static void ngx_http_lua_request_body_read_handler(ngx_http_request_t *r);


ngx_lua_web_stream_t *
ngx_http_lua_request_body_stream_create(ngx_http_request_t *r)
{
    int                          top;
    ngx_http_lua_ctx_t          *ctx;
    ngx_lua_web_stream_t        *stream;
    ngx_lua_web_stream_source_t *source;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    if (ctx == NULL || ctx->co == NULL) {
        return NULL;
    }

    top = lua_gettop(ctx->co);

    stream = ngx_lua_web_stream_create(ctx->co);
    if (stream == NULL) {
        return NULL;
    }

    source = ngx_pcalloc(r->pool, sizeof(ngx_lua_web_stream_source_t));
    if (source == NULL) {
        lua_settop(ctx->co, top);
        return NULL;
    }

    source->pull = ngx_http_lua_request_body_source_pull;
    source->data = r;

    ngx_lua_web_stream_set_source(stream, source);

    return stream;
}


static ngx_int_t
ngx_http_lua_request_body_source_pull(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source)
{
    return ngx_http_lua_request_body_read(source->data, stream);
}


static ngx_int_t
ngx_http_lua_request_body_read(ngx_http_request_t *r,
    ngx_lua_web_stream_t *stream)
{
    ngx_int_t  rc;

    if (r->request_body->bufs != NULL) {
        ngx_lua_web_stream_enqueue_bufs(stream, r->request_body->bufs);
        r->request_body->bufs = NULL;
        return NGX_OK;
    }

    if (!r->reading_body) {
        return NGX_DONE;
    }

    rc = ngx_http_read_unbuffered_request_body(r);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return NGX_ERROR;
    }

    if (r->request_body->bufs != NULL) {
        ngx_lua_web_stream_enqueue_bufs(stream, r->request_body->bufs);
        r->request_body->bufs = NULL;
        return NGX_OK;
    }

    if (rc == NGX_OK) {
        return NGX_DONE;
    }

    r->read_event_handler = ngx_http_lua_request_body_read_handler;

    return NGX_AGAIN;
}


static void
ngx_http_lua_request_body_read_handler(ngx_http_request_t *r)
{
    ngx_int_t            rc;
    ngx_http_lua_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    r->read_event_handler = ngx_http_block_reading;

    rc = ngx_http_lua_request_body_read(r, ctx->request_body);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_lua_web_stream_wake(ctx->request_body);
}
