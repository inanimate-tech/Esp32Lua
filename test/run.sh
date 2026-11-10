#!/bin/sh
# Builds and runs the host tests: test/run.sh [cc]
#
# hook_delivery: count hooks inside long-running library calls (also built
# by test/Makefile).
#
# cstack_test: the C-stack limit check (LUAI_CSTACKRESERVE), once with a
# C-stack limit the test controls and once with none known. The first build
# doubles the reserve and the error space, because a 64-bit host's frames
# are about twice the size of a 32-bit microcontroller's.
set -eu

cd "$(dirname "$0")/.."
CC=${1:-${CC:-cc}}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

CORE=$(ls src/lua/*.c | grep -v -e '/lua\.c$' -e '/luac\.c$')
CFLAGS="-std=gnu99 -O2 -Wall -Wextra -Isrc/lua ${EXTRA_CFLAGS:-}"

"$CC" $CFLAGS -DLUAI_CSTACKRESERVE=4096 -DLUAI_CSTACKERRSPACE=3072 \
  '-Dluai_cstacklimit(L)=test_cstacklimit()' -include test/cstack_limit.h \
  -o "$OUT/cstack_test" test/cstack_test.c $CORE -lm
"$CC" $CFLAGS -DTEST_NO_LIMIT \
  -o "$OUT/cstack_test_nolimit" test/cstack_test.c $CORE -lm

"$CC" $CFLAGS -o "$OUT/hook_delivery" test/hook_delivery.c $CORE -lm

echo "== count hooks in library calls"
"$OUT/hook_delivery"
echo "== C-stack limit known"
"$OUT/cstack_test"
echo "== no C-stack limit known"
"$OUT/cstack_test_nolimit"
