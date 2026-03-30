# Werk

**一键组合技能。**

```bash
werk skill1 skill2
```

Werk 将多个 SPCL 技能组合成一个新的组合技能，输出到 `trick/<skill1>-and-then-<skill2>/`。

---

## 安装

```bash
git clone https://github.com/aresbit/spcl.git
cd spcl
make clean && make all
sudo make install
```

安装后可用命令：
- `werk` — 技能组合
- `llskill2spcl` — SKILL.md 转 SPCL
- `spcl` / `cclq` — 解释器与查询工具

---

## 用法

### 组合两个技能

```bash
werk modern-c-makefile write-skill
```

输出：`trick/modern-c-makefile-and-then-write-skill/`

### 组合三个或更多

```bash
werk skill-a skill-b skill-c
```

### 开发测试（跳过 LLM）

```bash
werk --mock-llm my-skill
```

---

## 工作原理

```
SKILL.md × N  →  llskill2spcl  →  SKILL.spcl × N
                                           ↓
                                   spcl compose
                                           ↓
                 trick/<combo>/  ←  组装引用文件
```

1. **规范化** — 将每个技能的 `SKILL.md` 转为标准 `SKILL.spcl`
2. **合并** — 深度合并所有技能的配置与引用
3. **输出** — 生成完整的组合技能目录

---

## 环境变量

```bash
# LLM 配置（用于技能规范化）
export LLM_API_KEY="your-key"
export LLM_MODEL="deepseek-chat"      # 默认
export LLM_MAX_TOKENS="8192"          # 默认

# 工具路径
export SPCL_INTERPRETER=/path/to/spcl
export SPCL_LLSKILL2SPCL=/path/to/llskill2spcl
```

---

## 项目结构

```
include/spcl.h      # 公共 API
src/                # 解析器、模型、IO、CLI
tests/              # 回归测试
docs/               # DSL 设计文档
```

---

## 本地开发

```bash
make test       # 运行测试
make sanitize   # AddressSanitizer 检查
make format     # clang-format 格式化
```

---

## 设计来源

- [The Most Elegant Configuration Language](https://chshersh.com/blog/2025-01-06-the-most-elegant-configuration-language.html)
