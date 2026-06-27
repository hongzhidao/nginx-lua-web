/*
 * Copyright (C) Zhidao HONG
 *
 * This allocator is adapted from the QuickJS arena allocator.
 *
 * Copyright (c) 2017-2025 Fabrice Bellard
 * Copyright (c) 2017-2025 Charlie Gordon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include <ngx_config.h>
#include <ngx_core.h>

#include "ngx_lua_pool.h"


#define NGX_LUA_POOL_ARENA_SIZE        4096
#define NGX_LUA_POOL_BLOCK_SIZE_COUNT  31
#define NGX_LUA_POOL_MAX_SMALL_SIZE    512
#define NGX_LUA_POOL_FREE_NIL          0xffff


typedef struct {
    union {
        uint16_t  block_idx;
        uint16_t  free_next;
    } u;

    u_char    block_size_idx;
    u_char    reserved;
} ngx_lua_pool_block_header_t;


typedef struct {
    union {
        ngx_lua_pool_block_header_t  h;
        uintptr_t                    alignment;
    } u;

    u_char  data[];
} ngx_lua_pool_block_t;


typedef struct {
    ngx_queue_t           free_queue;
    ngx_queue_t           queue;
    u_char                block_size_idx;
    uint16_t              n_used_blocks;
    uint16_t              n_blocks;
    uint16_t              first_free_block;
    u_char                blocks[];
} ngx_lua_pool_arena_t;


typedef struct {
    ngx_queue_t           queue;
    ngx_lua_pool_block_t  block;
} ngx_lua_pool_large_block_t;


struct ngx_lua_pool_s {
    ngx_log_t             *log;
    ngx_queue_t            arena_queue[NGX_LUA_POOL_BLOCK_SIZE_COUNT];
    ngx_queue_t            free_arena_queue[NGX_LUA_POOL_BLOCK_SIZE_COUNT];
    ngx_queue_t            large_queue;
    ngx_lua_pool_block_t   zero_block;
};


static const uint16_t ngx_lua_pool_block_sizes[] = {
    16,
    24,
    32,
    40,
    48,
    56,
    64,
    72,
    80,
    88,
    96,
    104,
    112,
    120,
    128,
    144,
    160,
    176,
    192,
    208,
    224,
    240,
    256,
    288,
    320,
    352,
    384,
    416,
    448,
    480,
    512,
};


static ngx_uint_t
ngx_lua_pool_block_size_index(size_t size)
{
    if (size <= 16) {
        return 0;
    }

    if (size <= 128) {
        return (size + 7) / 8 - 2;
    }

    if (size <= 256) {
        return (size + 15) / 16 + 6;
    }

    if (size <= 512) {
        return (size + 31) / 32 + 14;
    }

    return NGX_LUA_POOL_BLOCK_SIZE_COUNT;
}


static ngx_lua_pool_block_t *
ngx_lua_pool_zero_block(ngx_lua_pool_t *pool)
{
    return &pool->zero_block;
}


static void *
ngx_lua_pool_arena_block(ngx_lua_pool_arena_t *arena, ngx_uint_t idx,
    ngx_uint_t block_size)
{
    return arena->blocks + idx * block_size;
}


static ngx_lua_pool_arena_t *
ngx_lua_pool_new_arena(ngx_lua_pool_t *pool, ngx_uint_t block_size_idx)
{
    int                    i, n_blocks;
    size_t                 size;
    ngx_uint_t             block_size;
    ngx_lua_pool_arena_t  *arena;
    ngx_lua_pool_block_t  *block;

    block_size = ngx_lua_pool_block_sizes[block_size_idx];
    n_blocks = (NGX_LUA_POOL_ARENA_SIZE
                - offsetof(ngx_lua_pool_arena_t, blocks)) / block_size;

    size = offsetof(ngx_lua_pool_arena_t, blocks) + n_blocks * block_size;

    arena = ngx_alloc(size, pool->log);
    if (arena == NULL) {
        return NULL;
    }

    arena->block_size_idx = (u_char) block_size_idx;
    arena->n_blocks = (uint16_t) n_blocks;
    arena->n_used_blocks = 0;
    arena->first_free_block = 0;

    for (i = 0; i < n_blocks - 1; i++) {
        block = ngx_lua_pool_arena_block(arena, i, block_size);
        block->u.h.u.free_next = (uint16_t) (i + 1);
        block->u.h.block_size_idx = (u_char) block_size_idx;
        block->u.h.reserved = 0;
    }

    block = ngx_lua_pool_arena_block(arena, n_blocks - 1, block_size);
    block->u.h.u.free_next = NGX_LUA_POOL_FREE_NIL;
    block->u.h.block_size_idx = (u_char) block_size_idx;
    block->u.h.reserved = 0;

    ngx_queue_insert_head(&pool->arena_queue[block_size_idx], &arena->queue);
    ngx_queue_insert_head(&pool->free_arena_queue[block_size_idx],
                          &arena->free_queue);

    return arena;
}


static void *
ngx_lua_pool_alloc_large(ngx_lua_pool_t *pool, size_t size)
{
    ngx_lua_pool_large_block_t  *block;

    block = ngx_alloc(sizeof(ngx_lua_pool_large_block_t) + size, pool->log);
    if (block == NULL) {
        return NULL;
    }

    block->block.u.h.u.block_idx = NGX_LUA_POOL_FREE_NIL;
    block->block.u.h.block_size_idx = 0xff;
    block->block.u.h.reserved = 0;

    ngx_queue_insert_head(&pool->large_queue, &block->queue);

    return block->block.data;
}


ngx_lua_pool_t *
ngx_lua_pool_create(ngx_log_t *log)
{
    ngx_uint_t       i;
    ngx_lua_pool_t  *pool;

    pool = ngx_alloc(sizeof(ngx_lua_pool_t), log);
    if (pool == NULL) {
        return NULL;
    }

    ngx_memzero(pool, sizeof(ngx_lua_pool_t));
    pool->log = log;

    pool->zero_block.u.h.u.block_idx = NGX_LUA_POOL_FREE_NIL;
    pool->zero_block.u.h.block_size_idx = 0xff;

    for (i = 0; i < NGX_LUA_POOL_BLOCK_SIZE_COUNT; i++) {
        ngx_queue_init(&pool->arena_queue[i]);
        ngx_queue_init(&pool->free_arena_queue[i]);
    }

    ngx_queue_init(&pool->large_queue);

    return pool;
}


void
ngx_lua_pool_destroy(ngx_lua_pool_t *pool)
{
    ngx_uint_t                 i;
    ngx_queue_t              *q, *next;
    ngx_lua_pool_arena_t     *arena;
    ngx_lua_pool_large_block_t  *large;

    if (pool == NULL) {
        return;
    }

    for (i = 0; i < NGX_LUA_POOL_BLOCK_SIZE_COUNT; i++) {
        for (q = ngx_queue_head(&pool->arena_queue[i]);
             q != ngx_queue_sentinel(&pool->arena_queue[i]);
             q = next)
        {
            next = ngx_queue_next(q);
            arena = ngx_queue_data(q, ngx_lua_pool_arena_t, queue);
            ngx_free(arena);
        }
    }

    for (q = ngx_queue_head(&pool->large_queue);
         q != ngx_queue_sentinel(&pool->large_queue);
         q = next)
    {
        next = ngx_queue_next(q);
        large = ngx_queue_data(q, ngx_lua_pool_large_block_t, queue);
        ngx_free(large);
    }

    ngx_free(pool);
}


void *
ngx_lua_palloc(ngx_lua_pool_t *pool, size_t size)
{
    size_t                 total_size;
    ngx_uint_t             block_idx, block_size, block_size_idx;
    ngx_queue_t           *head, *q;
    ngx_lua_pool_arena_t  *arena;
    ngx_lua_pool_block_t  *block;

    if (size == 0) {
        return ngx_lua_pool_zero_block(pool)->data;
    }

    if (size > (size_t) -1 - sizeof(ngx_lua_pool_block_t)
        - (NGX_ALIGNMENT - 1))
    {
        return NULL;
    }

    total_size = ngx_align(size, NGX_ALIGNMENT)
                 + sizeof(ngx_lua_pool_block_t);

    if (total_size > NGX_LUA_POOL_MAX_SMALL_SIZE) {
        return ngx_lua_pool_alloc_large(pool, size);
    }

    block_size_idx = ngx_lua_pool_block_size_index(total_size);
    block_size = ngx_lua_pool_block_sizes[block_size_idx];

    head = &pool->free_arena_queue[block_size_idx];
    q = ngx_queue_head(head);

    if (q == ngx_queue_sentinel(head)) {
        arena = ngx_lua_pool_new_arena(pool, block_size_idx);
        if (arena == NULL) {
            return NULL;
        }

    } else {
        arena = ngx_queue_data(q, ngx_lua_pool_arena_t, free_queue);
    }

    block_idx = arena->first_free_block;
    block = ngx_lua_pool_arena_block(arena, block_idx, block_size);

    arena->first_free_block = block->u.h.u.free_next;
    block->u.h.u.block_idx = (uint16_t) block_idx;
    arena->n_used_blocks++;

    if (arena->n_used_blocks == arena->n_blocks) {
        ngx_queue_remove(&arena->free_queue);
    }

    return block->data;
}


void *
ngx_lua_pcalloc(ngx_lua_pool_t *pool, size_t size)
{
    void  *p;

    p = ngx_lua_palloc(pool, size);
    if (p != NULL) {
        ngx_memzero(p, size);
    }

    return p;
}


void
ngx_lua_pfree(ngx_lua_pool_t *pool, void *ptr)
{
    ngx_uint_t             block_idx, block_size, block_size_idx;
    ngx_lua_pool_arena_t  *arena;
    ngx_lua_pool_block_t  *block;
    ngx_lua_pool_large_block_t  *large;

    if (ptr == NULL) {
        return;
    }

    block = (ngx_lua_pool_block_t *)
            ((u_char *) ptr - sizeof(ngx_lua_pool_block_t));

    if (block->u.h.u.block_idx == NGX_LUA_POOL_FREE_NIL) {
        if (block == ngx_lua_pool_zero_block(pool)) {
            return;
        }

        large = (ngx_lua_pool_large_block_t *)
                ((u_char *) block
                 - offsetof(ngx_lua_pool_large_block_t, block));
        ngx_queue_remove(&large->queue);
        ngx_free(large);
        return;
    }

    block_idx = block->u.h.u.block_idx;
    block_size_idx = block->u.h.block_size_idx;
    block_size = ngx_lua_pool_block_sizes[block_size_idx];

    arena = (ngx_lua_pool_arena_t *)
            ((u_char *) block - block_size * block_idx
             - offsetof(ngx_lua_pool_arena_t, blocks));

    block->u.h.u.free_next = arena->first_free_block;
    arena->first_free_block = (uint16_t) block_idx;

    if (arena->n_used_blocks == arena->n_blocks) {
        ngx_queue_insert_head(&pool->free_arena_queue[block_size_idx],
                              &arena->free_queue);
    }

    arena->n_used_blocks--;

    if (arena->n_used_blocks == 0) {
        ngx_queue_remove(&arena->queue);
        ngx_queue_remove(&arena->free_queue);
        ngx_free(arena);
    }
}
