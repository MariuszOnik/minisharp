#!/bin/sh
# Builds the browser version into web/dist (needs Python 3; installs Zig with pip if missing).
#   sh web/build.sh
set -e
cd "$(dirname "$0")"
ZIG="python3 -m ziglang"
$ZIG version >/dev/null 2>&1 || pip install --quiet ziglang || pip install --quiet --break-system-packages ziglang
rm -rf dist && mkdir -p dist/examples
$ZIG cc -target wasm32-wasi -mcpu=generic+exception_handling -mllvm -wasm-enable-sjlj \
    -mexec-model=reactor -O2 -std=gnu11 -D_DEFAULT_SOURCE -Wall -Wno-unused-function \
    -Wl,-z,stack-size=1048576 -Wl,--strip-all \
    ../src/parser.c ../src/compiler.c ../src/vm.c ../src/natives.c ../src/media.c ../src/project.c \
    main_web.c sjlj.s -o dist/minisharp.wasm
cp index.html app.js style.css dist/
cp ../sdk/Help.txt dist/help.txt
python3 make_examples.py ../examples dist/examples
touch dist/.nojekyll
echo "web/dist ready ($(wc -c < dist/minisharp.wasm) bytes of wasm)"
