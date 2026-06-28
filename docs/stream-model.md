# Stream Runtime Model

This document describes the complete stream model as:

```text
生产端  --->  Stream object  --->  消费端
producer     object             consumer
```

The `Stream object` is the middle object. It does not care whether data comes
from Lua, the client socket, or the upstream socket. It also does not care
whether the consumer is Lua code or C runtime code. It only connects the two
sides.

## Producers

Producer means where stream data comes from.

There are three producer shapes:

```text
Lua Stream.new producer
client socket producer
upstream socket producer
```

Expanded:

```text
Lua Stream.new producer
  -> data is produced by Lua code

client socket producer
  -> data comes from the client request body

upstream socket producer
  -> data comes from an upstream response, such as fetch response body
```

Combined producer side:

```text
Lua Stream.new producer  --------+
client socket producer  ---------+-->  Stream object
upstream socket producer --------+
```

## Consumers

Consumer means where stream data goes.

There are four consumer shapes:

```text
Lua Reader:read() consumer
Lua text() consumer
C response consumer
C fetch request consumer
```

Expanded:

```text
Lua Reader:read() consumer
  -> reads one chunk or done
  -> returns to Lua code

Lua text() consumer
  -> reads the whole stream
  -> returns one Lua string
  -> covers Request:text() and Response:text()

C response consumer
  -> reads stream chunks
  -> writes to the client socket

C fetch request consumer
  -> reads stream chunks
  -> writes to the upstream socket
```

Combined consumer side:

```text
Stream object  --->  Lua Reader:read() consumer
              \
               --->  Lua text() consumer
              \
               --->  C response consumer
              \
               --->  C fetch request consumer
```

## Complete Shape

```text
Lua Stream.new producer  --------+
client socket producer  ---------+-->  Stream object  --->  Lua Reader:read() consumer
upstream socket producer --------+                  \
                                                     --->  Lua text() consumer
                                                    \
                                                     --->  C response consumer
                                                    \
                                                     --->  C fetch request consumer
```

This diagram lists possible producers and possible consumers. It does not mean
one stream broadcasts to all consumers. A stream is consumed by the consumer
that reads it.

## Read Protocol

All consumers use the same basic read protocol:

```text
read() -> chunk | done | again
```

Meaning:

```text
chunk
  -> data is available now

done
  -> stream is closed and has no more data

again
  -> data is not available now, but may arrive later
```

The synchronous path is simple:

```text
consumer --read()--> Stream object
Stream object --chunk/done--> consumer
```

The async path adds waiting:

```text
consumer --read()--> Stream object
Stream object --wait/again--> consumer   --> set continuation

socket event
  -> producer/data source enqueue(chunk) or close
  -> wake registered continuation, if any
  -> consumer continues and reads again
```

`enqueue()` and `close()` only change stream state. They do not automatically
wake a waiter. The producer or data source that observes an event is
responsible for calling wake after it makes data available or closes the stream.

## Continuation and Wake

`again` means the current read cannot finish now.

Before waiting, the consumer must save a continuation:

```text
continuation = where this consumer should continue later
```

`wake` means the producer or data source tells that saved continuation to
continue:

```text
producer/data source --wake--> registered continuation
```

The continuation belongs to the actual consumer:

```text
Lua Reader:read() consumer
  -> continuation is the Lua coroutine

Lua text() consumer
  -> continuation is the Lua coroutine

C response consumer
  -> continuation is the C response output step

C fetch request consumer
  -> continuation is the C fetch request runtime step
```

So:

```text
Lua-side consumer -> wake Lua coroutine
C-side consumer   -> wake C continuation
```

## Naming Rule

The runtime uses two names for two different continuation layers:

```text
*_continue
  -> registered with stream wake
  -> called by producer/data-source wake
  -> continues the outer consumer/runtime step

*_resume
  -> registered with lua_yieldk()
  -> called by Lua after lua_resume() re-enters the yielded C function
  -> finishes the C API call that yielded
```

Example:

```text
Reader:read()
  -> ngx_lua_web_stream_reader_read_method()
  -> lua_yieldk(..., ngx_lua_web_stream_reader_read_resume)

request body event
  -> stream wake
  -> content_continue()
  -> lua_resume()
  -> ngx_lua_web_stream_reader_read_resume()
```

## Important Cases

### Lua Stream.new to C response

```text
Lua Stream.new producer  --->  Stream object  --->  C response consumer  --->  client socket
```

Lua produces chunks. C response consumes chunks and writes them to the client.

In the current runtime, Lua `Stream.new` sources are synchronous. `start()` runs
at stream creation, and `pull()` runs when a consumer needs data and the queue
is empty. A Lua `pull()` must enqueue a chunk or close the stream before it
returns.

If C response output itself is blocked by the client socket, nginx write events
resume the response output path. That is sink backpressure, not a stream
producer `again`.

### Lua Reader:read()

```text
producer  --->  Stream object  --->  Lua Reader:read() consumer
```

`Reader:read()` consumes one chunk at a time.

If the stream returns `again`, the waiting continuation is the Lua coroutine.
When data arrives, wake resumes the Lua coroutine and `Reader:read()` returns.

### Lua text()

```text
producer  --->  Stream object  --->  Lua text() consumer  --->  Lua string
```

`Request:text()` and `Response:text()` are both `Lua text()` consumers.

They read until `done`, collect all chunks, and return one Lua string.

If the stream returns `again`, the waiting continuation is the Lua coroutine.
When data arrives, wake resumes the Lua coroutine and `text()` continues
reading.

### Client Request Body Stream

```text
client socket producer  --->  Stream object  --->  Lua Reader:read() consumer
                                             \
                                              --->  Lua text() consumer
                                             \
                                              --->  C fetch request consumer
```

The producer is the client socket. The stream represents incoming request body
data.

Lua may consume it with `Reader:read()` or `Request:text()`.

C may also consume it as a fetch request body:

```text
client socket producer  --->  Stream object  --->  C fetch request consumer  --->  upstream socket
```

In that case, the stream consumer is C fetch request runtime, not Lua.

### Fetch Request Body

When Lua calls `fetch()` with a stream body:

```text
producer  --->  Stream object  --->  C fetch request consumer  --->  upstream socket
```

`fetch()` is a Lua API call, but request body stream consumption is done by C
fetch runtime.

If request stream read returns `again`, the runtime registers the Lua coroutine
that is currently inside `fetch()`. When request body data arrives, wake resumes
that coroutine, and the `lua_yieldk` continuation re-enters the C fetch runtime
so it can keep writing the upstream request body.

Separately, the Lua coroutine that called `fetch()` waits for the fetch API
result. It is resumed when `fetch()` can return to Lua.

### Fetch Response Body

`fetch()` can also create a stream from the upstream response body:

```text
upstream socket producer  --->  Stream object
```

That stream can be consumed by Lua:

```text
upstream socket producer  --->  Stream object  --->  Lua Reader:read() consumer
                                             \
                                              --->  Lua text() consumer
```

Or passed through to the client response:

```text
upstream socket producer  --->  Stream object  --->  C response consumer  --->  client socket
```

The continuation still belongs to whoever consumes that stream.

## Fetch Has Two Layers

`fetch()` is Lua-facing, but internally C-driven.

There are two different continuation layers:

```text
Lua fetch API continuation
  -> Lua coroutine waits for fetch(...) to return
  -> wake Lua coroutine when fetch can return a response or error

C fetch runtime continuation
  -> lua_yieldk *_resume continuation re-enters the C fetch state machine
  -> continues connect/write/read/body processing after the Lua coroutine resumes
```

So the rule is:

```text
fetch() API completion
  -> wake Lua coroutine

fetch internal stream/socket progress
  -> resume the Lua coroutine that is inside fetch()
  -> lua_yieldk *_resume continuation continues the C fetch runtime
```

## Final Rule

The stream object has one stable protocol:

```text
read() -> chunk | done | again
```

The producer decides where data comes from:

```text
Lua Stream.new
client socket
upstream socket
```

The consumer decides where data goes:

```text
Lua Reader:read()
Lua text()
C response
C fetch request
```

And `again` always means:

```text
save the current consumer's continuation
wait for producer/data-source event
producer/data-source event wakes that registered continuation
continue reading
```
