# SKILL.md Frontend To SPCL Rules

目标：把 `SKILL.md` 与其所在技能目录一起视为一个 Markdown Frontend AST，再转换成标准 `.spcl`。

第一原则：`SKILL.md` 中的原始技能内容必须尽量完整保留。转换器做的是“语法转换”和“结构显式化”，不是摘要、删减、改写或压缩。

## 1. 输入模型

转换输入不只是 `SKILL.md` 文本，还包括技能目录树。

- `SKILL.md` 是主前端文档
- 与 `SKILL.md` 同级的目录和文件是该技能的辅助节点
- 目录层级表示结构
- 同级文件/目录表示并列关系

## 2. 输出模型

输出必须是纯 `.spcl`，使用 `key = value` 与缩进表示树，不允许输出 Markdown、代码围栏或解释说明。

必须包含标准键空间：

```txt
meta =
  name = ...
  version = 1

title = ...

skill =
  name = ...
  description =
    ...
  entry = SKILL.spcl
  refs =
    = reference/*.spcl

resolve =
  merge = deep
  conflict = right-bias
  fixpoint = true
  max_iter = 64
```

如果技能天然包含组合意图，也可以补充 `compose.*`。

## 2.1 内容保真要求

转换时必须保留原文中绝大多数信息，尤其是：

- 长段说明
- 分步骤工作流
- 规则列表
- 资源说明
- 示例
- 注意事项
- 约束和禁止项

允许做的事只有：

- 把 Markdown 层级改写成 SPCL 缩进树
- 把并列列表改写成 `=` sibling 节点
- 把目录树显式编码进 `skill.refs`、`skill.extras` 或其他结构节点
- 为了满足标准键空间补最少量必要字段

不允许做的事：

- 摘要
- 改写成更短版本
- 省略“重复但有意义”的条目
- 把几百行技能说明压缩成一小段 `description`
- 只保留标题而丢掉正文
- 只保留结论而丢掉例子、规则、边界条件

## 3. Markdown 前端解释规则

### 3.1 Front matter

若 `SKILL.md` 带 YAML front matter：

- `name` -> `meta.name` 与 `skill.name`
- `description` -> `skill.description`
- 其他可识别元数据映射到 `meta.*`

### 3.2 标题层级

Markdown 标题层级必须被解释为结构，而不是简单拼成一段 prose。

- `#` 表示一层主节点
- `##` 表示其子节点
- `###` 表示更深子节点
- 同层标题是并列 sibling

“层级”必须转成缩进树；
“并列”必须转成同层多个 `key =` 分支。

### 3.3 列表

项目符号和编号列表是并列元素，不得压扁成自然语言段落。

- 列表项是 sibling
- 子列表是嵌套 child
- 若语义上是集合/枚举，使用重复的 `= value` 形式表达

### 3.4 代码块和引用块

- `spcl-md` 代码块优先视为显式前端控制面
- `@foo.bar` 要转换为标准 `foo.bar`
- 其他代码块可作为描述内容的一部分，但不得原样输出 Markdown 围栏

### 3.5 无法进一步结构化的正文

如果某段正文不能自然映射为更细的结构节点，也必须完整保留。

推荐做法：

- 作为某个 section 下的文本叶子节点保留
- 或放入 `content =` / `body =` / `text =` 一类的文本节点

关键原则：

- 允许“保留为文本”
- 不允许“因为不好结构化所以删除”

## 4. 目录树解释规则

技能目录树必须参与建模，不能忽略。

- `reference/` 或 `references/` 表示参考文档集合
- 其他同级目录表示 extras/tooling/assets/script groups
- 同级目录之间是并列关系
- 子目录层级表示更深的结构层级

目录信息至少要反映到：

- `skill.refs`
- `skill.extras`
- 必要时反映到 `meta.annotation.*`

例如：

- `reference/*.spcl`
- `reference/**/*.spcl`
- `assets/*.spcl`
- `scripts/*.spcl`

若目录中的文件当前不是 `.spcl`，也必须在语义上把它们视为前端资源节点，而不是忽略。

## 5. 规范化要求

- 输出必须是 fixpoint-friendly 的 ADT 配置
- 必须使用 `=` 分隔，不得输出 Markdown 样式层级
- 缺失字段要按最合理的标准结构补齐
- 相同层级节点必须保持 sibling 关系
- 不要把结构信息退化成单纯描述性 prose
- 原文正文必须尽量逐段保留，只是改成 SPCL 语法承载
- 若某段内容无法很好结构化，也要原样保留到某个文本节点里，不能删除

## 6. 禁止事项

- 不要输出 Markdown
- 不要输出解释
- 不要省略目录树信息
- 不要把 heading/list 的结构丢失为自然语言总结
- 不要把多个 sibling section 压成一段 description
- 不要删除长篇技能描述
- 不要为了“规范化”牺牲信息量
