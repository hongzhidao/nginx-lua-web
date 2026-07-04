/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_http_lua.h"


typedef struct {
    lua_State                *main;
    lua_State                *co;
    int                       app_ref;
    int                       co_ref;
} ngx_http_lua_ctx_t;


extern ngx_module_t  ngx_http_lua_module;


static ngx_int_t ngx_http_lua_request_body_source_pull(
    ngx_lua_web_stream_t *stream, ngx_lua_web_stream_source_t *source);


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
    (void) stream;
    (void) source;

    return NGX_OK;
}
