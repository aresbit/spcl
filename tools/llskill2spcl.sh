#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  bash tools/llskill2spcl.sh --skill PATH_TO_SKILL_MD [--out OUT_FILE]

Required env:
  LLM_API_KEY            API key for provider (generic)
  DEEPSEEK_API_KEY       DeepSeek API key (alternative)

Optional env:
  LLM_API_URL            Chat Completions endpoint
  DEEPSEEK_API_URL       DeepSeek endpoint (alternative)
                         default: https://api.deepseek.com/chat/completions
  LLM_MODEL              Model name
                         default: deepseek-chat
  LLM_MAX_TOKENS         Max output tokens
                         default: 8192
  LLM_TEMPERATURE        Sampling temperature
                         default: 0

Examples:
  export DEEPSEEK_API_KEY="..."
  bash tools/llskill2spcl.sh --skill ./skills/my-skill/SKILL.md
  bash tools/llskill2spcl.sh --skill ./skills/my-skill/SKILL.md --out /tmp/my-skill.md
EOF
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing command: $1" >&2
    exit 1
  }
}

has_proxy_env() {
  [[ -n "${https_proxy:-}" ]] || \
    [[ -n "${http_proxy:-}" ]] || \
    [[ -n "${HTTPS_PROXY:-}" ]] || \
    [[ -n "${HTTP_PROXY:-}" ]] || \
    [[ -n "${all_proxy:-}" ]] || \
    [[ -n "${ALL_PROXY:-}" ]] || \
    [[ -n "${no_proxy:-}" ]] || \
    [[ -n "${NO_PROXY:-}" ]]
}

curl_chat_completion() {
  local disable_proxy="${1:-0}"
  local -a curl_cmd=(
    curl
    --http1.1
    -sS
    -w
    '%{http_code}'
    "$api_url"
    -H
    "Content-Type: application/json"
    -H
    "Authorization: Bearer $api_key"
    --data-binary
    "@$tmp_payload"
    -o
    "$tmp_resp"
  )

  if [[ "$disable_proxy" -eq 1 ]]; then
    env -u https_proxy -u http_proxy -u HTTPS_PROXY -u HTTP_PROXY \
      -u all_proxy -u ALL_PROXY -u no_proxy -u NO_PROXY \
      "${curl_cmd[@]}"
    return $?
  fi

  "${curl_cmd[@]}"
}

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

find_support_file() {
  local rel_path="$1"
  local candidate=""

  if [[ -n "${SPCL_SHARE_DIR:-}" ]]; then
    candidate="$SPCL_SHARE_DIR/$rel_path"
    if [[ -f "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  fi

  candidate="$script_dir/../share/spcl/$rel_path"
  if [[ -f "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  candidate="$script_dir/../docs/$(basename "$rel_path")"
  if [[ -f "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  candidate="$(pwd)/$rel_path"
  if [[ -f "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  return 1
}

skill_tree_snapshot() {
  local skill_dir="$1"
  python3 - "$skill_dir" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
lines = []
for path in sorted(root.rglob("*")):
    rel = path.relative_to(root).as_posix()
    if not rel:
        continue
    suffix = "/" if path.is_dir() else ""
    lines.append(rel + suffix)

print("\n".join(lines))
PY
}

skill_md=""
out_file=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skill)
      skill_md="${2:?missing value for --skill}"
      shift 2
      ;;
    --out)
      out_file="${2:?missing value for --out}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown arg: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "$skill_md" ]]; then
  echo "--skill is required" >&2
  usage >&2
  exit 1
fi

need_cmd curl
need_cmd python3

if [[ ! -f "$skill_md" ]]; then
  echo "SKILL.md not found: $skill_md" >&2
  exit 1
fi

api_key="${LLM_API_KEY:-${DEEPSEEK_API_KEY:-}}"
if [[ -z "$api_key" ]]; then
  echo "Missing API key: set LLM_API_KEY or DEEPSEEK_API_KEY" >&2
  exit 1
fi

api_url="${LLM_API_URL:-${DEEPSEEK_API_URL:-https://api.deepseek.com/chat/completions}}"
model="${LLM_MODEL:-deepseek-chat}"
max_tokens="${LLM_MAX_TOKENS:-8192}"
temperature="${LLM_TEMPERATURE:-0}"

if [[ -z "$out_file" ]]; then
  dir_name="$(dirname "$skill_md")"
  skill_name="$(basename "$dir_name")"
  out_file="$dir_name/${skill_name}.md"
fi

dsl_doc="$(find_support_file "docs/SPCL-Skill-Composition-DSL.md" || true)"
template_doc="$(find_support_file "docs/skill-spcl-template.spcl" || true)"
frontend_doc="$(find_support_file "docs/skill-markdown-frontend.md" || true)"
skill_dir="$(dirname "$skill_md")"

if [[ ! -f "$dsl_doc" ]]; then
  echo "Missing DSL doc: $dsl_doc" >&2
  exit 1
fi

if [[ ! -f "$template_doc" ]]; then
  echo "Missing template doc: $template_doc" >&2
  exit 1
fi

if [[ ! -f "$frontend_doc" ]]; then
  echo "Missing frontend doc: $frontend_doc" >&2
  exit 1
fi

tmp_skill_input="$(mktemp)"
tmp_skill_tree="$(mktemp)"

cp "$skill_md" "$tmp_skill_input"

skill_tree_snapshot "$skill_dir" >"$tmp_skill_tree"

payload="$(python3 - "$model" "$max_tokens" "$temperature" "$tmp_skill_input" "$tmp_skill_tree" "$dsl_doc" "$frontend_doc" "$template_doc" <<'PY'
import json
import pathlib
import sys

model = sys.argv[1]
max_tokens = int(sys.argv[2])
temperature = float(sys.argv[3])
skill_path = pathlib.Path(sys.argv[4])
tree_path = pathlib.Path(sys.argv[5])
dsl_path = pathlib.Path(sys.argv[6])
frontend_path = pathlib.Path(sys.argv[7])
tpl_path = pathlib.Path(sys.argv[8])

skill = skill_path.read_text(encoding="utf-8")
tree = tree_path.read_text(encoding="utf-8")
dsl = dsl_path.read_text(encoding="utf-8")
frontend = frontend_path.read_text(encoding="utf-8")
tpl = tpl_path.read_text(encoding="utf-8")

obj = {
    "model": model,
    "max_tokens": max_tokens,
    "temperature": temperature,
    "messages": [
        {
            "role": "system",
            "content": "你是 SPCL Frontend 规范转换器。你的职责是把输入的 SKILL.md 与技能目录树无损地重编码为标准 .spcl。你做的是语法转换，不是摘要，不是改写，不是删减。必须尽量保留原始技能内容的全部信息，尤其是长段说明、规则、步骤、示例和限制。输出必须是纯文本 SPCL；只能使用 = 分隔与缩进树；不要 Markdown，不要解释。",
        },
        {
            "role": "user",
            "content": (
                "请把输入视为 SKILL Markdown Frontend，而不是普通说明文。你必须把 Markdown 标题层级、列表并列关系、目录层级与 sibling 关系一起前端解释成 fixpoint-friendly ADT 配置。\n\n"
                + "硬约束：不要删除 SKILL.md 中的大段内容。几百行技能描述也要尽量完整保留，只是转换为 SPCL 语法。不要总结，不要压缩，不要只提取要点，不要把整篇内容塞成很短的 description。\n\n"
                + "如果某些正文无法自然映射为结构化字段，也必须保留为文本节点，而不是删除。\n\n"
                + "请严格遵循以下 DSL 设计与模板，输出一个标准化的 SPCL 文档。\n\n"
                + "=== DSL 设计 ===\n" + dsl + "\n\n"
                + "=== Markdown Frontend 解释规则 ===\n" + frontend + "\n\n"
                + "=== SPCL 模板 ===\n" + tpl + "\n\n"
                + "=== 技能目录树 ===\n" + tree + "\n\n"
                + "=== 待转换 SKILL.md ===\n" + skill + "\n"
            ),
        },
    ],
}

print(json.dumps(obj, ensure_ascii=False))
PY
)"

tmp_resp="$(mktemp)"
tmp_payload="$(mktemp)"
tmp_content="$(mktemp)"
tmp_finish_reason="$(mktemp)"
cleanup() {
  rm -f "$tmp_resp" "$tmp_payload" "$tmp_content" "$tmp_finish_reason" "$tmp_skill_input" "$tmp_skill_tree"
}
trap cleanup EXIT

printf '%s' "$payload" >"$tmp_payload"

curl_rc=0
if has_proxy_env; then
  set +e
  http_code="$(curl_chat_completion 1)"
  curl_rc=$?
  set -e

  if [[ "$curl_rc" -ne 0 ]]; then
    echo "curl failed without proxy (code $curl_rc); retrying with proxy env" >&2
    set +e
    http_code="$(curl_chat_completion 0)"
    curl_rc=$?
    set -e
  fi
else
  set +e
  http_code="$(curl_chat_completion 0)"
  curl_rc=$?
  set -e
fi

if [[ "$curl_rc" -ne 0 ]]; then
  exit "$curl_rc"
fi

if [[ "$http_code" -lt 200 || "$http_code" -ge 300 ]]; then
  echo "LLM HTTP error: $http_code" >&2
  cat "$tmp_resp" >&2
  exit 1
fi

python3 - "$tmp_resp" "$tmp_content" "$tmp_finish_reason" <<'PY'
import json
import pathlib
import sys

resp = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
content_path = pathlib.Path(sys.argv[2])
finish_reason_path = pathlib.Path(sys.argv[3])

try:
    data = json.loads(resp)
except json.JSONDecodeError:
    content_path.write_text("", encoding="utf-8")
    finish_reason_path.write_text("", encoding="utf-8")
    sys.exit(0)

choices = data.get("choices") or []
if not choices:
    content_path.write_text("", encoding="utf-8")
    finish_reason_path.write_text("", encoding="utf-8")
    sys.exit(0)

msg = choices[0].get("message") or {}
content_path.write_text(msg.get("content", ""), encoding="utf-8")
finish_reason_path.write_text(choices[0].get("finish_reason", ""), encoding="utf-8")
PY

spcl_out="$(cat "$tmp_content")"
finish_reason="$(cat "$tmp_finish_reason")"

if [[ -z "$spcl_out" ]]; then
  echo "LLM returned empty content or unexpected response. Raw response:" >&2
  cat "$tmp_resp" >&2
  exit 1
fi

if [[ "$finish_reason" == "length" ]]; then
  echo "LLM output was truncated by max_tokens=$max_tokens. Increase LLM_MAX_TOKENS and retry." >&2
  exit 1
fi

# Remove fenced markdown wrappers if model returns ```spcl ... ```
spcl_out="$(printf '%s\n' "$spcl_out" | awk '
  NR == 1 && $0 ~ /^```/ { in_fence=1; next }
  in_fence && $0 ~ /^```[[:space:]]*$/ { in_fence=0; next }
  { print }
')"

mkdir -p "$(dirname "$out_file")"
printf '%s\n' "$spcl_out" >"$out_file"
echo "Generated: $out_file"
