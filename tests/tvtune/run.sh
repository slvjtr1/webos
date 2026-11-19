#!/bin/bash
# tests/tvtune/run.sh : the TV tuning against a fake settings service (any host with a C compiler and pthreads)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; mkdir -p "$ROOT/build"
cd "$ROOT"
cc -std=c11 -D_GNU_SOURCE -DTVTUNE_TEST -Wall -Wextra -Wno-unused-parameter -O1 -g ${SB_ASAN:+-fsanitize=address,undefined} -Isrc -Ithird_party/cjson \
  tests/tvtune/test.c src/core/tvtune.c third_party/cjson/cJSON.c -lpthread -o build/tvtune_test
./build/tvtune_test
