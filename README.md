# SPCL (C Edition)

SPCL is a C rewrite of the original CCL project with an extended syntax profile:

- CCL core: `key = value`, indentation-based multiline values, recursive nested parsing.
- SPCL extensions: `SKILLS:` block and `PROMPT:` block.

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
./build/bin/cclq examples/example.spcl -- skills prompt
```

## SPCL Syntax Extension

### Skills block

```txt
SKILLS:
  - modern-c-makefile
  - modern-c-dev
```

Equivalent internal CCL form:

```txt
skills =
  = modern-c-makefile
  = modern-c-dev
```

### Prompt block

```txt
PROMPT:
  Rewrite this project into modern C.
  Keep strict warning flags.
```

This is stored as raw string payload under key `prompt`.

## Project Layout

- `include/spcl.h`: public API
- `src/parser.c`: CCL parser + SPCL block parser
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
