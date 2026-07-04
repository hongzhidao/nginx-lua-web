/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_http_lua.h"


static ngx_int_t ngx_http_lua_send_response_chunk(ngx_http_request_t *r,
    ngx_str_t *chunk);
static ngx_int_t ngx_http_lua_send_response_last(ngx_http_request_t *r);
static void ngx_http_lua_response_stream_wake(void *data);


ngx_int_t
ngx_http_lua_send_response(ngx_http_request_t *r, ngx_http_lua_ctx_t *ctx,
    ngx_uint_t status, ngx_lua_web_stream_t *stream)
{
    ngx_int_t  rc;
    ngx_str_t  chunk;

    r->headers_out.status = status;
    ngx_str_set(&r->headers_out.content_type, "text/plain");

    if (stream == NULL) {
        r->headers_out.content_length_n = 0;
        return ngx_http_send_header(r);
    }

    ctx->response_status = status;
    ctx->response_stream = stream;

    if (!ctx->response_header_sent) {
        r->headers_out.content_length_n = -1;
        rc = ngx_http_send_header(r);
        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only
            || status == NGX_HTTP_NO_CONTENT
            || status == NGX_HTTP_NOT_MODIFIED)
        {
            return rc;
        }

        ctx->response_header_sent = 1;
    }

    for ( ;; ) {
        rc = ngx_lua_web_stream_read(stream, r->pool, &chunk);

        if (rc == NGX_OK) {
            rc = ngx_http_lua_send_response_chunk(r, &chunk);
            if (rc != NGX_OK) {
                return rc;
            }

            continue;
        }

        if (rc == NGX_DONE) {
            return ngx_http_lua_send_response_last(r);
        }

        if (rc == NGX_AGAIN) {
            ngx_lua_web_stream_wait(stream, ngx_http_lua_response_stream_wake,
                                    r);
            return NGX_AGAIN;
        }

        return NGX_ERROR;
    }
}


static ngx_int_t
ngx_http_lua_send_response_chunk(ngx_http_request_t *r, ngx_str_t *chunk)
{
    ngx_buf_t    *b;
    ngx_chain_t   out;

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }

    b->pos = chunk->data;
    b->last = chunk->data + chunk->len;
    b->memory = 1;
    b->flush = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}


static ngx_int_t
ngx_http_lua_send_response_last(ngx_http_request_t *r)
{
    ngx_buf_t    *b;
    ngx_chain_t   out;

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }

    b->last_buf = (r == r->main) ? 1 : 0;
    b->last_in_chain = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}


static void
ngx_http_lua_response_stream_wake(void *data)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r = data;
    ngx_http_lua_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    rc = ngx_http_lua_send_response(r, ctx, ctx->response_status,
                                    ctx->response_stream);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
}
