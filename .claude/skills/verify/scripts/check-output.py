#!/usr/bin/env python3
"""Step 4 of the verify checklist, on the builds copied aside by build-check.sh.

Usage: check-output.py OUT_DEV OUT_PROD [--before-dev DIR] [--before-prod DIR]

Checks:
  - nginx.conf only in prod;
  - prod: canonical / og: / hreflang values are absolute https://notfound404.dev/;
    dev: no canonical or og: at all;
  - relative src/href left in prod (only js/skills.js template literals allowed);
  - every page has exactly one application/ld+json block, it parses, and its
    "@id" / "url" values start with https://notfound404.dev/;
  - each article's <html lang> matches articles[] in main.c;
  - undoing link_headings() gives each article body back byte for byte;
  - with --before-*: lists the files that differ from an earlier build.
Not covered: nginx -t and the broken-translation build (see SKILL.md).
Run from anywhere; main.c and articles/ are found from this file's location.
"""
import argparse
import filecmp
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
SITE = "https://notfound404.dev/"
failures = []


def fail(msg):
    failures.append(msg)
    print("FAIL ", msg)


def ok(msg):
    print("ok   ", msg)


def pages(out):
    return sorted(p for p in out.rglob("*.html") if "assets" not in p.relative_to(out).parts)


def articles():
    """(slug, lang, body_path) for each articles[] entry in main.c."""
    src = (ROOT / "main.c").read_text()
    block = re.search(r"^static const Article articles\[\] = \{(.*?)^\};", src, re.S | re.M).group(1)
    entries = re.split(r"^\s*\.slug = ", block, flags=re.M)[1:]
    result = []
    for e in entries:
        slug = re.match(r'"([^"]+)"', e).group(1)
        lang = re.search(r'^\s*\.lang = "([^"]+)"', e, re.M).group(1)
        body = re.search(r'^\s*\.body_path = "([^"]+)"', e, re.M).group(1)
        result.append((slug, lang, body))
    return result


def undo_headings(html, body):
    """Remove what link_headings() added, keeping anything already in the body."""
    def unwrap(m):
        tag, attrs, ident, inner = m.group(1), m.group(2), m.group(3), m.group(4)
        return f"<{tag}{attrs}>{inner}</{tag}>" if f'<a href="#{ident}">' not in body else m.group(0)
    html = re.sub(r'<(h[2-6])([^>]*)><a href="#([^"]*)">(.*?)</a></\1>', unwrap, html, flags=re.S)

    def strip_id(m):
        return m.group(0) if m.group(0) in body else f"<{m.group(1)}"
    return re.sub(r'<(h[2-6]) id="[^"]*"', strip_id, html)


def check_jsonld(out, mode):
    bad = 0
    for page in pages(out):
        html = page.read_text()
        blocks = re.findall(r'<script type="application/ld\+json">(.*?)</script>', html, re.S)
        if len(blocks) != 1:
            fail(f"{mode} {page.relative_to(out)}: {len(blocks)} ld+json blocks")
            bad += 1
            continue
        try:
            data = json.loads(blocks[0])
        except json.JSONDecodeError as e:
            fail(f"{mode} {page.relative_to(out)}: JSON-LD does not parse: {e}")
            bad += 1
            continue
        stack = [data]
        while stack:
            node = stack.pop()
            if isinstance(node, dict):
                for k, v in node.items():
                    if k in ("@id", "url") and isinstance(v, str) and not v.startswith(SITE):
                        fail(f"{mode} {page.relative_to(out)}: {k} = {v}")
                        bad += 1
                    stack.append(v)
            elif isinstance(node, list):
                stack.extend(node)
    if not bad:
        ok(f"{mode}: one parsing JSON-LD block per page, absolute @id/url")


def check_meta(out, mode):
    found = 0
    for page in pages(out):
        html = page.read_text()
        for tag in re.findall(r"<(?:link|meta)\b[^>]*>", html):
            if not re.search(r'rel="canonical"|property="og:|hreflang=', tag):
                continue
            if "hreflang=" not in tag:
                found += 1
                if mode == "dev":
                    fail(f"dev {page.relative_to(out)}: share meta in dev: {tag}")
                if page.name == "404.html":
                    fail(f"{mode} 404.html: share meta present: {tag}")
            m = re.search(r'(?:href|content)="([^"]*)"', tag)
            value = m.group(1) if m else ""
            is_url = "hreflang=" in tag or 'rel="canonical"' in tag or re.search(r'og:(url|image)"', tag)
            if is_url and not value.startswith(SITE):
                fail(f"{mode} {page.relative_to(out)}: relative URL in {tag}")
    if mode == "prod" and not found:
        fail("prod: no canonical/og: tags found")
    else:
        ok(f"{mode}: canonical/og:/hreflang ({found} share tags)")


def check_relative(out):
    rel = set()
    for page in pages(out):
        for v in re.findall(r'(?:src|href)="([^"]*)"', page.read_text()):
            if not re.match(r"(/|https?:|#|mailto:)", v):
                rel.add(v)
    unexpected = sorted(v for v in rel if "${" not in v)
    if unexpected:
        fail(f"prod: relative src/href outside js/skills.js templates: {unexpected}")
    else:
        ok(f"prod: relative src/href only in template literals: {sorted(rel)}")


def check_articles(out, mode):
    for slug, lang, body_path in articles():
        page = out / "article" / f"{slug}.html"
        html = page.read_text()
        m = re.search(r'<html[^>]*\blang="([^"]*)"', html)
        if not m or m.group(1) != lang:
            fail(f"{mode} {slug}: <html lang> is {m and m.group(1)}, expected {lang}")
        body = (ROOT / body_path).read_text()
        if body not in undo_headings(html, body):
            fail(f"{mode} {slug}: undoing the heading transform does not give {body_path} back")
    ok(f"{mode}: article lang and heading transform checked")


def diff(before, after, mode):
    changed = []

    def walk(cmp, prefix=""):
        changed.extend(f"{prefix}{n}" for n in cmp.diff_files + cmp.left_only + cmp.right_only)
        for name, sub in cmp.subdirs.items():
            walk(sub, f"{prefix}{name}/")
    walk(filecmp.dircmp(before, after))
    print(f"info  {mode}: changed vs before: {sorted(changed) or 'nothing'}")


def main(argv):
    parser = argparse.ArgumentParser(usage=__doc__.splitlines()[2].removeprefix("Usage: "))
    parser.add_argument("out_dev", type=pathlib.Path)
    parser.add_argument("out_prod", type=pathlib.Path)
    parser.add_argument("--before-dev", type=pathlib.Path)
    parser.add_argument("--before-prod", type=pathlib.Path)
    a = parser.parse_args(argv)
    dev, prod = a.out_dev, a.out_prod
    (fail if (dev / "nginx.conf").exists() else ok)("dev: no nginx.conf")
    (ok if (prod / "nginx.conf").exists() else fail)("prod: nginx.conf written")
    for out, mode in ((dev, "dev"), (prod, "prod")):
        check_meta(out, mode)
        check_jsonld(out, mode)
        check_articles(out, mode)
    check_relative(prod)
    for before, out, mode in ((a.before_dev, dev, "dev"), (a.before_prod, prod, "prod")):
        if before:
            diff(before, out, mode)
    print(f"\n{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
