# Wasmtime Unit Module

This directory contains a sample implementation of using Wasmtime to implement a
Unit module which can be loaded into `unitd`. Currently this implementation
feigns looking like the "wasm" module but operates differently internally,
notably:

* The `module` configuration field when loading wasm is interpreted instead as a
  WebAssembly component. This component is expected to the [`wasi:http/proxy`
  world][proxy].

* Each request instantiates a new component. Previously a single instance was
  used for all requests, and instead a new instance is created per-request now.
  This means that state cannot be leaked between requests since sandboxes are
  entirely destroyed between requests.

* Previously configured functions about allocation and hooks are no longer read
  and/or processed. The runtime is guided by the implementation of the `proxy`
  world instead.

* No custom host APIs are provided, instead only those in the `proxy` world are
  available. This means that components must only use WASI base functions and
  some `wasi:http` functions.

[proxy]: https://github.com/WebAssembly/wasi-http/blob/8aa75f58a6c0c5819ae898ba911753a43660e4a7/wit/proxy.wit#L7-L34

## Building

The sample implementation here is written in Rust.
