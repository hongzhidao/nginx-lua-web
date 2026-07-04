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
    int                                     request_ref;
    ngx_lua_web_request_t                  *request;
    ngx_lua_web_response_t                 *response;
} ngx_http_lua_ctx_t;


extern ngx_module_t  ngx_http_lua_module;


ngx_lua_web_request_t *ngx_http_lua_request_create(ngx_http_request_t *r,
    ngx_http_lua_ctx_t *ctx);
void ngx_http_lua_set_request(lua_State *L, ngx_http_request_t *r);
ngx_http_request_t *ngx_http_lua_get_request(lua_State *L);
void ngx_http_lua_clear_request(lua_State *L);
ngx_int_t ngx_http_lua_send_response(ngx_http_request_t *r,
    ngx_lua_web_response_t *response);
void ngx_http_lua_fetch_register(lua_State *L);


#endif /* _NGX_HTTP_LUA_H_INCLUDED_ */
