# SPCL (C Edition)

SPCL is a C rewrite of the original CCL project and now follows CCL core syntax only:

- `key = value`
- indentation-based multiline values
- recursive nested parsing

## Build

```sh
make all
```

## Test

```sh
make test
```

## Run

```sh
./build/bin/cclq examples/example.spcl
./build/bin/cclq examples/example.spcl database=ports
```

## Project Layout

- `include/spcl.h`: public API
- `src/parser.c`: CCL parser
- `src/model.c`: recursive model, merge, query, pretty printer
- `src/io.c`: file decoding helper
- `src/cclq.c`: CLI query tool
- `tests/test_spcl.c`: minimal regression tests
- `Makefile`: modern C build/test/sanitize/lint/format workflow

## Design Source

Language design reference:

- https://chshersh.com/blog/2025-01-06-the-most-elegant-configuration-language.html

## 中文文档

- [使用指南](docs/使用指南.md)
