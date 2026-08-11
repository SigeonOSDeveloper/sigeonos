#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROFILE="$PROJECT_ROOT/profile"
WORK="$PROJECT_ROOT/work"
OUT="$PROJECT_ROOT/out"

mkdir -p "$OUT"

exec mkarchiso -v -r -w "$WORK" -o "$OUT" "$PROFILE"
