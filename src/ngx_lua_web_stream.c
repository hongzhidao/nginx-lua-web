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

    if (luaL_newmetatable(L, NGX_LUA_WEB_STREAM_METATABLE)) {
        lua_pushliteral(L, "__name");
        lua_pushliteral(L, "Stream");
        lua_rawset(L, -3);
    }

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
