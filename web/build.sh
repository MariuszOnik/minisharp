#!/bin/sh
# Builds the browser version into web/dist (needs Python 3; installs Zig with pip if missing).
#   sh web/build.sh
set -e
cd "$(dirname "$0")"
PY=python3; $PY -c "" >/dev/null 2>&1 || PY=python        # Windows has "python"
ZIG="$PY -m ziglang"
$ZIG version >/dev/null 2>&1 || pip install --quiet ziglang || pip install --quiet --break-system-packages ziglang
rm -rf dist && mkdir -p dist/examples
$ZIG cc -target wasm32-wasi -mcpu=generic+exception_handling -mllvm -wasm-enable-sjlj \
    -mexec-model=reactor -O2 -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wno-unused-function \
    -Wl,-z,stack-size=1048576 -Wl,--strip-all \
    ../src/parser.c ../src/compiler.c ../src/vm.c ../src/natives.c ../src/media.c ../src/project.c ../src/printer.c \
    main_web.c sjlj.s -o dist/minisharp.wasm
cp index.html app.js complete.js graph.js grapheditor.js style.css przewodnik.html dist/
sed -i "s/__BUILD__/$(date -u +%Y-%m-%d_%H:%M)Z/" dist/app.js
$PY ../tools/gen_help.py --web dist/help.json
$PY make_examples.py ../examples dist/examples
touch dist/.nojekyll
echo "web/dist ready ($(wc -c < dist/minisharp.wasm) bytes of wasm)"
