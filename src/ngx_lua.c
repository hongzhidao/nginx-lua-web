/*
 * Copyright (C) Zhidao HONG
 */


#include <ngx_config.h>
#include <ngx_core.h>

#include <lua.h>
#include <lauxlib.h>

#include "ngx_lua.h"


ngx_lua_ctx_t *
ngx_lua_ctx_create(ngx_log_t *log)
{
    ngx_lua_pool_t  *pool;
    ngx_lua_ctx_t   *ctx;

    pool = ngx_lua_pool_create(log);
    if (pool == NULL) {
        return NULL;
    }

    ctx = ngx_lua_pcalloc(pool, sizeof(ngx_lua_ctx_t));
    if (ctx == NULL) {
        ngx_lua_pool_destroy(pool);
        return NULL;
    }

    ctx->pool = pool;

    return ctx;
}


void
ngx_lua_ctx_destroy(ngx_lua_ctx_t *ctx)
{
    ngx_lua_pool_t  *pool;

    if (ctx == NULL) {
        return;
    }

    pool = ctx->pool;
    ngx_lua_pool_destroy(pool);
}


ngx_lua_ctx_t *
ngx_lua_get_ctx(lua_State *L)
{
    return *(ngx_lua_ctx_t **) lua_getextraspace(L);
}


void
ngx_lua_set_ctx(lua_State *L, ngx_lua_ctx_t *ctx)
{
    *(ngx_lua_ctx_t **) lua_getextraspace(L) = ctx;
}
