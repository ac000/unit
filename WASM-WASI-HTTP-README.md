# Wasmtime Unit Module

This directory contains a sample implementation of using Wasmtime to implement a
Unit module which can be loaded into `unitd`. This uses a language module type
of `wasm-wasi-http`.

* The specified `component` is expected to adhere to [`wasi:http/proxy`
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

The sample implementation here is written in Rust. Communication with UNIT is
done via FFI. First the UNIT server must be compiled:

```
./configure ...
make
```

Now you can simply

```
./configure wasm-wasi-http
make install
```

### Under the covers

This is what the above `configure` does...

Next Rust bindings will use the `nxt_unit.c` file so that object must built:

```
make build/src/nxt_unit.o
```

Next the Rust bindings can be built with

```
cargo build --release --manifest-path src/wasm-wasi-http/Cargo.toml
```

This will use [`bindgen`](https://crates.io/crates/bindgen) to create
auto-generated FFI bindings between UNIT and Rust. This ensures that if header
files in UNIT change then Rust code will fail to compile if not updated.

The output of compilation is located at
`src/wasm-wasi-http/target/release/libnxt_wasmtime.so`. This file can then be
placed in
`$UNIT_LIBDIR/unit/modules/wasmtime.unit.so`.

## Example Project

> **Note**: This walks through some manual steps for Rust to show what's going
> on, but if you're actually using Rust you'll probably use [`cargo
> component`](https://github.com/bytecodealliance/cargo-component/)

An example component is located in the `src/wasm-wasi-http/example` directory
with the bulk of the source code living in
`src/wasm-wasi-http/example/src/lib.rs`. Building this example can be done
with:

```
cargo build --target wasm32-wasi --release --manifest-path wasm-wasi-http-example/Cargo.toml
```

The output core wasm module is located at
`wasm-wasi-http-example/target/wasm32-wasi/release/example.wasm`. Note that
the Wasmtime module above takes components as an input, however. To create a
component first install the
[`wasm-tools`](https://github.com/bytecodealliance/wasm-tools) repository.
Next create the component with:

```
wasm-tools component new \
  wasm-wasi-http-example/target/wasm32-wasi/release/example.wasm \
  --adapt ./wasm-wasi-http-example/wasi_snapshot_preview1.reactor.wasm \
  -o wasm-wasi-http-example/target/wasm32-wasi/release/example.component.wasm
```

This will create a component at
`wasm-wasi-http-example/target/wasm32-wasi/release/example.component.wasm`.
This can then be configured as:

```
curl -X PUT --data-binary '{
      "listeners": {
          "127.0.0.1:8080": {
              "pass": "applications/wasm"
          }
      },

      "applications": {
          "wasm": {
              "type": "wasm-wasi-http",
              "component": "/path/to/example.component.wasm"
          }
      }
  }' --unix-socket $HOME/unit/var/run/unit/control.unit.sock http://localhost/config/
```

Next you can curl with:

```
$ curl -v http://localhost:8080/hello\?a\=b -d 'xyzabcd'
 * Welcome to the component model in Rust! *

[Request Info]
REQUEST_PATH = /hello?a=b
METHOD = POST
SCHEME = http
AUTHORITY = localhost

[Request Headers]
host = localhost:8080
user-agent = curl/7.81.0
accept = */*
content-length = 7
content-type = application/x-www-form-urlencoded

[Request Data]
xyzabcd
```

## Running the same component in Wasmtime

As an example, the same component can be executed in Wasmtime:

```
$ wasmtime serve wasm-wasi-http-example/target/wasm32-wasi/release/example.component.wasm
```

```
$ curl http://localhost:8080/hello\?a\=b -d 'xyzabcd'
<same as above>
```
```
