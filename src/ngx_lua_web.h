/*
 * Copyright (C) Zhidao HONG
 */


#ifndef _NGX_LUA_WEB_H_INCLUDED_
#define _NGX_LUA_WEB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#include <lua.h>

#include "ngx_lua.h"


typedef struct ngx_lua_web_stream_s         ngx_lua_web_stream_t;
typedef struct ngx_lua_web_stream_source_s  ngx_lua_web_stream_source_t;


typedef void (*ngx_lua_web_stream_wake_pt)(ngx_lua_web_stream_t *stream,
    void *data);
typedef ngx_int_t (*ngx_lua_web_stream_source_start_pt)(
    lua_State *L, ngx_lua_web_stream_t *stream, void *data);
typedef ngx_int_t (*ngx_lua_web_stream_source_pull_pt)(
    lua_State *L, ngx_lua_web_stream_t *stream, void *data);


struct ngx_lua_web_stream_source_s {
    void                                  *data;
    ngx_lua_web_stream_source_start_pt  start;
    ngx_lua_web_stream_source_pull_pt   pull;
};


ngx_lua_web_stream_t *ngx_lua_web_stream_create(lua_State *L);
void ngx_lua_web_stream_register(lua_State *L);
void ngx_lua_web_stream_push(lua_State *L, ngx_lua_web_stream_t *stream);
ngx_lua_web_stream_t *ngx_lua_web_stream_get(lua_State *L, int index);
ngx_int_t ngx_lua_web_stream_set_source(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source);
ngx_int_t ngx_lua_web_stream_start_source(lua_State *L,
    ngx_lua_web_stream_t *stream);
ngx_int_t ngx_lua_web_stream_pull_source(lua_State *L,
    ngx_lua_web_stream_t *stream);
ngx_int_t ngx_lua_web_stream_take_chain(lua_State *L,
    ngx_lua_web_stream_t *stream, ngx_chain_t **out);
void ngx_lua_web_stream_set_wake(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_wake_pt wake, void *data);
void ngx_lua_web_stream_wake(ngx_lua_web_stream_t *stream);
ngx_uint_t ngx_lua_web_stream_has_data(ngx_lua_web_stream_t *stream);
ngx_uint_t ngx_lua_web_stream_is_closed(ngx_lua_web_stream_t *stream);

void ngx_lua_web_stream_enqueue(ngx_lua_web_stream_t *stream, ngx_chain_t *in);
void ngx_lua_web_stream_close(ngx_lua_web_stream_t *stream);


#endif /* _NGX_LUA_WEB_H_INCLUDED_ */
