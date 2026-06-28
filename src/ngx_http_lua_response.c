/*
 * Copyright (C) Zhidao HONG
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <lua.h>

#include "ngx_lua_web.h"
#include "ngx_http_lua.h"


static void ngx_http_lua_response_continue(ngx_lua_web_stream_t *stream,
    void *data);
static void ngx_http_lua_response_write_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_response_output(ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_response_wait_writable(ngx_http_request_t *r);


ngx_int_t
ngx_http_lua_response_send(ngx_http_request_t *r)
{
    ngx_int_t  rc;

    r->write_event_handler = ngx_http_lua_response_write_handler;
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = -1;
    ngx_str_set(&r->headers_out.content_type, "text/plain");

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_lua_response_output(r);
}


static void
ngx_http_lua_response_continue(ngx_lua_web_stream_t *stream, void *data)
{
    ngx_http_request_t  *r = data;
    ngx_int_t            rc;

    (void) stream;

    rc = ngx_http_lua_response_output(r);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
}


static void
ngx_http_lua_response_write_handler(ngx_http_request_t *r)
{
    ngx_int_t          rc;
    ngx_connection_t  *c;
    ngx_event_t       *wev;

    c = r->connection;
    wev = c->write;

    if (wev->timedout) {
        c->timedout = 1;
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }

    if (wev->delayed || r->aio) {
        if (ngx_http_lua_response_wait_writable(r) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_ERROR);
        }

        return;
    }

    rc = ngx_http_output_filter(r, NULL);
    if (rc == NGX_ERROR) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (r->buffered || r->postponed || c->buffered) {
        if (ngx_http_lua_response_wait_writable(r) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_ERROR);
        }

        return;
    }

    rc = ngx_http_lua_response_output(r);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
}


static ngx_int_t
ngx_http_lua_response_output(ngx_http_request_t *r)
{
    ngx_chain_t          *out;
    ngx_int_t            rc;
    ngx_connection_t    *c;
    ngx_http_lua_ctx_t  *ctx;

    c = r->connection;
    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    for ( ;; ) {
        if (r->buffered || r->postponed || c->buffered) {
            return ngx_http_lua_response_wait_writable(r);
        }

        rc = ngx_lua_web_stream_take_chain(ctx->coroutine, ctx->response_body,
                                           &out);

        if (rc == NGX_OK) {
            rc = ngx_http_output_filter(r, out);
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }

            continue;
        }

        if (rc == NGX_DONE) {
            rc = ngx_http_send_special(r, NGX_HTTP_LAST);
            if (rc == NGX_AGAIN) {
                return ngx_http_lua_response_wait_writable(r);
            }

            return rc;
        }

        if (rc == NGX_AGAIN) {
            ngx_lua_web_stream_set_wake(ctx->response_body,
                                        ngx_http_lua_response_continue, r);
            return NGX_AGAIN;
        }

        return rc;
    }
}


static ngx_int_t
ngx_http_lua_response_wait_writable(ngx_http_request_t *r)
{
    ngx_event_t               *wev;
    ngx_http_core_loc_conf_t  *clcf;

    wev = r->connection->write;
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (!wev->delayed && !wev->timer_set) {
        ngx_add_timer(wev, clcf->send_timeout);
    }

    if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_AGAIN;
}
