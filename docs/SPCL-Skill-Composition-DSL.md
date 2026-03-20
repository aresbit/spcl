# SPCL 技能组合 DSL 重构提案

## 1. 目标

这门语言专门解决三件事：

1. 输入层：`Markdown + 自注释协议`，方便给 LLM 读写。
2. 规范层：统一转换为标准 `.spcl` 文档（可被解释器稳定解析）。
3. 执行层：对标准化 `skill` 目录做递归组合，产出新技能 `trick`。

核心原则：`轻量、可组合、可递归、可达不动点（fixpoint）`。

## 2. 三层模型

### 2.1 M 层（Markdown Frontend）

给 LLM 的输入是 Markdown 文档，包含自注释块。

约定：只解析 fenced code block 中 `spcl-md` 内容；其余 Markdown 仅作说明。

```md
# Skill: s1

```spcl-md
@meta.name = s1
@meta.version = 1

skill.entry =
  file = SKILL.spcl
  refs =
    = reference/base.spcl
```
```

### 2.2 N 层（Normalized SPCL）

LLM 输出必须是纯 `.spcl`，并满足标准键空间：

- `meta.*`：文档元数据
- `skill.*`：单技能规范
- `compose.*`：组合任务
- `resolve.*`：求值策略（含 fixpoint）

这层是解释器唯一可信输入。

### 2.3 E 层（Evaluator）

`spcl` 解释器读取 N 层文档，按规则：

1. 标准化 skill 目录
2. 组合多个技能
3. 递归求值直到 fixpoint
4. 输出组合技能目录（默认名 `trick` 或显式名）

---

## 3. 自注释语法协议（Frontend）

自注释键使用 `@` 前缀，语义是“给 LLM/转换器的注释化控制面”。

### 3.1 约束

1. `@` 键只允许出现在 `spcl-md` 代码块。
2. 转换到 N 层时去掉 `@`，映射到同名标准键。
3. 未识别的 `@` 键进入 `meta.annotation.*`，不得丢弃。

示例：

```txt
@compose.inputs =
  = s1
  = s2
@compose.output = s1-and-then-s2
@resolve.fixpoint = true
```

转换后：

```txt
compose.inputs =
  = s1
  = s2
compose.output = s1-and-then-s2
resolve.fixpoint = true
```

---

## 4. 标准 SPCL 键空间（N 层）

```txt
meta =
  name = <doc-name>
  version = <int>

skill =
  name = <skill-name>
  root = <path>
  entry = SKILL.spcl
  refs =
    = reference/*.spcl
  extras =
    = prompt/*.spcl
    = tools/*.spcl

compose =
  inputs =
    = <skill-a>
    = <skill-b>
  op = and-then
  output = <new-skill-name>

resolve =
  merge = deep
  conflict = right-bias
  fixpoint = true
  max_iter = 64
```

含义：

1. `compose.inputs` 是有序列表，顺序即组合顺序。
2. `op = and-then` 定义为“后者追加/覆盖前者”。
3. `conflict = right-bias`：同键冲突时后输入优先。
4. `fixpoint = true`：重复执行组合，直到输出稳定或达到 `max_iter`。

---

## 5. 组合语义（关键）

设技能目录标准化后都有：

- `SKILL.spcl`
- `reference/**/*.spcl`
- 其他可选子目录下 `.spcl`

定义：

1. `s1 and-then s2` 的主文件不是简单追加，使用“头部感知合并”：
   - 先解析 `SKILL1.spcl` 和 `SKILL2.spcl` 的头部字段。
   - 将 `SKILL2` 的 `title` 与 `skill.description` 追加到 `SKILL1` 头部对应字段。
   - 再把 `SKILL2` 头部移除，仅保留其正文参与后续合并。
2. 头部处理完成后，执行 `SKILL.spcl = merge(SKILL1_with_patched_header, SKILL2_body_only)`。
3. `reference` 目录做并集合并；同路径文件按 `right-bias` 深合并。
4. 其他目录（如 `prompt/`, `tools/`）同样按目录树深合并。
5. 输出目录名默认：`<s1>-and-then-<s2>`，可由 `compose.output` 覆盖。

### 5.1 SKILL 头部规范（用于头部感知合并）

`SKILL.spcl` 约定头部字段：

```txt
title = <single line>
skill =
  description =
    <multi-line text>
```

追加策略：

1. `title`: `title_merged = title_1 + " + " + title_2`
2. `skill.description`: 在 `description_1` 末尾追加一段 `description_2`
3. 若任一字段缺失：
   - 缺失 `title` 时按空串处理
   - 缺失 `skill.description` 时按空块处理
4. `SKILL2` 的头部字段在正文合并阶段必须剔除，避免重复进入最终文档

递归组合：

1. `compose.inputs = [s1, s2, s3]`
2. 等价于 `((s1 and-then s2) and-then s3)`
3. 产物可继续作为输入参与下一轮组合，形成 `trick`。

---

## 6. Fixpoint 语义

目标：组合结果在递归展开后稳定。

算法：

1. `state_0 = compose(inputs)`
2. `state_{k+1} = normalize(compose(expand(state_k)))`
3. 若 `hash(state_{k+1}) == hash(state_k)`，达到 fixpoint 停止
4. 若 `k == max_iter` 仍未稳定，返回错误 `ERR_FIXPOINT_NOT_REACHED`

要求：

1. `normalize` 必须确定性（同输入同输出）。
2. 文件排序、键排序必须稳定。
3. 哈希前做 canonical pretty-print。

---

## 7. 解释器 I/O 协议

### 7.1 输入

1. Markdown 文档（含 `spcl-md` 块）或
2. 已标准化 `.spcl` 文档

### 7.2 输出

1. `manifest.spcl`：组合任务执行清单
2. 组合后的技能目录（例如 `trick/`）
3. `trick/SKILL.spcl` + `trick/reference/**/*.spcl` + 其他目录 `.spcl`

### 7.3 错误码建议

1. `ERR_PARSE_FRONTEND`
2. `ERR_INVALID_SCHEMA`
3. `ERR_SKILL_NOT_FOUND`
4. `ERR_MERGE_CONFLICT_UNRESOLVED`
5. `ERR_FIXPOINT_NOT_REACHED`

---

## 8. 最小端到端示例

### 8.1 Markdown 输入

```md
```spcl-md
@meta.name = compose-demo
@compose.inputs =
  = s1
  = s2
@compose.op = and-then
@compose.output = s1-and-then-s2
@resolve.fixpoint = true
```
```

### 8.2 规范化输出（给解释器）

```txt
meta =
  name = compose-demo

compose =
  inputs =
    = s1
    = s2
  op = and-then
  output = s1-and-then-s2

resolve =
  fixpoint = true
  merge = deep
  conflict = right-bias
  max_iter = 64
```

### 8.3 解释器行为

1. 加载 `s1/`、`s2/` 标准技能目录
2. 对 `SKILL.spcl` 先做头部感知处理（`title` 与 `skill.description` 追加）
3. 移除 `s2` 的头部后再合并正文
4. 再合并 `reference/**/*.spcl` 与其他 `.spcl`
5. 输出目录：`s1-and-then-s2/`

---

## 9. 与当前仓库实现的对齐建议

当前仓库已有：

1. `key=value` 解析
2. 递归 decode
3. `spcl_node_merge_into` 深合并
4. `cclq` 多文件 merge/query

建议增量改造：

1. 新增 `md_frontend`：提取 `spcl-md` 块并做 `@` 键映射
2. 新增 `schema_validate`：校验 `meta/skill/compose/resolve` 键空间
3. 新增 `skill_compose`：目录级 `.spcl` 文件发现 + 深合并策略
4. 新增 `fixpoint_runner`：迭代到稳定态
5. CLI 增加子命令：
   - `spcl normalize input.md -o manifest.spcl`
   - `spcl compose manifest.spcl --skills ./skills --out ./trick`

---

## 10. 语言设计结论

你的思想可以落成一个很清晰的内核：

1. **Markdown 是人机协同界面**（自注释协议友好给 LLM）
2. **SPCL 是严格中间表示**（保证解释器稳定）
3. **组合 + fixpoint 是执行语义核心**（把 N 个技能递归组合成 `trick`）

这会把“技能工程”从 prompt 拼接，升级为可验证、可递归、可复现的配置语言系统。
