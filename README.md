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
- [技能组合 DSL 重构提案](docs/SPCL-Skill-Composition-DSL.md)

## SKILL.md 转 SPCL

将修改后的 `SKILL.md` 发送给 LLM，返回 SPCL 内容（文件后缀使用 `.md`）：

```sh
export DEEPSEEK_API_KEY="YOUR_KEY"
bash tools/llskill2spcl.sh --skill ./skills/<skill_name>/SKILL.md
```

默认使用 `LLM_MODEL=deepseek-chat`。密钥支持 `DEEPSEEK_API_KEY`（或通用 `LLM_API_KEY`）；地址支持 `DEEPSEEK_API_URL`（或通用 `LLM_API_URL`），默认是 `https://api.deepseek.com/chat/completions`。

输出默认写入同目录：`<skill_name>.md`。也可指定输出：

```sh
bash tools/llskill2spcl.sh \
  --skill ./skills/<skill_name>/SKILL.md \
  --out /tmp/<skill_name>.md
```

## 组合流水线（parsespcl）

端到端执行：规范化 `SKILL.md -> SKILL.spcl` -> 生成 `compose manifest` -> 调解释器执行组合：

```sh
bash tools/parsespcl.sh skill1dir skill2dir
```

默认产物目录由解释器生成，脚本本身只保留最终输出目录，不保留中间规范化目录。

说明：
- `parsespcl.sh` 不在 Bash 里实现组合语义
- `skill.description` 的 append、`title` 追加、`reference/**/*.spcl` 合并，都由解释器处理
- 当前脚本会为每个输入技能生成临时 `SKILL.spcl`，并生成临时 `manifest.spcl` 后调用解释器
- 若你的解释器尚未实现 `compose` 子命令，脚本会直接报错退出
