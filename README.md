# nginx-lua

This repository is an AI pair coding example: a small, from-scratch nginx HTTP
module that experiments with a Hono-like developer experience at the nginx
layer.

The long-term idea is to make nginx feel like a tiny web framework host: route
requests in nginx, run Lua handlers close to the server, and keep the API small
enough to read in one sitting.

Today it is deliberately minimal. The module registers a `lua_content`
directive, links nginx against the embedded Lua source in `lua-5.5.0/`, and
installs a content handler that currently returns `404`. That small loop gives
the project a real nginx entry point, a build path, and a runtime test before
the Lua request API is added.

Example configuration:

```nginx
location /hello {
    lua_content app/hello.lua;
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
`lua_content`, and verifies that the response status is `404`.

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
- Location content handler that returns `404`.
- Python runtime acceptance test.

Not implemented yet:

- Request-time Lua execution.
- Loading Lua files.
- Request and response objects.
- Fetch-style APIs and streaming bodies.
