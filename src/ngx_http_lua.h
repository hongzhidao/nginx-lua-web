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


ngx_lua_web_stream_source_t *ngx_http_lua_request_body_source_create(
    lua_State *L, ngx_http_request_t *r);


#endif /* _NGX_HTTP_LUA_H_INCLUDED_ */
