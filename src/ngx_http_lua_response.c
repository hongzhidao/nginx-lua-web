/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_http_lua.h"


static ngx_int_t ngx_http_lua_send_response_chunk(ngx_http_request_t *r,
    ngx_chain_t *chunk);
static void ngx_http_lua_free_response_chunk(ngx_http_request_t *r,
    ngx_chain_t *chunk);
static ngx_int_t ngx_http_lua_set_response_headers(ngx_http_request_t *r,
    ngx_lua_web_response_t *response);
static ngx_int_t ngx_http_lua_copy_response_header(ngx_pool_t *pool,
    ngx_str_t *dst, ngx_str_t *src);
static ngx_uint_t ngx_http_lua_response_header_is(ngx_str_t *name,
    const char *value, size_t len);
static ngx_int_t ngx_http_lua_send_response_last(ngx_http_request_t *r);
static ngx_uint_t ngx_http_lua_response_output_pending(
    ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_response_wait_write(ngx_http_request_t *r);
static void ngx_http_lua_response_write_handler(ngx_http_request_t *r);
static void ngx_http_lua_response_stream_wake(void *data);


ngx_int_t
ngx_http_lua_send_response(ngx_http_request_t *r,
    ngx_lua_web_response_t *response)
{
    ngx_chain_t          *cl;
    ngx_int_t             rc;
    ngx_lua_web_stream_t *stream;

    stream = response->body;

    if (stream == NULL) {
        if (!r->header_sent) {
            if (ngx_http_lua_set_response_headers(r, response) != NGX_OK) {
                return NGX_ERROR;
            }

            r->headers_out.content_length_n = 0;
            rc = ngx_http_send_header(r);
            if (rc != NGX_OK && rc != NGX_AGAIN) {
                return rc;
            }

            if (rc == NGX_AGAIN
                || ngx_http_lua_response_output_pending(r))
            {
                return ngx_http_lua_response_wait_write(r);
            }

            return NGX_OK;
        }

        return ngx_http_lua_response_output_pending(r)
               ? ngx_http_lua_response_wait_write(r) : NGX_OK;
    }

    if (!r->header_sent) {
        if (ngx_http_lua_set_response_headers(r, response) != NGX_OK) {
            return NGX_ERROR;
        }

        r->headers_out.content_length_n = -1;
        rc = ngx_http_send_header(r);
        if (rc != NGX_OK && rc != NGX_AGAIN) {
            return rc;
        }

        if (rc == NGX_AGAIN
            || ngx_http_lua_response_output_pending(r))
        {
            return ngx_http_lua_response_wait_write(r);
        }
    }

    if (r->header_only || response->status == NGX_HTTP_NO_CONTENT
        || response->status == NGX_HTTP_NOT_MODIFIED)
    {
        return ngx_http_lua_response_output_pending(r)
               ? ngx_http_lua_response_wait_write(r) : NGX_OK;
    }

    for ( ;; ) {
        cl = NULL;
        rc = ngx_lua_web_stream_dequeue_chunk(stream, &cl);

        if (rc == NGX_OK) {
            if (cl == NULL) {
                return NGX_ERROR;
            }

            rc = ngx_http_lua_send_response_chunk(r, cl);
            if (rc == NGX_DECLINED) {
                continue;
            }

            if (rc != NGX_OK && rc != NGX_AGAIN) {
                return rc;
            }

            if (rc == NGX_AGAIN
                || ngx_http_lua_response_output_pending(r))
            {
                return ngx_http_lua_response_wait_write(r);
            }

            continue;
        }

        if (rc == NGX_DONE) {
            rc = ngx_http_lua_send_response_last(r);
            if (rc != NGX_OK && rc != NGX_AGAIN) {
                return rc;
            }

            if (rc == NGX_AGAIN
                || ngx_http_lua_response_output_pending(r))
            {
                return ngx_http_lua_response_wait_write(r);
            }

            return NGX_OK;
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
ngx_http_lua_send_response_chunk(ngx_http_request_t *r, ngx_chain_t *chunk)
{
    off_t         size;
    ngx_buf_t    *b, *last_buf;
    ngx_chain_t  *cl, *next, *out, **last;
    ngx_int_t     rc;
    ngx_uint_t    has_data;

    has_data = 0;

    for (cl = chunk; cl != NULL; cl = cl->next) {
        b = cl->buf;

        if (b == NULL) {
            ngx_http_lua_free_response_chunk(r, chunk);
            return NGX_ERROR;
        }

        if (ngx_buf_special(b)) {
            continue;
        }

        size = ngx_buf_size(b);

        if (size < 0 || (size > 0 && !ngx_buf_in_memory(b))) {
            ngx_http_lua_free_response_chunk(r, chunk);
            return NGX_ERROR;
        }

        if (size > 0) {
            has_data = 1;
        }
    }

    if (!has_data) {
        ngx_http_lua_free_response_chunk(r, chunk);
        return NGX_DECLINED;
    }

    out = NULL;
    last = &out;
    last_buf = NULL;

    for (cl = chunk; cl != NULL; cl = next) {
        next = cl->next;
        cl->next = NULL;
        b = cl->buf;

        if (ngx_buf_special(b) || ngx_buf_size(b) == 0) {
            ngx_free_chain(r->pool, cl);
            continue;
        }

        b->flush = 0;
        b->last_buf = 0;
        b->last_in_chain = 0;

        *last = cl;
        last = &cl->next;
        last_buf = b;
    }

    last_buf->flush = 1;
    rc = ngx_http_output_filter(r, out);
    ngx_http_lua_free_response_chunk(r, out);

    return rc;
}


static void
ngx_http_lua_free_response_chunk(ngx_http_request_t *r, ngx_chain_t *chunk)
{
    ngx_chain_t  *next;

    while (chunk != NULL) {
        next = chunk->next;
        ngx_free_chain(r->pool, chunk);
        chunk = next;
    }
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


static ngx_uint_t
ngx_http_lua_response_output_pending(ngx_http_request_t *r)
{
    ngx_connection_t  *c;

    c = r->connection;

    return r->buffered || r->postponed
           || (r == r->main && c->buffered);
}


static ngx_int_t
ngx_http_lua_response_wait_write(ngx_http_request_t *r)
{
    ngx_event_t               *wev;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    wev = c->write;

    r->http_state = NGX_HTTP_WRITING_REQUEST_STATE;
    r->read_event_handler = r->discard_body
                            ? ngx_http_discarded_request_body_handler
                            : ngx_http_test_reading;
    r->write_event_handler = ngx_http_lua_response_write_handler;

    if (wev->ready && wev->delayed) {
        return NGX_AGAIN;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (!wev->delayed) {
        ngx_add_timer(wev, clcf->send_timeout);
    }

    if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_AGAIN;
}


static void
ngx_http_lua_response_write_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_event_t               *wev;
    ngx_connection_t          *c;
    ngx_http_lua_ctx_t        *ctx;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    wev = c->write;
    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    if (ctx == NULL || ctx->response == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    clcf = ngx_http_get_module_loc_conf(r->main, ngx_http_core_module);

    if (wev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT,
                      "client timed out while sending Lua response");
        c->timedout = 1;
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }

    if (wev->delayed || r->aio) {
        if (!wev->delayed) {
            ngx_add_timer(wev, clcf->send_timeout);
        }

        if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_ERROR);
        }

        return;
    }

    rc = ngx_http_output_filter(r, NULL);

    if (rc != NGX_OK && rc != NGX_AGAIN) {
        ngx_http_finalize_request(r, rc);
        return;
    }

    if (rc == NGX_AGAIN || ngx_http_lua_response_output_pending(r)) {
        if (ngx_http_lua_response_wait_write(r) == NGX_ERROR) {
            ngx_http_finalize_request(r, NGX_ERROR);
        }

        return;
    }

    if (wev->timer_set) {
        ngx_del_timer(wev);
    }

    r->write_event_handler = ngx_http_request_empty_handler;

    if (r->response_sent) {
        ngx_http_finalize_request(r, NGX_OK);
        return;
    }

    rc = ngx_http_lua_send_response(r, ctx->response);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
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
