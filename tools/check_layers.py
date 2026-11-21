#!/usr/bin/env python3
"""Check a ports-and-adapters subsystem in any common language: inward dependencies, one type per file.

Usage:
    python3 check_layers.py <subsystem-dir> [--name NAME] [--inner model core ports]
                            [--outer adapters service] [--lang auto|python|ts|java|...]

Rules (exit 1 on any violation):
  1. Files in the inner layers (default: files named model.* / core.*, and everything under
     ports/) must not import the outer layers (default: adapters/ and service.*). Relative
     imports are resolved against the file's location. Absolute imports are matched when they
     contain the subsystem's name followed by an outer layer's name (`app.billing.adapters`,
     `github.com/acme/app/internal/billing/adapters`, `Acme.Billing.Adapters`,
     `crate::billing::adapters`), case-insensitively.
  2. Every file under ports/ and adapters/ (except index files and tests) declares exactly one
     top-level type: class, interface, protocol, trait, struct, record or object.

Languages, by file extension: Python, TypeScript/JavaScript, Java, Kotlin, C#, Go, Rust, C/C++.
Python files are parsed with `ast`. The others go through a small lexer that drops comments and
strings and tracks braces, so it is a heuristic: it is good for a refactor's checks, while the
repo's CI should enforce the same rule with the language's native tool (see
references/layouts.md). Needs Python 3.8+ and the standard library only.
"""
from __future__ import annotations

import argparse
import ast
import os
import re
import sys
from pathlib import Path, PurePosixPath

LANGS = {
    ".py": "python",
    ".ts": "ts", ".tsx": "ts", ".mts": "ts", ".cts": "ts", ".js": "ts", ".jsx": "ts", ".mjs": "ts", ".cjs": "ts",
    ".java": "java", ".kt": "kotlin", ".kts": "kotlin",
    ".cs": "csharp",
    ".go": "go",
    ".rs": "rust",
    ".h": "cpp", ".hh": "cpp", ".hpp": "cpp", ".hxx": "cpp", ".c": "cpp", ".cc": "cpp", ".cpp": "cpp", ".cxx": "cpp",
}
INDEX_FILES = {"__init__.py", "index.ts", "index.tsx", "index.js", "index.mjs", "mod.rs", "lib.rs",
               "package-info.java", "doc.go"}
TEST_FILE = re.compile(r"(^test_.*\.py$|_test\.(py|go)$|\.(test|spec)\.[cm]?[jt]sx?$|Tests?\.(java|kt|cs)$)")
SKIP_DIRS = {".git", "node_modules", "target", "build", "dist", "bin", "obj", "__pycache__", ".venv", "venv"}


# ----------------------------------------------------------------- lexing
def strip_comments_and_strings(text: str, lang: str, keep_strings: bool = False) -> str:
    """Blank out comments and, unless `keep_strings`, string/char literal *contents* (keeping
    quotes and newlines), so keywords inside them are ignored. Imports are read with
    `keep_strings=True`: their specifiers are strings."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
            continue
        if lang == "python" and c == "#":
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c in "\"'`" and not (lang == "rust" and c == "'"):   # Rust ' is also a lifetime
            q = c
            if lang in ("python", "kotlin", "csharp") and text.startswith(q * 3, i):
                q = q * 3
            j = i + len(q)
            raw = lang == "go" and q == "`"                      # Go raw strings have no escapes
            while j < n and not text.startswith(q, j):
                j += 2 if text[j] == "\\" and not raw else 1
            j = min(n, j + len(q))
            out.append(text[i:j] if keep_strings else q + "\n" * text.count("\n", i, j) + q)
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


# ----------------------------------------------------------------- imports
IMPORT_PATTERNS = {
    "ts": [r"""^\s*import\s+(?:type\s+)?(?:[\w*{}\s,$]+\s+from\s+)?["']([^"']+)["']""",
           r"""^\s*export\s+(?:type\s+)?[\w*{}\s,$]*\s+from\s+["']([^"']+)["']""",
           r"""\brequire\(\s*["']([^"']+)["']\s*\)""",
           r"""\bimport\(\s*["']([^"']+)["']\s*\)"""],
    "java": [r"^\s*import\s+(?:static\s+)?([\w.]+)(?:\.\*)?\s*;"],
    "kotlin": [r"^\s*import\s+([\w.`]+)"],
    "csharp": [r"^\s*(?:global\s+)?using\s+(?:static\s+)?(?:\w+\s*=\s*)?([\w.]+)\s*;"],
    "go": [r"""^\s*import\s+(?:\w+\s+|\.\s+|_\s+)?"([^"]+)\"""",
           r"""^\s*(?:\w+\s+|\.\s+|_\s+)?"([^"]+)"\s*$"""],          # lines inside import ( ... )
    "rust": [r"^\s*(?:pub(?:\([\w:]+\))?\s+)?use\s+([\w:]+)", r"^\s*(?:pub\s+)?mod\s+(\w+)\s*;"],
    "cpp": [r'^\s*#\s*include\s+"([^"]+)"'],
}


def python_imports(text: str, module_parts: list, is_pkg_init: bool) -> list:
    """(kind, target) pairs: kind is 'abs' (dotted name) or 'rel' (path relative to root)."""
    found = []
    for node in ast.walk(ast.parse(text)):
        if isinstance(node, ast.Import):
            found += [("abs", a.name) for a in node.names]
        elif isinstance(node, ast.ImportFrom):
            if node.level:
                base = list(module_parts) if is_pkg_init else list(module_parts[:-1])
                base = base[: len(base) - (node.level - 1)] if node.level > 1 else base
                mod = base + (node.module.split(".") if node.module else [])
                found.append(("rel", "/".join(mod)))
                found += [("rel", "/".join(mod + [a.name])) for a in node.names]
            elif node.module:
                found.append(("abs", node.module))
                found += [("abs", f"{node.module}.{a.name}") for a in node.names]
    return found


def other_imports(text: str, lang: str, rel_file: PurePosixPath) -> list:
    text = strip_comments_and_strings(text, lang, keep_strings=True)
    found = []
    in_go_block = False
    for line in text.splitlines():
        if lang == "go":
            if re.match(r"^\s*import\s*\(", line):
                in_go_block = True
                continue
            if in_go_block and line.strip() == ")":
                in_go_block = False
                continue
            m = re.match(IMPORT_PATTERNS["go"][1 if in_go_block else 0], line)
            if m:
                found.append(("abs", m.group(1)))
            continue
        for pat in IMPORT_PATTERNS.get(lang, []):
            for m in re.finditer(pat, line):
                spec = m.group(1).replace("`", "")
                if lang == "ts" and spec.startswith("."):
                    found.append(("rel", os.path.normpath(str(rel_file.parent / spec)).replace(os.sep, "/")))
                elif lang == "cpp":
                    found.append(("rel", os.path.normpath(str(rel_file.parent / spec)).replace(os.sep, "/")))
                    found.append(("rel", os.path.normpath(spec).replace(os.sep, "/")))
                    # A project-rooted include ("src/billing/adapters/x.h") names the
                    # subsystem before the layer, like an absolute import.
                    found.append(("abs", os.path.normpath(spec).replace(os.sep, "/")))
                elif lang == "rust" and pat.startswith(r"^\s*(?:pub\s+)?mod"):
                    found.append(("rel", str(rel_file.parent / spec)))
                elif lang == "rust" and spec.split("::")[0] in ("super", "self"):
                    parts = list(rel_file.with_suffix("").parts)
                    if parts and parts[-1] in ("mod", "lib"):
                        parts = parts[:-1]
                    segs = spec.split("::")
                    while segs and segs[0] in ("super", "self"):
                        if segs.pop(0) == "super" and parts:
                            parts = parts[:-1]
                    found.append(("rel", "/".join(parts + segs)))
                else:
                    found.append(("abs", spec))
    return found


def _segments(target: str) -> list:
    return [s.lower() for s in re.split(r"[./:\\]+", target) if s and s not in (".", "..")]


def is_outer(kind: str, target: str, name: str, outer: tuple) -> bool:
    segs = _segments(target)
    outer_l = [o.lower() for o in outer]
    if kind == "rel":
        if target.startswith(".."):            # escapes the subsystem: not one of its layers
            return False
        return bool(segs) and (segs[0] in outer_l or os.path.splitext(segs[0])[0] in outer_l)
    name_l = name.lower()
    for i in range(len(segs) - 1):
        if segs[i] == name_l and (segs[i + 1] in outer_l or os.path.splitext(segs[i + 1])[0] in outer_l):
            return True
    return False


# ----------------------------------------------------------------- top-level types
TYPE_WORDS = r"(class|interface|struct|record|object|trait|protocol)"


def python_types(text: str) -> list:
    return [n.name for n in ast.parse(text).body if isinstance(n, ast.ClassDef)]


def brace_types(clean: str, lang: str) -> list:
    """Type declarations whose enclosing blocks are only namespaces / packages."""
    names = []
    stack = []                     # 'ns' or 'other' per open brace
    pending_ns = False
    tokens = re.finditer(r"[A-Za-z_][\w]*|[{};]", clean)
    prev_words = []
    for m in tokens:
        tok = m.group(0)
        if tok == "{":
            stack.append("ns" if pending_ns else "other")
            pending_ns = False
            prev_words = []
            continue
        if tok == "}":
            if stack:
                stack.pop()
            prev_words = []
            continue
        if tok == ";":
            pending_ns = False
            prev_words = []
            continue
        if tok in ("namespace", "module") and lang in ("csharp", "cpp", "ts"):
            pending_ns = True
        elif re.fullmatch(TYPE_WORDS, tok) and all(k == "ns" for k in stack):
            if prev_words and prev_words[-1] in ("enum",):            # C++ `enum class`
                prev_words.append(tok)
                continue
            if lang == "kotlin" and tok == "object" and prev_words and prev_words[-1] == "companion":
                prev_words.append(tok)
                continue
            rest = clean[m.end():]
            nm = re.match(r"\s+([A-Za-z_]\w*)", rest)
            if nm:
                after = rest[nm.end():].lstrip()
                if not after.startswith(";"):                          # skip forward declarations
                    if lang != "ts" or tok in ("class", "interface"):
                        names.append(nm.group(1))
        prev_words.append(tok)
    return names


def go_types(clean: str) -> list:
    names = re.findall(r"^type\s+([A-Za-z_]\w*)\s+(?:struct|interface)\b", clean, re.M)
    for block in re.findall(r"^type\s*\((.*?)^\)", clean, re.M | re.S):
        names += re.findall(r"^\s*([A-Za-z_]\w*)\s+(?:struct|interface)\b", block, re.M)
    return names


def rust_types(clean: str) -> list:
    return re.findall(r"^(?:pub(?:\([\w:]+\))?\s+)?(?:struct|trait|enum)\s+([A-Za-z_]\w*)", clean, re.M)


def top_level_types(text: str, lang: str) -> list:
    if lang == "python":
        return python_types(text)
    clean = strip_comments_and_strings(text, lang)
    if lang == "go":
        return go_types(clean)
    if lang == "rust":
        return rust_types(clean)
    return brace_types(clean, lang)


# ----------------------------------------------------------------- driver
def _walk(root: Path):
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("."))
        for f in sorted(files):
            yield Path(dirpath) / f


def _layer(rel: PurePosixPath, inner: tuple, outer: tuple) -> str:
    first = rel.parts[0]
    stem = Path(first).stem if len(rel.parts) == 1 else first
    if stem in inner:
        return "inner"
    if stem in outer:
        return "outer"
    return "other"


def violations(root, name=None, inner=("model", "core", "ports"), outer=("adapters", "service"),
               single_type_dirs=("ports", "adapters"), lang="auto") -> list:
    root = Path(root).resolve()
    name = name or root.name
    problems = []
    for path in _walk(root):
        file_lang = lang if lang != "auto" else LANGS.get(path.suffix.lower())
        if not file_lang:
            continue
        rel = PurePosixPath(path.relative_to(root).as_posix())
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        try:
            if _layer(rel, inner, outer) == "inner":
                if file_lang == "python":
                    parts = list(rel.with_suffix("").parts)
                    is_init = parts[-1] == "__init__"
                    if is_init:
                        parts = parts[:-1]
                    imports = python_imports(text, parts, is_init)
                else:
                    imports = other_imports(text, file_lang, rel)
                for kind, target in imports:
                    if is_outer(kind, target, name, outer):
                        problems.append(f"{rel}: imports {target} (inner layers must not depend on "
                                        f"{', '.join(outer)})")
            if (rel.parts[0] in single_type_dirs and len(rel.parts) > 1 and path.name not in INDEX_FILES
                    and not TEST_FILE.search(path.name) and not path.name.endswith((".c", ".cc", ".cpp", ".cxx"))):
                types = top_level_types(text, file_lang)
                if len(types) != 1:
                    kind = rel.parts[0][:-1] if rel.parts[0].endswith("s") else rel.parts[0]
                    problems.append(f"{rel}: {len(types)} top-level types {types} (expected exactly one "
                                    f"per {kind} file)")
        except SyntaxError as e:
            problems.append(f"{rel}: could not parse ({e.msg})")
    return sorted(set(problems))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("root", type=Path, help="the subsystem's directory")
    ap.add_argument("--name", help="the subsystem's name as it appears in absolute imports "
                                   "(default: the directory name)")
    ap.add_argument("--inner", nargs="+", default=["model", "core", "ports"],
                    help="inner layers: file stems or top-level directories")
    ap.add_argument("--outer", nargs="+", default=["adapters", "service"],
                    help="outer layers: file stems or top-level directories")
    ap.add_argument("--lang", default="auto", choices=["auto", *sorted(set(LANGS.values()))])
    a = ap.parse_args(argv)
    if not a.root.is_dir():
        print(f"error: {a.root} is not a directory", file=sys.stderr)
        return 2
    problems = violations(a.root, a.name, tuple(a.inner), tuple(a.outer), lang=a.lang)
    for p in problems:
        print(p)
    print(f"{'FAIL' if problems else 'OK'}: {a.name or a.root.resolve().name} ({len(problems)} problem(s))")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
