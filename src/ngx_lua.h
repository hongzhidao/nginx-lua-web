/*
 * Copyright (C) 2026 Zhidao HONG
 */


#ifndef _NGX_LUA_H_INCLUDED_
#define _NGX_LUA_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#include <lua.h>


typedef struct ngx_lua_ctx_s  ngx_lua_ctx_t;
typedef struct ngx_lua_app_s  ngx_lua_app_t;
typedef void (*ngx_lua_resume_pt)(void *data);


struct ngx_lua_ctx_s {
    lua_State          *thread;
    ngx_pool_t         *pool;
    ngx_lua_resume_pt   resume;
    void               *data;
};


lua_State *ngx_lua_create_coroutine(lua_State *L, ngx_pool_t *pool);
void ngx_lua_destroy_coroutine(lua_State *L, lua_State *from);
void ngx_lua_set_ctx(lua_State *L, ngx_lua_ctx_t *ctx);
ngx_lua_ctx_t *ngx_lua_get_ctx(lua_State *L);
void ngx_lua_disable_coroutine(lua_State *L);
void ngx_lua_app_register(lua_State *L);
ngx_lua_app_t *ngx_lua_app_get(lua_State *L, int index);
int ngx_lua_app_find_handler(ngx_lua_app_t *app, const char *method,
    size_t method_len, const char *path, size_t len);


#endif /* _NGX_LUA_H_INCLUDED_ */
