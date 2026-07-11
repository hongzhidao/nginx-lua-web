/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_http_lua.h"


static ngx_int_t ngx_http_lua_request_set_url(lua_State *L,
    ngx_lua_web_request_t *request, ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_request_set_headers(lua_State *L,
    ngx_lua_web_request_t *request, ngx_http_request_t *r);
static ngx_str_t ngx_http_lua_request_scheme(ngx_http_request_t *r);
static ngx_uint_t ngx_http_lua_request_default_port(ngx_str_t *scheme,
    in_port_t port);
static ngx_int_t ngx_http_lua_request_body_source_pull(
    ngx_lua_web_stream_t *stream, ngx_lua_web_stream_source_t *source);
static ngx_lua_web_stream_t *ngx_http_lua_request_body_stream_create(
    ngx_http_request_t *r, ngx_http_lua_ctx_t *ctx);
static ngx_lua_web_stream_t *ngx_http_lua_request_body_get(
    ngx_http_request_t *r);
static ngx_uint_t ngx_http_lua_request_has_body(ngx_http_request_t *r);
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

    if (ngx_http_lua_request_set_url(ctx->co, request, r) != NGX_OK) {
        lua_settop(ctx->co, top);
        return NULL;
    }

    if (ngx_http_lua_request_set_headers(ctx->co, request, r) != NGX_OK) {
        lua_settop(ctx->co, top);
        return NULL;
    }

    if (ngx_http_lua_request_has_body(r)) {
        body = ngx_http_lua_request_body_stream_create(r, ctx);
        if (body == NULL) {
            lua_settop(ctx->co, top);
            return NULL;
        }

        request->body = body;
        lua_setiuservalue(ctx->co, request_index, 2);
    }

    return request;
}


static ngx_int_t
ngx_http_lua_request_set_headers(lua_State *L,
    ngx_lua_web_request_t *request, ngx_http_request_t *r)
{
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_table_elt_t  *header;

    part = &r->headers_in.headers.part;
    header = part->elts;

    for (i = 0; /* void */; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            header = part->elts;
            i = 0;
        }

        if (header[i].hash == 0) {
            continue;
        }

        if (ngx_lua_web_headers_set_entry(
                L, request->headers, (const char *) header[i].key.data,
                header[i].key.len, (const char *) header[i].value.data,
                header[i].value.len)
            != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_request_set_url(lua_State *L, ngx_lua_web_request_t *request,
    ngx_http_request_t *r)
{
    size_t       len, port_len;
    u_char     *p, *url;
    u_char      addr[NGX_SOCKADDR_STRLEN];
    u_char      port_text[sizeof(":65535") - 1];
    ngx_str_t   host, scheme, uri;

    scheme = ngx_http_lua_request_scheme(r);
    port_len = 0;

    if (r->headers_in.server.len) {
        host = r->headers_in.server;

        if (r->port != 0
            && !ngx_http_lua_request_default_port(&scheme, r->port))
        {
            port_len = ngx_sprintf(port_text, ":%ui", (ngx_uint_t) r->port)
                       - port_text;
        }

    } else {
        host.len = NGX_SOCKADDR_STRLEN;
        host.data = addr;

        if (ngx_connection_local_sockaddr(r->connection, &host, 1)
            != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    uri = r->unparsed_uri;
    if (uri.len == 0) {
        ngx_str_set(&uri, "/");
    }

    len = scheme.len + sizeof("://") - 1 + host.len + port_len + uri.len;

    url = ngx_pnalloc(r->pool, len);
    if (url == NULL) {
        return NGX_ERROR;
    }

    p = ngx_cpymem(url, scheme.data, scheme.len);
    p = ngx_cpymem(p, "://", sizeof("://") - 1);
    p = ngx_cpymem(p, host.data, host.len);

    if (port_len != 0) {
        p = ngx_cpymem(p, port_text, port_len);
    }

    p = ngx_cpymem(p, uri.data, uri.len);

    return ngx_lua_web_request_set_string(L, &request->url,
                                          (const char *) url, p - url);
}


static ngx_str_t
ngx_http_lua_request_scheme(ngx_http_request_t *r)
{
    ngx_str_t  scheme;

    if (r->schema.len != 0) {
        return r->schema;
    }

#if (NGX_HTTP_SSL)

    if (r->connection->ssl) {
        ngx_str_set(&scheme, "https");
        return scheme;
    }

#endif

    ngx_str_set(&scheme, "http");
    return scheme;
}


static ngx_uint_t
ngx_http_lua_request_default_port(ngx_str_t *scheme, in_port_t port)
{
    if (scheme->len == sizeof("http") - 1
        && ngx_strncmp(scheme->data, "http", sizeof("http") - 1) == 0)
    {
        return port == 80;
    }

    if (scheme->len == sizeof("https") - 1
        && ngx_strncmp(scheme->data, "https", sizeof("https") - 1) == 0)
    {
        return port == 443;
    }

    return 0;
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


static ngx_uint_t
ngx_http_lua_request_has_body(ngx_http_request_t *r)
{
    return r->headers_in.chunked || r->headers_in.content_length_n > 0;
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
        if (ngx_lua_web_stream_enqueue_chunk(stream, r->request_body->bufs)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

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
        if (ngx_lua_web_stream_enqueue_chunk(stream, r->request_body->bufs)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

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
