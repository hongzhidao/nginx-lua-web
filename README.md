# nginx-lua-web

nginx-lua-web is a Lua web runtime for NGINX with built-in Web-style `fetch()`.
Lua handlers can route requests, call HTTP or HTTPS upstreams, and proxy
streaming request and response bodies without buffering them in Lua.

## Features

- `lua_web_file` nginx directive for mounting a Lua app file per location.
- Lightweight routing with exact, prefix, wildcard and parameter routes.
- Built-in Web-style `fetch()` for HTTP and HTTPS upstream calls from Lua.
- Lua Web APIs: `Request`, `Response`, `Headers`, `URL`, `URLSearchParams`,
  `ReadableStream` and `fetch`.
- `fetch()` supports nginx resolver DNS, TLS verification, timeouts, streamed
  bodies and basic keepalive.
- Streaming request and response bodies for low-buffering HTTP applications and
  programmable proxying.
- Lua-level control over headers, upstream targets, timeout policy and error
  handling.
- Lua runtime JSON helpers: `JSON.stringify()`, `JSON.parse()` and `JSON.null`.

## Real-world Example: DeepSeek Proxy

nginx-lua-web is general-purpose; this example uses DeepSeek as a realistic
streaming HTTP service. It exposes a local `/chat/completions` endpoint,
forwards the client request body to DeepSeek with `fetch()`, and streams the
upstream response back to the client.

The DeepSeek API key is configured on the nginx side, so clients call the local
service without sending upstream credentials.

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

Because the handler directly returns `fetch(...)`, the DeepSeek response body is
streamed back to the client as it is read. See
[Getting Started](docs/getting-started.md) for build, configuration and run
instructions.

## Documentation

- [Getting Started](docs/getting-started.md)
- [API Reference](docs/api.md)

## License

nginx-lua-web is released under the MIT License. See [LICENSE](LICENSE).

This repository vendors Lua 5.5.0. Lua keeps its own MIT license and copyright
notice; see [NOTICE](NOTICE) and the files under [lua-5.5.0](lua-5.5.0).
