#!/bin/sh
# Runs every test and compares the output with the saved expected output.
cd "$(dirname "$0")/.."
BIN=./minisharp
pass=0; fail=0
check() { # name, actual, expected-file
    if [ "$2" = "$(cat "$3")" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL: $1"; fi
}
check basics    "$($BIN tests/basics.cs 2>&1)"     tests/expected/basics.txt
check gc_stress "$($BIN tests/gc_stress.cs 2>&1)"  tests/expected/gc_stress.txt
check hello     "$($BIN examples/hello.cs 2>&1)"   tests/expected/hello.txt
check features2 "$($BIN tests/features2.cs 2>&1)" tests/expected/features2.txt
check list_gc "$($BIN tests/list_gc.cs 2>&1)" tests/expected/list_gc.txt
check math2 "$($BIN tests/math2.cs 2>&1)" tests/expected/math2.txt
check coroutines "$($BIN tests/coroutines.cs --frames 40 2>&1)" tests/expected/coroutines.txt
check coro_gc "$($BIN tests/coro_gc.cs --frames 400 2>&1)" tests/expected/coro_gc.txt
check coro_stop "$($BIN tests/coro_stop.cs --frames 20 2>&1)" tests/expected/coro_stop.txt
check dict "$($BIN tests/dict.cs 2>&1)" tests/expected/dict.txt
check dict_gc "$($BIN tests/dict_gc.cs 2>&1)" tests/expected/dict_gc.txt
check media "$($BIN tests/media.cs 2>&1)" tests/expected/media.txt
check namespace "$($BIN tests/namespace.cs 2>&1)"  tests/expected/namespace.txt
check rt_dictkey "$($BIN tests/rt_dictkey.cs 2>&1 >/dev/null)" tests/expected/rt_dictkey.txt
check rt_dictdup "$($BIN tests/rt_dictdup.cs 2>&1 >/dev/null)" tests/expected/rt_dictdup.txt
check rt_imgmissing "$($BIN tests/rt_imgmissing.cs 2>&1 >/dev/null)" tests/expected/rt_imgmissing.txt
check rt_imgpart "$($BIN tests/rt_imgpart.cs 2>&1 >/dev/null)" tests/expected/rt_imgpart.txt
check rt_imgid "$($BIN tests/rt_imgid.cs 2>&1 >/dev/null)" tests/expected/rt_imgid.txt
check rt_sndbad "$($BIN tests/rt_sndbad.cs 2>&1 >/dev/null)" tests/expected/rt_sndbad.txt
check rt_null   "$($BIN tests/rt_null.cs 2>&1 >/dev/null)"  tests/expected/rt_null.txt
check rt_index  "$($BIN tests/rt_index.cs 2>&1 >/dev/null)" tests/expected/rt_index.txt
check repl "$($BIN --repl < tests/repl.txt 2>&1)" tests/expected/repl.txt
check ci "$($BIN --ci tests/ci 2>&1)" tests/expected/ci.txt
for t in project project_err project_rt project_twomain; do
    check "$t" "$($BIN tests/$t 2>&1)" tests/expected/$t.txt
done
for f in tests/errors/*.cs; do
    n=$(basename "$f" .cs)
    check "error/$n" "$($BIN "$f" 2>&1 >/dev/null)" "tests/errors/$n.expected"
done
for f in examples/*.cs examples/*/; do
    $BIN "$f" --frames 300 >/dev/null 2>&1 || { fail=$((fail+1)); echo "FAIL: example $f"; }
done
echo "passed: $pass, failed: $fail"
[ $fail -eq 0 ]
