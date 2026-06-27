/*
 * Copyright (C) Zhidao HONG
 */


#ifndef _NGX_LUA_POOL_H_INCLUDED_
#define _NGX_LUA_POOL_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_lua_pool_s  ngx_lua_pool_t;


ngx_lua_pool_t *ngx_lua_pool_create(ngx_log_t *log);
void ngx_lua_pool_destroy(ngx_lua_pool_t *pool);

void *ngx_lua_palloc(ngx_lua_pool_t *pool, size_t size);
void *ngx_lua_pcalloc(ngx_lua_pool_t *pool, size_t size);
void ngx_lua_pfree(ngx_lua_pool_t *pool, void *ptr);


#endif /* _NGX_LUA_POOL_H_INCLUDED_ */
