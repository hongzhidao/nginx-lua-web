/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "ngx_lua.h"


typedef struct {
    lua_State               *lua;
} ngx_http_lua_main_conf_t;


typedef struct {
    ngx_str_t                lua_web_file;
    int                      lua_ref;
} ngx_http_lua_loc_conf_t;


static ngx_int_t ngx_http_lua_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_run(ngx_http_request_t *r, ngx_uint_t *status,
    ngx_str_t *body);
static ngx_int_t ngx_http_lua_read_result(ngx_http_request_t *r,
    lua_State *co, int nresults, ngx_uint_t *status, ngx_str_t *body);
static ngx_int_t ngx_http_lua_send_response(ngx_http_request_t *r,
    ngx_uint_t status, ngx_str_t *body);
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
    ngx_int_t                  rc;
    ngx_str_t                  body;
    ngx_uint_t                 status;

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    rc = ngx_http_lua_run(r, &status, &body);
    if (rc != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    return ngx_http_lua_send_response(r, status, &body);
}


static ngx_int_t
ngx_http_lua_run(ngx_http_request_t *r, ngx_uint_t *status, ngx_str_t *body)
{
    int                       app_index, app_ref, co_ref, handler_ref;
    int                       nresults, rc;
    lua_State                *L, *co;
    ngx_int_t                 rv;
    ngx_lua_app_t            *app;
    ngx_http_lua_main_conf_t  *lmcf;
    ngx_http_lua_loc_conf_t   *llcf;

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);
    L = lmcf->lua;
    app_ref = LUA_NOREF;
    co_ref = LUA_NOREF;
    nresults = 0;
    rv = NGX_ERROR;

    co = lua_newthread(L);
    if (co == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_newthread() failed");
        return NGX_ERROR;
    }

    co_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_rawgeti(L, LUA_REGISTRYINDEX, llcf->lua_ref);

    if (!lua_isfunction(L, -1)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" registry entry is not a function",
                      &llcf->lua_web_file);
        lua_pop(L, 1);
        goto done;
    }

    lua_xmove(L, co, 1);

    rc = lua_resume(co, L, 0, &nresults);

    if (rc == LUA_YIELD) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" yielded without an async "
                      "continuation",
                      &llcf->lua_web_file);
        goto done;
    }

    if (rc != LUA_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" failed: %s",
                      &llcf->lua_web_file, lua_tostring(co, -1));
        goto done;
    }

    if (nresults < 1) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" returned no app",
                      &llcf->lua_web_file);
        goto done;
    }

    app_index = lua_gettop(co) - nresults + 1;

    app = ngx_lua_app_get(co, app_index);
    if (app == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" must return app",
                      &llcf->lua_web_file);
        goto done;
    }

    handler_ref = ngx_lua_app_find_handler(app, (char *) r->uri.data,
                                           r->uri.len);

    if (handler_ref == LUA_NOREF) {
        ngx_str_set(body, "");
        *status = NGX_HTTP_NOT_FOUND;
        rv = NGX_OK;
        goto done;
    }

    lua_pushvalue(co, app_index);
    app_ref = luaL_ref(co, LUA_REGISTRYINDEX);

    lua_settop(co, 0);
    lua_rawgeti(co, LUA_REGISTRYINDEX, handler_ref);

    if (!lua_isfunction(co, -1)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" route handler is not a function",
                      &llcf->lua_web_file);
        goto done;
    }

    rc = lua_resume(co, L, 0, &nresults);

    if (rc == LUA_OK) {
        rv = ngx_http_lua_read_result(r, co, nresults, status, body);

    } else if (rc == LUA_YIELD) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" route handler yielded without "
                      "an async continuation",
                      &llcf->lua_web_file);

    } else {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua_web_file \"%V\" route handler failed: %s",
                      &llcf->lua_web_file, lua_tostring(co, -1));
    }

done:

    (void) lua_closethread(co, L);
    luaL_unref(L, LUA_REGISTRYINDEX, app_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, co_ref);

    return rv;
}


static ngx_int_t
ngx_http_lua_read_result(ngx_http_request_t *r, lua_State *co, int nresults,
    ngx_uint_t *status, ngx_str_t *body)
{
    int            isnum, table;
    size_t         len;
    u_char        *p;
    const char    *text;
    lua_Integer    code;

    if (nresults < 1) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler returned no values");
        return NGX_ERROR;
    }

    table = lua_absindex(co, -nresults);

    if (!lua_istable(co, table)) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler must return { status, text }");
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
    text = lua_tolstring(co, -1, &len);

    if (text == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler returned invalid text");
        lua_pop(co, 1);
        return NGX_ERROR;
    }

    if (len == 0) {
        body->data = (u_char *) "";
        body->len = 0;
        *status = (ngx_uint_t) code;
        lua_pop(co, 1);
        return NGX_OK;
    }

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        lua_pop(co, 1);
        return NGX_ERROR;
    }

    ngx_memcpy(p, text, len);

    body->data = p;
    body->len = len;
    *status = (ngx_uint_t) code;

    lua_pop(co, 1);

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_send_response(ngx_http_request_t *r, ngx_uint_t status,
    ngx_str_t *body)
{
    ngx_buf_t    *b;
    ngx_chain_t   out;
    ngx_int_t     rc;

    r->headers_out.status = status;
    r->headers_out.content_length_n = body->len;
    ngx_str_set(&r->headers_out.content_type, "text/plain");

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only
        || status == NGX_HTTP_NO_CONTENT || status == NGX_HTTP_NOT_MODIFIED)
    {
        return rc;
    }

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = body->data;
    b->last = body->data + body->len;
    b->memory = 1;
    b->last_buf = (r == r->main) ? 1 : 0;
    b->last_in_chain = 1;

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
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
    ngx_lua_app_register(conf->lua);

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
