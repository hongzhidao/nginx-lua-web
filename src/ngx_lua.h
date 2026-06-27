/*
 * Copyright (C) Zhidao HONG
 */


#ifndef _NGX_LUA_H_INCLUDED_
#define _NGX_LUA_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#include <lua.h>

#include "ngx_lua_pool.h"


typedef struct {
    ngx_lua_pool_t  *pool;
    void            *data;
} ngx_lua_ctx_t;


ngx_lua_ctx_t *ngx_lua_ctx_create(ngx_log_t *log);
void ngx_lua_ctx_destroy(ngx_lua_ctx_t *ctx);

ngx_lua_ctx_t *ngx_lua_get_ctx(lua_State *L);
void ngx_lua_set_ctx(lua_State *L, ngx_lua_ctx_t *ctx);


#endif /* _NGX_LUA_H_INCLUDED_ */
