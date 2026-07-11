/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua_web.h"
#include "ngx_lua.h"

#include <lauxlib.h>
#include <stdint.h>


#define NGX_LUA_WEB_STREAM_METATABLE  "ngx_lua_web.ReadableStream"
#define NGX_LUA_WEB_STREAM_CONTROLLER_METATABLE                              \
    "ngx_lua_web.ReadableStreamDefaultController"
#define NGX_LUA_WEB_STREAM_READER_METATABLE                                   \
    "ngx_lua_web.ReadableStreamDefaultReader"


typedef struct ngx_lua_web_stream_chunk_s  ngx_lua_web_stream_chunk_t;


struct ngx_lua_web_stream_chunk_s {
    ngx_chain_t                 *bufs;
    ngx_lua_web_stream_chunk_t  *next;
};


struct ngx_lua_web_stream_s {
    ngx_lua_web_stream_source_t  *source;
    ngx_pool_t                   *pool;
    ngx_lua_web_stream_chunk_t   *chunks;
    ngx_lua_web_stream_chunk_t  **last_chunk;
    ngx_lua_web_stream_chunk_t   *free_chunks;
    ngx_lua_web_stream_wake_pt    wake;
    void                         *data;
    unsigned  closed:1;
    unsigned  errored:1;
    unsigned  locked:1;
    unsigned  body_used:1;
};


typedef struct {
    ngx_lua_web_stream_t         *stream;
} ngx_lua_web_stream_reader_t;


typedef struct {
    ngx_lua_web_stream_t         *stream;
} ngx_lua_web_stream_controller_t;


typedef struct {
    lua_State                    *L;
    int                           controller_ref;
    int                           pull_ref;
} ngx_lua_web_stream_lua_source_t;


static int ngx_lua_web_stream_new(lua_State *L);
static int ngx_lua_web_stream_get_reader(lua_State *L);
static int ngx_lua_web_stream_controller_enqueue(lua_State *L);
static int ngx_lua_web_stream_controller_close(lua_State *L);
static int ngx_lua_web_stream_controller_gc(lua_State *L);
static ngx_lua_web_stream_t *ngx_lua_web_stream_controller_check_stream(
    lua_State *L, ngx_lua_web_stream_controller_t *controller);
static ngx_lua_web_stream_controller_t *ngx_lua_web_stream_push_controller(
    lua_State *L, ngx_lua_web_stream_t *stream, int stream_index);
static ngx_int_t ngx_lua_web_stream_lua_source_pull(
    ngx_lua_web_stream_t *stream, ngx_lua_web_stream_source_t *source);
static void ngx_lua_web_stream_lua_source_cleanup(void *data);
static int ngx_lua_web_stream_reader_read(lua_State *L);
static int ngx_lua_web_stream_reader_continue(lua_State *L, int status,
    lua_KContext ctx);
static int ngx_lua_web_stream_reader_release_lock(lua_State *L);
static int ngx_lua_web_stream_reader_gc(lua_State *L);
static ngx_lua_web_stream_t *ngx_lua_web_stream_reader_check_stream(
    lua_State *L, ngx_lua_web_stream_reader_t *reader);
static void ngx_lua_web_stream_reader_wake(void *data);


static const luaL_Reg  ngx_lua_web_stream_global_methods[] = {
    { "new", ngx_lua_web_stream_new },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_web_stream_methods[] = {
    { "getReader", ngx_lua_web_stream_get_reader },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_web_stream_controller_methods[] = {
    { "enqueue", ngx_lua_web_stream_controller_enqueue },
    { "close", ngx_lua_web_stream_controller_close },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_web_stream_reader_methods[] = {
    { "read", ngx_lua_web_stream_reader_read },
    { "releaseLock", ngx_lua_web_stream_reader_release_lock },
    { NULL, NULL }
};


static void ngx_lua_web_stream_register_metatable(lua_State *L);
static void ngx_lua_web_stream_controller_register_metatable(lua_State *L);
static void ngx_lua_web_stream_reader_register_metatable(lua_State *L);
static ngx_int_t ngx_lua_web_stream_dequeue_buffered_chunk(
    ngx_lua_web_stream_t *stream, ngx_chain_t **bufs);
static ngx_int_t ngx_lua_web_stream_push_lua_chunk(lua_State *L,
    ngx_lua_web_stream_t *stream, ngx_chain_t *cl);
static void ngx_lua_web_stream_free_chunk(ngx_lua_web_stream_t *stream,
    ngx_chain_t *cl);
static int ngx_lua_web_stream_read_all(lua_State *L,
    ngx_lua_web_stream_t *stream, lua_KFunction continuation,
    ngx_uint_t parse_json);
static int ngx_lua_web_stream_read_all_finish(lua_State *L,
    ngx_uint_t parse_json);
static int ngx_lua_web_stream_read_text_continue(lua_State *L, int status,
    lua_KContext ctx);
static int ngx_lua_web_stream_read_json_continue(lua_State *L, int status,
    lua_KContext ctx);


/* Lua metatables. */

static void
ngx_lua_web_stream_register_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_STREAM_METATABLE)) {
        lua_pushliteral(L, "ReadableStream");
        lua_setfield(L, -2, "__name");
        luaL_setfuncs(L, ngx_lua_web_stream_methods, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }

    lua_pop(L, 1);
}


static void
ngx_lua_web_stream_controller_register_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_STREAM_CONTROLLER_METATABLE)) {
        lua_pushliteral(L, "ReadableStreamDefaultController");
        lua_setfield(L, -2, "__name");
        luaL_setfuncs(L, ngx_lua_web_stream_controller_methods, 0);
        lua_pushcfunction(L, ngx_lua_web_stream_controller_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }

    lua_pop(L, 1);
}


static void
ngx_lua_web_stream_reader_register_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_STREAM_READER_METATABLE)) {
        lua_pushliteral(L, "ReadableStreamDefaultReader");
        lua_setfield(L, -2, "__name");
        luaL_setfuncs(L, ngx_lua_web_stream_reader_methods, 0);
        lua_pushcfunction(L, ngx_lua_web_stream_reader_gc);
        lua_setfield(L, -2, "__gc");
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }

    lua_pop(L, 1);
}


/* C stream API. */

ngx_lua_web_stream_t *
ngx_lua_web_stream_create(lua_State *L, ngx_pool_t *pool)
{
    ngx_lua_web_stream_t  *stream;

    stream = lua_newuserdatauv(L, sizeof(ngx_lua_web_stream_t), 0);
    if (stream == NULL) {
        return NULL;
    }

    ngx_memzero(stream, sizeof(ngx_lua_web_stream_t));
    stream->pool = pool;
    stream->last_chunk = &stream->chunks;

    luaL_setmetatable(L, NGX_LUA_WEB_STREAM_METATABLE);

    return stream;
}


ngx_lua_web_stream_t *
ngx_lua_web_stream_get(lua_State *L, int index)
{
    return luaL_testudata(L, index, NGX_LUA_WEB_STREAM_METATABLE);
}


ngx_uint_t
ngx_lua_web_stream_body_used(ngx_lua_web_stream_t *stream)
{
    return stream->body_used;
}


int
ngx_lua_web_stream_read_text(lua_State *L, ngx_lua_web_stream_t *stream)
{
    if (stream == NULL) {
        lua_pushliteral(L, "");
        return 1;
    }

    if (stream->locked) {
        return luaL_error(L, "body stream is locked");
    }

    if (stream->body_used) {
        return luaL_error(L, "body stream is already used");
    }

    lua_settop(L, 1);
    lua_newtable(L);

    return ngx_lua_web_stream_read_all(L, stream,
                                       ngx_lua_web_stream_read_text_continue,
                                       0);
}


int
ngx_lua_web_stream_read_json(lua_State *L, ngx_lua_web_stream_t *stream)
{
    if (stream == NULL) {
        lua_pushliteral(L, "");
        ngx_lua_json_decode(L, -1);
        return 1;
    }

    if (stream->locked) {
        return luaL_error(L, "body stream is locked");
    }

    if (stream->body_used) {
        return luaL_error(L, "body stream is already used");
    }

    lua_settop(L, 1);
    lua_newtable(L);

    return ngx_lua_web_stream_read_all(L, stream,
                                       ngx_lua_web_stream_read_json_continue,
                                       1);
}


void
ngx_lua_web_stream_register(lua_State *L)
{
    ngx_lua_web_stream_register_metatable(L);
    ngx_lua_web_stream_controller_register_metatable(L);
    ngx_lua_web_stream_reader_register_metatable(L);

    lua_newtable(L);
    luaL_setfuncs(L, ngx_lua_web_stream_global_methods, 0);
    lua_setglobal(L, "ReadableStream");
}


void
ngx_lua_web_stream_set_source(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source)
{
    stream->source = source;
}


ngx_int_t
ngx_lua_web_stream_enqueue_chunk(ngx_lua_web_stream_t *stream,
    ngx_chain_t *bufs)
{
    ngx_lua_web_stream_chunk_t  *chunk;

    if (bufs == NULL) {
        return NGX_OK;
    }

    if (stream->closed || stream->errored) {
        return NGX_ERROR;
    }

    chunk = stream->free_chunks;

    if (chunk != NULL) {
        stream->free_chunks = chunk->next;

    } else {
        chunk = ngx_palloc(stream->pool,
                           sizeof(ngx_lua_web_stream_chunk_t));
        if (chunk == NULL) {
            return NGX_ERROR;
        }
    }

    chunk->bufs = bufs;
    chunk->next = NULL;

    *stream->last_chunk = chunk;
    stream->last_chunk = &chunk->next;

    return NGX_OK;
}


ngx_int_t
ngx_lua_web_stream_dequeue_chunk(ngx_lua_web_stream_t *stream,
    ngx_chain_t **bufs)
{
    ngx_int_t  pull_rc, rc;

    *bufs = NULL;
    stream->body_used = 1;

    /* Deliver queued chunks before reporting a terminal stream state. */
    rc = ngx_lua_web_stream_dequeue_buffered_chunk(stream, bufs);
    if (rc == NGX_OK) {
        return rc;
    }

    if (stream->errored) {
        return NGX_ERROR;
    }

    if (stream->closed) {
        return NGX_DONE;
    }

    if (stream->source == NULL) {
        return NGX_DONE;
    }

    pull_rc = stream->source->pull(stream, stream->source);

    if (pull_rc == NGX_ERROR) {
        stream->errored = 1;

    } else if (pull_rc == NGX_DONE) {
        stream->closed = 1;
    }

    rc = ngx_lua_web_stream_dequeue_buffered_chunk(stream, bufs);
    if (rc == NGX_OK) {
        return rc;
    }

    if (stream->errored) {
        return NGX_ERROR;
    }

    if (stream->closed) {
        return NGX_DONE;
    }

    return pull_rc == NGX_OK ? NGX_AGAIN : pull_rc;
}


static ngx_int_t
ngx_lua_web_stream_dequeue_buffered_chunk(ngx_lua_web_stream_t *stream,
    ngx_chain_t **bufs)
{
    ngx_chain_t                 *cl;
    ngx_lua_web_stream_chunk_t  *chunk;

    chunk = stream->chunks;
    if (chunk == NULL) {
        return NGX_AGAIN;
    }

    stream->chunks = chunk->next;

    if (stream->chunks == NULL) {
        stream->last_chunk = &stream->chunks;
    }

    *bufs = chunk->bufs;
    chunk->bufs = NULL;
    chunk->next = stream->free_chunks;
    stream->free_chunks = chunk;

    for (cl = *bufs; cl != NULL; cl = cl->next) {
        if (cl->buf != NULL && cl->buf->last_buf) {
            stream->closed = 1;
        }
    }

    return NGX_OK;
}


void
ngx_lua_web_stream_close(ngx_lua_web_stream_t *stream)
{
    stream->closed = 1;
}


void
ngx_lua_web_stream_error(ngx_lua_web_stream_t *stream)
{
    stream->errored = 1;
}


void
ngx_lua_web_stream_wait(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_wake_pt wake, void *data)
{
    stream->wake = wake;
    stream->data = data;
}


void
ngx_lua_web_stream_wake(ngx_lua_web_stream_t *stream)
{
    void                         *data;
    ngx_lua_web_stream_wake_pt    wake;

    wake = stream->wake;
    if (wake == NULL) {
        return;
    }

    data = stream->data;
    stream->wake = NULL;
    stream->data = NULL;

    wake(data);
}


/* ReadableStream constructor and Lua source. */

static int
ngx_lua_web_stream_new(lua_State *L)
{
    int                              nargs, stream_index;
    ngx_lua_ctx_t                   *ctx;
    ngx_lua_web_stream_t            *stream;
    ngx_lua_web_stream_source_t     *source;
    ngx_lua_web_stream_lua_source_t *lua_source;
    ngx_pool_cleanup_t              *cln;
    unsigned                         has_pull, has_start;

    nargs = lua_gettop(L);
    has_pull = 0;
    has_start = 0;

    if (nargs > 1) {
        return luaL_error(L,
                          "ReadableStream.new() takes optional underlying "
                          "source");
    }

    if (nargs == 1 && !lua_istable(L, 1)) {
        return luaL_argerror(L, 1, "table expected");
    }

    ctx = ngx_lua_get_ctx(L);

    stream = ngx_lua_web_stream_create(L, ctx->pool);
    if (stream == NULL) {
        return luaL_error(L, "no memory");
    }

    source = ngx_pcalloc(ctx->pool, sizeof(ngx_lua_web_stream_source_t));
    if (source == NULL) {
        return luaL_error(L, "no memory");
    }

    lua_source = ngx_pcalloc(ctx->pool, sizeof(ngx_lua_web_stream_lua_source_t));
    if (lua_source == NULL) {
        return luaL_error(L, "no memory");
    }

    lua_source->L = L;
    lua_source->controller_ref = LUA_NOREF;
    lua_source->pull_ref = LUA_NOREF;

    cln = ngx_pool_cleanup_add(ctx->pool, 0);
    if (cln == NULL) {
        return luaL_error(L, "no memory");
    }

    cln->handler = ngx_lua_web_stream_lua_source_cleanup;
    cln->data = lua_source;

    source->pull = ngx_lua_web_stream_lua_source_pull;
    source->data = lua_source;
    ngx_lua_web_stream_set_source(stream, source);

    if (nargs == 0) {
        return 1;
    }

    stream_index = lua_gettop(L);

    lua_getfield(L, 1, "pull");
    if (!lua_isnil(L, -1)) {
        if (!lua_isfunction(L, -1)) {
            return luaL_argerror(L, 1, "pull must be a function");
        }

        lua_source->pull_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        has_pull = 1;

    } else {
        lua_pop(L, 1);
    }

    lua_getfield(L, 1, "start");
    if (!lua_isfunction(L, -1)) {
        if (!lua_isnil(L, -1)) {
            return luaL_argerror(L, 1, "start must be a function");
        }

        lua_pop(L, 1);

    } else {
        has_start = 1;
    }

    if (!has_pull && !has_start) {
        return 1;
    }

    if (ngx_lua_web_stream_push_controller(L, stream, stream_index) == NULL) {
        return luaL_error(L, "no memory");
    }

    lua_pushvalue(L, -1);
    lua_source->controller_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if (has_start) {
        lua_call(L, 1, 0);
    }

    lua_settop(L, stream_index);

    return 1;
}


static ngx_int_t
ngx_lua_web_stream_lua_source_pull(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source)
{
    lua_State                         *L;
    ngx_lua_web_stream_lua_source_t   *lua_source;

    lua_source = source->data;

    if (lua_source->pull_ref == LUA_NOREF) {
        return NGX_AGAIN;
    }

    L = lua_source->L;

    lua_rawgeti(L, LUA_REGISTRYINDEX, lua_source->pull_ref);
    lua_rawgeti(L, LUA_REGISTRYINDEX, lua_source->controller_ref);

    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        lua_pop(L, 1);
        stream->errored = 1;
        return NGX_ERROR;
    }

    if (stream->chunks == NULL && !stream->closed && !stream->errored) {
        return NGX_AGAIN;
    }

    return NGX_OK;
}


static void
ngx_lua_web_stream_lua_source_cleanup(void *data)
{
    ngx_lua_web_stream_lua_source_t  *source = data;

    if (source->controller_ref != LUA_NOREF) {
        luaL_unref(source->L, LUA_REGISTRYINDEX, source->controller_ref);
        source->controller_ref = LUA_NOREF;
    }

    if (source->pull_ref != LUA_NOREF) {
        luaL_unref(source->L, LUA_REGISTRYINDEX, source->pull_ref);
        source->pull_ref = LUA_NOREF;
    }
}


/* ReadableStreamDefaultController. */

static int
ngx_lua_web_stream_controller_enqueue(lua_State *L)
{
    size_t                            len;
    const char                       *data;
    ngx_buf_t                        *b;
    ngx_chain_t                      *cl;
    ngx_lua_web_stream_t             *stream;
    ngx_lua_web_stream_controller_t  *controller;

    if (lua_gettop(L) != 2) {
        return luaL_error(L,
                          "ReadableStreamDefaultController:enqueue() takes "
                          "chunk");
    }

    controller = luaL_checkudata(L, 1,
                                 NGX_LUA_WEB_STREAM_CONTROLLER_METATABLE);
    stream = ngx_lua_web_stream_controller_check_stream(L, controller);

    if (stream->errored) {
        return luaL_error(L, "ReadableStream is errored");
    }

    if (stream->closed) {
        return luaL_error(L, "ReadableStream is closed");
    }

    data = luaL_checklstring(L, 2, &len);

    if (len == 0) {
        return 0;
    }

    cl = ngx_alloc_chain_link(stream->pool);
    if (cl == NULL) {
        return luaL_error(L, "no memory");
    }

    b = ngx_create_temp_buf(stream->pool, len);
    if (b == NULL) {
        ngx_free_chain(stream->pool, cl);
        return luaL_error(L, "no memory");
    }

    ngx_memcpy(b->last, data, len);
    b->last += len;

    cl->buf = b;
    cl->next = NULL;

    if (ngx_lua_web_stream_enqueue_chunk(stream, cl) != NGX_OK) {
        ngx_free_chain(stream->pool, cl);
        return luaL_error(L, "no memory");
    }

    return 0;
}


static int
ngx_lua_web_stream_controller_close(lua_State *L)
{
    ngx_lua_web_stream_t             *stream;
    ngx_lua_web_stream_controller_t  *controller;

    if (lua_gettop(L) != 1) {
        return luaL_error(L,
                          "ReadableStreamDefaultController:close() takes no "
                          "arguments");
    }

    controller = luaL_checkudata(L, 1,
                                 NGX_LUA_WEB_STREAM_CONTROLLER_METATABLE);
    stream = ngx_lua_web_stream_controller_check_stream(L, controller);

    if (stream->errored) {
        return luaL_error(L, "ReadableStream is errored");
    }

    if (stream->closed) {
        return luaL_error(L, "ReadableStream is closed");
    }

    ngx_lua_web_stream_close(stream);

    return 0;
}


static int
ngx_lua_web_stream_controller_gc(lua_State *L)
{
    ngx_lua_web_stream_controller_t  *controller;

    controller = luaL_checkudata(L, 1,
                                 NGX_LUA_WEB_STREAM_CONTROLLER_METATABLE);
    controller->stream = NULL;

    return 0;
}


static ngx_lua_web_stream_t *
ngx_lua_web_stream_controller_check_stream(lua_State *L,
    ngx_lua_web_stream_controller_t *controller)
{
    ngx_lua_web_stream_t  *stream;

    stream = controller->stream;
    if (stream == NULL) {
        (void) luaL_error(L,
                          "ReadableStreamDefaultController is detached");
        return NULL;
    }

    return stream;
}


static ngx_lua_web_stream_controller_t *
ngx_lua_web_stream_push_controller(lua_State *L, ngx_lua_web_stream_t *stream,
    int stream_index)
{
    ngx_lua_web_stream_controller_t  *controller;

    controller = lua_newuserdatauv(L,
                                   sizeof(ngx_lua_web_stream_controller_t),
                                   1);
    if (controller == NULL) {
        return NULL;
    }

    controller->stream = stream;

    lua_pushvalue(L, stream_index);
    lua_setiuservalue(L, -2, 1);

    luaL_setmetatable(L, NGX_LUA_WEB_STREAM_CONTROLLER_METATABLE);

    return controller;
}


/* ReadableStreamDefaultReader. */


static int
ngx_lua_web_stream_get_reader(lua_State *L)
{
    ngx_lua_web_stream_t         *stream;
    ngx_lua_web_stream_reader_t  *reader;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "ReadableStream:getReader() takes no arguments");
    }

    stream = luaL_checkudata(L, 1, NGX_LUA_WEB_STREAM_METATABLE);

    if (stream->locked) {
        return luaL_error(L, "ReadableStream is locked");
    }

    reader = lua_newuserdatauv(L, sizeof(ngx_lua_web_stream_reader_t), 1);
    if (reader == NULL) {
        return luaL_error(L, "no memory");
    }

    reader->stream = stream;
    stream->locked = 1;

    lua_pushvalue(L, 1);
    lua_setiuservalue(L, -2, 1);

    luaL_setmetatable(L, NGX_LUA_WEB_STREAM_READER_METATABLE);

    return 1;
}


static int
ngx_lua_web_stream_reader_read(lua_State *L)
{
    ngx_int_t                      push_rc, rc;
    ngx_chain_t                   *cl;
    ngx_lua_ctx_t                 *ctx;
    ngx_lua_web_stream_t          *stream;
    ngx_lua_web_stream_reader_t  *reader;

    if (lua_gettop(L) != 1) {
        return luaL_error(L,
                          "ReadableStreamDefaultReader:read() takes no "
                          "arguments");
    }

    reader = luaL_checkudata(L, 1, NGX_LUA_WEB_STREAM_READER_METATABLE);
    stream = ngx_lua_web_stream_reader_check_stream(L, reader);

    for ( ;; ) {
        rc = ngx_lua_web_stream_dequeue_chunk(stream, &cl);

        if (rc == NGX_OK) {
            lua_createtable(L, 0, 2);
            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "done");

            push_rc = ngx_lua_web_stream_push_lua_chunk(L, stream, cl);
            if (push_rc == NGX_DECLINED) {
                lua_pop(L, 1);
                continue;
            }

            if (push_rc != NGX_OK) {
                return luaL_error(L, "ReadableStream read failed");
            }

            lua_setfield(L, -2, "value");
            return 1;
        }

        if (rc == NGX_DONE) {
            lua_createtable(L, 0, 1);
            lua_pushboolean(L, 1);
            lua_setfield(L, -2, "done");
            return 1;
        }

        if (rc == NGX_AGAIN) {
            ctx = ngx_lua_get_ctx(L);
            ngx_lua_web_stream_wait(stream, ngx_lua_web_stream_reader_wake,
                                    ctx);

            return lua_yieldk(L, 0, 0,
                              ngx_lua_web_stream_reader_continue);
        }

        return luaL_error(L, "ReadableStream read failed");
    }
}


static int
ngx_lua_web_stream_reader_continue(lua_State *L, int status,
    lua_KContext ctx)
{
    ngx_int_t                      push_rc, rc;
    ngx_chain_t                   *cl;
    ngx_lua_web_stream_t          *stream;
    ngx_lua_web_stream_reader_t  *reader;

    reader = luaL_checkudata(L, 1, NGX_LUA_WEB_STREAM_READER_METATABLE);
    stream = ngx_lua_web_stream_reader_check_stream(L, reader);

    for ( ;; ) {
        rc = ngx_lua_web_stream_dequeue_buffered_chunk(stream, &cl);

        if (rc != NGX_OK) {
            break;
        }

        lua_createtable(L, 0, 2);
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "done");

        push_rc = ngx_lua_web_stream_push_lua_chunk(L, stream, cl);
        if (push_rc == NGX_DECLINED) {
            lua_pop(L, 1);
            continue;
        }

        if (push_rc != NGX_OK) {
            return luaL_error(L, "ReadableStream read failed");
        }

        lua_setfield(L, -2, "value");
        return 1;
    }

    if (stream->errored) {
        return luaL_error(L, "ReadableStream read failed");
    }

    if (stream->closed) {
        lua_createtable(L, 0, 1);
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "done");
        return 1;
    }

    return luaL_error(L, "ReadableStream resumed without data");
}


static int
ngx_lua_web_stream_reader_release_lock(lua_State *L)
{
    ngx_lua_web_stream_reader_t  *reader;

    if (lua_gettop(L) != 1) {
        return luaL_error(L,
                          "ReadableStreamDefaultReader:releaseLock() takes "
                          "no arguments");
    }

    reader = luaL_checkudata(L, 1, NGX_LUA_WEB_STREAM_READER_METATABLE);

    if (reader->stream != NULL) {
        reader->stream->locked = 0;
        reader->stream = NULL;
        lua_pushnil(L);
        lua_setiuservalue(L, 1, 1);
    }

    return 0;
}


static int
ngx_lua_web_stream_reader_gc(lua_State *L)
{
    ngx_lua_web_stream_reader_t  *reader;

    reader = luaL_checkudata(L, 1, NGX_LUA_WEB_STREAM_READER_METATABLE);

    if (reader->stream != NULL) {
        reader->stream->locked = 0;
        reader->stream = NULL;
    }

    return 0;
}


static ngx_lua_web_stream_t *
ngx_lua_web_stream_reader_check_stream(lua_State *L,
    ngx_lua_web_stream_reader_t *reader)
{
    ngx_lua_web_stream_t  *stream;

    stream = reader->stream;
    if (stream == NULL) {
        (void) luaL_error(L, "ReadableStreamDefaultReader lock is released");
        return NULL;
    }

    return stream;
}


static void
ngx_lua_web_stream_reader_wake(void *data)
{
    void             *resume_data;
    ngx_lua_ctx_t    *ctx;
    ngx_lua_resume_pt resume;

    ctx = data;
    resume = ctx->resume;
    resume_data = ctx->data;
    ctx->resume = NULL;
    ctx->data = NULL;

    resume(resume_data);
}


/* Whole-body readers. */

static int
ngx_lua_web_stream_read_all(lua_State *L, ngx_lua_web_stream_t *stream,
    lua_KFunction continuation, ngx_uint_t parse_json)
{
    lua_Unsigned    n;
    ngx_int_t       rc;
    ngx_chain_t    *cl;
    ngx_lua_ctx_t  *ctx;

    for ( ;; ) {
        rc = ngx_lua_web_stream_dequeue_chunk(stream, &cl);

        if (rc == NGX_OK) {
            n = lua_rawlen(L, 2);

            rc = ngx_lua_web_stream_push_lua_chunk(L, stream, cl);
            if (rc == NGX_DECLINED) {
                continue;
            }

            if (rc != NGX_OK) {
                return luaL_error(L, "ReadableStream read failed");
            }

            lua_rawseti(L, 2, (lua_Integer) n + 1);
            continue;
        }

        if (rc == NGX_DONE) {
            return ngx_lua_web_stream_read_all_finish(L, parse_json);
        }

        if (rc == NGX_AGAIN) {
            ctx = ngx_lua_get_ctx(L);
            ngx_lua_web_stream_wait(stream, ngx_lua_web_stream_reader_wake,
                                    ctx);

            return lua_yieldk(L, 0, (lua_KContext) (intptr_t) stream,
                              continuation);
        }

        return luaL_error(L, "ReadableStream read failed");
    }
}


static int
ngx_lua_web_stream_read_all_finish(lua_State *L, ngx_uint_t parse_json)
{
    size_t          len;
    const char     *value;
    lua_Unsigned    i, n;
    luaL_Buffer     buffer;

    luaL_buffinit(L, &buffer);

    n = lua_rawlen(L, 2);

    for (i = 1; i <= n; i++) {
        lua_rawgeti(L, 2, (lua_Integer) i);
        value = lua_tolstring(L, -1, &len);
        luaL_addlstring(&buffer, value, len);
        lua_pop(L, 1);
    }

    luaL_pushresult(&buffer);

    if (!parse_json) {
        return 1;
    }

    ngx_lua_json_decode(L, -1);

    return 1;
}


static int
ngx_lua_web_stream_read_text_continue(lua_State *L, int status,
    lua_KContext ctx)
{
    ngx_lua_web_stream_t  *stream;

    (void) status;

    stream = (ngx_lua_web_stream_t *) (intptr_t) ctx;

    return ngx_lua_web_stream_read_all(L, stream,
                                       ngx_lua_web_stream_read_text_continue,
                                       0);
}


static int
ngx_lua_web_stream_read_json_continue(lua_State *L, int status,
    lua_KContext ctx)
{
    ngx_lua_web_stream_t  *stream;

    (void) status;

    stream = (ngx_lua_web_stream_t *) (intptr_t) ctx;

    return ngx_lua_web_stream_read_all(L, stream,
                                       ngx_lua_web_stream_read_json_continue,
                                       1);
}


/* Buffer helpers. */

static ngx_int_t
ngx_lua_web_stream_push_lua_chunk(lua_State *L,
    ngx_lua_web_stream_t *stream, ngx_chain_t *cl)
{
    off_t          size;
    ngx_uint_t     has_data;
    ngx_buf_t     *b;
    ngx_chain_t   *next, *part;
    luaL_Buffer    buffer;

    has_data = 0;

    for (part = cl; part != NULL; part = part->next) {
        b = part->buf;

        if (b == NULL) {
            stream->errored = 1;
            ngx_lua_web_stream_free_chunk(stream, cl);
            return NGX_ERROR;
        }

        if (ngx_buf_special(b)) {
            continue;
        }

        size = ngx_buf_size(b);

        if (size < 0 || (size > 0 && !ngx_buf_in_memory(b))) {
            stream->errored = 1;
            ngx_lua_web_stream_free_chunk(stream, cl);
            return NGX_ERROR;
        }

        if (size > 0) {
            has_data = 1;
        }
    }

    if (!has_data) {
        ngx_lua_web_stream_free_chunk(stream, cl);
        return NGX_DECLINED;
    }

    luaL_buffinit(L, &buffer);

    for (part = cl; part != NULL; part = next) {
        next = part->next;
        b = part->buf;

        if (!ngx_buf_special(b) && ngx_buf_size(b) > 0) {
            luaL_addlstring(&buffer, (const char *) b->pos,
                            b->last - b->pos);
            b->pos = b->last;
        }

        ngx_free_chain(stream->pool, part);
    }

    luaL_pushresult(&buffer);

    return NGX_OK;
}


static void
ngx_lua_web_stream_free_chunk(ngx_lua_web_stream_t *stream, ngx_chain_t *cl)
{
    ngx_chain_t  *next;

    while (cl != NULL) {
        next = cl->next;
        ngx_free_chain(stream->pool, cl);
        cl = next;
    }
}
