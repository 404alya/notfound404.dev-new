#!/usr/bin/env bash
# Steps 1-3 of the verify checklist: clang builds in both modes, gcc, ASan in
# both modes. Copies each build aside and leaves out/ in dev mode.
#
# Usage: build-check.sh [WORK_DIR]
#   WORK_DIR gets out-dev/, out-prod/ and the logs (default: a new temp dir).
# Exit status: 0 when every check passed, 1 otherwise.

set -uo pipefail
cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

work=${1:-$(mktemp -d -t verify.XXXXXX)}
mkdir -p "$work"
fail=0
ok()  { printf 'ok    %s\n' "$*"; }
bad() { printf 'FAIL  %s\n' "$*"; fail=1; }

# Slugs of articles[] entries: their `.slug =` starts a line; a translation's
# `.slug =` sits inside an inline { ... }.
mapfile -t slugs < <(sed -n '/^static const Article articles\[\]/,/^};/p' main.c |
                     sed -nE 's/^[[:space:]]*\.slug = "([^"]+)".*/\1/p')

check_pages() { # dir mode
    local dir=$1 mode=$2 page
    for page in index.html articles.html 404.html "${slugs[@]/#/article/}"; do
        [[ $page == article/* ]] && page=$page.html
        [[ -f $dir/$page ]] || bad "$mode: missing $page"
    done
    [[ -d $dir/assets ]] || bad "$mode: missing assets/"
    if [[ $mode == prod ]]; then
        [[ -f $dir/nginx.conf ]] || bad "prod: out/nginx.conf not written"
    else
        [[ ! -e $dir/nginx.conf ]] || bad "dev: out/nginx.conf present"
    fi
}

# 3. ASan, both modes (runs first: it writes out/ too).
if clang -Wall -Wextra -g -fsanitize=address,undefined ./deps/htmc/htm.c main.c \
        -o "$work/asan.o" 2>"$work/asan-build.log"; then
    for mode in --dev --prod; do
        "$work/asan.o" "$mode" >/dev/null 2>"$work/asan$mode.log"
        status=$?
        if [[ $status -eq 0 ]] && ! grep -qE 'Sanitizer|runtime error' "$work/asan$mode.log"; then
            ok "ASan $mode: exit 0, no reports"
        else
            bad "ASan $mode: exit $status, see $work/asan$mode.log"
        fi
    done
else
    bad "ASan build failed, see $work/asan-build.log"
fi

# 2. gcc warnings.
if gcc -Wall -Wextra ./deps/htmc/htm.c main.c -o "$work/g.o" 2>"$work/gcc.log" &&
   ! grep -q 'warning:' "$work/gcc.log"; then
    ok "gcc: no warnings"
else
    bad "gcc: see $work/gcc.log"
fi

# 1. clang builds; prod first so out/ ends in dev mode.
for mode in prod dev; do
    if ./build.sh ./deps/htmc/htm.c main.c "--$mode" >"$work/build-$mode.log" 2>&1 &&
       ! grep -q 'warning:' "$work/build-$mode.log"; then
        ok "build $mode: no warnings"
    else
        bad "build $mode: see $work/build-$mode.log"
    fi
    check_pages out "$mode"
    rm -rf "$work/out-$mode"
    cp -a out "$work/out-$mode"
done

echo "work dir: $work (out-dev/, out-prod/, logs)"
exit $fail
