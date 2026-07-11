/*
 * Copyright (C) 2026 Zhidao HONG
 */


#ifndef _NGX_LUA_WEB_H_INCLUDED_
#define _NGX_LUA_WEB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#include <lua.h>


typedef struct ngx_lua_web_request_s  ngx_lua_web_request_t;
typedef struct ngx_lua_web_response_s  ngx_lua_web_response_t;
typedef struct ngx_lua_web_headers_s  ngx_lua_web_headers_t;
typedef struct ngx_lua_web_url_s  ngx_lua_web_url_t;
typedef struct ngx_lua_web_search_params_s  ngx_lua_web_search_params_t;
typedef struct ngx_lua_web_stream_s  ngx_lua_web_stream_t;
typedef struct ngx_lua_web_stream_source_s  ngx_lua_web_stream_source_t;

typedef ngx_int_t (*ngx_lua_web_stream_pull_pt)(
    ngx_lua_web_stream_t *stream, ngx_lua_web_stream_source_t *source);
typedef void (*ngx_lua_web_stream_wake_pt)(void *data);

struct ngx_lua_web_request_s {
    ngx_lua_web_headers_t  *headers;
    ngx_lua_web_stream_t   *body;
    ngx_str_t               url;
    ngx_str_t               method;
};


struct ngx_lua_web_response_s {
    ngx_lua_web_headers_t  *headers;
    ngx_lua_web_stream_t   *body;
    ngx_uint_t              status;
};


struct ngx_lua_web_stream_source_s {
    ngx_lua_web_stream_pull_pt   pull;
    void                        *data;
};


ngx_lua_web_request_t *ngx_lua_web_request_create(lua_State *L);
ngx_lua_web_request_t *ngx_lua_web_request_get(lua_State *L, int index);
void ngx_lua_web_request_init(lua_State *L, ngx_lua_web_request_t *request,
    int request_index, int input_index, int init_index, int arg);
ngx_int_t ngx_lua_web_request_set_string(lua_State *L, ngx_str_t *field,
    const char *value, size_t len);
void ngx_lua_web_request_register(lua_State *L);
ngx_lua_web_response_t *ngx_lua_web_response_create(lua_State *L);
ngx_lua_web_response_t *ngx_lua_web_response_get(lua_State *L, int index);
void ngx_lua_web_response_register(lua_State *L);
ngx_lua_web_headers_t *ngx_lua_web_headers_create(lua_State *L);
void ngx_lua_web_headers_init(lua_State *L, ngx_lua_web_headers_t *headers,
    int init_index, int arg);
ngx_lua_web_headers_t *ngx_lua_web_headers_get(lua_State *L, int index);
size_t ngx_lua_web_headers_count(ngx_lua_web_headers_t *headers);
ngx_int_t ngx_lua_web_headers_get_entry(ngx_lua_web_headers_t *headers,
    size_t index, ngx_str_t *name, ngx_str_t *value);
ngx_uint_t ngx_lua_web_headers_validate_name(const char *name, size_t len);
ngx_uint_t ngx_lua_web_headers_validate_value(const char *value, size_t len);
ngx_int_t ngx_lua_web_headers_set_entry(lua_State *L,
    ngx_lua_web_headers_t *headers, const char *name, size_t name_len,
    const char *value, size_t value_len);
void ngx_lua_web_headers_set(lua_State *L,
    ngx_lua_web_headers_t *headers, const char *name, size_t name_len,
    const char *value, size_t value_len);
void ngx_lua_web_headers_register(lua_State *L);
void ngx_lua_web_url_sync_search_params(lua_State *L,
    ngx_lua_web_url_t *url);
void ngx_lua_web_url_register(lua_State *L);
ngx_lua_web_search_params_t *ngx_lua_web_search_params_create(lua_State *L);
ngx_int_t ngx_lua_web_search_params_init_query(lua_State *L,
    ngx_lua_web_search_params_t *params, const char *query, size_t len);
ngx_int_t ngx_lua_web_search_params_to_string(lua_State *L,
    ngx_lua_web_search_params_t *params, ngx_str_t *dst);
void ngx_lua_web_search_params_free_string(lua_State *L, ngx_str_t *value);
void ngx_lua_web_search_params_set_owner(lua_State *L,
    ngx_lua_web_search_params_t *params, int params_index,
    ngx_lua_web_url_t *url, int url_index);
void ngx_lua_web_search_params_register(lua_State *L);
ngx_lua_web_stream_t *ngx_lua_web_stream_create(lua_State *L,
    ngx_pool_t *pool);
ngx_lua_web_stream_t *ngx_lua_web_stream_get(lua_State *L, int index);
ngx_uint_t ngx_lua_web_stream_body_used(ngx_lua_web_stream_t *stream);
int ngx_lua_web_stream_read_text(lua_State *L,
    ngx_lua_web_stream_t *stream);
int ngx_lua_web_stream_read_json(lua_State *L,
    ngx_lua_web_stream_t *stream);
void ngx_lua_web_stream_register(lua_State *L);
void ngx_lua_web_stream_set_source(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_source_t *source);
/*
 * Chain links must use the pool passed to stream_create(), and backing buffers
 * must remain valid until consumed.  A successful enqueue transfers the
 * complete chain as one logical chunk; on failure the caller retains it.  A
 * successful dequeue transfers that complete chain to the caller, which must
 * return every link to the same pool with ngx_free_chain().
 */
ngx_int_t ngx_lua_web_stream_enqueue_chunk(ngx_lua_web_stream_t *stream,
    ngx_chain_t *bufs);
ngx_int_t ngx_lua_web_stream_dequeue_chunk(ngx_lua_web_stream_t *stream,
    ngx_chain_t **bufs);
void ngx_lua_web_stream_close(ngx_lua_web_stream_t *stream);
void ngx_lua_web_stream_error(ngx_lua_web_stream_t *stream);
void ngx_lua_web_stream_wait(ngx_lua_web_stream_t *stream,
    ngx_lua_web_stream_wake_pt wake, void *data);
void ngx_lua_web_stream_wake(ngx_lua_web_stream_t *stream);


#endif /* _NGX_LUA_WEB_H_INCLUDED_ */
