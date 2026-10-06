#!/usr/bin/env bash
# Usage: ./build.sh source.c... [clang args] [--dev | --prod]
#   ./build.sh ./deps/htmc/htm.c main.c          development build (default)
#   ./build.sh ./deps/htmc/htm.c main.c --prod   production build (+ out/nginx.conf)
# --dev / --prod go to the generator, everything else to clang.

set -e

mode=()
cc_args=()
for arg in "$@"; do
    case $arg in
        --dev | --prod) mode=("$arg") ;;
        *) cc_args+=("$arg") ;;
    esac
done

set -x
clang -Wall -Wextra "${cc_args[@]}" -o output.o && ./output.o "${mode[@]}"
