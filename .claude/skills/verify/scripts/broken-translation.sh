#!/usr/bin/env bash
# Step 4, translations: build copies of main.c with a deliberately broken
# translation (unknown slug, same lang) and confirm check_translations()
# die_msg()s before any page is written. main.c itself is never touched.
#
# Usage: broken-translation.sh
# Exit status: 0 when both broken builds were stopped, 1 otherwise.

set -uo pipefail
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
work=$(mktemp -d -t broken-translation.XXXXXX)
trap 'rm -rf "$work"' EXIT
fail=0

# The first articles[] entry: its slug and lang.
first=$(sed -n '/^static const Article articles\[\]/,/^};/p' main.c)
slug=$(sed -nE 's/^[[:space:]]*\.slug = "([^"]+)".*/\1/p' <<<"$first" | head -1)
lang=$(sed -nE 's/^[[:space:]]*\.lang = "([^"]+)".*/\1/p' <<<"$first" | head -1)

try() { # name translation-initializer
    local name=$1 link=$2
    local entry="{ .slug = \"zz-broken\", .lang = \"$lang\", .title = \"x\", .description = \"\", .published = \"2025-01-01\", .body_path = \"articles/none.html\", .translations = (const ArticleTranslation[]){ $link, { 0 } } },"
    sed "/^static const Article articles\[\] = {/a\\
$entry" main.c >"$work/main-$name.c"
    # -iquote: the copy's #include of htmc.h resolves from the project root.
    if ! clang -w -iquote "$root" deps/htmc/htm.c "$work/main-$name.c" -o "$work/$name.o" 2>"$work/$name-cc.log"; then
        echo "FAIL  $name: does not compile:"; cat "$work/$name-cc.log"; fail=1; return
    fi
    local before
    before=$(stat -c %Y out/index.html 2>/dev/null || echo none)
    if "$work/$name.o" >/dev/null 2>"$work/$name.log"; then
        echo "FAIL  $name: generator exited 0 (out/ may now hold its pages: rebuild)"
        fail=1
    elif [[ $(stat -c %Y out/index.html 2>/dev/null || echo none) != "$before" ]]; then
        echo "FAIL  $name: stopped, but only after writing pages"
        fail=1
    else
        echo "ok    $name: stopped with: $(tail -1 "$work/$name.log")"
    fi
}

try unknown-slug "{ .lang = \"xx\", .slug = \"zz-no-such-slug\" }"
try same-lang    "{ .lang = \"$lang\", .slug = \"$slug\" }"
exit $fail
