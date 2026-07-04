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
static ngx_int_t ngx_http_lua_read_result(ngx_http_request_t *r,
    lua_State *co, int nresults, ngx_uint_t *status,
    ngx_lua_web_stream_t **stream);
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
    lua_State                *co;
    ngx_lua_web_stream_t     *request_body;
    ngx_http_lua_loc_conf_t  *llcf;

    co = ctx->co;
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);

    handler_ref = ngx_lua_app_find_handler(app, (char *) r->uri.data,
                                           r->uri.len);

    if (handler_ref == LUA_NOREF) {
        return ngx_http_lua_send_response(r, ctx, NGX_HTTP_NOT_FOUND, NULL);
    }

    lua_pushvalue(co, 1);
    ctx->app_ref = luaL_ref(co, LUA_REGISTRYINDEX);

    lua_settop(co, 0);
    lua_rawgeti(co, LUA_REGISTRYINDEX, handler_ref);

    if (!lua_isfunction(co, -1)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" route handler is not a function",
                      &llcf->lua_web_file);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    request_body = ngx_http_lua_request_body_stream_create(r);
    if (request_body == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->request_body = request_body;

    return ngx_http_lua_resume_request(r, 1);
}


static ngx_int_t
ngx_http_lua_resume_request(ngx_http_request_t *r, int nargs)
{
    int                       nresults, rc;
    ngx_uint_t                status;
    ngx_http_lua_ctx_t       *ctx;
    ngx_lua_ctx_t            *lctx;
    ngx_lua_web_stream_t     *stream;
    ngx_http_lua_loc_conf_t  *llcf;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);
    nresults = 0;

    rc = lua_resume(ctx->co, ctx->main, nargs, &nresults);

    if (rc == LUA_OK) {
        rc = ngx_http_lua_read_result(r, ctx->co, nresults, &status, &stream);
        if (rc != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        return ngx_http_lua_send_response(r, ctx, status, stream);
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


static ngx_int_t
ngx_http_lua_read_result(ngx_http_request_t *r, lua_State *co, int nresults,
    ngx_uint_t *status, ngx_lua_web_stream_t **stream)
{
    int            isnum, table;
    lua_Integer    code;

    if (nresults < 1) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler returned no values");
        return NGX_ERROR;
    }

    table = lua_absindex(co, -nresults);

    if (!lua_istable(co, table)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler must return { status, stream }");
        return NGX_ERROR;
    }

    lua_geti(co, table, 1);
    code = lua_tointegerx(co, -1, &isnum);
    lua_pop(co, 1);

    if (!isnum || code < 100 || code > 599) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler returned invalid status");
        return NGX_ERROR;
    }

    lua_geti(co, table, 2);
    *stream = ngx_lua_web_stream_get(co, -1);

    if (*stream == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler returned invalid stream");
        lua_pop(co, 1);
        return NGX_ERROR;
    }

    *status = (ngx_uint_t) code;

    lua_pop(co, 1);

    return NGX_OK;
}


static void
ngx_http_lua_cleanup_ctx(void *data)
{
    ngx_http_lua_ctx_t  *ctx = data;

    if (ctx->co != NULL) {
        ngx_lua_destroy_coroutine(ctx->co, ctx->main);
        ctx->co = NULL;
    }

    if (ctx->app_ref != LUA_NOREF) {
        luaL_unref(ctx->main, LUA_REGISTRYINDEX, ctx->app_ref);
        ctx->app_ref = LUA_NOREF;
    }

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
    ngx_lua_web_stream_register(conf->lua);

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
