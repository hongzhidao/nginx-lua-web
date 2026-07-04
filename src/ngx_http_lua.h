/*
 * Copyright (C) 2026 Zhidao HONG
 */


#ifndef _NGX_HTTP_LUA_H_INCLUDED_
#define _NGX_HTTP_LUA_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <lua.h>

#include "ngx_lua_web.h"


typedef struct {
    lua_State                              *main;
    lua_State                              *co;
    int                                     app_ref;
    int                                     co_ref;
    ngx_lua_web_stream_t                   *request_body;
} ngx_http_lua_ctx_t;


extern ngx_module_t  ngx_http_lua_module;


ngx_lua_web_stream_t *ngx_http_lua_request_body_stream_create(
    ngx_http_request_t *r);


#endif /* _NGX_HTTP_LUA_H_INCLUDED_ */
