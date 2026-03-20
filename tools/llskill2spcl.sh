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
has_jq=0
if command -v jq >/dev/null 2>&1; then
  has_jq=1
else
  need_cmd python3
fi

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
temperature="${LLM_TEMPERATURE:-0}"

if [[ -z "$out_file" ]]; then
  dir_name="$(dirname "$skill_md")"
  skill_name="$(basename "$dir_name")"
  out_file="$dir_name/${skill_name}.md"
fi

dsl_doc="docs/SPCL-Skill-Composition-DSL.md"
template_doc="docs/skill-spcl-template.spcl"

if [[ ! -f "$dsl_doc" ]]; then
  echo "Missing DSL doc: $dsl_doc" >&2
  exit 1
fi

if [[ ! -f "$template_doc" ]]; then
  echo "Missing template doc: $template_doc" >&2
  exit 1
fi

tmp_skill_input="$(mktemp)"

awk '
  $0 == "<!-- SPCL:BEGIN -->" { in_block = 1; next }
  in_block && $0 == "<!-- SPCL:END -->" { in_block = 0; next }
  !in_block { print }
' "$skill_md" >"$tmp_skill_input"

if [[ ! -s "$tmp_skill_input" ]]; then
  cp "$skill_md" "$tmp_skill_input"
fi

if [[ "$has_jq" -eq 1 ]]; then
  payload="$(jq -n \
    --arg model "$model" \
    --arg temperature "$temperature" \
    --rawfile skill "$tmp_skill_input" \
    --rawfile dsl "$dsl_doc" \
    --rawfile tpl "$template_doc" '
  {
    model: $model,
    temperature: ($temperature | tonumber),
    messages: [
      {
        role: "system",
        content: "你是 SPCL 规范转换器。任务是把输入的 SKILL.md 转成可被 SPCL 解释器解析的标准 .spcl 文档。只输出纯文本 SPCL，不要 Markdown，不要解释。"
      },
      {
        role: "user",
        content:
          "请严格遵循以下 DSL 设计与模板，输出一个标准化的 SPCL 文档。\n\n" +
          "=== DSL 设计 ===\n" + $dsl + "\n\n" +
          "=== SPCL 模板 ===\n" + $tpl + "\n\n" +
          "=== 待转换 SKILL.md ===\n" + $skill + "\n"
      }
    ]
  }
  ')"
else
  payload="$(python3 - "$model" "$temperature" "$tmp_skill_input" "$dsl_doc" "$template_doc" <<'PY'
import json
import pathlib
import sys

model = sys.argv[1]
temperature = float(sys.argv[2])
skill_path = pathlib.Path(sys.argv[3])
dsl_path = pathlib.Path(sys.argv[4])
tpl_path = pathlib.Path(sys.argv[5])

skill = skill_path.read_text(encoding="utf-8")
dsl = dsl_path.read_text(encoding="utf-8")
tpl = tpl_path.read_text(encoding="utf-8")

obj = {
    "model": model,
    "temperature": temperature,
    "messages": [
        {
            "role": "system",
            "content": "你是 SPCL 规范转换器。任务是把输入的 SKILL.md 转成可被 SPCL 解释器解析的标准 .spcl 文档。只输出纯文本 SPCL，不要 Markdown，不要解释。",
        },
        {
            "role": "user",
            "content": (
                "请严格遵循以下 DSL 设计与模板，输出一个标准化的 SPCL 文档。\n\n"
                + "=== DSL 设计 ===\n" + dsl + "\n\n"
                + "=== SPCL 模板 ===\n" + tpl + "\n\n"
                + "=== 待转换 SKILL.md ===\n" + skill + "\n"
            ),
        },
    ],
}

print(json.dumps(obj, ensure_ascii=False))
PY
)"
fi

tmp_resp="$(mktemp)"
cleanup() {
  rm -f "$tmp_resp" "$tmp_skill_input"
}
trap cleanup EXIT

http_code="$(curl -sS -w '%{http_code}' "$api_url" \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $api_key" \
  -d "$payload" \
  -o "$tmp_resp")"

if [[ "$http_code" -lt 200 || "$http_code" -ge 300 ]]; then
  echo "LLM HTTP error: $http_code" >&2
  cat "$tmp_resp" >&2
  exit 1
fi

if [[ "$has_jq" -eq 1 ]]; then
  spcl_out="$(jq -r '.choices[0].message.content // empty' "$tmp_resp")"
else
  spcl_out="$(python3 - "$tmp_resp" <<'PY'
import json
import pathlib
import sys

resp = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
try:
    data = json.loads(resp)
except json.JSONDecodeError:
    print("")
    sys.exit(0)

choices = data.get("choices") or []
if not choices:
    print("")
    sys.exit(0)

msg = choices[0].get("message") or {}
print(msg.get("content", ""))
PY
)"
fi

if [[ -z "$spcl_out" ]]; then
  echo "LLM returned empty content or unexpected response. Raw response:" >&2
  cat "$tmp_resp" >&2
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
