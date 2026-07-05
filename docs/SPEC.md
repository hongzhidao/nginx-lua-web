# SPEC - Lua Web APIs

本文按业务域列出当前 Lua Web Runtime 的功能状态。

状态说明：

- 是否开发：`是` 表示已有实现，`部分` 表示已有基础能力但语义未完整，`否` 表示尚未实现。
- 是否测试：`是` 表示已有自动化覆盖，`部分` 表示只被其他场景间接覆盖或覆盖不完整，`否` 表示尚无自动化覆盖。

## 完成度

统计口径：不包含“近期建议顺序”；`是` 记 1，`部分` 记 0.5，`否` 记 0。

| 业务 | 功能数 | 开发完成度 | 测试完成度 |
| --- | --- | --- | --- |
| 整体 | 119 | 100.0% | 100.0% |
| Module | 6 | 100.0% | 100.0% |
| App | 12 | 100.0% | 100.0% |
| Request | 11 | 100.0% | 100.0% |
| Response | 12 | 100.0% | 100.0% |
| Headers | 12 | 100.0% | 100.0% |
| URL | 20 | 100.0% | 100.0% |
| URLSearchParams | 15 | 100.0% | 100.0% |
| ReadableStream | 11 | 100.0% | 100.0% |
| fetch | 20 | 100.0% | 100.0% |

## Module

| 业务 | 功能 | 是否开发 | 是否测试 |
| --- | --- | --- | --- |
| Module | `lua_web_file path` | 是 | 是 |
| Module | 每个 location 配置一个 Lua app 文件 | 是 | 是 |
| Module | Lua VM 在 nginx main conf 初始化 | 是 | 是 |
| Module | 禁用 Lua `coroutine` 标准库 | 是 | 是 |
| Module | handler 通过内部 coroutine yield 等待 request/fetch/stream | 是 | 是 |
| Module | subrequest 显式不支持并报错 | 是 | 是 |

## App

| 业务 | 功能 | 是否开发 | 是否测试 |
| --- | --- | --- | --- |
| App | `App.new()` | 是 | 是 |
| App | `app:all(pattern, handler)` | 是 | 是 |
| App | `app:get(pattern, handler)` | 是 | 是 |
| App | `app:post(pattern, handler)` | 是 | 是 |
| App | 精确路径匹配 | 是 | 是 |
| App | `*` 兜底匹配 | 是 | 是 |
| App | `prefix*` 前缀匹配 | 是 | 是 |
| App | 按添加顺序匹配路由 | 是 | 是 |
| App | 按 HTTP method 区分 handler | 是 | 是 |
| App | handler 返回 `Response` | 是 | 是 |
| App | 无匹配 handler 返回 404 | 是 | 是 |
| App | 路由参数，例如 `/users/:id` | 是 | 是 |

## Lua Web APIs

### Request

| 业务 | 功能 | 是否开发 | 是否测试 |
| --- | --- | --- | --- |
| Request | `Request.new(input?, init?)` | 是 | 是 |
| Request | `request.url` | 是 | 是 |
| Request | `request.method` | 是 | 是 |
| Request | `request.headers` | 是 | 是 |
| Request | `request.body` | 是 | 是 |
| Request | nginx 请求生成绝对 URL | 是 | 是 |
| Request | 有请求体时 `request.body` 是 `ReadableStream` | 是 | 是 |
| Request | 无请求体时 `request.body == nil` | 是 | 是 |
| Request | `GET` / `HEAD` 请求禁止带 body | 是 | 是 |
| Request | 构造时传 `url`、`method`、`headers`、`body` | 是 | 是 |
| Request | `request.bodyUsed` | 是 | 是 |

### Response

| 业务 | 功能 | 是否开发 | 是否测试 |
| --- | --- | --- | --- |
| Response | `Response.new(init?)` | 是 | 是 |
| Response | 默认 `status = 200` | 是 | 是 |
| Response | `response.status` | 是 | 是 |
| Response | `response.headers` | 是 | 是 |
| Response | `response.body` | 是 | 是 |
| Response | body 支持 `ReadableStream` | 是 | 是 |
| Response | 校验 status 范围 `200..599` | 是 | 是 |
| Response | `204` / `205` / `304` 禁止带 body | 是 | 是 |
| Response | 发送响应 headers 到 nginx | 是 | 是 |
| Response | 流式发送响应 body | 是 | 是 |
| Response | body used 状态 | 是 | 是 |
| Response | 完整 response header 校验 | 是 | 是 |

### Headers

| 业务 | 功能 | 是否开发 | 是否测试 |
| --- | --- | --- | --- |
| Headers | `Headers.new(init?)` | 是 | 是 |
| Headers | 从 Lua table 初始化 | 是 | 是 |
| Headers | 从另一个 `Headers` 复制 | 是 | 是 |
| Headers | `headers:get(name)` | 是 | 是 |
| Headers | header name 大小写不敏感查询 | 是 | 是 |
| Headers | 内部 header name 小写化 | 是 | 是 |
| Headers | `Request.new({ headers = ... })` 复制 headers | 是 | 是 |
| Headers | `Response.new({ headers = ... })` 复制 headers | 是 | 是 |
| Headers | `headers:set(name, value)` | 是 | 是 |
| Headers | `headers:has(name)` | 是 | 是 |
| Headers | `headers:delete(name)` | 是 | 是 |
| Headers | `headers:entries()` | 是 | 是 |

### URL

| 业务 | 功能 | 是否开发 | 是否测试 |
| --- | --- | --- | --- |
| URL | `URL.new(input, base?)` | 是 | 是 |
| URL | `url.href` | 是 | 是 |
| URL | `url.origin` | 是 | 是 |
| URL | `url.protocol` | 是 | 是 |
| URL | `url.username` | 是 | 是 |
| URL | `url.password` | 是 | 是 |
| URL | `url.host` | 是 | 是 |
| URL | `url.hostname` | 是 | 是 |
| URL | `url.port` | 是 | 是 |
| URL | `url.pathname` | 是 | 是 |
| URL | `url.search` | 是 | 是 |
| URL | `url.hash` | 是 | 是 |
| URL | `url.searchParams` | 是 | 是 |
| URL | `url:toString()` | 是 | 是 |
| URL | `tostring(url)` | 是 | 是 |
| URL | 基于 base 解析相对 URL | 是 | 是 |
| URL | 默认端口处理 | 是 | 是 |
| URL | path normalization | 是 | 是 |
| URL | `searchParams` 修改后同步 `url.search` / `url.href` | 是 | 是 |
| URL | 非法 URL 报错 | 是 | 是 |

### URLSearchParams

| 业务 | 功能 | 是否开发 | 是否测试 |
| --- | --- | --- | --- |
| URLSearchParams | `URLSearchParams.new(init?)` | 是 | 是 |
| URLSearchParams | init 支持 query string | 是 | 是 |
| URLSearchParams | init 支持 table map | 是 | 是 |
| URLSearchParams | init 支持 sequence pairs | 是 | 是 |
| URLSearchParams | init 支持另一个 `URLSearchParams` | 是 | 是 |
| URLSearchParams | `params:append(name, value)` | 是 | 是 |
| URLSearchParams | `params:delete(name, value?)` | 是 | 是 |
| URLSearchParams | `params:get(name)` | 是 | 是 |
| URLSearchParams | `params:getAll(name)` | 是 | 是 |
| URLSearchParams | `params:has(name, value?)` | 是 | 是 |
| URLSearchParams | `params:set(name, value)` | 是 | 是 |
| URLSearchParams | `params:toString()` | 是 | 是 |
| URLSearchParams | `params.size` | 是 | 是 |
| URLSearchParams | `tostring(params)` | 是 | 是 |
| URLSearchParams | 和所属 `URL` 的 query 同步 | 是 | 是 |

### ReadableStream

| 业务 | 功能 | 是否开发 | 是否测试 |
| --- | --- | --- | --- |
| ReadableStream | `ReadableStream.new(init)` | 是 | 是 |
| ReadableStream | `start(controller)` | 是 | 是 |
| ReadableStream | `pull(controller)` | 是 | 是 |
| ReadableStream | controller `enqueue(value)` | 是 | 是 |
| ReadableStream | controller `close()` | 是 | 是 |
| ReadableStream | `stream:getReader()` | 是 | 是 |
| ReadableStream | reader `read()` | 是 | 是 |
| ReadableStream | reader `releaseLock()` | 是 | 是 |
| ReadableStream | 从 nginx request body 接入 stream | 是 | 是 |
| ReadableStream | 从 fetch response body 接入 stream | 是 | 是 |
| ReadableStream | response body 直接返回 stream | 是 | 是 |

### fetch

| 业务 | 功能 | 是否开发 | 是否测试 |
| --- | --- | --- | --- |
| fetch | `fetch(input, init?, options?)` | 是 | 是 |
| fetch | input 支持 URL string | 是 | 是 |
| fetch | init 支持 table | 是 | 是 |
| fetch | input 支持 `Request` | 是 | 是 |
| fetch | init 支持 `method` | 是 | 是 |
| fetch | init 支持 `headers` | 是 | 是 |
| fetch | init 支持 `body` | 是 | 是 |
| fetch | options 支持 `target` | 是 | 是 |
| fetch | HTTP upstream 请求 | 是 | 是 |
| fetch | HTTPS/TLS upstream 请求 | 是 | 是 |
| fetch | nginx resolver DNS 解析 | 是 | 是 |
| fetch | chunked request body | 是 | 是 |
| fetch | response status | 是 | 是 |
| fetch | response headers | 是 | 是 |
| fetch | response body stream | 是 | 是 |
| fetch | HEAD response body 置 nil | 是 | 是 |
| fetch | no-body response body 置 nil | 是 | 是 |
| fetch | 基础 keepalive 复用 | 是 | 是 |
| fetch | 可配置 connect/send/read/keepalive timeout | 是 | 是 |
| fetch | `fetch(Request, init)` 合并规则 | 是 | 是 |

## 近期建议顺序

| 业务 | 功能 | 是否开发 | 是否测试 |
| --- | --- | --- | --- |
| 近期任务 | `Response.json(value, init?)` | 否 | 否 |
| 近期任务 | `app:put/patch/delete/options/head` | 否 | 否 |
