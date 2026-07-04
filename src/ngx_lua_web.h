/*
 * Copyright (C) 2026 Zhidao HONG
 */


#ifndef _NGX_LUA_WEB_H_INCLUDED_
#define _NGX_LUA_WEB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#include <lua.h>


typedef struct ngx_lua_web_stream_s  ngx_lua_web_stream_t;
typedef struct ngx_lua_web_stream_source_s  ngx_lua_web_stream_source_t;

typedef ngx_int_t (*ngx_lua_web_stream_pull_pt)(
    ngx_lua_web_stream_t *stream, ngx_lua_web_stream_source_t *source);

struct ngx_lua_web_stream_source_s {
    ngx_lua_web_stream_pull_pt   pull;
    void                        *data;
};


ngx_lua_web_stream_t *ngx_lua_web_stream_create(lua_State *L);
void ngx_lua_web_stream_set_source(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source);


#endif /* _NGX_LUA_WEB_H_INCLUDED_ */
