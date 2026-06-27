/*
 * Copyright (C) Zhidao HONG
 */


#include <ngx_config.h>
#include <ngx_core.h>

#include <lua.h>
#include <lauxlib.h>

#include "ngx_lua.h"
#include "ngx_lua_web.h"


#define NGX_LUA_WEB_STREAM_METATABLE  "ngx_lua_web_stream"
#define NGX_LUA_WEB_STREAM_CONTROLLER_METATABLE \
    "ngx_lua_web_stream_controller"


typedef struct {
    int                    start_ref;
    int                    pull_ref;
} ngx_lua_web_stream_lua_source_t;


typedef struct {
    ngx_lua_web_stream_t             *stream;
    ngx_lua_web_stream_lua_source_t   source;
    unsigned                          has_source:1;
} ngx_lua_web_stream_ud_t;


typedef struct {
    ngx_lua_web_stream_t  *stream;
} ngx_lua_web_stream_controller_ud_t;


struct ngx_lua_web_stream_s {
    ngx_chain_t                  *in;
    ngx_chain_t                 **last;
    ngx_lua_web_stream_source_t  *source;
    ngx_lua_web_stream_wake_pt    wake;
    void                         *wake_data;
    unsigned                      closed:1;
};


static int ngx_lua_web_stream_new(lua_State *L);
static ngx_int_t ngx_lua_web_stream_lua_source_start(
    lua_State *L, ngx_lua_web_stream_t *stream, void *data);
static ngx_int_t ngx_lua_web_stream_lua_source_pull(
    lua_State *L, ngx_lua_web_stream_t *stream, void *data);
static ngx_int_t ngx_lua_web_stream_lua_source_call(
    lua_State *L, ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_lua_source_t *source, int ref);
static void ngx_lua_web_stream_push_controller(lua_State *L,
    ngx_lua_web_stream_t *stream);
static int ngx_lua_web_stream_controller_enqueue_method(lua_State *L);
static int ngx_lua_web_stream_controller_close_method(lua_State *L);
static int ngx_lua_web_stream_gc(lua_State *L);
static int ngx_lua_web_stream_read_method(lua_State *L);
static int ngx_lua_web_stream_read_continue(lua_State *L, int status,
    lua_KContext ctx);
static int ngx_lua_web_stream_get_reader_method(lua_State *L);
static ngx_lua_web_stream_t *ngx_lua_web_stream_check(lua_State *L, int index);
static ngx_lua_web_stream_t *ngx_lua_web_stream_controller_check(lua_State *L,
    int index);
static void ngx_lua_web_stream_register_metatable(lua_State *L);
static void ngx_lua_web_stream_register_controller_metatable(lua_State *L);


ngx_lua_web_stream_t *
ngx_lua_web_stream_create(lua_State *L)
{
    ngx_lua_ctx_t         *ctx;
    ngx_lua_web_stream_t  *stream;

    if (L == NULL) {
        return NULL;
    }

    ctx = ngx_lua_get_ctx(L);
    if (ctx == NULL) {
        return NULL;
    }

    stream = ngx_lua_pcalloc(ctx->pool, sizeof(ngx_lua_web_stream_t));
    if (stream == NULL) {
        return NULL;
    }

    stream->last = &stream->in;

    return stream;
}


void
ngx_lua_web_stream_register(lua_State *L)
{
    lua_newtable(L);

    lua_pushliteral(L, "new");
    lua_pushcfunction(L, ngx_lua_web_stream_new);
    lua_rawset(L, -3);

    lua_setglobal(L, "Stream");
}


void
ngx_lua_web_stream_push(lua_State *L, ngx_lua_web_stream_t *stream)
{
    ngx_lua_web_stream_ud_t  *ud;

    ud = lua_newuserdatauv(L, sizeof(ngx_lua_web_stream_ud_t), 0);
    ud->stream = stream;

    ngx_lua_web_stream_register_metatable(L);

    lua_setmetatable(L, -2);
}


static int
ngx_lua_web_stream_new(lua_State *L)
{
    ngx_lua_ctx_t                    *ctx;
    ngx_lua_web_stream_ud_t          *ud;
    ngx_lua_web_stream_t             *stream;
    ngx_lua_web_stream_source_t      *source;
    ngx_lua_web_stream_lua_source_t  *lua_source;

    ctx = ngx_lua_get_ctx(L);
    if (ctx == NULL || ctx->data == NULL) {
        return luaL_error(L, "Stream.new() is only allowed in request scope");
    }

    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "start");
    if (!lua_isnil(L, -1)) {
        luaL_checktype(L, -1, LUA_TFUNCTION);
    }

    lua_getfield(L, 1, "pull");
    if (!lua_isnil(L, -1)) {
        luaL_checktype(L, -1, LUA_TFUNCTION);
    }

    stream = ngx_lua_web_stream_create(L);
    if (stream == NULL) {
        return luaL_error(L, "failed to create stream");
    }

    source = ngx_lua_pcalloc(ctx->pool, sizeof(ngx_lua_web_stream_source_t));
    if (source == NULL) {
        return luaL_error(L, "failed to create stream source");
    }

    ngx_lua_web_stream_push(L, stream);

    ud = lua_touserdata(L, -1);
    ud->has_source = 1;
    lua_source = &ud->source;

    lua_source->start_ref = LUA_NOREF;
    lua_source->pull_ref = LUA_NOREF;

    if (lua_isfunction(L, 2)) {
        lua_pushvalue(L, 2);
        lua_source->start_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    if (lua_isfunction(L, 3)) {
        lua_pushvalue(L, 3);
        lua_source->pull_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    lua_remove(L, 3);
    lua_remove(L, 2);

    source->data = lua_source;
    source->start = ngx_lua_web_stream_lua_source_start;
    source->pull = ngx_lua_web_stream_lua_source_pull;

    if (ngx_lua_web_stream_set_source(stream, source) != NGX_OK) {
        return luaL_error(L, "failed to set stream source");
    }

    if (ngx_lua_web_stream_start_source(L, stream) != NGX_OK) {
        return luaL_error(L, "stream start failed");
    }

    return 1;
}


static ngx_int_t
ngx_lua_web_stream_lua_source_start(lua_State *L,
    ngx_lua_web_stream_t *stream, void *data)
{
    ngx_lua_web_stream_lua_source_t  *source = data;

    return ngx_lua_web_stream_lua_source_call(L, stream, source,
                                             source->start_ref);
}


static ngx_int_t
ngx_lua_web_stream_lua_source_pull(lua_State *L, ngx_lua_web_stream_t *stream,
    void *data)
{
    ngx_int_t                            rc;
    ngx_lua_web_stream_lua_source_t  *source = data;

    if (source->pull_ref == LUA_NOREF || source->pull_ref == LUA_REFNIL)
    {
        ngx_lua_web_stream_close(stream);
        return NGX_OK;
    }

    rc = ngx_lua_web_stream_lua_source_call(L, stream, source,
                                            source->pull_ref);
    if (rc != NGX_OK) {
        return rc;
    }

    if (!ngx_lua_web_stream_has_data(stream)
        && !ngx_lua_web_stream_is_closed(stream))
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_lua_web_stream_lua_source_call(lua_State *L, ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_lua_source_t *source, int ref)
{
    const char  *error;

    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
        return NGX_OK;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    ngx_lua_web_stream_push_controller(L, stream);

    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        error = lua_tostring(L, -1);
        luaL_error(L, "stream source failed: %s",
                   error ? error : "unknown error");
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_lua_web_stream_push_controller(lua_State *L, ngx_lua_web_stream_t *stream)
{
    ngx_lua_web_stream_controller_ud_t  *ud;

    ud = lua_newuserdatauv(L, sizeof(ngx_lua_web_stream_controller_ud_t), 0);
    ud->stream = stream;

    ngx_lua_web_stream_register_controller_metatable(L);

    lua_setmetatable(L, -2);
}


static int
ngx_lua_web_stream_controller_enqueue_method(lua_State *L)
{
    size_t                  len;
    const char             *data;
    ngx_buf_t              *b;
    ngx_chain_t            *cl;
    ngx_lua_ctx_t          *ctx;
    ngx_lua_web_stream_t   *stream;

    stream = ngx_lua_web_stream_controller_check(L, 1);
    data = luaL_checklstring(L, 2, &len);

    if (len == 0) {
        return 0;
    }

    ctx = ngx_lua_get_ctx(L);
    if (ctx == NULL) {
        return luaL_error(L, "missing Lua context");
    }

    b = ngx_lua_pcalloc(ctx->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return luaL_error(L, "failed to allocate stream buffer");
    }

    b->start = ngx_lua_palloc(ctx->pool, len);
    if (b->start == NULL && len != 0) {
        return luaL_error(L, "failed to allocate stream buffer data");
    }

    if (len != 0) {
        ngx_memcpy(b->start, data, len);
    }

    b->pos = b->start;
    b->last = b->start + len;
    b->end = b->last;
    b->memory = 1;

    cl = ngx_lua_pcalloc(ctx->pool, sizeof(ngx_chain_t));
    if (cl == NULL) {
        return luaL_error(L, "failed to allocate stream chain");
    }

    cl->buf = b;

    ngx_lua_web_stream_enqueue(stream, cl);

    return 0;
}


static int
ngx_lua_web_stream_controller_close_method(lua_State *L)
{
    ngx_lua_web_stream_t  *stream;

    stream = ngx_lua_web_stream_controller_check(L, 1);
    ngx_lua_web_stream_close(stream);

    return 0;
}


ngx_int_t
ngx_lua_web_stream_set_source(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source)
{
    if (stream == NULL || source == NULL) {
        return NGX_ERROR;
    }

    stream->source = source;

    return NGX_OK;
}


ngx_int_t
ngx_lua_web_stream_start_source(lua_State *L, ngx_lua_web_stream_t *stream)
{
    if (stream == NULL || stream->source == NULL
        || stream->source->start == NULL)
    {
        return NGX_OK;
    }

    return stream->source->start(L, stream, stream->source->data);
}


ngx_int_t
ngx_lua_web_stream_pull_source(lua_State *L, ngx_lua_web_stream_t *stream)
{
    if (stream == NULL || stream->source == NULL
        || stream->source->pull == NULL)
    {
        return NGX_OK;
    }

    return stream->source->pull(L, stream, stream->source->data);
}


ngx_int_t
ngx_lua_web_stream_read(lua_State *L, ngx_lua_web_stream_t *stream)
{
    ngx_buf_t    *b;
    ngx_chain_t  *in;
    ngx_int_t     rc;

    if (stream == NULL) {
        return NGX_ERROR;
    }

    for ( ;; ) {
        in = stream->in;

        if (in != NULL) {
            stream->in = in->next;
            if (stream->in == NULL) {
                stream->last = &stream->in;
            }

            in->next = NULL;
            b = in->buf;

            if (b == NULL || b->pos == b->last) {
                continue;
            }

            lua_pushlstring(L, (char *) b->pos, b->last - b->pos);
            b->pos = b->last;

            return NGX_OK;
        }

        if (stream->closed) {
            lua_pushnil(L);
            return NGX_OK;
        }

        rc = ngx_lua_web_stream_pull_source(L, stream);
        if (rc != NGX_OK) {
            return rc;
        }
    }
}


void
ngx_lua_web_stream_set_wake(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_wake_pt wake, void *data)
{
    stream->wake = wake;
    stream->wake_data = data;
}


void
ngx_lua_web_stream_wake(ngx_lua_web_stream_t *stream)
{
    ngx_lua_web_stream_wake_pt  wake;
    void                       *data;

    if (stream->wake == NULL) {
        return;
    }

    wake = stream->wake;
    data = stream->wake_data;
    stream->wake = NULL;
    stream->wake_data = NULL;

    wake(stream, data);
}


ngx_uint_t
ngx_lua_web_stream_has_data(ngx_lua_web_stream_t *stream)
{
    return stream->in != NULL;
}


ngx_uint_t
ngx_lua_web_stream_is_closed(ngx_lua_web_stream_t *stream)
{
    return stream->closed;
}


void
ngx_lua_web_stream_enqueue(ngx_lua_web_stream_t *stream, ngx_chain_t *in)
{
    ngx_chain_t  *next;

    while (in) {
        next = in->next;
        in->next = NULL;

        *stream->last = in;
        stream->last = &in->next;

        in = next;
    }
}


void
ngx_lua_web_stream_close(ngx_lua_web_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }

    stream->closed = 1;
}


static int
ngx_lua_web_stream_gc(lua_State *L)
{
    ngx_lua_web_stream_ud_t  *ud;

    ud = luaL_checkudata(L, 1, NGX_LUA_WEB_STREAM_METATABLE);

    if (ud->has_source) {
        if (ud->source.start_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, ud->source.start_ref);
            ud->source.start_ref = LUA_NOREF;
        }

        if (ud->source.pull_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, ud->source.pull_ref);
            ud->source.pull_ref = LUA_NOREF;
        }

        ud->has_source = 0;
    }

    ud->stream = NULL;

    return 0;
}


static int
ngx_lua_web_stream_read_method(lua_State *L)
{
    ngx_int_t              rc;
    ngx_lua_web_stream_t  *stream;

    stream = ngx_lua_web_stream_check(L, 1);

    rc = ngx_lua_web_stream_read(L, stream);

    if (rc == NGX_OK) {
        return 1;
    }

    if (rc == NGX_AGAIN) {
        lua_pushvalue(L, 1);

        return lua_yieldk(L, 1, 0, ngx_lua_web_stream_read_continue);
    }

    return luaL_error(L, "stream read failed");
}


static int
ngx_lua_web_stream_read_continue(lua_State *L, int status,
    lua_KContext opaque)
{
    ngx_int_t              rc;
    ngx_lua_web_stream_t  *stream;

    (void) status;
    (void) opaque;

    stream = ngx_lua_web_stream_check(L, 1);

    rc = ngx_lua_web_stream_read(L, stream);

    if (rc == NGX_OK) {
        return 1;
    }

    if (rc == NGX_AGAIN) {
        lua_pushvalue(L, 1);

        return lua_yieldk(L, 1, 0, ngx_lua_web_stream_read_continue);
    }

    return luaL_error(L, "stream read failed");
}


static int
ngx_lua_web_stream_get_reader_method(lua_State *L)
{
    (void) ngx_lua_web_stream_check(L, 1);

    lua_pushvalue(L, 1);

    return 1;
}


static ngx_lua_web_stream_t *
ngx_lua_web_stream_check(lua_State *L, int index)
{
    ngx_lua_web_stream_ud_t  *ud;

    ud = luaL_checkudata(L, index, NGX_LUA_WEB_STREAM_METATABLE);
    luaL_argcheck(L, ud != NULL && ud->stream != NULL, index, "Stream expected");

    return ud->stream;
}


ngx_lua_web_stream_t *
ngx_lua_web_stream_get(lua_State *L, int index)
{
    ngx_lua_web_stream_ud_t  *ud;

    ud = luaL_testudata(L, index, NGX_LUA_WEB_STREAM_METATABLE);
    if (ud == NULL) {
        return NULL;
    }

    return ud->stream;
}


static ngx_lua_web_stream_t *
ngx_lua_web_stream_controller_check(lua_State *L, int index)
{
    ngx_lua_web_stream_controller_ud_t  *ud;

    ud = luaL_checkudata(L, index, NGX_LUA_WEB_STREAM_CONTROLLER_METATABLE);
    luaL_argcheck(L, ud != NULL && ud->stream != NULL, index,
                  "Stream controller expected");

    return ud->stream;
}


static void
ngx_lua_web_stream_register_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_STREAM_METATABLE)) {
        lua_pushliteral(L, "__name");
        lua_pushliteral(L, "Stream");
        lua_rawset(L, -3);

        lua_pushliteral(L, "__gc");
        lua_pushcfunction(L, ngx_lua_web_stream_gc);
        lua_rawset(L, -3);

        lua_pushliteral(L, "__index");
        lua_newtable(L);

        lua_pushliteral(L, "read");
        lua_pushcfunction(L, ngx_lua_web_stream_read_method);
        lua_rawset(L, -3);

        lua_pushliteral(L, "getReader");
        lua_pushcfunction(L, ngx_lua_web_stream_get_reader_method);
        lua_rawset(L, -3);

        lua_rawset(L, -3);
    }
}


static void
ngx_lua_web_stream_register_controller_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_STREAM_CONTROLLER_METATABLE)) {
        lua_pushliteral(L, "__name");
        lua_pushliteral(L, "StreamController");
        lua_rawset(L, -3);

        lua_pushliteral(L, "__index");
        lua_newtable(L);

        lua_pushliteral(L, "enqueue");
        lua_pushcfunction(L, ngx_lua_web_stream_controller_enqueue_method);
        lua_rawset(L, -3);

        lua_pushliteral(L, "close");
        lua_pushcfunction(L, ngx_lua_web_stream_controller_close_method);
        lua_rawset(L, -3);

        lua_rawset(L, -3);
    }
}
