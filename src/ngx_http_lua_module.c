
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
static void ngx_http_lua_cleanup(void *data);
static void ngx_http_lua_body_handler(ngx_http_request_t *r);
static void ngx_http_lua_content_continue(ngx_lua_web_stream_t *stream,
    void *data);
static void ngx_http_lua_response_continue(ngx_lua_web_stream_t *stream,
    void *data);
static void ngx_http_lua_response_write_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_response_output(ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_response_wait_writable(ngx_http_request_t *r);
static ngx_int_t ngx_http_lua_content_handler(ngx_http_request_t *r,
    ngx_uint_t narg);
static ngx_int_t ngx_http_lua_send_response(ngx_http_request_t *r);
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
    ngx_lua_web_stream_register(lmcf->lua);

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


static void
ngx_http_lua_cleanup(void *data)
{
    ngx_http_lua_ctx_t  *ctx = data;

    if (ctx->state != NULL && ctx->request_stream_ref != LUA_NOREF) {
        luaL_unref(ctx->state, LUA_REGISTRYINDEX, ctx->request_stream_ref);
        ctx->request_stream_ref = LUA_NOREF;
    }

    if (ctx->state != NULL && ctx->response_stream_ref != LUA_NOREF) {
        luaL_unref(ctx->state, LUA_REGISTRYINDEX, ctx->response_stream_ref);
        ctx->response_stream_ref = LUA_NOREF;
    }

    if (ctx->state != NULL && ctx->coroutine_ref != LUA_NOREF) {
        luaL_unref(ctx->state, LUA_REGISTRYINDEX, ctx->coroutine_ref);
        ctx->coroutine_ref = LUA_NOREF;
    }

    ngx_lua_ctx_destroy(ctx->lua);
    ctx->lua = NULL;
}


static void
ngx_http_lua_body_handler(ngx_http_request_t *r)
{
    lua_State      *coroutine;
    lua_State      *state;
    ngx_int_t                  rc;
    ngx_http_lua_ctx_t       *ctx;
    ngx_http_lua_main_conf_t  *lmcf;
    ngx_http_lua_loc_conf_t   *llcf;
    ngx_lua_web_stream_source_t  *body_source;
    ngx_lua_web_stream_t         *body;

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);
    state = lmcf->lua;

    coroutine = lua_newthread(state);
    if (coroutine == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    ctx->state = state;
    ctx->coroutine = coroutine;
    ctx->coroutine_ref = luaL_ref(state, LUA_REGISTRYINDEX);

    ngx_lua_set_ctx(coroutine, ctx->lua);

    lua_rawgeti(state, LUA_REGISTRYINDEX, llcf->handler_ref);
    lua_xmove(state, coroutine, 1);

    body = ngx_lua_web_stream_create(coroutine);
    if (body == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    body_source = ngx_http_lua_request_body_source_create(coroutine, r);
    if (body_source == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    if (ngx_lua_web_stream_set_source(body, body_source) != NGX_OK) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    ctx->request_body = body;

    rc = ngx_lua_web_stream_start_source(coroutine, body);
    if (rc != NGX_OK) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    ngx_lua_web_stream_push(coroutine, body);

    rc = ngx_http_lua_content_handler(r, 1);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
}


static void
ngx_http_lua_content_continue(ngx_lua_web_stream_t *stream, void *data)
{
    ngx_http_request_t  *r = data;
    ngx_http_lua_ctx_t  *ctx;
    ngx_int_t  rc;

    (void) stream;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    if (ctx->request_stream_ref != LUA_NOREF) {
        luaL_unref(ctx->state, LUA_REGISTRYINDEX, ctx->request_stream_ref);
        ctx->request_stream_ref = LUA_NOREF;
    }

    rc = ngx_http_lua_content_handler(r, 0);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
}


static void
ngx_http_lua_response_continue(ngx_lua_web_stream_t *stream, void *data)
{
    ngx_http_request_t  *r = data;
    ngx_int_t            rc;

    (void) stream;

    rc = ngx_http_lua_response_output(r);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
}


static void
ngx_http_lua_response_write_handler(ngx_http_request_t *r)
{
    ngx_int_t            rc;
    ngx_connection_t    *c;
    ngx_event_t         *wev;

    c = r->connection;
    wev = c->write;

    if (wev->timedout) {
        c->timedout = 1;
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }

    if (wev->delayed || r->aio) {
        if (ngx_http_lua_response_wait_writable(r) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_ERROR);
        }

        return;
    }

    rc = ngx_http_output_filter(r, NULL);
    if (rc == NGX_ERROR) {
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (r->buffered || r->postponed || c->buffered) {
        if (ngx_http_lua_response_wait_writable(r) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_ERROR);
        }

        return;
    }

    rc = ngx_http_lua_response_output(r);

    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_http_finalize_request(r, rc);
}


static ngx_int_t
ngx_http_lua_response_output(ngx_http_request_t *r)
{
    ngx_chain_t          *out;
    ngx_int_t            rc;
    ngx_connection_t    *c;
    ngx_http_lua_ctx_t  *ctx;

    c = r->connection;
    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    for ( ;; ) {
        if (r->buffered || r->postponed || c->buffered) {
            return ngx_http_lua_response_wait_writable(r);
        }

        rc = ngx_lua_web_stream_take_chain(ctx->coroutine, ctx->response_body,
                                           &out);

        if (rc == NGX_OK) {
            rc = ngx_http_output_filter(r, out);
            if (rc == NGX_ERROR) {
                return NGX_ERROR;
            }

            continue;
        }

        if (rc == NGX_DONE) {
            rc = ngx_http_send_special(r, NGX_HTTP_LAST);
            if (rc == NGX_AGAIN) {
                return ngx_http_lua_response_wait_writable(r);
            }

            return rc;
        }

        if (rc == NGX_AGAIN) {
            ngx_lua_web_stream_set_wake(ctx->response_body,
                                        ngx_http_lua_response_continue, r);
            return NGX_AGAIN;
        }

        return rc;
    }
}


static ngx_int_t
ngx_http_lua_response_wait_writable(ngx_http_request_t *r)
{
    ngx_event_t               *wev;
    ngx_http_core_loc_conf_t  *clcf;

    wev = r->connection->write;
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (!wev->delayed && !wev->timer_set) {
        ngx_add_timer(wev, clcf->send_timeout);
    }

    if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_AGAIN;
}


static ngx_int_t
ngx_http_lua_content_handler(ngx_http_request_t *r, ngx_uint_t narg)
{
    int                    nres;
    int                    status;
    ngx_http_lua_ctx_t    *ctx;
    ngx_lua_web_stream_t  *stream;
    ngx_lua_web_stream_t  *response;

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    status = lua_resume(ctx->coroutine, ctx->state, (int) narg, &nres);

    if (status != LUA_OK) {
        if (status == LUA_YIELD) {
            stream = nres == 1 ? ngx_lua_web_stream_get(ctx->coroutine, -1)
                               : NULL;
            if (stream == NULL) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "lua handler yielded without a stream wake");
                return NGX_ERROR;
            }

            lua_pushvalue(ctx->coroutine, -1);
            ctx->request_stream_ref = luaL_ref(ctx->coroutine,
                                               LUA_REGISTRYINDEX);

            ngx_lua_web_stream_set_wake(stream,
                                        ngx_http_lua_content_continue, r);
            lua_pop(ctx->coroutine, nres);

            return NGX_AGAIN;
        }

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "lua handler failed: %s",
                      lua_tostring(ctx->coroutine, -1));
        lua_pop(ctx->coroutine, 1);
        return NGX_ERROR;
    }

    response = nres == 1 ? ngx_lua_web_stream_get(ctx->coroutine, -1)
                         : NULL;

    if (response == NULL) {
        lua_settop(ctx->coroutine, 0);
        return NGX_HTTP_NOT_FOUND;
    }

    lua_pushvalue(ctx->coroutine, -1);
    ctx->response_stream_ref = luaL_ref(ctx->coroutine, LUA_REGISTRYINDEX);
    ctx->response_body = response;

    lua_settop(ctx->coroutine, 0);

    return ngx_http_lua_send_response(r);
}


static ngx_int_t
ngx_http_lua_send_response(ngx_http_request_t *r)
{
    ngx_int_t  rc;

    r->write_event_handler = ngx_http_lua_response_write_handler;
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = -1;
    ngx_str_set(&r->headers_out.content_type, "text/plain");

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_lua_response_output(r);
}


static ngx_int_t
ngx_http_lua_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_http_cleanup_t        *cln;
    ngx_http_lua_ctx_t        *ctx;
    ngx_http_lua_main_conf_t  *lmcf;
    ngx_http_lua_loc_conf_t   *llcf;
    ngx_lua_ctx_t             *lua_ctx;

    if (r != r->main) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);

    if (lmcf == NULL || lmcf->lua == NULL || llcf == NULL
        || llcf->handler_ref == LUA_NOREF)
    {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_lua_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->coroutine_ref = LUA_NOREF;
    ctx->request_stream_ref = LUA_NOREF;
    ctx->response_stream_ref = LUA_NOREF;

    lua_ctx = ngx_lua_ctx_create(r->connection->log);
    if (lua_ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cln = ngx_http_cleanup_add(r, 0);
    if (cln == NULL) {
        ngx_lua_ctx_destroy(lua_ctx);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cln->handler = ngx_http_lua_cleanup;
    cln->data = ctx;

    ctx->lua = lua_ctx;
    lua_ctx->data = r;
    ngx_http_set_ctx(r, ctx, ngx_http_lua_module);

    r->request_body_no_buffering = 1;

    rc = ngx_http_read_client_request_body(r, ngx_http_lua_body_handler);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}
