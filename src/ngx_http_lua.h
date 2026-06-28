/*
 * Copyright (C) Zhidao HONG
 */


#ifndef _NGX_HTTP_LUA_H_INCLUDED_
#define _NGX_HTTP_LUA_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <lua.h>

#include "ngx_lua_web.h"


typedef struct {
    ngx_lua_ctx_t          *lua;
    lua_State              *state;
    lua_State              *coroutine;
    int                     coroutine_ref;
    int                     request_stream_ref;
    int                     response_stream_ref;
    ngx_lua_web_stream_t   *request_body;
    ngx_lua_web_stream_t   *response_body;
} ngx_http_lua_ctx_t;


ngx_int_t ngx_http_lua_request_push(lua_State *L, ngx_http_request_t *r);
ngx_int_t ngx_http_lua_response_send(ngx_http_request_t *r);


extern ngx_module_t  ngx_http_lua_module;


#endif /* _NGX_HTTP_LUA_H_INCLUDED_ */
