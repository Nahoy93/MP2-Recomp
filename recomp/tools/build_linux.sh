#!/usr/bin/env bash
# Bygger Mario Party 2: Recompiled på Linux från din egen ROM.
# Builds Mario Party 2: Recompiled on Linux from your own ROM.
#
#   recomp/tools/build_linux.sh /sökväg/till/Mario\ Party\ 2\ (USA).z64
#
# Steg / steps:
#   1. git-submoduler        4. N64Recomp + RSPRecomp (byggs och körs)
#   2. Python-miljö (.venv)  5. deklarationer för genererad kod
#   3. symboler från ROM:en  6. själva spelet (CMake + Ninja + Clang)
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Användning: $0 <ROM-fil>"
    echo "Usage:      $0 <rom file>"
    exit 1
fi

ROM="$(realpath "$1")"
RECOMP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$RECOMP_DIR/.." && pwd)"
GEN_DIR="$RECOMP_DIR/generated"
TOOLS_BUILD="$RECOMP_DIR/build-tools"
GAME_BUILD="$RECOMP_DIR/build"
JOBS="$(nproc 2>/dev/null || echo 4)"

step() { printf '\n\033[1;32m==> %s\033[0m\n' "$*"; }
fail() { printf '\n\033[1;31mFEL: %s\033[0m\n' "$*" >&2; exit 1; }

for tool in git python3 cmake ninja clang clang++; do
    command -v "$tool" >/dev/null 2>&1 || fail "'$tool' saknas. Se recomp/BYGGA.md för vilka paket som behövs."
done
[[ -f "$ROM" ]] || fail "Hittar inte ROM-filen: $ROM"

step "1/6 Hämtar submoduler (första gången tar det en stund)"
git -C "$REPO_DIR" submodule update --init --recursive recomp/lib

step "2/6 Python-miljö"
if [[ ! -x "$RECOMP_DIR/.venv/bin/python" ]]; then
    python3 -m venv "$RECOMP_DIR/.venv" || fail "Kunde inte skapa venv (installera python3-venv)."
fi
PY="$RECOMP_DIR/.venv/bin/python"
"$PY" -m pip install --quiet --upgrade pip
"$PY" -m pip install --quiet -r "$RECOMP_DIR/requirements.txt"

step "3/6 Läser funktioner ur ROM:en med splat"
"$PY" "$RECOMP_DIR/tools/gen_syms.py" --rom "$ROM" --out "$GEN_DIR"

step "4/6 Bygger och kör N64Recomp"
cmake -S "$RECOMP_DIR/lib/N64ModernRuntime/N64Recomp" -B "$TOOLS_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ >/dev/null
cmake --build "$TOOLS_BUILD" --target N64Recomp RSPRecomp -j "$JOBS"
rm -rf "$GEN_DIR/RecompiledFuncs"
"$TOOLS_BUILD/N64Recomp" "$RECOMP_DIR/config/mp2.us.toml"
"$TOOLS_BUILD/RSPRecomp" "$RECOMP_DIR/config/aspMain.us.toml"

step "5/6 Deklarationer"
"$PY" "$RECOMP_DIR/tools/gen_decls.py" --funcs "$GEN_DIR/RecompiledFuncs"

step "6/6 Bygger spelet"
cmake -S "$RECOMP_DIR" -B "$GAME_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build "$GAME_BUILD" --target mp2-recomp -j "$JOBS"

printf '\n\033[1;32mKlart!\033[0m Starta spelet med:\n  %s/mp2-recomp\n' "$GAME_BUILD"
printf 'Första gången: välj din ROM i startmenyn ("Välj ROM").\n'
