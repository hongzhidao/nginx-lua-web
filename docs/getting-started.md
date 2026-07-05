# Getting Started

This guide covers building nginx-lua-web with NGINX, configuring a Lua app, and
running a real streaming proxy example.

## Build

nginx-lua-web is built with NGINX from source as a third-party HTTP module. A
typical source layout is:

```sh
git clone https://github.com/nginx/nginx.git
git clone https://github.com/hongzhidao/nginx-lua-web.git
```

```text
/path/to/workspace/
  nginx/
  nginx-lua-web/
```

Build from the NGINX source directory:

```sh
./auto/configure --add-module=../nginx-lua-web --with-http_ssl_module
make
```

`--with-http_ssl_module` is optional for plain HTTP use, but required for HTTPS
`fetch()` support.

The module's `config.make` builds the vendored Lua static library
`lua-5.5.0/src/liblua.a` with `-fPIC` and links it into NGINX.

If the module is elsewhere, use an absolute path:

```sh
./auto/configure --add-module=/absolute/path/to/nginx-lua-web --with-http_ssl_module
make
```

## Configure

Create a Lua app file and mount it with `lua_web_file`:

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

For `fetch()` calls to DNS names, configure an NGINX resolver:

```nginx
http {
    resolver 1.1.1.1 8.8.8.8 ipv6=off;
    resolver_timeout 5s;
}
```

## Run

Start NGINX with a prefix:

```sh
mkdir -p /tmp/nginx-lua-web-demo/conf /tmp/nginx-lua-web-demo/logs
cp nginx.conf /tmp/nginx-lua-web-demo/conf/nginx.conf

/path/to/nginx/objs/nginx -p /tmp/nginx-lua-web-demo/ -c conf/nginx.conf
curl http://127.0.0.1:8080/
/path/to/nginx/objs/nginx -p /tmp/nginx-lua-web-demo/ -c conf/nginx.conf -s stop
```

## DeepSeek Streaming Proxy

This example exposes a local `/chat/completions` endpoint, forwards the client
request body to DeepSeek with `fetch()`, and streams the upstream response back
to the client.

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

Run NGINX with the key in the environment:

```sh
mkdir -p /tmp/deepseek-proxy/logs
export DEEPSEEK_API_KEY='sk-...'
/path/to/nginx/objs/nginx -p /tmp/deepseek-proxy/ -c /absolute/path/to/nginx.conf
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

Stop NGINX when finished:

```sh
/path/to/nginx/objs/nginx -p /tmp/deepseek-proxy/ -c /absolute/path/to/nginx.conf -s stop
```

Because the handler directly returns `fetch(...)`, the DeepSeek response body is
streamed back to the client as it is read. DeepSeek HTTP responses such as
authentication errors are passed through to the client. Network-level failures
return `nil, err`, which this minimal example leaves to nginx-lua-web as a
handler error.

## Test

From this repository:

```sh
make test
```

By default the test runner expects NGINX at `../nginx/objs/nginx`. To use a
specific binary:

```sh
NGINX=/path/to/nginx/objs/nginx make test
```
