#!/usr/bin/env python3
"""no_stubs.py -- stub detector for views-shell's committed C++ and Python.

Lifted from the agency lineage (shell/PROVENANCE.md). It refuses to let stub
functions land in the tree. Python is parsed with the stdlib `ast` module. C++
has two back ends:

  libclang  the real Clang AST. Structural: it knows whether `return nullptr;`
            is the sole body of a function (a stub) or one branch of an
            early-return guard (fine). It only judges bodies correctly when the
            file parses, and outside a Chromium build (no include paths, no
            compile database) a Views file does not: unknown types drop bodies,
            so ordinary getters read as empty, and headers fail to load.
  tokens    a C++ lexer (comments, string, raw-string and character literals
            removed exactly). It needs nothing installed and judges only what is
            lexical: marker comments and placeholder calls, anywhere in the file.

`--cpp auto` (the default) picks libclang when the bindings load and tokens
otherwise; tools/validate.py passes `--cpp tokens`, so the repository gate does
not depend on what the checking host has installed.

Detected stub patterns:
  * C++ and Python: a `TODO` / `FIXME` / `XXX:` marker in a comment (libclang
    and Python: inside a function body; tokens: anywhere in the file)
  * C++: `NOTIMPLEMENTED()` / `IMMEDIATE_CRASH()` placeholder calls
  * C++ (libclang only): an empty body that is not a constructor or destructor;
    `return {};`, `return nullptr;`, `return mojo::Status::Ok();` or
    `return absl::OkStatus();` as the sole statement
  * Python: a body that is only `pass`, `...`, a docstring,
    `raise NotImplementedError` or `return None`

`NOTREACHED()` is not a placeholder: since M128 it is a fatal invariant check,
and ported Chromium code uses it for impossible switch arms.

ALLOWLIST below names marker lines that are deliberately kept: attributed
upstream comments carried by code ported from Chromium. An entry is a path
suffix and a fragment of the line; it is matched by text, never by line number,
so it survives edits around it. A new entry needs the same justification.

CLI:
  no_stubs.py [--cpp auto|libclang|tokens] [--exclude DIR]... <path...>
                            scan files/dirs; exit 0 clean, 1 if stubs. With
                            --summary, print "no_stubs ok (N files)" on success.
  no_stubs.py --self-test   run the bundled adversarial fixtures through every
                            available C++ back end
  no_stubs.py --backend     print the back end --cpp auto would pick
"""
from __future__ import annotations

import argparse
import ast
import os
import sys
from dataclasses import dataclass

C_EXT = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh", ".hxx", ".mm"}
PY_EXT = {".py"}

# Tokens that, appearing anywhere inside a function body, are stub markers.
TODO_MARKERS = ("TODO", "FIXME", "XXX:")
PLACEHOLDER_CALLS = ("NOTIMPLEMENTED", "IMMEDIATE_CRASH")

# Marker lines kept on purpose: (path suffix, fragment of the line). Each is an
# attributed upstream comment in code ported from Chromium 154.0.8037.92
# (CHROME-PORT-LEDGER.md), kept verbatim so the port diffs cleanly.
ALLOWLIST = (
    ("shell/tabs/tab.cc", "TODO(collinbaker): investigate why"),
    ("shell/tabs/tab_close_button.cc",
     "TODO(http://crbug.com/40120351): Make ink drops in RTL"),
    ("shell/tabs/tab_close_button.cc",
     "TODO(http://crbug.com/40120351): Once this bug is solved"),
    ("shell/tabs/tab_close_button.cc",
     "TODO(pkasting): It seems like touch events would generate"),
    ("shell/tabs/tab_style.h",
     "TODO(tbergquist): Non-Tab callers of this should probably"),
)


@dataclass
class Finding:
    file: str
    line: int
    reason: str

    def __str__(self) -> str:
        return f"{self.file}:{self.line}: {self.reason}"


# --------------------------------------------------------------------------
# C++ backend (libclang -- the real Clang AST)
# --------------------------------------------------------------------------
def _import_libclang():
    try:
        import clang.cindex as cidx  # type: ignore
    except Exception:
        return None
    try:
        cidx.Index.create()
    except Exception:
        # Bindings present but native libclang.so missing.
        return None
    return cidx


def cpp_backend_name() -> str | None:
    return "libclang" if _import_libclang() is not None else None


def _body_compound(cidx, fn):
    """Return the CompoundStmt cursor that is the function body, or None."""
    for ch in fn.get_children():
        if ch.kind == cidx.CursorKind.COMPOUND_STMT:
            return ch
    return None


def _comment_tokens(cidx, cursor):
    """Yield comment-token spellings within a cursor's extent."""
    for tok in cursor.get_tokens():
        if tok.kind == cidx.TokenKind.COMMENT:
            yield tok.spelling, tok.location.line


def _stmt_kinds(stmts):
    return [s.kind.name for s in stmts]


def _scan_cpp(path: str) -> list[Finding]:
    cidx = _import_libclang()
    assert cidx is not None
    index = cidx.Index.create()
    args = ["-std=c++20", "-fsyntax-only", "-ferror-limit=0"]
    try:
        tu = index.parse(
            path,
            args=args,
            options=cidx.TranslationUnit.PARSE_INCOMPLETE
            | cidx.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES * 0,
        )
    except cidx.TranslationUnitLoadError as exc:  # pragma: no cover
        return [Finding(path, 0, f"libclang failed to parse: {exc}")]

    findings: list[Finding] = []
    seen: set[tuple[int, str]] = set()

    def add(line: int, reason: str):
        key = (line, reason)
        if key not in seen:
            seen.add(key)
            findings.append(Finding(path, line, reason))

    def fn_in_file(fn) -> bool:
        loc = fn.location
        return bool(loc.file) and os.path.samefile(loc.file.name, path)

    FUNC_KINDS = {
        cidx.CursorKind.FUNCTION_DECL,
        cidx.CursorKind.CXX_METHOD,
        cidx.CursorKind.CONSTRUCTOR,
        cidx.CursorKind.DESTRUCTOR,
        cidx.CursorKind.FUNCTION_TEMPLATE,
        cidx.CursorKind.CONVERSION_FUNCTION,
    }

    def check_fn(fn):
        if not fn.is_definition():
            return
        try:
            if not fn_in_file(fn):
                return
        except OSError:
            return
        body = _body_compound(cidx, fn)
        if body is None:
            return
        line = fn.location.line
        name = fn.spelling

        stmts = list(body.get_children())

        # --- marker comments lexically inside the body ---
        for spelling, cline in _comment_tokens(cidx, body):
            up = spelling.upper()
            if any(m in up for m in TODO_MARKERS):
                add(cline, f"TODO/FIXME marker inside body of '{name}'")

        # --- placeholder calls anywhere in the body ---
        body_tokens = [t.spelling for t in body.get_tokens()]
        for call in PLACEHOLDER_CALLS:
            if call in body_tokens:
                add(line, f"placeholder call {call}() in '{name}'")

        # --- empty / near-empty body ---
        # Constructors with member-init lists doing real work have children
        # before the compound stmt; we only judge the compound body here.
        if not stmts:
            # genuinely empty {}. Empty dtors/ctors are common & legitimate,
            # but for the floor lock we only exempt *special members*.
            if fn.kind in (
                cidx.CursorKind.CONSTRUCTOR,
                cidx.CursorKind.DESTRUCTOR,
            ):
                return
            add(line, f"empty function body for '{name}'")
            return

        # --- sole-statement stub patterns (non-trivial function) ---
        if len(stmts) == 1 and stmts[0].kind == cidx.CursorKind.RETURN_STMT:
            ret = stmts[0]
            toks = [t.spelling for t in ret.get_tokens()]
            joined = " ".join(toks)
            compact = joined.replace(" ", "")
            # return {};
            if compact in ("return{}", "return{};"):
                add(ret.location.line,
                    f"sole body 'return {{}};' stub in '{name}'")
                return
            # return nullptr; / return NULL; / return 0; (pointer-ish stubs)
            if compact in ("returnnullptr", "returnnullptr;",
                           "returnNULL", "returnNULL;"):
                add(ret.location.line,
                    f"sole body 'return nullptr;' stub in '{name}'")
                return
            # placeholder mojo status
            if "mojo::Status::Ok" in joined.replace(" ", "") or \
               "mojo::Status::Ok" in compact:
                add(ret.location.line,
                    f"placeholder 'return mojo::Status::Ok();' in '{name}'")
                return
            if compact.replace(";", "") in ("returnabsl::OkStatus()",):
                add(ret.location.line,
                    f"placeholder 'return absl::OkStatus();' sole body in '{name}'")
                return

    def walk(node):
        if node.kind in FUNC_KINDS:
            check_fn(node)
        for ch in node.get_children():
            walk(ch)

    walk(tu.cursor)
    return findings


# --------------------------------------------------------------------------
# Python backend (stdlib ast)
# --------------------------------------------------------------------------
def _scan_python(path: str) -> list[Finding]:
    with open(path, "r", encoding="utf-8") as fh:
        source = fh.read()
    try:
        tree = ast.parse(source, filename=path)
    except SyntaxError as exc:
        return [Finding(path, exc.lineno or 0, f"python syntax error: {exc.msg}")]

    src_lines = source.splitlines()
    findings: list[Finding] = []

    def is_docstring(node) -> bool:
        return (
            isinstance(node, ast.Expr)
            and isinstance(node.value, ast.Constant)
            and isinstance(node.value.value, str)
        )

    def strip_docstring(body):
        if body and is_docstring(body[0]):
            return body[1:]
        return body

    class Visitor(ast.NodeVisitor):
        def _check(self, node):
            name = node.name
            line = node.lineno
            real = strip_docstring(node.body)

            # empty / near-empty: only pass / ... left
            if not real:
                findings.append(Finding(path, line,
                    f"empty body (docstring only) in '{name}'"))
            elif len(real) == 1:
                only = real[0]
                if isinstance(only, ast.Pass):
                    findings.append(Finding(path, only.lineno,
                        f"stub body 'pass' in '{name}'"))
                elif (isinstance(only, ast.Expr)
                      and isinstance(only.value, ast.Constant)
                      and only.value.value is Ellipsis):
                    findings.append(Finding(path, only.lineno,
                        f"stub body '...' in '{name}'"))
                elif (isinstance(only, ast.Raise)
                      and self._is_notimpl(only)):
                    findings.append(Finding(path, only.lineno,
                        f"stub 'raise NotImplementedError' in '{name}'"))
                elif isinstance(only, ast.Return) and (
                    only.value is None
                    or (isinstance(only.value, ast.Constant)
                        and only.value.value is None)
                ):
                    findings.append(Finding(path, only.lineno,
                        f"stub sole body 'return None' in '{name}'"))

            # marker comments inside the function's source span
            self._todo_scan(node, name)
            self.generic_visit(node)

        @staticmethod
        def _is_notimpl(raise_node) -> bool:
            exc = raise_node.exc
            if isinstance(exc, ast.Call):
                exc = exc.func
            if isinstance(exc, ast.Name):
                return exc.id == "NotImplementedError"
            if isinstance(exc, ast.Attribute):
                return exc.attr == "NotImplementedError"
            return False

        def _todo_scan(self, node, name):
            start = node.lineno
            end = getattr(node, "end_lineno", node.lineno)
            for i in range(start, min(end, len(src_lines)) + 1):
                raw = src_lines[i - 1]
                hidx = raw.find("#")
                if hidx == -1:
                    continue
                comment = raw[hidx:].upper()
                if any(m in comment for m in TODO_MARKERS):
                    findings.append(Finding(path, i,
                        f"TODO/FIXME marker inside body of '{name}'"))

        def visit_FunctionDef(self, node):
            self._check(node)

        def visit_AsyncFunctionDef(self, node):
            self._check(node)

    Visitor().visit(tree)
    return findings


# --------------------------------------------------------------------------
# C++ token backend (a lexer; no include paths, no compile database)
# --------------------------------------------------------------------------
def _cpp_lex(source: str):
    """Splits C++ source into (kind, text, line) with kind "comment" or "code".
    String, raw-string and character literals are dropped whole, so a marker
    inside a literal is never seen; digit separators (1'000) are kept as code."""
    out = []
    i, n, line = 0, len(source), 1
    code_start, code_line = 0, 1

    def flush(end):
        if end > code_start:
            out.append(("code", source[code_start:end], code_line))

    while i < n:
        c = source[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if source.startswith("//", i):
            flush(i)
            j = source.find("\n", i)
            j = n if j == -1 else j
            # A backslash-newline continues a line comment.
            while j < n and source[j - 1] == "\\":
                k = source.find("\n", j + 1)
                j = n if k == -1 else k
            out.append(("comment", source[i:j], line))
            line += source.count("\n", i, j)
            i = j
            code_start, code_line = i, line
            continue
        if source.startswith("/*", i):
            flush(i)
            j = source.find("*/", i + 2)
            j = n if j == -1 else j + 2
            out.append(("comment", source[i:j], line))
            line += source.count("\n", i, j)
            i = j
            code_start, code_line = i, line
            continue
        if c == "R" and source.startswith('R"', i) and (
                i == 0 or not (source[i - 1].isalnum() or source[i - 1] == "_")
                or source[i - 1] in "uU8L"):
            k = source.find("(", i + 2)
            if k != -1:
                delim = source[i + 2:k]
                j = source.find(")" + delim + '"', k)
                j = n if j == -1 else j + len(delim) + 2
                flush(i)
                line += source.count("\n", i, j)
                i = j
                code_start, code_line = i, line
                continue
        if c == '"' or (c == "'" and not (i > 0 and source[i - 1].isalnum()
                                          and i + 1 < n and source[i + 1].isalnum())):
            flush(i)
            j = i + 1
            while j < n and source[j] != c and source[j] != "\n":
                j += 2 if source[j] == "\\" else 1
            j = min(j + 1, n)
            line += source.count("\n", i, j)
            i = j
            code_start, code_line = i, line
            continue
        i += 1
    flush(n)
    return out


def _scan_cpp_tokens(path: str) -> list[Finding]:
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        source = fh.read()
    findings: list[Finding] = []
    for kind, text, line in _cpp_lex(source):
        if kind == "comment":
            for off, row in enumerate(text.split("\n")):
                if any(m in row.upper() for m in TODO_MARKERS):
                    findings.append(Finding(path, line + off,
                        "TODO/FIXME marker in a comment"))
            continue
        for call in PLACEHOLDER_CALLS:
            start = 0
            while True:
                k = text.find(call, start)
                if k == -1:
                    break
                start = k + len(call)
                before = text[k - 1] if k else " "
                after = text[start:].lstrip(" \t")
                if (before.isalnum() or before == "_") or not after.startswith("("):
                    continue
                if start < len(text) and (text[start].isalnum() or text[start] == "_"):
                    continue
                findings.append(Finding(path, line + text.count("\n", 0, k),
                    f"placeholder call {call}()"))
    return findings


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------
def _allowed(finding: Finding) -> bool:
    if finding.line <= 0:
        return False
    norm = finding.file.replace(os.sep, "/")
    try:
        with open(finding.file, "r", encoding="utf-8", errors="replace") as fh:
            rows = fh.read().split("\n")
    except OSError:
        return False
    if finding.line > len(rows):
        return False
    row = rows[finding.line - 1]
    return any(norm.endswith(suffix) and fragment in row
               for suffix, fragment in ALLOWLIST)


def scan_file(path: str, cpp: str = "auto") -> list[Finding]:
    ext = os.path.splitext(path)[1].lower()
    if ext in PY_EXT:
        found = _scan_python(path)
    elif ext in C_EXT:
        if cpp == "auto":
            cpp = "libclang" if cpp_backend_name() else "tokens"
        if cpp == "tokens":
            found = _scan_cpp_tokens(path)
        elif cpp_backend_name() is None:
            found = [Finding(path, 0,
                "no C++ AST backend available (need libclang)")]
        else:
            found = _scan_cpp(path)
    else:
        found = []
    return [f for f in found if not _allowed(f)]


def iter_targets(paths, exclude=()):
    ex = [os.path.normpath(e) for e in exclude]

    def excluded(p):
        p = os.path.normpath(p)
        return any(p == e or p.startswith(e + os.sep) for e in ex)

    for p in paths:
        if os.path.isdir(p):
            for root, dirs, files in os.walk(p):
                dirs[:] = sorted(d for d in dirs
                                 if not excluded(os.path.join(root, d)))
                for f in sorted(files):
                    ext = os.path.splitext(f)[1].lower()
                    full = os.path.join(root, f)
                    if (ext in C_EXT or ext in PY_EXT) and not excluded(full):
                        yield full
        elif not excluded(p):
            yield p


def run_scan(paths, cpp: str = "auto", exclude=(), summary=False) -> int:
    findings: list[Finding] = []
    count = 0
    for target in iter_targets(paths, exclude):
        count += 1
        findings.extend(scan_file(target, cpp))
    if findings:
        for f in sorted(findings, key=lambda x: (x.file, x.line)):
            print(f"STUB  {f}", file=sys.stderr)
        print(f"\n{len(findings)} stub finding(s). A kept upstream comment "
              "goes into ALLOWLIST in shell/tools/no_stubs.py.", file=sys.stderr)
        return 1
    if summary:
        print(f"no_stubs ok ({count} files)")
    return 0


def self_test() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    fix = os.path.join(here, "no_stubs_fixtures")
    backends = ["tokens"] + (["libclang"] if cpp_backend_name() else [])
    print(f"C++ back ends: {', '.join(backends)}")
    print("Python back end: stdlib ast")

    good = [os.path.join(fix, "good.cc"), os.path.join(fix, "good.py")]
    bad_cc = os.path.join(fix, "bad.cc")
    bad_py = os.path.join(fix, "bad.py")

    ok = True
    for cpp in backends:
        print(f"\n== GOOD fixtures, C++ via {cpp} (expect clean) ==")
        if run_scan(good, cpp) != 0:
            print("FAIL: good fixtures produced findings", file=sys.stderr)
            ok = False
        else:
            print("PASS: good fixtures clean")
        print(f"\n== BAD fixtures, C++ via {cpp} (expect findings) ==")
        # Each bad file on its own, so one flagged file cannot hide the other.
        for bad in (bad_cc, bad_py):
            if run_scan([bad], cpp) == 0:
                print(f"FAIL: {os.path.basename(bad)} was NOT flagged",
                      file=sys.stderr)
                ok = False
            else:
                print(f"PASS: {os.path.basename(bad)} flagged")
    return 0 if ok else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("paths", nargs="*", help="files or dirs to scan")
    ap.add_argument("--self-test", action="store_true",
                    help="run bundled adversarial fixtures")
    ap.add_argument("--backend", action="store_true",
                    help="print the C++ back end --cpp auto picks and exit")
    ap.add_argument("--cpp", choices=("auto", "libclang", "tokens"),
                    default="auto", help="C++ back end (default auto)")
    ap.add_argument("--exclude", action="append", default=[],
                    help="a file or directory left out of the scan (repeatable)")
    ap.add_argument("--summary", action="store_true",
                    help='print "no_stubs ok (N files)" when clean')
    ns = ap.parse_args(argv)

    if ns.backend:
        print(cpp_backend_name() or "tokens")
        return 0
    if ns.self_test:
        return self_test()
    if not ns.paths:
        ap.error("no paths given (use --self-test for fixtures)")
    return run_scan(ns.paths, ns.cpp, ns.exclude, ns.summary)


if __name__ == "__main__":
    sys.exit(main())
