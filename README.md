# Open Web Libraries

> [!WARNING]
> This library is not in production state yet.
> There are eg. many known memory leaks that
> will be naturally resolved with implementation
> of arena allocator (in v0.1).

**Open Web Libraries** (`owl`) is a project to bring set of simple,
fast and secure web libraries for C.

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

## Roadmap (v0.1)
- [ ] Replace `read()`/`write()` with `recv()`/`send()` with flags
- [ ] Implement own "String View"
- [ ] Make parser a real "zero-copy" one
- [ ] Implement arena allocator

## In the future
- Implement Client
- Maybe Listener should have a list of listenables (Server/Client)?
- Generally think about rewriting Listener

## License
`owl` is released under BSD 3-clause license.
