/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_http_lua.h"


static ngx_int_t ngx_http_lua_request_body_source_pull(
    ngx_lua_web_stream_t *stream, ngx_lua_web_stream_source_t *source);
static ngx_lua_web_stream_t *ngx_http_lua_request_body_stream_create(
    ngx_http_request_t *r, ngx_http_lua_ctx_t *ctx);
static ngx_lua_web_stream_t *ngx_http_lua_request_body_get(
    ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_request_body_read(ngx_http_request_t *r,
    ngx_lua_web_stream_t *stream);
static void ngx_http_lua_request_body_read_handler(ngx_http_request_t *r);


ngx_lua_web_request_t *
ngx_http_lua_request_create(ngx_http_request_t *r, ngx_http_lua_ctx_t *ctx)
{
    int                         request_index, top;
    ngx_lua_web_request_t      *request;
    ngx_lua_web_stream_t       *body;

    if (ctx == NULL || ctx->co == NULL) {
        return NULL;
    }

    top = lua_gettop(ctx->co);

    request = ngx_lua_web_request_create(ctx->co);
    if (request == NULL) {
        lua_settop(ctx->co, top);
        return NULL;
    }

    request_index = lua_absindex(ctx->co, -1);

    if (ngx_lua_web_request_set_string(ctx->co, &request->method,
                                       (const char *) r->method_name.data,
                                       r->method_name.len)
        != NGX_OK)
    {
        lua_settop(ctx->co, top);
        return NULL;
    }

    if (ngx_lua_web_request_set_string(ctx->co, &request->url,
                                       (const char *) r->unparsed_uri.data,
                                       r->unparsed_uri.len)
        != NGX_OK)
    {
        lua_settop(ctx->co, top);
        return NULL;
    }

    body = ngx_http_lua_request_body_stream_create(r, ctx);
    if (body == NULL) {
        lua_settop(ctx->co, top);
        return NULL;
    }

    request->body = body;
    lua_setiuservalue(ctx->co, request_index, 2);

    return request;
}


static ngx_lua_web_stream_t *
ngx_http_lua_request_body_stream_create(ngx_http_request_t *r,
    ngx_http_lua_ctx_t *ctx)
{
    int                          top;
    ngx_lua_web_stream_t        *stream;
    ngx_lua_web_stream_source_t *source;

    top = lua_gettop(ctx->co);

    stream = ngx_lua_web_stream_create(ctx->co, r->pool);
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


static ngx_lua_web_stream_t *
ngx_http_lua_request_body_get(ngx_http_request_t *r)
{
    ngx_http_lua_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    if (ctx == NULL || ctx->request == NULL) {
        return NULL;
    }

    return ctx->request->body;
}


static ngx_int_t
ngx_http_lua_request_body_source_pull(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source)
{
    ngx_int_t  rc;

    rc = ngx_http_lua_request_body_read(source->data, stream);

    if (rc == NGX_DONE) {
        ngx_lua_web_stream_close(stream);
        return NGX_OK;
    }

    if (rc == NGX_ERROR) {
        ngx_lua_web_stream_error(stream);
    }

    return rc;
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
    ngx_int_t               rc;
    ngx_lua_web_stream_t   *body;

    body = ngx_http_lua_request_body_get(r);
    if (body == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    r->read_event_handler = ngx_http_block_reading;

    rc = ngx_http_lua_request_body_read(r, body);

    if (rc == NGX_AGAIN) {
        return;
    }

    if (rc == NGX_DONE) {
        ngx_lua_web_stream_close(body);

    } else if (rc == NGX_ERROR) {
        ngx_lua_web_stream_error(body);
    }

    ngx_lua_web_stream_wake(body);
}
