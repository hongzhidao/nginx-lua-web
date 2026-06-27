/*
 * Copyright (C) Zhidao HONG
 */


#ifndef _NGX_LUA_WEB_H_INCLUDED_
#define _NGX_LUA_WEB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#include <lua.h>

#include "ngx_lua.h"


typedef struct ngx_lua_web_stream_s  ngx_lua_web_stream_t;


ngx_lua_web_stream_t *ngx_lua_web_stream_create(lua_State *L);


#endif /* _NGX_LUA_WEB_H_INCLUDED_ */
