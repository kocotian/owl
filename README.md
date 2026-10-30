# Open Web Libraries

> [!WARNING]
> This library is not in production state yet.
> Currently the library can leak memory when
> using specific functions, that use stb_ds that
> will be replaced with custom linked lists.

**Open Web Libraries** (`owl`) is a project to bring set of simple,
fast and secure web libraries for C. **Single threaded**!

It provides a set of portable and easy-to-use APIs for building web
applications, including HTTP server (routing, middleware, and more soon).

## Why?
C is the most great programming language of all times.
It is fast, efficient, and widely used in various domains.
Ability to write web applications in C can be beneficial for
performance-critical applications, embedded systems, and
low-level programming. Also, it can make server bills cheaper
by using less resources.

## Main goals
- Performance,
- simplicity,
- security,
- reliability,
- readability,
- small codebase.

## wrk benchmark
I tried to serve simple Hello world app.
That is a benchmark that I'd run on my ThinkPad X13 on single
Intel i5-10310U core (with circa 100 pages open in chromium):
```
$ wrk -c 10000 -d 5 -t 8 http://localhost:8080
Running 5s test @ http://localhost:8080
  8 threads and 10000 connections
  Thread Stats Avg Stdev Max +/- Stdev
    Latency 8.13ms 61.56ms 1.97s 99.34%
    Req/Sec 4.16k 2.85k 23.87k 79.75%
  165662 requests in 5.06s, 20.70MB read
  Socket errors: connect 0, read 0, write 0, timeout 43
Requests/sec: 32739.87
Transfer/sec: 4.09MB
```
## Quickstart
You can build and run a simple example server (on localhost:8080):
```
$ make example
$ ./example
```

## Roadmap (v0.1)
- [X] Replace `read()`/`write()` with `recv()`/`send()` with flags
- [ ] Implement own "String View"
- [ ] Make parser a real "zero-copy" one
- [X] Implement arena allocator

## In the future
- Implement Client
- Maybe Listener should have a list of listenables (Server/Client)?
- Generally think about rewriting Listener

## License
`owl` is released under BSD 3-clause license.
