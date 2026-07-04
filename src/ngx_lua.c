/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua.h"

lua_State *
ngx_lua_create_coroutine(lua_State *L, ngx_pool_t *pool)
{
    lua_State      *co;
    ngx_lua_ctx_t *ctx;

    co = lua_newthread(L);
    if (co == NULL) {
        return NULL;
    }

    ctx = ngx_pcalloc(pool, sizeof(ngx_lua_ctx_t));
    if (ctx == NULL) {
        ngx_lua_destroy_coroutine(co, L);
        lua_pop(L, 1);
        return NULL;
    }

    ctx->thread = co;
    ngx_lua_set_ctx(co, ctx);

    return co;
}


void
ngx_lua_destroy_coroutine(lua_State *L, lua_State *from)
{
    ngx_lua_ctx_t  *ctx;

    ctx = ngx_lua_get_ctx(L);
    if (ctx != NULL) {
        if (ctx->thread != NULL) {
            ngx_lua_set_ctx(ctx->thread, NULL);
            ctx->thread = NULL;
        }

        ctx->resume = NULL;
        ctx->data = NULL;
    }

    (void) lua_closethread(L, from);
}


void
ngx_lua_set_ctx(lua_State *L, ngx_lua_ctx_t *ctx)
{
    ngx_lua_ctx_t  **pctx;

    pctx = (ngx_lua_ctx_t **) lua_getextraspace(L);
    *pctx = ctx;
}


ngx_lua_ctx_t *
ngx_lua_get_ctx(lua_State *L)
{
    ngx_lua_ctx_t  **pctx;

    pctx = (ngx_lua_ctx_t **) lua_getextraspace(L);
    return *pctx;
}


void
ngx_lua_disable_coroutine(lua_State *L)
{
    lua_pushnil(L);
    lua_setglobal(L, "coroutine");

    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "loaded");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "coroutine");
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "preload");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, "coroutine");
    }
    lua_pop(L, 1);

    lua_pop(L, 1);
}
