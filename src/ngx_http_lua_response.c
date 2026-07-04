/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_http_lua.h"


static ngx_int_t ngx_http_lua_send_response_chunk(ngx_http_request_t *r,
    ngx_str_t *chunk);
static ngx_int_t ngx_http_lua_set_response_headers(ngx_http_request_t *r,
    ngx_lua_web_response_t *response);
static ngx_int_t ngx_http_lua_copy_response_header(ngx_pool_t *pool,
    ngx_str_t *dst, ngx_str_t *src);
static ngx_uint_t ngx_http_lua_response_header_is(ngx_str_t *name,
    const char *value, size_t len);
static ngx_int_t ngx_http_lua_send_response_last(ngx_http_request_t *r);
static void ngx_http_lua_response_stream_wake(void *data);


ngx_int_t
ngx_http_lua_send_response(ngx_http_request_t *r,
    ngx_lua_web_response_t *response)
{
    ngx_int_t             rc;
    ngx_str_t             chunk;
    ngx_lua_web_stream_t *stream;

    stream = response->body;

    if (stream == NULL) {
        if (!r->header_sent) {
            if (ngx_http_lua_set_response_headers(r, response) != NGX_OK) {
                return NGX_ERROR;
            }

            r->headers_out.content_length_n = 0;
            return ngx_http_send_header(r);
        }

        return NGX_OK;
    }

    if (!r->header_sent) {
        if (ngx_http_lua_set_response_headers(r, response) != NGX_OK) {
            return NGX_ERROR;
        }

        r->headers_out.content_length_n = -1;
        rc = ngx_http_send_header(r);
        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only
            || response->status == NGX_HTTP_NO_CONTENT
            || response->status == NGX_HTTP_NOT_MODIFIED)
        {
            return rc;
        }
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
ngx_http_lua_set_response_headers(ngx_http_request_t *r,
    ngx_lua_web_response_t *response)
{
    size_t           i, n;
    ngx_str_t        name, value;
    ngx_table_elt_t *header;

    r->headers_out.status = response->status;

    n = ngx_lua_web_headers_count(response->headers);

    for (i = 0; i < n; i++) {
        if (ngx_lua_web_headers_get_entry(response->headers, i, &name, &value)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        if (ngx_http_lua_response_header_is(&name, "content-type",
                                            sizeof("content-type") - 1))
        {
            if (ngx_http_lua_copy_response_header(r->pool,
                                                  &r->headers_out.content_type,
                                                  &value)
                != NGX_OK)
            {
                return NGX_ERROR;
            }

            r->headers_out.content_type_len = value.len;
            r->headers_out.content_type_lowcase = NULL;
            continue;
        }

        if (ngx_http_lua_response_header_is(&name, "content-length",
                                            sizeof("content-length") - 1)
            || ngx_http_lua_response_header_is(&name, "transfer-encoding",
                                               sizeof("transfer-encoding") - 1))
        {
            continue;
        }

        header = ngx_list_push(&r->headers_out.headers);
        if (header == NULL) {
            return NGX_ERROR;
        }

        header->hash = 1;
        header->next = NULL;

        if (ngx_http_lua_copy_response_header(r->pool, &header->key, &name)
            != NGX_OK)
        {
            header->hash = 0;
            return NGX_ERROR;
        }

        if (ngx_http_lua_copy_response_header(r->pool, &header->value, &value)
            != NGX_OK)
        {
            header->hash = 0;
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_copy_response_header(ngx_pool_t *pool, ngx_str_t *dst,
    ngx_str_t *src)
{
    dst->len = src->len;
    dst->data = NULL;

    if (src->len == 0) {
        return NGX_OK;
    }

    dst->data = ngx_pnalloc(pool, src->len);
    if (dst->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(dst->data, src->data, src->len);

    return NGX_OK;
}


static ngx_uint_t
ngx_http_lua_response_header_is(ngx_str_t *name, const char *value,
    size_t len)
{
    if (name->len != len) {
        return 0;
    }

    return ngx_strncmp(name->data, value, len) == 0;
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
    rc = ngx_http_lua_send_response(r, ctx->response);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
}
