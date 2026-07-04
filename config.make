ngx_lua_src="$ngx_addon_dir/lua-5.5.0/src"
ngx_lua_lib="$ngx_lua_src/liblua.a"
ngx_lua_cflags="-fPIC"

cat << END >> $NGX_MAKEFILE

$ngx_lua_lib: ngx_lua_force
	cd $ngx_lua_src && \$(MAKE) a CC="\$(CC)" MYCFLAGS="$ngx_lua_cflags"

.PHONY: ngx_lua_force
ngx_lua_force:

END
