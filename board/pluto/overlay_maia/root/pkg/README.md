# maia-wasm

[![Crates.io][crates-badge]][crates-url]

[crates-badge]: https://img.shields.io/crates/v/maia-wasm.svg
[crates-url]: https://crates.io/crates/maia-wasm

maia-wasm is a Rust crate that implements the frontend web application used in
[Maia SDR](https://maia-sdr.org/). The web application is built using
WebAssembly and uses WebGL2 to render the waterfall display on a web browser.

## Building

The crate can be built with [wasm-pack](https://rustwasm.github.io/wasm-pack/)
by doing
```
wasm-pack build -t web
```

## API documentation

The API documentation is hosted in [docs.rs](https://docs.rs/maia-wasm/).

## License

Licensed under either of

 * Apache License, Version 2.0
   ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
 * MIT license
   ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

## Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be
dual licensed as above, without any additional terms or conditions.
