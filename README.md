# nginx-lua-web

nginx-lua-web is an AI pair coding example project: an experimental Lua web
runtime for NGINX built with an AI coding assistant. It embeds Lua into NGINX
request handling and exposes a small Web-style server API with `App`, `Request`,
`Response`, `Headers`, `URL`, `URLSearchParams`, `ReadableStream` and `fetch`.

The runtime is built as a third-party NGINX HTTP module. A `lua_web_file`
location loads a Lua file, expects that file to return an `App`, and routes each
matching request through a fresh Lua coroutine. Handlers receive a `Request`,
can consume request bodies as streams, can call `fetch()` for upstream HTTP or
HTTPS requests, and return a `Response`.

The repository vendors Lua 5.5.0 and builds as an NGINX addon.

## AI Pair Coding Example

This repository is intended to demonstrate how an AI coding assistant can help
grow a small systems project from module plumbing to API design, tests and
documentation. The Web-style Lua API is deliberate: prompts can describe server
behavior in familiar terms while the implementation still runs inside NGINX.

## Features

- `lua_web_file` nginx directive for mounting a Lua app file per location.
- Lightweight routing with exact, prefix, wildcard and parameter routes.
- Lua Web APIs: `Request`, `Response`, `Headers`, `URL`, `URLSearchParams`,
  `ReadableStream` and `fetch`.
- Built-in `fetch()` supports HTTP, HTTPS, nginx resolver DNS, TLS verification,
  timeouts and basic keepalive.

## Install

nginx-lua-web is built with nginx from source. A typical source layout is:

```text
/path/to/workspace/
  nginx/
  nginx-lua-web/
```

Build from the nginx source directory:

```sh
./auto/configure --add-module=../nginx-lua-web --with-http_ssl_module
make
```

`--with-http_ssl_module` is optional for plain HTTP use, but required for HTTPS
`fetch()` support.

The module's `config.make` builds the vendored Lua static library
`lua-5.5.0/src/liblua.a` with `-fPIC` and links it into nginx.

If the module is elsewhere, use an absolute path:

```sh
./auto/configure --add-module=/absolute/path/to/nginx-lua-web --with-http_ssl_module
make
```

## Configure

Create an app file and mount it with `lua_web_file`:

```nginx
worker_processes 1;
error_log logs/error.log notice;
pid logs/nginx.pid;

events {
    worker_connections 64;
}

http {
    server {
        listen 127.0.0.1:8080;

        location / {
            lua_web_file /absolute/path/to/app.lua;
        }
    }
}
```

For `fetch()` calls to DNS names, configure nginx `resolver`:

```nginx
http {
    resolver 1.1.1.1 8.8.8.8 ipv6=off;
    resolver_timeout 5s;
}
```

## Quick Example

`app.lua`:

```lua
local function text_stream(text)
    return ReadableStream.new({
        start = function(controller)
            controller:enqueue(text)
            controller:close()
        end,
    })
end

local app = App.new()

app:get("/", function(request)
    return Response.new({
        status = 200,
        headers = { ["content-type"] = "text/plain" },
        body = text_stream("hello from nginx-lua-web\n"),
    })
end)

app:get("/users/:id", function(_, params)
    return Response.new({
        headers = { ["content-type"] = "text/plain" },
        body = text_stream("user " .. params.id .. "\n"),
    })
end)

return app
```

`Response.new({ body = ... })` currently expects a `ReadableStream`, so simple
text responses should use a helper like `text_stream()`.

## Run

Start nginx with a prefix:

```sh
mkdir -p /tmp/nginx-lua-web-demo/conf /tmp/nginx-lua-web-demo/logs
cp nginx.conf /tmp/nginx-lua-web-demo/conf/nginx.conf

/path/to/nginx/objs/nginx -p /tmp/nginx-lua-web-demo/ -c conf/nginx.conf
curl http://127.0.0.1:8080/
/path/to/nginx/objs/nginx -p /tmp/nginx-lua-web-demo/ -c conf/nginx.conf -s stop
```

## Test

From this repository:

```sh
make test
```

By default the test runner expects nginx at `../nginx/objs/nginx`. To use a
specific binary:

```sh
NGINX=/path/to/nginx/objs/nginx make test
```

## Documentation

- [API Reference](docs/api.md)
- [Development status](docs/SPEC.md)

## License

nginx-lua-web is released under the MIT License. See [LICENSE](LICENSE).

This repository vendors Lua 5.5.0. Lua keeps its own MIT license and copyright
notice; see [NOTICE](NOTICE) and the files under [lua-5.5.0](lua-5.5.0).
