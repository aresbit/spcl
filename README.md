# SPCL (C Edition)

SPCL is a C rewrite of the original CCL project and now follows CCL core syntax only:

- `key = value`
- indentation-based multiline values
- recursive nested parsing

## Build

```sh
make all
```

## Install

```sh
make install
```

默认安装到 `/usr/local`，会安装：

- `spcl` / `cclq`
- `parsespcl` / `llskill2spcl`
- `libspcl.a` 和公共头文件
- 脚本依赖的 DSL/模板文档到 `share/spcl/docs`

如果需要自定义前缀或做打包安装：

```sh
make install PREFIX=/usr
make install DESTDIR=/tmp/pkgroot PREFIX=/usr
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
- [技能组合 DSL 重构提案](docs/SPCL-Skill-Composition-DSL.md)

## 一键执行入口

日常做技能融合时，直接使用 `parsespcl` 即可；它会自动调用 `llskill2spcl` 完成规范化，不需要手动先跑一遍：

```sh
parsespcl skill1dir skill2dir
```

`llskill2spcl` 是底层子工具，适合只做 `SKILL.md -> SPCL` 规范化时单独使用。

## SKILL.md 转 SPCL

将修改后的 `SKILL.md` 发送给 LLM，返回 SPCL 内容（输出文件后缀使用 `.spcl`）：

```sh
export DEEPSEEK_API_KEY="YOUR_KEY"
llskill2spcl --skill ./skills/<skill_name>/SKILL.md
```

默认使用 `LLM_MODEL=deepseek-chat`。密钥支持 `DEEPSEEK_API_KEY`（或通用 `LLM_API_KEY`）；地址支持 `DEEPSEEK_API_URL`（或通用 `LLM_API_URL`），默认是 `https://api.deepseek.com/chat/completions`。

输出默认写入同目录：`<skill_name>.spcl`。也可指定输出：

```sh
llskill2spcl \
  --skill ./skills/<skill_name>/SKILL.md \
  --out /tmp/<skill_name>.spcl
```

## 组合流水线（parsespcl）

端到端执行：规范化 `SKILL.md -> SKILL.spcl` -> 生成 `compose manifest` -> 调解释器执行组合：

```sh
parsespcl skill1dir skill2dir
```

默认最终产物目录在 `trick/<skill1>-and-then-<skill2>`；中间规范化目录和解释器临时输出目录会被脚本清理掉。最终主文件输出为 `SKILL.md`。

说明：
- `parsespcl` 先把每个输入技能的 `SKILL.md` 通过 `llskill2spcl` 规范化成临时 `SKILL.spcl`
- 然后脚本生成临时 `manifest.spcl`，调用 `spcl compose <manifest> --skills <dir> --out <dir>`
- 解释器负责 `title` 追加、`skill.description` 追加、`SKILL.spcl` 深合并、额外 `.spcl` 文件深合并，以及 fixpoint 收敛
- `parsespcl` 最后负责组装最终目录：把解释器产出的主 `SKILL.spcl` 包装进最终的 `SKILL.md`，顶部只生成普通 Markdown 说明，不注入任何 YAML/frontmatter 元数据；同时保留 `source-skills/*/SKILL.spcl`，并把原技能目录里的 `reference/` 或 `references/` 文件复制进结果目录；若原技能包含顶层 `script/` 或 `scripts/` 目录，则保留到组合技能的同名目录下，而不是挪进 `reference/`
- 若你的解释器尚未实现 `compose` 子命令，脚本会直接报错退出
