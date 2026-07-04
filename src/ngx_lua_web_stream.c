/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua_web.h"

#include <lauxlib.h>


#define NGX_LUA_WEB_STREAM_METATABLE  "ngx_lua_web.ReadableStream"


struct ngx_lua_web_stream_s {
    ngx_lua_web_stream_source_t  *source;
    unsigned  closed:1;
};


static void ngx_lua_web_stream_set_metatable(lua_State *L);


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

    ngx_lua_web_stream_set_metatable(L);

    return stream;
}


void
ngx_lua_web_stream_set_source(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source)
{
    stream->source = source;
}
