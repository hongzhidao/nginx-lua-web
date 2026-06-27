lua_src="$ngx_addon_dir/lua-5.5.0/src"
lua_lib="$lua_src/liblua.a"

cat << END >> $NGX_MAKEFILE

$lua_lib:
	cd $lua_src && \$(MAKE) linux

END
