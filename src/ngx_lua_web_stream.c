/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua_web.h"

#include <lauxlib.h>


#define NGX_LUA_WEB_STREAM_METATABLE  "ngx_lua_web.ReadableStream"


struct ngx_lua_web_stream_s {
    ngx_lua_web_stream_source_t  *source;
    ngx_chain_t                  *bufs;
    ngx_chain_t                 **last;
    ngx_lua_web_stream_wake_pt    wake;
    void                         *data;
    unsigned  closed:1;
    unsigned  errored:1;
};


static void ngx_lua_web_stream_set_metatable(lua_State *L);
static void ngx_lua_web_stream_pop_buf(ngx_lua_web_stream_t *stream,
    ngx_pool_t *pool);


static void
ngx_lua_web_stream_set_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_STREAM_METATABLE)) {
        lua_pushliteral(L, "ReadableStream");
        lua_setfield(L, -2, "__name");
    }

    lua_setmetatable(L, -2);
}


ngx_lua_web_stream_t *
ngx_lua_web_stream_create(lua_State *L)
{
    ngx_lua_web_stream_t  *stream;

    stream = lua_newuserdatauv(L, sizeof(ngx_lua_web_stream_t), 0);
    if (stream == NULL) {
        return NULL;
    }

    ngx_memzero(stream, sizeof(ngx_lua_web_stream_t));
    stream->last = &stream->bufs;

    ngx_lua_web_stream_set_metatable(L);

    return stream;
}


void
ngx_lua_web_stream_set_source(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source)
{
    stream->source = source;
}


void
ngx_lua_web_stream_enqueue_bufs(ngx_lua_web_stream_t *stream,
    ngx_chain_t *bufs)
{
    *stream->last = bufs;

    while (*stream->last) {
        stream->last = &(*stream->last)->next;
    }
}


ngx_int_t
ngx_lua_web_stream_enqueue_string(ngx_lua_web_stream_t *stream,
    ngx_pool_t *pool, u_char *data, size_t len)
{
    ngx_buf_t    *b;
    ngx_chain_t  *cl;

    if (len == 0) {
        return NGX_OK;
    }

    cl = ngx_alloc_chain_link(pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    b = ngx_create_temp_buf(pool, len);
    if (b == NULL) {
        ngx_free_chain(pool, cl);
        return NGX_ERROR;
    }

    ngx_memcpy(b->last, data, len);
    b->last += len;

    cl->buf = b;
    cl->next = NULL;

    ngx_lua_web_stream_enqueue_bufs(stream, cl);

    return NGX_OK;
}


ngx_int_t
ngx_lua_web_stream_read(ngx_lua_web_stream_t *stream, ngx_pool_t *pool,
    ngx_str_t *value)
{
    ngx_buf_t    *b;
    ngx_int_t     rc;

    value->data = NULL;
    value->len = 0;

    for ( ;; ) {
        while (stream->bufs) {
            b = stream->bufs->buf;

            if (ngx_buf_special(b)) {
                if (b->last_buf) {
                    stream->closed = 1;
                }

                ngx_lua_web_stream_pop_buf(stream, pool);
                continue;
            }

            if (!ngx_buf_in_memory(b)) {
                stream->errored = 1;
                return NGX_ERROR;
            }

            if (b->pos < b->last) {
                value->data = b->pos;
                value->len = b->last - b->pos;
                b->pos = b->last;

                if (b->last_buf) {
                    stream->closed = 1;
                }

                ngx_lua_web_stream_pop_buf(stream, pool);

                return NGX_OK;
            }

            if (b->last_buf) {
                stream->closed = 1;
            }

            ngx_lua_web_stream_pop_buf(stream, pool);
        }

        if (stream->closed) {
            return NGX_DONE;
        }

        rc = stream->source->pull(stream, stream->source);
        if (rc == NGX_OK) {
            continue;
        }

        if (rc == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        if (rc == NGX_DONE) {
            stream->closed = 1;
            return NGX_DONE;
        }

        stream->errored = 1;

        return NGX_ERROR;
    }
}


static void
ngx_lua_web_stream_pop_buf(ngx_lua_web_stream_t *stream, ngx_pool_t *pool)
{
    ngx_chain_t  *cl;

    cl = stream->bufs;
    stream->bufs = cl->next;

    if (stream->bufs == NULL) {
        stream->last = &stream->bufs;
    }

    ngx_free_chain(pool, cl);
}


void
ngx_lua_web_stream_wait(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_wake_pt wake, void *data)
{
    stream->wake = wake;
    stream->data = data;
}


void
ngx_lua_web_stream_wake(ngx_lua_web_stream_t *stream)
{
    void                         *data;
    ngx_lua_web_stream_wake_pt    wake;

    wake = stream->wake;
    if (wake == NULL) {
        return;
    }

    data = stream->data;
    stream->wake = NULL;
    stream->data = NULL;

    wake(data);
}
