#!/usr/bin/env bash
# Supply cJSON from ESP-IDF v5.5.2 (c859b25da02955fef659d658b8f324b5cde87be3).
# Usage: CJSON_DIR=/path/to/cJSON BUILD_DIR=/path/to/build bash tests/host/run.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
: "${CJSON_DIR:?Set CJSON_DIR to the ESP-IDF cJSON source directory}"
: "${BUILD_DIR:?Set BUILD_DIR to a disposable build directory}"
mkdir -p "$BUILD_DIR"
for variant in default minimax custom; do
    defines=()
    if [[ "$variant" != default ]]; then
        defines+=('-DMIMI_SECRET_MODEL_PROVIDER="minimax"')
    fi
    if [[ "$variant" == custom ]]; then
        defines+=('-DMIMI_SECRET_MODEL="build-custom-model"')
    fi
    "${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -g \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        "${defines[@]}" -Itests/host -Imain -I"$CJSON_DIR" \
        tests/host/test_llm_transport.c "$CJSON_DIR/cJSON.c" \
        -o "$BUILD_DIR/test-llm-$variant"
    "$BUILD_DIR/test-llm-$variant"
done
