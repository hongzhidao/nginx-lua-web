/*
 * Copyright (C) 2026 Zhidao HONG
 */


#ifndef _NGX_LUA_H_INCLUDED_
#define _NGX_LUA_H_INCLUDED_


#include <stddef.h>

#include <lua.h>


typedef struct ngx_lua_app_s  ngx_lua_app_t;


void ngx_lua_app_register(lua_State *L);
ngx_lua_app_t *ngx_lua_app_get(lua_State *L, int index);
int ngx_lua_app_find_handler(ngx_lua_app_t *app, const char *path,
    size_t len);


#endif /* _NGX_LUA_H_INCLUDED_ */
