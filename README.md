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

This app uses nginx as the request entry point, forwards the incoming request to
an HTTPS upstream with `fetch()`, and streams the upstream response body back to
the client:

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

app:all("*", function(request)
    local target = "https://api.example.internal"

    local response, err = fetch(request, nil, {
        target = target,
        connect_timeout = 1000,
        send_timeout = 1000,
        read_timeout = 5000,
    })

    if response == nil then
        return Response.new({
            status = 502,
            headers = { ["content-type"] = "text/plain" },
            body = text_stream("upstream fetch failed: " .. err .. "\n"),
        })
    end

    return Response.new({
        status = response.status,
        headers = response.headers,
        body = response.body,
    })
end)

return app
```

With `target`, `fetch()` connects to the upstream origin while preserving the
incoming request path, query string, method, ordinary headers and body stream.
It regenerates controlled HTTP headers such as `Host` and `Content-Length`.
Network failures return `nil, err`. Successful responses expose status, headers
and a `ReadableStream` body, so the handler can proxy the body without buffering
it in Lua. `Response.new({ body = ... })` currently expects a `ReadableStream`,
so simple text responses should use a helper like `text_stream()`.

## DeepSeek Example

This example exposes a local streaming `/chat/completions` endpoint and forwards
the client request body to DeepSeek. The DeepSeek API key is configured on the
nginx side, so clients call the local service without sending upstream
credentials.

`deepseek.lua`:

```lua
local app = App.new()
local deepseek_api_key = os.getenv("DEEPSEEK_API_KEY")

app:post("/chat/completions", function(request)
    return fetch(request, {
        headers = {
            ["content-type"] = "application/json",
            ["authorization"] = "Bearer " .. deepseek_api_key,
        },
    }, {
        target = "https://api.deepseek.com",
        connect_timeout = 5000,
        send_timeout = 5000,
        read_timeout = 30000,
    })
end)

return app
```

`nginx.conf`:

```nginx
worker_processes 1;
env DEEPSEEK_API_KEY;
error_log logs/error.log notice;
pid logs/nginx.pid;

events {
    worker_connections 64;
}

http {
    resolver 1.1.1.1 8.8.8.8 ipv6=off;
    resolver_timeout 5s;

    server {
        listen 127.0.0.1:8080;

        location /chat/completions {
            lua_web_file /absolute/path/to/deepseek.lua;
        }
    }
}
```

Run nginx with the key in the environment:

```sh
mkdir -p /tmp/nginx-lua-web-deepseek/logs
export DEEPSEEK_API_KEY='sk-...'
/path/to/nginx/objs/nginx -p /tmp/nginx-lua-web-deepseek/ -c /absolute/path/to/nginx.conf
```

Call the local service with a DeepSeek-compatible streaming JSON body:

```sh
curl -N http://127.0.0.1:8080/chat/completions \
  -H 'Content-Type: application/json' \
  -d '{
        "model": "deepseek-chat",
        "messages": [
          {"role": "system", "content": "You are a helpful assistant."},
          {"role": "user", "content": "Hello!"}
        ],
        "stream": true
      }'
```

Stop nginx when finished:

```sh
/path/to/nginx/objs/nginx -p /tmp/nginx-lua-web-deepseek/ -c /absolute/path/to/nginx.conf -s stop
```

Because the handler directly returns `fetch(...)`, the DeepSeek response body is
streamed back to the client as it is read. DeepSeek HTTP responses such as
authentication errors are passed through to the client. Network-level failures
return `nil, err`, which this minimal example leaves to nginx-lua-web as a
handler error.

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
