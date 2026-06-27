
/*
 * Copyright (C) Zhidao HONG
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "ngx_lua.h"
#include "ngx_lua_web.h"
#include "ngx_http_lua.h"


typedef struct ngx_http_lua_loc_conf_s  ngx_http_lua_loc_conf_t;


typedef struct {
    ngx_array_t  *locations;
    lua_State    *lua;
} ngx_http_lua_main_conf_t;


struct ngx_http_lua_loc_conf_s {
    ngx_str_t   content;
    ngx_int_t   handler_ref;
};


static char *ngx_http_lua_content(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_http_lua_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_lua_create_loc_conf(ngx_conf_t *cf);
static ngx_int_t ngx_http_lua_init_process(ngx_cycle_t *cycle);
static void ngx_http_lua_exit_process(ngx_cycle_t *cycle);
static ngx_int_t ngx_http_lua_init_content_handlers(ngx_cycle_t *cycle,
    ngx_http_lua_main_conf_t *lmcf);
static ngx_int_t ngx_http_lua_init_content_handler(ngx_cycle_t *cycle,
    ngx_http_lua_main_conf_t *lmcf, ngx_http_lua_loc_conf_t *llcf);
static ngx_int_t ngx_http_lua_run_handler(ngx_http_request_t *r,
    ngx_http_lua_main_conf_t *lmcf, ngx_http_lua_loc_conf_t *llcf);
static ngx_int_t ngx_http_lua_send_hello_world(ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_handler(ngx_http_request_t *r);


static ngx_command_t  ngx_http_lua_commands[] = {

    { ngx_string("lua_content"),
      NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_content,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, content),
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
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_lua_module = {
    NGX_MODULE_V1,
    &ngx_http_lua_module_ctx,              /* module context */
    ngx_http_lua_commands,                 /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_lua_init_process,             /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_http_lua_exit_process,             /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_http_lua_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_lua_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    return conf;
}


static void *
ngx_http_lua_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_lua_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->handler_ref = LUA_NOREF;

    return conf;
}


static ngx_int_t
ngx_http_lua_init_process(ngx_cycle_t *cycle)
{
    ngx_http_lua_main_conf_t  *lmcf;

    lmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_lua_module);
    if (lmcf == NULL || lmcf->locations == NULL) {
        return NGX_OK;
    }

    lmcf->lua = luaL_newstate();
    if (lmcf->lua == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "luaL_newstate() failed");
        return NGX_ERROR;
    }

    luaL_openlibs(lmcf->lua);

    if (ngx_http_lua_init_content_handlers(cycle, lmcf) != NGX_OK) {
        lua_close(lmcf->lua);
        lmcf->lua = NULL;
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_http_lua_exit_process(ngx_cycle_t *cycle)
{
    ngx_http_lua_main_conf_t  *lmcf;

    lmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_lua_module);
    if (lmcf == NULL || lmcf->lua == NULL) {
        return;
    }

    lua_close(lmcf->lua);
    lmcf->lua = NULL;
}


static char *
ngx_http_lua_content(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_lua_main_conf_t  *lmcf;
    ngx_http_lua_loc_conf_t   *llcf = conf;
    ngx_str_t                 *value;
    ngx_http_lua_loc_conf_t  **location;

    if (llcf->content.data != NULL) {
        return "is duplicate";
    }

    value = cf->args->elts;
    llcf->content = value[1];

    if (ngx_conf_full_name(cf->cycle, &llcf->content, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lua_module);

    if (lmcf->locations == NULL) {
        lmcf->locations = ngx_array_create(cf->pool, 4,
                                           sizeof(ngx_http_lua_loc_conf_t *));
        if (lmcf->locations == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    location = ngx_array_push(lmcf->locations);
    if (location == NULL) {
        return NGX_CONF_ERROR;
    }

    *location = llcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_http_lua_handler;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_lua_init_content_handlers(ngx_cycle_t *cycle,
    ngx_http_lua_main_conf_t *lmcf)
{
    ngx_uint_t                 i;
    ngx_http_lua_loc_conf_t  **llcf;

    llcf = lmcf->locations->elts;

    for (i = 0; i < lmcf->locations->nelts; i++) {
        if (ngx_http_lua_init_content_handler(cycle, lmcf, llcf[i])
            != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_init_content_handler(ngx_cycle_t *cycle,
    ngx_http_lua_main_conf_t *lmcf, ngx_http_lua_loc_conf_t *llcf)
{
    lua_State   *L;
    const char  *error;
    char        *filename;

    L = lmcf->lua;

    filename = ngx_pnalloc(cycle->pool, llcf->content.len + 1);
    if (filename == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(filename, llcf->content.data, llcf->content.len);
    filename[llcf->content.len] = '\0';

    if (luaL_loadfile(L, filename) != LUA_OK) {
        error = lua_tostring(L, -1);
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "luaL_loadfile(\"%V\") failed: %s",
                      &llcf->content, error ? error : "unknown error");
        lua_pop(L, 1);
        return NGX_ERROR;
    }

    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        error = lua_tostring(L, -1);
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "lua file \"%V\" failed: %s",
                      &llcf->content, error ? error : "unknown error");
        lua_pop(L, 1);
        return NGX_ERROR;
    }

    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "handler");
        lua_remove(L, -2);
    }

    if (!lua_isfunction(L, -1)) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "lua file \"%V\" must return a function or a table "
                      "with a handler function", &llcf->content);
        lua_pop(L, 1);
        return NGX_ERROR;
    }

    llcf->handler_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_run_handler(ngx_http_request_t *r,
    ngx_http_lua_main_conf_t *lmcf, ngx_http_lua_loc_conf_t *llcf)
{
    int             nres;
    int             status;
    lua_State      *co;
    lua_State      *L;
    ngx_int_t       rc;
    ngx_lua_ctx_t  *ctx;
    ngx_lua_web_stream_source_t  *body_source;
    ngx_lua_web_stream_t         *body;

    L = lmcf->lua;

    ctx = ngx_lua_ctx_create(r->connection->log);
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->data = r;
    ngx_http_set_ctx(r, ctx, ngx_http_lua_module);

    co = lua_newthread(L);
    if (co == NULL) {
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        goto done;
    }

    ngx_lua_set_ctx(co, ctx);

    lua_rawgeti(L, LUA_REGISTRYINDEX, llcf->handler_ref);
    lua_xmove(L, co, 1);

    body = ngx_lua_web_stream_create(co);
    if (body == NULL) {
        lua_pop(L, 1);
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        goto done;
    }

    body_source = ngx_http_lua_request_body_source_create(co, r);
    if (body_source == NULL) {
        lua_pop(L, 1);
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        goto done;
    }

    if (ngx_lua_web_stream_set_source(body, body_source) != NGX_OK) {
        lua_pop(L, 1);
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        goto done;
    }

    rc = ngx_lua_web_stream_start_source(body);
    if (rc != NGX_OK) {
        lua_pop(L, 1);
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        goto done;
    }

    ngx_lua_web_stream_push(co, body);

    status = lua_resume(co, L, 1, &nres);

    if (status != LUA_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler \"%V\" failed: %s",
                      &llcf->content, lua_tostring(co, -1));
        lua_pop(co, 1);
        lua_pop(L, 1);
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        goto done;
    }

    lua_settop(co, 0);
    lua_pop(L, 1);

    rc = NGX_OK;

done:

    ngx_http_set_ctx(r, NULL, ngx_http_lua_module);
    ngx_lua_ctx_destroy(ctx);

    return rc;
}


static ngx_int_t
ngx_http_lua_send_hello_world(ngx_http_request_t *r)
{
    ngx_buf_t    *b;
    ngx_chain_t   out;
    ngx_int_t     rc;
    static u_char body[] = "hello world";

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = sizeof(body) - 1;
    ngx_str_set(&r->headers_out.content_type, "text/plain");

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = body;
    b->last = body + sizeof(body) - 1;
    b->memory = 1;
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}


static ngx_int_t
ngx_http_lua_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_http_lua_main_conf_t  *lmcf;
    ngx_http_lua_loc_conf_t   *llcf;

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);

    if (lmcf == NULL || lmcf->lua == NULL || llcf == NULL
        || llcf->handler_ref == LUA_NOREF)
    {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rc = ngx_http_lua_run_handler(r, lmcf, llcf);
    if (rc != NGX_OK) {
        return rc;
    }

    return ngx_http_lua_send_hello_world(r);
}
