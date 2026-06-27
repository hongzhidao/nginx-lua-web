/*
 * Copyright (C) Zhidao HONG
 */


#include <ngx_config.h>
#include <ngx_core.h>

#include <lua.h>
#include <lauxlib.h>

#include "ngx_lua.h"
#include "ngx_lua_web.h"


#define NGX_LUA_WEB_STREAM_METATABLE  "ngx_lua_web_stream"


typedef struct {
    ngx_lua_web_stream_t  *stream;
} ngx_lua_web_stream_ud_t;


struct ngx_lua_web_stream_s {
    ngx_chain_t                  *in;
    ngx_chain_t                 **last;
    ngx_lua_web_stream_source_t  *source;
    unsigned                      closed:1;
};


static int ngx_lua_web_stream_read_method(lua_State *L);
static int ngx_lua_web_stream_read_continue(lua_State *L, int status,
    lua_KContext ctx);
static int ngx_lua_web_stream_get_reader_method(lua_State *L);
static ngx_lua_web_stream_t *ngx_lua_web_stream_check(lua_State *L, int index);
static void ngx_lua_web_stream_register_metatable(lua_State *L);


ngx_lua_web_stream_t *
ngx_lua_web_stream_create(lua_State *L)
{
    ngx_lua_ctx_t         *ctx;
    ngx_lua_web_stream_t  *stream;

    if (L == NULL) {
        return NULL;
    }

    ctx = ngx_lua_get_ctx(L);
    if (ctx == NULL) {
        return NULL;
    }

    stream = ngx_lua_pcalloc(ctx->pool, sizeof(ngx_lua_web_stream_t));
    if (stream == NULL) {
        return NULL;
    }

    stream->last = &stream->in;

    return stream;
}


void
ngx_lua_web_stream_push(lua_State *L, ngx_lua_web_stream_t *stream)
{
    ngx_lua_web_stream_ud_t  *ud;

    ud = lua_newuserdatauv(L, sizeof(ngx_lua_web_stream_ud_t), 0);
    ud->stream = stream;

    ngx_lua_web_stream_register_metatable(L);

    lua_setmetatable(L, -2);
}


ngx_int_t
ngx_lua_web_stream_set_source(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source)
{
    if (stream == NULL || source == NULL) {
        return NGX_ERROR;
    }

    stream->source = source;

    return NGX_OK;
}


ngx_int_t
ngx_lua_web_stream_start_source(ngx_lua_web_stream_t *stream)
{
    if (stream == NULL || stream->source == NULL
        || stream->source->start == NULL)
    {
        return NGX_OK;
    }

    return stream->source->start(stream, stream->source->data);
}


ngx_int_t
ngx_lua_web_stream_pull_source(ngx_lua_web_stream_t *stream)
{
    if (stream == NULL || stream->source == NULL
        || stream->source->pull == NULL)
    {
        return NGX_OK;
    }

    return stream->source->pull(stream, stream->source->data);
}


ngx_int_t
ngx_lua_web_stream_read(lua_State *L, ngx_lua_web_stream_t *stream)
{
    ngx_buf_t    *b;
    ngx_chain_t  *in;
    ngx_int_t     rc;

    if (stream == NULL) {
        return NGX_ERROR;
    }

    for ( ;; ) {
        in = stream->in;

        if (in != NULL) {
            stream->in = in->next;
            if (stream->in == NULL) {
                stream->last = &stream->in;
            }

            in->next = NULL;
            b = in->buf;

            if (b == NULL || b->pos == b->last) {
                continue;
            }

            lua_pushlstring(L, (char *) b->pos, b->last - b->pos);
            b->pos = b->last;

            return NGX_OK;
        }

        if (stream->closed) {
            lua_pushnil(L);
            return NGX_OK;
        }

        rc = ngx_lua_web_stream_pull_source(stream);
        if (rc != NGX_OK) {
            return rc;
        }
    }
}


ngx_uint_t
ngx_lua_web_stream_has_pending(ngx_lua_web_stream_t *stream)
{
    return stream != NULL && stream->in != NULL;
}


ngx_uint_t
ngx_lua_web_stream_is_closed(ngx_lua_web_stream_t *stream)
{
    return stream != NULL && stream->closed;
}


void
ngx_lua_web_stream_enqueue_chain(ngx_lua_web_stream_t *stream,
    ngx_chain_t *in)
{
    ngx_chain_t  *next;

    if (stream == NULL) {
        return;
    }

    while (in) {
        next = in->next;
        in->next = NULL;

        *stream->last = in;
        stream->last = &in->next;

        in = next;
    }
}


void
ngx_lua_web_stream_close(ngx_lua_web_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }

    stream->closed = 1;
}


static int
ngx_lua_web_stream_read_method(lua_State *L)
{
    ngx_int_t              rc;
    ngx_lua_ctx_t         *ctx;
    ngx_lua_web_stream_t  *stream;

    stream = ngx_lua_web_stream_check(L, 1);

    rc = ngx_lua_web_stream_read(L, stream);

    if (rc == NGX_OK) {
        return 1;
    }

    if (rc == NGX_AGAIN) {
        ctx = ngx_lua_get_ctx(L);
        if (ctx == NULL) {
            return luaL_error(L, "stream read has no request context");
        }

        ctx->waiting_stream = stream;

        return lua_yieldk(L, 0, 0, ngx_lua_web_stream_read_continue);
    }

    return luaL_error(L, "stream read failed");
}


static int
ngx_lua_web_stream_read_continue(lua_State *L, int status,
    lua_KContext opaque)
{
    ngx_int_t              rc;
    ngx_lua_ctx_t         *ctx;
    ngx_lua_web_stream_t  *stream;

    (void) status;
    (void) opaque;

    stream = ngx_lua_web_stream_check(L, 1);

    rc = ngx_lua_web_stream_read(L, stream);

    if (rc == NGX_OK) {
        return 1;
    }

    if (rc == NGX_AGAIN) {
        ctx = ngx_lua_get_ctx(L);
        if (ctx == NULL) {
            return luaL_error(L, "stream read has no request context");
        }

        ctx->waiting_stream = stream;

        return lua_yieldk(L, 0, 0, ngx_lua_web_stream_read_continue);
    }

    return luaL_error(L, "stream read failed");
}


static int
ngx_lua_web_stream_get_reader_method(lua_State *L)
{
    (void) ngx_lua_web_stream_check(L, 1);

    lua_pushvalue(L, 1);

    return 1;
}


static ngx_lua_web_stream_t *
ngx_lua_web_stream_check(lua_State *L, int index)
{
    ngx_lua_web_stream_ud_t  *ud;

    ud = luaL_checkudata(L, index, NGX_LUA_WEB_STREAM_METATABLE);
    luaL_argcheck(L, ud != NULL && ud->stream != NULL, index, "Stream expected");

    return ud->stream;
}


static void
ngx_lua_web_stream_register_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_STREAM_METATABLE)) {
        lua_pushliteral(L, "__name");
        lua_pushliteral(L, "Stream");
        lua_rawset(L, -3);

        lua_pushliteral(L, "__index");
        lua_newtable(L);

        lua_pushliteral(L, "read");
        lua_pushcfunction(L, ngx_lua_web_stream_read_method);
        lua_rawset(L, -3);

        lua_pushliteral(L, "getReader");
        lua_pushcfunction(L, ngx_lua_web_stream_get_reader_method);
        lua_rawset(L, -3);

        lua_rawset(L, -3);
    }
}
