/*
 * Copyright (C) Zhidao HONG
 */


#include <ngx_config.h>
#include <ngx_core.h>

#include <lua.h>

#include "ngx_lua.h"
#include "ngx_lua_web.h"


struct ngx_lua_web_stream_s {
    unsigned  closed:1;
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

    return stream;
}
