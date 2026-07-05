# API Reference

## Module

nginx-lua-web is exposed to nginx through the `lua_web_file` directive.

```nginx
location / {
    lua_web_file /absolute/path/to/app.lua;
}
```

- Context: nginx `location`.
- The file must return an `App` object.
- A location can define one `lua_web_file`.
- Nested locations inherit the parent setting unless they define their own.
- Subrequests are intentionally unsupported and rejected.

Runtime notes:

- One Lua VM is created from the nginx main configuration.
- Lua standard libraries are opened, then the `coroutine` library is removed.
- Request handlers run in internal coroutines managed by the module.
- Global Lua state is shared across requests and locations in the same nginx
  main configuration.

## App

```lua
local app = App.new()

app:get(pattern, handler)
app:post(pattern, handler)
app:all(pattern, handler)

return app
```

Handlers are called as `handler(request, params)` and must return a `Response`.
If no route matches, the module returns `404` with an empty body.

Supported patterns:

- Exact path: `/users`.
- Catch-all: `*`.
- Prefix: `/assets/*`.
- Route parameters: `/users/:id/posts/:post_id`.

Routes are matched in registration order. Parameter names must be Lua-style
identifiers, start a path segment, and cannot be duplicated in the same pattern.

## Lua Web APIs

### Request

```lua
Request.new()
Request.new(url)
Request.new(url, init)
Request.new(request)
Request.new(request, init)
```

Properties:

- `request.url`
- `request.method`
- `request.headers`
- `request.body`
- `request.bodyUsed`

`init` supports:

- `method`: string.
- `headers`: Lua table or `Headers`.
- `body`: `ReadableStream`.

The URL is taken from the constructor input. `GET` and `HEAD` requests cannot
have a body.

### Response

```lua
Response.new()
Response.new({
    status = 200,
    headers = { ["content-type"] = "text/plain" },
    body = stream,
})
```

Properties:

- `response.status`
- `response.headers`
- `response.body`
- `response.bodyUsed`

`status` must be from `200` to `599`. Status `204`, `205` and `304` cannot have
a body. Body values must be `ReadableStream` objects.

### Headers

```lua
local headers = Headers.new({ ["X-Test"] = "one" })

headers:get(name)
headers:set(name, value)
headers:has(name)
headers:delete(name)
headers:entries()
```

Header names are matched case-insensitively and stored in lowercase.
`entries()` returns a Lua iterator yielding `name, value`.

### URL

```lua
local url = URL.new("HTTP://Example.TEST:80/a/./b/../c?x=1#top")
local child = URL.new("../next?b=2", url)
```

Properties:

- `href`
- `origin`
- `protocol`
- `username`
- `password`
- `host`
- `hostname`
- `port`
- `pathname`
- `search`
- `hash`
- `searchParams`

Methods:

- `url:toString()`
- `tostring(url)`

The current implementation covers network-style absolute URLs and relative
resolution against an absolute base. It normalizes host case, default ports and
dot segments in paths.

### URLSearchParams

```lua
URLSearchParams.new()
URLSearchParams.new("a=1&a=2")
URLSearchParams.new("?a=1")
URLSearchParams.new({ a = "1", q = "hello world" })
URLSearchParams.new({ { "a", "1" }, { "a", "2" } })
URLSearchParams.new(existingParams)
```

Methods and properties:

- `params:append(name, value)`
- `params:delete(name, value?)`
- `params:get(name)`
- `params:getAll(name)`
- `params:has(name, value?)`
- `params:set(name, value)`
- `params:sort()`
- `params:toString()`
- `params.size`
- `tostring(params)`

When `params` belongs to a `URL`, changes to `searchParams` update `url.search`
and `url.href`.

### ReadableStream

```lua
local stream = ReadableStream.new({
    start = function(controller)
        controller:enqueue("hello")
        controller:close()
    end,
    pull = function(controller)
        -- enqueue more data or close
    end,
})
```

Controller methods:

- `controller:enqueue(value)`: `value` must be a string.
- `controller:close()`.

Reader methods:

```lua
local reader = stream:getReader()
local result = reader:read()
reader:releaseLock()
```

`read()` returns `{ done = false, value = chunk }` or `{ done = true }`.

### fetch

```lua
local response, err = fetch(input, init, options)
```

`input` can be a URL string or a `Request`.

`init` supports:

- `method`: string.
- `headers`: Lua table or `Headers`.
- `body`: `ReadableStream`.

`options` supports:

- `target`: absolute origin used for the TCP/TLS connection while preserving the
  request URL path and query.
- `connect_timeout`: non-negative integer milliseconds. Default `5000`.
- `send_timeout`: non-negative integer milliseconds. Default `5000`.
- `read_timeout`: non-negative integer milliseconds. Default `5000`.
- `keepalive_timeout`: non-negative integer milliseconds. Default `60000`.
- `tls_verify`: boolean. Default `true`.
- `tls_verify_host`: boolean. Default `true`.

On upstream/network failures, `fetch()` returns `nil, err`. Invalid arguments
raise Lua errors. Response bodies are `ReadableStream` objects, except `HEAD`
and no-body responses where `response.body == nil`.
