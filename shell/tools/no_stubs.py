#!/usr/bin/env python3
"""no_stubs.py -- AST-based stub detector for the agency Chromium fork.

This is the Floor Lock / Decision #11 enforcer. It refuses to let stub
functions land in the tree. It is *structural*, never regex-over-source for
the live decision: C++ is parsed with libclang (the real Clang AST) and
Python with the stdlib `ast` module.

Why AST and not regex: a regex sees `return nullptr;` and screams. The AST
knows whether that statement is the *sole* body of a non-trivial function
(a stub) or one branch of a legitimate early-return guard (fine). That
distinction is the whole point of this gate.

Detected stub patterns (C++ and, where meaningful, Python):
  * empty / near-empty bodies (only `pass` / `...` / a docstring / nothing)
  * `// TODO` / `# TODO` / `/* TODO */` lexically inside a function body
  * `return {};` as the SOLE statement of a non-trivial function
  * `return nullptr;` as the SOLE statement of a non-trivial function
  * `NOTIMPLEMENTED()` / `NOTREACHED()` placeholder calls
  * placeholder `return mojo::Status::Ok();` as a sole body
  * Python: `raise NotImplementedError`, bare `return`/`return None` sole body

CLI:
  no_stubs.py <path...>     scan files/dirs; exit 0 clean, nonzero if stubs
  no_stubs.py --self-test   run the bundled adversarial fixtures

AST backend is auto-selected at runtime and printed with --self-test.
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
PLACEHOLDER_CALLS = ("NOTIMPLEMENTED", "NOTREACHED", "IMMEDIATE_CRASH")


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

        # --- TODO/FIXME comments lexically inside the body ---
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

            # TODO/FIXME comments inside the function's source span
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
# Driver
# --------------------------------------------------------------------------
def scan_file(path: str) -> list[Finding]:
    ext = os.path.splitext(path)[1].lower()
    if ext in PY_EXT:
        return _scan_python(path)
    if ext in C_EXT:
        if cpp_backend_name() is None:
            return [Finding(path, 0,
                "no C++ AST backend available (need libclang)")]
        return _scan_cpp(path)
    return []


def iter_targets(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _dirs, files in os.walk(p):
                for f in sorted(files):
                    ext = os.path.splitext(f)[1].lower()
                    if ext in C_EXT or ext in PY_EXT:
                        yield os.path.join(root, f)
        else:
            yield p


def run_scan(paths) -> int:
    findings: list[Finding] = []
    for target in iter_targets(paths):
        findings.extend(scan_file(target))
    if findings:
        for f in sorted(findings, key=lambda x: (x.file, x.line)):
            print(f"STUB  {f}", file=sys.stderr)
        print(f"\n{len(findings)} stub finding(s).", file=sys.stderr)
        return 1
    return 0


def self_test() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    fix = os.path.join(here, "no_stubs_fixtures")
    backend = cpp_backend_name() or "NONE"
    print(f"AST backend (C++): {backend}")
    print(f"AST backend (Python): stdlib ast")

    good = [os.path.join(fix, "good.cc"), os.path.join(fix, "good.py")]
    bad = [os.path.join(fix, "bad.cc"), os.path.join(fix, "bad.py")]

    ok = True
    print("\n== GOOD fixtures (expect clean) ==")
    rc_good = run_scan(good)
    if rc_good != 0:
        print("FAIL: good fixtures produced findings", file=sys.stderr)
        ok = False
    else:
        print("PASS: good fixtures clean")

    print("\n== BAD fixtures (expect findings) ==")
    rc_bad = run_scan(bad)
    if rc_bad == 0:
        print("FAIL: bad fixtures were NOT flagged", file=sys.stderr)
        ok = False
    else:
        print("PASS: bad fixtures flagged")

    return 0 if ok else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("paths", nargs="*", help="files or dirs to scan")
    ap.add_argument("--self-test", action="store_true",
                    help="run bundled adversarial fixtures")
    ap.add_argument("--backend", action="store_true",
                    help="print selected C++ AST backend and exit")
    ns = ap.parse_args(argv)

    if ns.backend:
        print(cpp_backend_name() or "NONE")
        return 0
    if ns.self_test:
        return self_test()
    if not ns.paths:
        ap.error("no paths given (use --self-test for fixtures)")
    return run_scan(ns.paths)


if __name__ == "__main__":
    sys.exit(main())
