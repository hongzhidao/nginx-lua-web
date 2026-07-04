/*
 * Copyright (C) 2026 Zhidao HONG
 */


#include "ngx_lua_web.h"
#include "ngx_lua.h"

#include <lauxlib.h>


#define NGX_LUA_WEB_STREAM_METATABLE  "ngx_lua_web.ReadableStream"
#define NGX_LUA_WEB_STREAM_READER_METATABLE                                   \
    "ngx_lua_web.ReadableStreamDefaultReader"


struct ngx_lua_web_stream_s {
    ngx_lua_web_stream_source_t  *source;
    ngx_pool_t                   *pool;
    ngx_chain_t                  *bufs;
    ngx_chain_t                 **last;
    ngx_lua_web_stream_wake_pt    wake;
    void                         *data;
    unsigned  closed:1;
    unsigned  errored:1;
    unsigned  locked:1;
};


typedef struct {
    ngx_lua_web_stream_t         *stream;
} ngx_lua_web_stream_reader_t;


static int ngx_lua_web_stream_get_reader(lua_State *L);
static int ngx_lua_web_stream_reader_read(lua_State *L);
static int ngx_lua_web_stream_reader_resume(lua_State *L, int status,
    lua_KContext ctx);
static int ngx_lua_web_stream_reader_release_lock(lua_State *L);
static int ngx_lua_web_stream_reader_gc(lua_State *L);
static ngx_lua_web_stream_t *ngx_lua_web_stream_reader_check_stream(
    lua_State *L, ngx_lua_web_stream_reader_t *reader);
static void ngx_lua_web_stream_reader_wake(void *data);
static void ngx_lua_web_stream_reader_set_metatable(lua_State *L);


static const luaL_Reg  ngx_lua_web_stream_methods[] = {
    { "getReader", ngx_lua_web_stream_get_reader },
    { NULL, NULL }
};


static const luaL_Reg  ngx_lua_web_stream_reader_methods[] = {
    { "read", ngx_lua_web_stream_reader_read },
    { "releaseLock", ngx_lua_web_stream_reader_release_lock },
    { NULL, NULL }
};


static void ngx_lua_web_stream_set_metatable(lua_State *L);
static ngx_int_t ngx_lua_web_stream_read_buffer(
    ngx_lua_web_stream_t *stream, ngx_pool_t *pool, ngx_str_t *value);
static void ngx_lua_web_stream_pop_buf(ngx_lua_web_stream_t *stream,
    ngx_pool_t *pool);


static void
ngx_lua_web_stream_set_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, NGX_LUA_WEB_STREAM_METATABLE)) {
        lua_pushliteral(L, "ReadableStream");
        lua_setfield(L, -2, "__name");
        luaL_setfuncs(L, ngx_lua_web_stream_methods, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }

    lua_setmetatable(L, -2);
}


static void
ngx_lua_web_stream_reader_set_metatable(lua_State *L)
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

    lua_setmetatable(L, -2);
}


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
    stream->last = &stream->bufs;

    ngx_lua_web_stream_set_metatable(L);

    return stream;
}


void
ngx_lua_web_stream_set_source(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source)
{
    stream->source = source;
}


void
ngx_lua_web_stream_enqueue_bufs(ngx_lua_web_stream_t *stream,
    ngx_chain_t *bufs)
{
    *stream->last = bufs;

    while (*stream->last) {
        stream->last = &(*stream->last)->next;
    }
}


ngx_int_t
ngx_lua_web_stream_enqueue_string(ngx_lua_web_stream_t *stream,
    ngx_pool_t *pool, u_char *data, size_t len)
{
    ngx_buf_t    *b;
    ngx_chain_t  *cl;

    if (len == 0) {
        return NGX_OK;
    }

    cl = ngx_alloc_chain_link(pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    b = ngx_create_temp_buf(pool, len);
    if (b == NULL) {
        ngx_free_chain(pool, cl);
        return NGX_ERROR;
    }

    ngx_memcpy(b->last, data, len);
    b->last += len;

    cl->buf = b;
    cl->next = NULL;

    ngx_lua_web_stream_enqueue_bufs(stream, cl);

    return NGX_OK;
}


ngx_int_t
ngx_lua_web_stream_read(ngx_lua_web_stream_t *stream, ngx_pool_t *pool,
    ngx_str_t *value)
{
    ngx_int_t     rc;

    value->data = NULL;
    value->len = 0;

    for ( ;; ) {
        if (stream->errored) {
            return NGX_ERROR;
        }

        rc = ngx_lua_web_stream_read_buffer(stream, pool, value);
        if (rc != NGX_AGAIN) {
            return rc;
        }

        if (stream->closed) {
            return NGX_DONE;
        }

        if (stream->source == NULL) {
            stream->closed = 1;
            return NGX_DONE;
        }

        rc = stream->source->pull(stream, stream->source);
        if (rc == NGX_OK) {
            continue;
        }

        if (rc == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        if (rc == NGX_DONE) {
            stream->closed = 1;
            return NGX_DONE;
        }

        stream->errored = 1;

        return NGX_ERROR;
    }
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

    ngx_lua_web_stream_reader_set_metatable(L);

    return 1;
}


static int
ngx_lua_web_stream_reader_read(lua_State *L)
{
    ngx_int_t                      rc;
    ngx_lua_ctx_t                 *ctx;
    ngx_lua_web_stream_t          *stream;
    ngx_lua_web_stream_reader_t  *reader;
    ngx_str_t                      value;

    if (lua_gettop(L) != 1) {
        return luaL_error(L,
                          "ReadableStreamDefaultReader:read() takes no "
                          "arguments");
    }

    reader = luaL_checkudata(L, 1, NGX_LUA_WEB_STREAM_READER_METATABLE);
    stream = ngx_lua_web_stream_reader_check_stream(L, reader);

    rc = ngx_lua_web_stream_read(stream, stream->pool, &value);

    if (rc == NGX_OK) {
        lua_createtable(L, 0, 2);
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "done");
        lua_pushlstring(L, (const char *) value.data, value.len);
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
        if (ctx == NULL) {
            return luaL_error(L, "ReadableStream cannot yield");
        }

        ngx_lua_web_stream_wait(stream, ngx_lua_web_stream_reader_wake, L);

        return lua_yieldk(L, 0, 0, ngx_lua_web_stream_reader_resume);
    }

    return luaL_error(L, "ReadableStream read failed");
}


static int
ngx_lua_web_stream_reader_resume(lua_State *L, int status,
    lua_KContext ctx)
{
    ngx_int_t                      rc;
    ngx_lua_web_stream_t          *stream;
    ngx_lua_web_stream_reader_t  *reader;
    ngx_str_t                      value;

    reader = luaL_checkudata(L, 1, NGX_LUA_WEB_STREAM_READER_METATABLE);
    stream = ngx_lua_web_stream_reader_check_stream(L, reader);

    if (stream->errored) {
        return luaL_error(L, "ReadableStream read failed");
    }

    if (stream->closed) {
        lua_createtable(L, 0, 1);
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "done");
        return 1;
    }

    value.data = NULL;
    value.len = 0;

    rc = ngx_lua_web_stream_read_buffer(stream, stream->pool, &value);

    if (rc == NGX_ERROR) {
        return luaL_error(L, "ReadableStream read failed");
    }

    if (rc == NGX_OK) {
        lua_createtable(L, 0, 2);
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "done");
        lua_pushlstring(L, (const char *) value.data, value.len);
        lua_setfield(L, -2, "value");
        return 1;
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

    if (stream->pool == NULL) {
        (void) luaL_error(L, "ReadableStream has no memory pool");
        return NULL;
    }

    return stream;
}


static void
ngx_lua_web_stream_reader_wake(void *data)
{
    void             *resume_data;
    lua_State        *L;
    ngx_lua_ctx_t    *ctx;
    ngx_lua_resume_pt resume;

    L = data;
    ctx = ngx_lua_get_ctx(L);

    if (ctx == NULL || ctx->resume == NULL) {
        return;
    }

    resume = ctx->resume;
    resume_data = ctx->data;
    ctx->resume = NULL;
    ctx->data = NULL;

    resume(resume_data);
}


static ngx_int_t
ngx_lua_web_stream_read_buffer(ngx_lua_web_stream_t *stream,
    ngx_pool_t *pool, ngx_str_t *value)
{
    ngx_buf_t    *b;

    while (stream->bufs) {
        b = stream->bufs->buf;

        if (ngx_buf_special(b)) {
            if (b->last_buf) {
                stream->closed = 1;
            }

            ngx_lua_web_stream_pop_buf(stream, pool);
            continue;
        }

        if (!ngx_buf_in_memory(b)) {
            stream->errored = 1;
            return NGX_ERROR;
        }

        if (b->pos < b->last) {
            value->data = b->pos;
            value->len = b->last - b->pos;
            b->pos = b->last;

            if (b->last_buf) {
                stream->closed = 1;
            }

            ngx_lua_web_stream_pop_buf(stream, pool);

            return NGX_OK;
        }

        if (b->last_buf) {
            stream->closed = 1;
        }

        ngx_lua_web_stream_pop_buf(stream, pool);
    }

    return NGX_AGAIN;
}


static void
ngx_lua_web_stream_pop_buf(ngx_lua_web_stream_t *stream, ngx_pool_t *pool)
{
    ngx_chain_t  *cl;

    cl = stream->bufs;
    stream->bufs = cl->next;

    if (stream->bufs == NULL) {
        stream->last = &stream->bufs;
    }

    ngx_free_chain(pool, cl);
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
