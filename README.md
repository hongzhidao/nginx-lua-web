# nginx-lua

This repository is an AI pair coding example: a small, from-scratch nginx HTTP
module that experiments with a Hono-like developer experience at the nginx
layer.

The long-term idea is to make nginx feel like a tiny web framework host: route
requests in nginx, run Lua handlers close to the server, and keep the API small
enough to read in one sitting.

Today it is deliberately minimal. The module registers a `lua_content`
directive, links nginx against the embedded Lua source in `lua-5.5.0/`, loads
the configured Lua file during worker startup, and runs its handler in a fresh
Lua coroutine for each request. The HTTP response is still fixed at
`hello world`; this keeps the first Lua integration step focused on loading and
executing Lua code before the request and response APIs are added.

Example configuration:

```nginx
location /hello {
    lua_content app/hello.lua;
}
```

Example Lua file:

```lua
return {
    handler = function(r)
        -- Request and response APIs are not implemented yet.
    end
}
```

## Layout

```text
src/      Nginx module source
tests/    Python acceptance tests
docs/     Project documentation
articles/ Project notes
lua-5.5.0 Lua source tree compiled into the module
```

## Build

Build it as a third-party Nginx module from an Nginx source tree.

If this project is checked out as `lua` inside the Nginx source tree, run this
from the Nginx source directory:

```sh
./auto/configure --add-module=lua
make
```

For any other layout, pass the module path:

```sh
./auto/configure --add-module=/path/to/nginx-lua
make
```

The module `config` links the local Lua 5.5.0 static library into Nginx.
`config.make` builds `lua-5.5.0/src/liblua.a` when Nginx runs `make`.

## Test

The current test suite is a runtime acceptance test:

```sh
make test
```

It starts `../nginx/objs/nginx`, sends a request to a location configured with
`lua_content`, verifies that the response is `200 hello world`, and checks that
the Lua handler ran.

Build Nginx first if `../nginx/objs/nginx` does not exist. You can override the
binary path:

```sh
NGINX=./path/to/objs/nginx make test
```

## Current Status

Implemented:

- Nginx external module `config`.
- Lua 5.5.0 linked as a static library in the Nginx module build.
- HTTP module declaration.
- `lua_content` location directive.
- Loading Lua files during worker startup.
- Per-request Lua coroutine execution.
- Location content handler that runs Lua and then returns `hello world`.
- Python runtime acceptance test.

Not implemented yet:

- Request and response objects.
- Lua-driven HTTP responses.
- Fetch-style APIs and streaming bodies.
