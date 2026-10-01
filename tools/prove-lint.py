#!/usr/bin/env python3
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Lint PROVE.md, and the one parser of its table that tools/prove.sh also uses.

Usage:
  python3 tools/prove-lint.py [PROVE.md] [--repo DIR]

PROVE.md is one Markdown table, append-only, columns
  id | claim | task | form | command | rc | result | evidence | commit | at
Cells are split on unescaped `|`; a literal pipe inside a cell is written `\\|`.
Every line that starts with `|`, other than the header and the `|---|` rule, is a
row, and every row must satisfy:

  - exactly ten cells;
  - id matches P<n>.<m>; claim matches C<n>.<m> with the same <n>.<m>;
  - task is T<n>, w<n><a-e>, or one word ([A-Za-z][A-Za-z0-9-]*);
  - form is `local` or `bench`;
  - rc is an integer; result is `pass` or `fail`, and rc 0 <=> pass;
  - commit is 7-40 hex digits naming a commit in the repository (git cat-file -e);
  - at is an ISO-8601 UTC timestamp (YYYY-MM-DDTHH:MM[:SS[.f]]Z);
  - no cell contains /home/<user>;
  - the command cell is runnable (see parse_command): one backticked span, or one
    span followed by prose that adds no second command (a parenthetical may quote
    more spans). Inside a span `\\|` is a pipe and `\\`` a backtick; every other
    backslash is part of the shell text.

LEGACY lists rows written before this lint existed that break a rule which an
append-only file cannot repair; for them the named rule is reported as info, all
other rules still apply. Do not add to it: new rows must pass.

Information (never an error): rows whose claim SPEC.md does not name yet
(chapter-2 claims live in PR bodies until the synthesiser lifts them), and rows
recorded as `local` whose command contacts the bench (tools/prove.sh runs those
as bench rows).

Prints `prove-lint ok (N rows)` and exits 0, or names each offending row
(`prove-lint: line L <id>: <problem>`) and exits 1.
"""
import datetime
import pathlib
import re
import subprocess
import sys

COLUMNS = ["id", "claim", "task", "form", "command", "rc", "result",
           "evidence", "commit", "at"]

# Rows from before the lint, keyed by id, with the rules each may break.
#   grammar: id/claim/task grammar (the tabstrip side session's PTS.* rows)
#   manual:  the command cell is prose or several commands, run by hand
LEGACY = {
    **{f"PTS.{i}": {"grammar"} for i in range(1, 7)},
    "PTS.7": {"grammar", "manual"},
    "P9.1": {"manual"},
    "P9.3": {"manual"},
}

ID_RE = re.compile(r"^P(\d+\.\d+)$")
CLAIM_RE = re.compile(r"^C(\d+\.\d+)$")
TASK_RE = re.compile(r"^(T\d+|w\d+[a-e]|[A-Za-z][A-Za-z0-9-]*)$")
HEX_RE = re.compile(r"^[0-9a-f]{7,40}$")
AT_RE = re.compile(r"^(\d{4}-\d{2}-\d{2})T(\d{2}):(\d{2})(?::(\d{2})(?:\.\d+)?)?Z$")
HOME_RE = re.compile(r"/home/[a-z_][a-z0-9_-]*")
# A command that reaches the bench: an ssh, the bench scripts that ssh, or the
# scripts that only run on the bench.
BENCH_RE = re.compile(
    r"(^|[^A-Za-z0-9_-])ssh\s|tools/bench/(sync|job|lock)\.sh|worker/\w+\.sh|headless\.sh")
# A prose command cell that points at another row ("the P9.2 command with ...").
ROWREF_RE = re.compile(r"\b(P[A-Z]*\d*\.\d+)\b")


def id_order(row_id):
    """Sort key: P<n>.<m> numerically, other ids after them by name."""
    m = ID_RE.match(row_id)
    return (0, [int(x) for x in m.group(1).split(".")], "") if m else (1, [], row_id)


def split_cells(line):
    """Split one table line on unescaped pipes; None if it is not a row."""
    s = line.rstrip("\n")
    if not s.startswith("|"):
        return None
    parts = re.split(r"(?<!\\)\|", s)
    # A well-formed row starts and ends with a pipe: drop the empty edges.
    if parts and parts[0].strip() == "":
        parts = parts[1:]
    if parts and parts[-1].strip() == "":
        parts = parts[:-1]
    return [p.strip() for p in parts]


def spans(text):
    """The backticked spans of a cell, as (start, end, raw inner text).

    A span opens at an unescaped backtick and closes at the next unescaped one.
    Returns None when a span never closes."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == "\\" and i + 1 < n:
            i += 2
            continue
        if c == "`":
            j = i + 1
            while j < n:
                if text[j] == "\\" and j + 1 < n:
                    j += 2
                    continue
                if text[j] == "`":
                    break
                j += 1
            if j >= n:
                return None
            out.append((i, j + 1, text[i + 1:j]))
            i = j + 1
            continue
        i += 1
    return out


def unescape(raw):
    """Span text to shell text: `\\|` -> `|`, `\\`` -> `` ` ``, nothing else."""
    return re.sub(r"\\([|`])", r"\1", raw)


def parse_command(cell):
    """Classify a command cell. Returns (kind, command, note):
       ("run", shell text, annotation or "") when it is mechanically runnable,
       ("manual", None, reason) when it is prose or several commands."""
    sp = spans(cell)
    if sp is None:
        return "manual", None, "unclosed backtick"
    if not sp or sp[0][0] != 0:
        return "manual", None, "does not start with a backticked command"
    start, end, raw = sp[0]
    rest = cell[end:].strip()
    if rest and len(sp) > 1 and not rest.startswith("("):
        return "manual", None, "several commands joined by prose"
    return "run", unescape(raw), rest


class Row:
    def __init__(self, line_no, cells):
        self.line = line_no
        self.cells = cells
        self.ok_shape = len(cells) == len(COLUMNS)
        d = dict(zip(COLUMNS, cells)) if self.ok_shape else {}
        self.id = d.get("id", cells[0] if cells else "?")
        self.claim = d.get("claim", "")
        self.task = d.get("task", "")
        self.form = d.get("form", "")
        self.command_cell = d.get("command", "")
        self.rc = d.get("rc", "")
        self.result = d.get("result", "")
        self.commit = d.get("commit", "")
        self.at = d.get("at", "")
        self.kind, self.command, self.note = (
            parse_command(self.command_cell) if self.ok_shape
            else ("manual", None, "malformed row"))
        self.refs_bench = False  # set by read_rows for prose cells

    @property
    def contacts_bench(self):
        return bool(BENCH_RE.search(self.command_cell)) or self.refs_bench

    @property
    def effective_form(self):
        """bench when recorded as bench or when the command reaches the bench."""
        return "bench" if self.form == "bench" or self.contacts_bench else "local"

    @property
    def sort_key(self):
        """For --latest: the newest `at`, then the later line."""
        return (self.at, self.line)


def read_rows(path):
    """All rows of the table, in file order. Header and rule lines are skipped."""
    rows = []
    for no, line in enumerate(pathlib.Path(path).read_text().splitlines(), 1):
        cells = split_cells(line)
        if cells is None:
            continue
        if cells == COLUMNS or all(re.fullmatch(r":?-+:?", c) for c in cells):
            continue
        rows.append(Row(no, cells))
    # A prose command that names another row runs where that row runs.
    bench_ids = {r.id for r in rows if BENCH_RE.search(r.command_cell)}
    for r in rows:
        if r.kind != "run":
            r.refs_bench = any(ref in bench_ids for ref in ROWREF_RE.findall(r.command_cell))
    return rows


def commit_exists(repo, sha, cache={}):
    if sha not in cache:
        cache[sha] = subprocess.run(
            ["git", "-C", str(repo), "cat-file", "-e", f"{sha}^{{commit}}"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    return cache[sha]


def row_problems(row, repo):
    """Errors and infos for one row."""
    errs, infos = [], []
    legacy = LEGACY.get(row.id, set())
    if not row.ok_shape:
        return [f"{len(row.cells)} cells, expected {len(COLUMNS)} "
                "(an unescaped | inside a cell?)"], infos
    grammar = []
    m_id, m_claim = ID_RE.match(row.id), CLAIM_RE.match(row.claim)
    if not m_id:
        grammar.append(f"id {row.id!r} is not P<n>.<m>")
    if not m_claim:
        grammar.append(f"claim {row.claim!r} is not C<n>.<m>")
    if m_id and m_claim and m_id.group(1) != m_claim.group(1):
        grammar.append(f"claim {row.claim} does not share the number of {row.id}")
    if not TASK_RE.match(row.task):
        grammar.append(f"task {row.task!r} is not T<n>, w<n><a-e> or a word")
    if grammar and "grammar" in legacy:
        infos.append("grammar")
    else:
        errs.extend(grammar)
    if row.form not in ("local", "bench"):
        errs.append(f"form {row.form!r} is not local or bench")
    if not re.fullmatch(r"-?\d+", row.rc):
        errs.append(f"rc {row.rc!r} is not an integer")
    if row.result not in ("pass", "fail"):
        errs.append(f"result {row.result!r} is not pass or fail")
    elif re.fullmatch(r"-?\d+", row.rc) and (int(row.rc) == 0) != (row.result == "pass"):
        errs.append(f"rc {row.rc} disagrees with result {row.result} (rc 0 <=> pass)")
    if not HEX_RE.match(row.commit):
        errs.append(f"commit {row.commit!r} is not 7-40 hex digits")
    elif not commit_exists(repo, row.commit):
        errs.append(f"commit {row.commit} does not exist in the repository")
    m = AT_RE.match(row.at)
    if not m:
        errs.append(f"at {row.at!r} is not an ISO-8601 UTC timestamp")
    else:
        try:
            datetime.datetime.fromisoformat(
                f"{m.group(1)}T{m.group(2)}:{m.group(3)}:{m.group(4) or '00'}")
        except ValueError:
            errs.append(f"at {row.at!r} is not a real date and time")
    for name, cell in zip(COLUMNS, row.cells):
        if HOME_RE.search(cell):
            errs.append(f"{name} cell contains {HOME_RE.search(cell).group(0)}")
    if row.kind != "run":
        if "manual" in legacy:
            infos.append("manual")
        else:
            errs.append(f"command is not runnable: {row.note}")
    return errs, infos


def main(argv):
    args = list(argv)
    repo = pathlib.Path(__file__).resolve().parent.parent
    if "--repo" in args:
        i = args.index("--repo")
        repo = pathlib.Path(args[i + 1])
        del args[i:i + 2]
    path = pathlib.Path(args[0]) if args else repo / "PROVE.md"
    rows = read_rows(path)
    if not rows:
        print(f"prove-lint: {path}: no rows")
        return 1
    spec = repo / "SPEC.md"
    spec_text = spec.read_text() if spec.exists() else ""
    bad = 0
    legacy_notes, missing_claims, local_bench = {}, set(), []
    for row in rows:
        errs, infos = row_problems(row, repo)
        for e in errs:
            print(f"prove-lint: line {row.line} {row.id}: {e}")
        bad += len(errs)
        for i in infos:
            legacy_notes.setdefault(i, []).append(row.id)
        if row.ok_shape and CLAIM_RE.match(row.claim) and \
                not re.search(rf"\b{re.escape(row.claim)}\b", spec_text):
            missing_claims.add(row.claim)
        if row.ok_shape and row.form == "local" and row.contacts_bench:
            local_bench.append(row.id)
    if bad:
        print(f"prove-lint FAILED ({bad} problems in {len(rows)} rows)")
        return 1
    what = {"grammar": "id/claim/task grammar", "manual": "command run by hand"}
    for rule, ids in legacy_notes.items():
        print(f"info: {len(ids)} legacy rows exempt from {what[rule]}: {' '.join(ids)}")
    print(f"info: {len(missing_claims)} claims not found in SPEC.md"
          + (": " + " ".join(sorted(missing_claims, key=lambda c: [int(x) for x in c[1:].split('.')]))
             if missing_claims else ""))
    if local_bench:
        ids = sorted(set(local_bench), key=id_order)
        print(f"info: {len(local_bench)} rows ({len(ids)} ids) recorded as local contact "
              f"the bench, so tools/prove.sh runs them as bench: {' '.join(ids)}")
    print(f"prove-lint ok ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
