#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  werk [OPTIONS] SKILL_DIR [SKILL_DIR ...]

Behavior:
  1) Resolve input skills from work/source directories
  2) Normalize each SKILL.md to SKILL.spcl via LLM (or mock mode)
  3) Generate a compose manifest for the interpreter
  4) Invoke the interpreter to merge SKILL.spcl only
  5) Assemble the final combo skill directory under trick/<combo-name>
  6) Merge reference files in Bash

Important:
  - This script delegates SKILL.spcl composition semantics to the interpreter.
  - Final reference/ assembly is handled here in Bash.
  - Interpreter temporary output is removed after the run.
  - Normalized source SKILL.spcl files are preserved in the final combo directory.

Options:
  --source-dir DIR      Fallback source skills directory. Default: ~/.claude/skills
  --work-dir DIR        Preferred local skills directory. Default: ./skills
  --out-dir DIR         Final bundle root. Default: ./trick
  --interpreter PATH    Interpreter executable path. Default: auto-detect (installed spcl, PATH, or ./build/bin/spcl)
  --refresh-copy        Re-copy requested skills from source dir into work dir before run
  --mock-llm            Skip API call and generate deterministic mock SKILL.spcl
  -h, --help            Show help

Examples:
  werk modern-c-makefile write-skill
  werk --mock-llm modern-c-makefile
  werk --interpreter ./build/bin/spcl modern-c-makefile write-skill
EOF
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing command: $1" >&2
    exit 1
  }
}

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

resolve_llskill2spcl() {
  if [[ -n "${SPCL_LLSKILL2SPCL:-}" ]]; then
    printf '%s\n' "$SPCL_LLSKILL2SPCL"
    return 0
  fi

  if [[ -x "$script_dir/llskill2spcl" ]]; then
    printf '%s\n' "$script_dir/llskill2spcl"
    return 0
  fi

  if [[ -f "$script_dir/llskill2spcl.sh" ]]; then
    printf '%s\n' "$script_dir/llskill2spcl.sh"
    return 0
  fi

  if command -v llskill2spcl >/dev/null 2>&1; then
    command -v llskill2spcl
    return 0
  fi

  if [[ -f "$(pwd)/tools/llskill2spcl.sh" ]]; then
    printf '%s\n' "$(pwd)/tools/llskill2spcl.sh"
    return 0
  fi

  return 1
}

resolve_interpreter() {
  if [[ -n "${SPCL_INTERPRETER:-}" ]]; then
    printf '%s\n' "$SPCL_INTERPRETER"
    return 0
  fi

  if [[ -x "$script_dir/spcl" ]]; then
    printf '%s\n' "$script_dir/spcl"
    return 0
  fi

  if command -v spcl >/dev/null 2>&1; then
    command -v spcl
    return 0
  fi

  if [[ -x "$(pwd)/build/bin/spcl" ]]; then
    printf '%s\n' "$(pwd)/build/bin/spcl"
    return 0
  fi

  return 1
}

source_dir="$HOME/.claude/skills"
work_dir="./skills"
out_dir="./trick"
interpreter=""
refresh_copy=0
mock_llm=0

skills=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source-dir)
      source_dir="${2:?missing value for --source-dir}"
      shift 2
      ;;
    --work-dir)
      work_dir="${2:?missing value for --work-dir}"
      shift 2
      ;;
    --out-dir)
      out_dir="${2:?missing value for --out-dir}"
      shift 2
      ;;
    --interpreter)
      interpreter="${2:?missing value for --interpreter}"
      shift 2
      ;;
    --refresh-copy)
      refresh_copy=1
      shift
      ;;
    --mock-llm)
      mock_llm=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
    *)
      skills+=("$1")
      shift
      ;;
  esac
done

if [[ "${#skills[@]}" -eq 0 ]]; then
  echo "At least one SKILL_DIR is required." >&2
  usage >&2
  exit 1
fi

need_cmd bash
need_cmd find
need_cmd sort
llskill2spcl_cmd="$(resolve_llskill2spcl || true)"

if [[ -z "$interpreter" ]]; then
  interpreter="$(resolve_interpreter || true)"
fi

if [[ -z "$llskill2spcl_cmd" ]]; then
  echo "Unable to locate llskill2spcl helper script" >&2
  exit 1
fi

if [[ -z "$interpreter" ]]; then
  echo "Unable to locate spcl interpreter" >&2
  echo "Looked for: \$SPCL_INTERPRETER, sibling spcl next to werk, spcl in PATH, and ./build/bin/spcl" >&2
  echo "Expected an interpreter command that supports: compose <manifest> --skills <dir> --out <dir>" >&2
  exit 1
fi

if [[ "$refresh_copy" -eq 1 ]]; then
  if [[ ! -d "$source_dir" ]]; then
    echo "Source skills directory not found: $source_dir" >&2
    exit 1
  fi

  rm -rf "$work_dir"
  mkdir -p "$work_dir"

  for skill in "${skills[@]}"; do
    if [[ ! -d "$source_dir/$skill" ]]; then
      echo "Skill not found in source dir: $source_dir/$skill" >&2
      exit 1
    fi
    cp -R "$source_dir/$skill" "$work_dir/$skill"
  done
fi

resolved_skill_dirs=()
for skill in "${skills[@]}"; do
  if [[ -d "$work_dir/$skill" ]]; then
    resolved_skill_dirs+=("$work_dir/$skill")
    continue
  fi

  if [[ -d "$source_dir/$skill" ]]; then
    resolved_skill_dirs+=("$source_dir/$skill")
    continue
  fi

  echo "Skill directory not found in work/source dirs: $skill" >&2
  exit 1
done

if [[ ! -x "$interpreter" ]]; then
  echo "Interpreter not found or not executable: $interpreter" >&2
  echo "Expected an interpreter command that supports: compose <manifest> --skills <dir> --out <dir>" >&2
  exit 1
fi

tmp_root="$(mktemp -d)"
trap 'rm -rf "$tmp_root"' EXIT

normalized_root="$tmp_root/skills"
manifest_file="$tmp_root/manifest.spcl"
combo_name="$(printf '%s-and-then-' "${skills[@]}")"
combo_name="${combo_name%-and-then-}"
combo_dir="$out_dir/$combo_name"
interpreter_out="$tmp_root/interpreter-out"

copy_into_reference() {
  local src_file="$1"
  local rel_path="$2"
  local dest_path="$combo_dir/reference/$rel_path"

  mkdir -p "$(dirname "$dest_path")"
  cp "$src_file" "$dest_path"
}

copy_into_combo_root() {
  local src_file="$1"
  local rel_path="$2"
  local dest_path="$combo_dir/$rel_path"

  mkdir -p "$(dirname "$dest_path")"
  cp "$src_file" "$dest_path"
}

merge_skill_reference() {
  local skill_dir="$1"
  local ref_dir=""

  if [[ -d "$skill_dir/reference" ]]; then
    ref_dir="$skill_dir/reference"
  elif [[ -d "$skill_dir/references" ]]; then
    ref_dir="$skill_dir/references"
  fi

  if [[ -n "$ref_dir" ]]; then
    while IFS= read -r path; do
      local rel_path="${path#"$ref_dir"/}"
      copy_into_reference "$path" "$rel_path"
    done < <(find "$ref_dir" -type f | sort)
    return
  fi

  while IFS= read -r path; do
    local rel_path="${path#"$skill_dir"/}"

    case "$rel_path" in
      script/*|scripts/*)
        copy_into_combo_root "$path" "$rel_path"
        ;;
      *)
        copy_into_reference "$path" "$rel_path"
        ;;
    esac
  done < <(find "$skill_dir" -type f ! -name 'SKILL.md' ! -name 'SKILL.spcl' | sort)
}

normalize_embedded_spcl() {
  local spcl_file="$1"

  python3 - "$spcl_file" <<'PY'
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
text = re.sub(r"(^[ \t]*entry[ \t]*=\n)([ \t]*)SKILL\.spcl[ \t]*=$", r"\1\2SKILL.md =", text, count=1, flags=re.M)
print(text, end="")
PY
}

build_combo_description() {
  local count="$#"

  if [[ "$count" -eq 0 ]]; then
    printf 'Composite skill.'
    return
  fi

  if [[ "$count" -eq 1 ]]; then
    printf 'Composite skill based on %s.' "$1"
    return
  fi

  if [[ "$count" -eq 2 ]]; then
    printf 'Composite skill that chains %s and %s.' "$1" "$2"
    return
  fi

  local joined=""
  local i=0
  for name in "$@"; do
    if [[ "$i" -eq 0 ]]; then
      joined="$name"
    elif [[ "$i" -eq $((count - 1)) ]]; then
      joined="$joined, and $name"
    else
      joined="$joined, $name"
    fi
    i=$((i + 1))
  done

  printf 'Composite skill that chains %s.' "$joined"
}

write_combo_skill_md() {
  local out_file="$1"
  local spcl_file="$2"
  local combo_description=""

  combo_description="$(build_combo_description "${skills[@]}")"

  {
    printf -- '---
'
    printf 'name: %s
' "$combo_name"
    printf 'description: %s
' "$combo_description"
    printf 'version: 1
'
    printf -- '---

'
    normalize_embedded_spcl "$spcl_file"
    printf '
'
  } >"$out_file"
}

mkdir -p "$normalized_root"

for i in "${!skills[@]}"; do
  skill="${skills[$i]}"
  skill_dir="${resolved_skill_dirs[$i]}"
  skill_out="$normalized_root/$skill"
  skill_md="$skill_dir/SKILL.md"
  skill_spcl="$skill_out/SKILL.spcl"

  if [[ ! -f "$skill_md" ]]; then
    echo "SKILL.md not found: $skill_md" >&2
    exit 1
  fi

  mkdir -p "$skill_out"

  if [[ "$mock_llm" -eq 1 ]]; then
    cat >"$skill_spcl" <<EOF
meta =
  name = $skill
  version = 1

title = $skill

skill =
  name = $skill
  description =
    generated by mock mode
  entry = SKILL.spcl
  refs =
    = reference/*.spcl
EOF
  else
    bash "$llskill2spcl_cmd" --skill "$skill_md" --out "$skill_spcl"
  fi
done

{
  printf 'meta =\n'
  printf '  name = %s\n' "$combo_name"
  printf '  version = 1\n\n'
  printf 'compose =\n'
  printf '  inputs =\n'
  for skill in "${skills[@]}"; do
    printf '    = %s\n' "$skill"
  done
  printf '  op = and-then\n'
  printf '  output = %s\n\n' "$combo_name"
  printf 'resolve =\n'
  printf '  merge = deep\n'
  printf '  conflict = right-bias\n'
  printf '  fixpoint = true\n'
  printf '  max_iter = 64\n'
} >"$manifest_file"

rm -rf "$combo_dir"
mkdir -p "$combo_dir/reference" "$combo_dir/source-skills"

for i in "${!skills[@]}"; do
  skill="${skills[$i]}"
  mkdir -p "$combo_dir/source-skills/$skill"
  cp "$normalized_root/$skill/SKILL.spcl" "$combo_dir/source-skills/$skill/SKILL.spcl"
done

"$interpreter" compose "$manifest_file" --skills "$normalized_root" --out "$interpreter_out"

if [[ ! -f "$interpreter_out/SKILL.spcl" ]]; then
  echo "Interpreter output missing SKILL.spcl: $interpreter_out/SKILL.spcl" >&2
  exit 1
fi

write_combo_skill_md "$combo_dir/SKILL.md" "$interpreter_out/SKILL.spcl"

for i in "${!skills[@]}"; do
  merge_skill_reference "${resolved_skill_dirs[$i]}"
done

echo "Composed skill generated: $combo_dir"
