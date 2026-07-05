/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "ngx_http_lua.h"
#include "ngx_lua.h"


typedef struct {
    lua_State               *lua;
} ngx_http_lua_main_conf_t;


typedef struct {
    ngx_str_t                lua_web_file;
    int                      lua_ref;
} ngx_http_lua_loc_conf_t;


static ngx_int_t ngx_http_lua_handler(ngx_http_request_t *r);
static void ngx_http_lua_request_body_handler(ngx_http_request_t *r);
static ngx_lua_app_t *ngx_http_lua_run_file(ngx_http_request_t *r,
    ngx_http_lua_ctx_t *ctx);
static ngx_int_t ngx_http_lua_run_handler(ngx_http_request_t *r,
    ngx_http_lua_ctx_t *ctx, ngx_lua_app_t *app);
static ngx_int_t ngx_http_lua_resume_request(ngx_http_request_t *r,
    int nargs);
static void ngx_http_lua_resume_handler(void *data);
static ngx_lua_web_response_t *ngx_http_lua_get_response(
    ngx_http_request_t *r, lua_State *co, int nresults);
static void ngx_http_lua_cleanup_ctx(void *data);
static char *ngx_http_lua_load_file(ngx_conf_t *cf,
    ngx_http_lua_loc_conf_t *llcf);
static void *ngx_http_lua_create_main_conf(ngx_conf_t *cf);
static void ngx_http_lua_cleanup_vm(void *data);
static char *ngx_http_lua_web_file(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_http_lua_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_lua_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static void ngx_http_lua_push_request_registry(lua_State *L);


static char ngx_http_lua_request_registry_key;


static ngx_command_t  ngx_http_lua_commands[] = {

    { ngx_string("lua_web_file"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_web_file,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_lua_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    ngx_http_lua_create_main_conf,         /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_lua_create_loc_conf,          /* create location configuration */
    ngx_http_lua_merge_loc_conf            /* merge location configuration */
};


ngx_module_t  ngx_http_lua_module = {
    NGX_MODULE_V1,
    &ngx_http_lua_module_ctx,              /* module context */
    ngx_http_lua_commands,                 /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


void
ngx_http_lua_set_request(lua_State *L, ngx_http_request_t *r)
{
    ngx_http_lua_push_request_registry(L);

    lua_pushlightuserdata(L, L);
    lua_pushlightuserdata(L, r);
    lua_rawset(L, -3);

    lua_pop(L, 1);
}


ngx_http_request_t *
ngx_http_lua_get_request(lua_State *L)
{
    ngx_http_request_t  *r;

    ngx_http_lua_push_request_registry(L);

    lua_pushlightuserdata(L, L);
    lua_rawget(L, -2);

    r = lua_touserdata(L, -1);

    lua_pop(L, 2);

    return r;
}


void
ngx_http_lua_clear_request(lua_State *L)
{
    ngx_http_lua_push_request_registry(L);

    lua_pushlightuserdata(L, L);
    lua_pushnil(L);
    lua_rawset(L, -3);

    lua_pop(L, 1);
}


static void
ngx_http_lua_push_request_registry(lua_State *L)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, &ngx_http_lua_request_registry_key);

    if (!lua_isnil(L, -1)) {
        return;
    }

    lua_pop(L, 1);

    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &ngx_http_lua_request_registry_key);
}


static ngx_int_t
ngx_http_lua_handler(ngx_http_request_t *r)
{
    ngx_int_t  rc;

    if (r != r->main) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file does not support subrequests");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->request_body_no_buffering = 1;

    rc = ngx_http_read_client_request_body(r,
                                           ngx_http_lua_request_body_handler);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}


static void
ngx_http_lua_request_body_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_pool_cleanup_t        *cln;
    ngx_http_lua_ctx_t        *ctx;
    ngx_http_lua_main_conf_t  *lmcf;
    ngx_lua_app_t             *app;

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_lua_ctx_t));
    if (ctx == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);

    ctx->main = lmcf->lua;
    ctx->app_ref = LUA_NOREF;
    ctx->co_ref = LUA_NOREF;
    ctx->request_ref = LUA_NOREF;
    ctx->request = NULL;
    ctx->response = NULL;

    cln->handler = ngx_http_lua_cleanup_ctx;
    cln->data = ctx;

    ngx_http_set_ctx(r, ctx, ngx_http_lua_module);

    app = ngx_http_lua_run_file(r, ctx);
    if (app == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    rc = ngx_http_lua_run_handler(r, ctx, app);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
}


static ngx_lua_app_t *
ngx_http_lua_run_file(ngx_http_request_t *r, ngx_http_lua_ctx_t *ctx)
{
    int                       app_index;
    int                       nresults, rc;
    lua_State                *L, *co;
    ngx_lua_app_t            *app;
    ngx_http_lua_main_conf_t  *lmcf;
    ngx_http_lua_loc_conf_t   *llcf;

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);
    L = lmcf->lua;
    nresults = 0;

    co = ngx_lua_create_coroutine(L, r->pool);
    if (co == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "ngx_lua_create_coroutine() failed");
        return NULL;
    }

    ctx->co = co;
    ctx->co_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, llcf->lua_ref);

    if (!lua_isfunction(L, -1)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" registry entry is not a function",
                      &llcf->lua_web_file);
        lua_pop(L, 1);
        return NULL;
    }

    lua_xmove(L, co, 1);

    rc = lua_resume(co, L, 0, &nresults);

    if (rc == LUA_YIELD) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" yielded",
                      &llcf->lua_web_file);
        return NULL;
    }

    if (rc != LUA_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" failed: %s",
                      &llcf->lua_web_file, lua_tostring(co, -1));
        return NULL;
    }

    if (nresults < 1) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" returned no app",
                      &llcf->lua_web_file);
        return NULL;
    }

    app_index = lua_gettop(co) - nresults + 1;

    app = ngx_lua_app_get(co, app_index);
    if (app == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" must return app",
                      &llcf->lua_web_file);
        return NULL;
    }

    lua_pushvalue(co, app_index);
    lua_replace(co, 1);
    lua_settop(co, 1);

    return app;
}


static ngx_int_t
ngx_http_lua_run_handler(ngx_http_request_t *r, ngx_http_lua_ctx_t *ctx,
    ngx_lua_app_t *app)
{
    int                       handler_ref;
    int                       params_ref;
    ngx_int_t                 rc;
    lua_State                *co;
    ngx_http_lua_loc_conf_t  *llcf;

    co = ctx->co;
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);

    params_ref = LUA_NOREF;

    handler_ref = ngx_lua_app_find_handler(co, app,
                                           (char *) r->method_name.data,
                                           r->method_name.len,
                                           (char *) r->uri.data, r->uri.len);

    if (handler_ref == LUA_NOREF) {
        r->headers_out.status = NGX_HTTP_NOT_FOUND;
        r->headers_out.content_length_n = 0;
        rc = ngx_http_send_header(r);

        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
            return rc;
        }

        return ngx_http_send_special(r, NGX_HTTP_LAST);
    }

    params_ref = luaL_ref(co, LUA_REGISTRYINDEX);

    lua_pushvalue(co, 1);
    ctx->app_ref = luaL_ref(co, LUA_REGISTRYINDEX);

    lua_settop(co, 0);
    lua_rawgeti(co, LUA_REGISTRYINDEX, handler_ref);

    if (!lua_isfunction(co, -1)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" route handler is not a function",
                      &llcf->lua_web_file);
        luaL_unref(co, LUA_REGISTRYINDEX, params_ref);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->request = ngx_http_lua_request_create(r, ctx);
    if (ctx->request == NULL) {
        luaL_unref(co, LUA_REGISTRYINDEX, params_ref);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    lua_pushvalue(co, -1);
    ctx->request_ref = luaL_ref(co, LUA_REGISTRYINDEX);

    lua_rawgeti(co, LUA_REGISTRYINDEX, params_ref);
    luaL_unref(co, LUA_REGISTRYINDEX, params_ref);

    ngx_http_lua_set_request(co, r);

    return ngx_http_lua_resume_request(r, 2);
}


static ngx_int_t
ngx_http_lua_resume_request(ngx_http_request_t *r, int nargs)
{
    int                       nresults, rc;
    ngx_http_lua_ctx_t       *ctx;
    ngx_lua_ctx_t            *lctx;
    ngx_lua_web_response_t   *response;
    ngx_http_lua_loc_conf_t  *llcf;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);
    nresults = 0;

    rc = lua_resume(ctx->co, ctx->main, nargs, &nresults);

    if (rc == LUA_OK) {
        response = ngx_http_lua_get_response(r, ctx->co, nresults);
        if (response == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        ctx->response = response;

        return ngx_http_lua_send_response(r, response);
    }

    if (rc == LUA_YIELD) {
        lctx = ngx_lua_get_ctx(ctx->co);
        lctx->resume = ngx_http_lua_resume_handler;
        lctx->data = r;
        return NGX_AGAIN;
    }

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "lua_web_file \"%V\" route handler failed: %s",
                  &llcf->lua_web_file, lua_tostring(ctx->co, -1));

    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}


static void
ngx_http_lua_resume_handler(void *data)
{
    ngx_int_t            rc;
    ngx_http_request_t  *r = data;

    rc = ngx_http_lua_resume_request(r, 0);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
}


static ngx_lua_web_response_t *
ngx_http_lua_get_response(ngx_http_request_t *r, lua_State *co, int nresults)
{
    int                     response_index;
    ngx_lua_web_response_t *response;

    if (nresults < 1) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler returned no values");
        return NULL;
    }

    response_index = lua_absindex(co, -nresults);

    response = ngx_lua_web_response_get(co, response_index);
    if (response == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler must return Response");
        return NULL;
    }

    return response;
}


static void
ngx_http_lua_cleanup_ctx(void *data)
{
    ngx_http_lua_ctx_t  *ctx = data;

    if (ctx->co != NULL) {
        ngx_http_lua_clear_request(ctx->co);
    }

    if (ctx->co != NULL) {
        ngx_lua_destroy_coroutine(ctx->co, ctx->main);
        ctx->co = NULL;
    }

    if (ctx->app_ref != LUA_NOREF) {
        luaL_unref(ctx->main, LUA_REGISTRYINDEX, ctx->app_ref);
        ctx->app_ref = LUA_NOREF;
    }

    if (ctx->request_ref != LUA_NOREF) {
        luaL_unref(ctx->main, LUA_REGISTRYINDEX, ctx->request_ref);
        ctx->request_ref = LUA_NOREF;
    }

    ctx->request = NULL;
    ctx->response = NULL;

    if (ctx->co_ref != LUA_NOREF) {
        luaL_unref(ctx->main, LUA_REGISTRYINDEX, ctx->co_ref);
        ctx->co_ref = LUA_NOREF;
    }
}


static char *
ngx_http_lua_load_file(ngx_conf_t *cf, ngx_http_lua_loc_conf_t *llcf)
{
    lua_State                *L;
    ngx_http_lua_main_conf_t *lmcf;

    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lua_module);
    L = lmcf->lua;

    if (luaL_loadfile(L, (char *) llcf->lua_web_file.data) != LUA_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "failed to parse lua_web_file \"%V\": %s",
                           &llcf->lua_web_file, lua_tostring(L, -1));
        lua_pop(L, 1);
        return NGX_CONF_ERROR;
    }

    llcf->lua_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    return NGX_CONF_OK;
}


static void *
ngx_http_lua_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_lua_main_conf_t  *conf;
    ngx_pool_cleanup_t        *cln;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NULL;
    }

    conf->lua = luaL_newstate();
    if (conf->lua == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "luaL_newstate() failed");
        return NULL;
    }

    luaL_openlibs(conf->lua);
    ngx_lua_set_ctx(conf->lua, NULL);

    ngx_lua_disable_coroutine(conf->lua);
    ngx_lua_app_register(conf->lua);
    ngx_lua_web_request_register(conf->lua);
    ngx_lua_web_response_register(conf->lua);
    ngx_lua_web_headers_register(conf->lua);
    ngx_lua_web_url_register(conf->lua);
    ngx_lua_web_search_params_register(conf->lua);
    ngx_lua_web_stream_register(conf->lua);
    ngx_http_lua_fetch_register(conf->lua);

    cln->handler = ngx_http_lua_cleanup_vm;
    cln->data = conf;

    return conf;
}


static void
ngx_http_lua_cleanup_vm(void *data)
{
    ngx_http_lua_main_conf_t  *lmcf = data;

    lua_close(lmcf->lua);
}


static char *
ngx_http_lua_web_file(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                  *value;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_lua_loc_conf_t    *llcf = conf;

    (void) cmd;

    if (llcf->lua_web_file.data != NULL) {
        return "is duplicate";
    }

    value = cf->args->elts;

    llcf->lua_web_file = value[1];

    if (ngx_conf_full_name(cf->cycle, &llcf->lua_web_file, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (ngx_http_lua_load_file(cf, llcf) != NGX_CONF_OK) {
        return NGX_CONF_ERROR;
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_lua_handler;

    return NGX_CONF_OK;
}


static void *
ngx_http_lua_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_lua_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->lua_ref = LUA_NOREF;

    return conf;
}


static char *
ngx_http_lua_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_lua_loc_conf_t *prev = parent;
    ngx_http_lua_loc_conf_t *conf = child;

    if (conf->lua_web_file.data == NULL) {
        conf->lua_web_file = prev->lua_web_file;
        conf->lua_ref = prev->lua_ref;
    }

    if (conf->lua_web_file.data == NULL) {
        ngx_str_set(&conf->lua_web_file, "");
        conf->lua_ref = LUA_NOREF;
    }

    return NGX_CONF_OK;
}
