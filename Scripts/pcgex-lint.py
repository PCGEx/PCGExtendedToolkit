#!/usr/bin/env python
"""
PCGEx lint: static checks for PCG / PCGEx API misuse that compiles everywhere and corrupts data at runtime.

fab-preflight.py owns build breaks -- a non-zero exit there means FAB will fail to compile. This script
owns the other class: code every compiler accepts that silently produces wrong output. Each check
encodes a defect that actually shipped.

    python Scripts/pcgex-lint.py                     # this plugin
    python Scripts/pcgex-lint.py ../.. --exclude VoxelPlugin   # all sibling plugins
    python Scripts/pcgex-lint.py --list
    python Scripts/pcgex-lint.py --only entry-range-noalloc
    python Scripts/pcgex-lint.py --selftest          # prove every detector still fires
    python Scripts/pcgex-lint.py --check-mirror      # every sibling workspace's copy is byte-identical

Exit code is 1 if any finding is reported, 2 on a usage error.

A detector that silently stops matching reports "clean" over a real defect. --selftest builds a throwaway
tree holding every defect plus correct-code counterparts, and fails unless each check reports exactly its
own cases, on the right lines, and nothing in the correct code. Run it whenever you touch this file.
"""

import argparse
import difflib
import os
import re
import shutil
import sys
import tempfile

# ---------------------------------------------------------------------------- scanning

SKIP_DIRS = ("Intermediate", "Binaries", "ThirdParty", "DerivedDataCache", ".git")
SOURCE_EXT = (".h", ".hpp", ".inl", ".cpp")


def walk(root, exts):
    for dp, dn, fn in os.walk(root):
        dn[:] = [d for d in dn if d not in SKIP_DIRS]
        for f in fn:
            if f.endswith(exts):
                yield os.path.join(dp, f).replace("\\", "/")


def read(path):
    try:
        with open(path, encoding="utf-8-sig", errors="ignore") as fh:
            return fh.read()
    except OSError:
        return ""


def blank(text):
    """Spaces in place of every character but newlines, so offsets and line numbers survive."""
    return re.sub(r"[^\n]", " ", text)


RAW_STRING_RE = re.compile(r'R"([^()\\\s]{0,16})\(')


def is_digit_separator(text, i):
    """A quote between two digits of a number literal (1'000) is not a char literal."""
    if not i or i + 1 >= len(text) or not text[i - 1].isdigit() or not text[i + 1].isalnum():
        return False
    return text[max(0, i - 2):i] != "u8"


def strip_code(text):
    """Blank comments, string literals and char literals; every newline stays in place."""
    out, i, n = [], 0, len(text)
    while i < n:
        c, pair = text[i], text[i:i + 2]
        if pair == "//":
            end = text.find("\n", i)
            end = n if end < 0 else end
        elif pair == "/*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
        elif c == '"' or (c == "'" and not is_digit_separator(text, i)):
            raw = RAW_STRING_RE.match(text, i - 1) if c == '"' and i and text[i - 1] == "R" else None
            if raw:
                close = text.find(")" + raw.group(1) + '"', raw.end())
                end = n if close < 0 else close + len(raw.group(1)) + 2
            else:
                end = i + 1
                while end < n and text[end] not in (c, "\n"):
                    end += 2 if text[end] == "\\" else 1
                end = min(end + 1, n)
        else:
            out.append(c)
            i += 1
            continue
        out.append(blank(text[i:end]))
        i = end
    return "".join(out)


def balanced_close(text, open_pos):
    """Index of the ')' closing the '(' at text[open_pos], or -1."""
    depth = 0
    for i in range(open_pos, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    return -1


class Finding:
    def __init__(self, check, severity, path, line, message, detail=""):
        self.check, self.severity = check, severity
        self.path, self.line = path, line
        self.message, self.detail = message, detail


class Tree:
    """Indexes a source root once; every check reads from here."""

    def __init__(self, root, exclude=()):
        self.root = root
        self.exclude = tuple(e.lower() for e in exclude)
        self.files = sorted(p for p in walk(root, SOURCE_EXT) if not self._skip(p))
        self._code = {}

    def _skip(self, path):
        low = path.lower()
        return any(e in low for e in self.exclude)

    def code(self, path):
        """The file with comments and literals blanked."""
        if path not in self._code:
            self._code[path] = strip_code(read(path))
        return self._code[path]


# ------------------------------------------------------------------------- checks

CHECKS = {}


def check(name, severity, blurb):
    def wrap(fn):
        fn.check_name, fn.severity, fn.blurb = name, severity, blurb
        CHECKS[name] = fn
        return fn
    return wrap


ENTRY_RANGE_RE = re.compile(r"\bGetMetadataEntryValueRange\s*\(")
ENTRY_RANGE_DECL_RE = re.compile(r"(?:const\s+)?bool\b")


@check("entry-range-noalloc", "error",
       "A mutable metadata-entry range taken without allocation: GetMetadataEntryValueRange with any argument "
       "but true. Point data without element attributes arrives with MetadataEntry unallocated (engine Flatten, "
       "PCGEx output sanitize); the engine range then writes index 0 to the one shared slot and every other "
       "index to a throwaway stub (PCGValueRange.h), so every point lands on one entry and shows the last value "
       "written. Take GetMetadataEntryValueRange() on the thread that owns the data, and allocate before "
       "parallel loops. Token-pasted Get##_NAME##ValueRange calls are not seen.")
def check_entry_range_noalloc(tree):
    out = []
    for path in tree.files:
        code = tree.code(path)
        for m in ENTRY_RANGE_RE.finditer(code):
            close = balanced_close(code, m.end() - 1)
            if close < 0:
                continue
            args = " ".join(code[m.end():close].split())
            if args in ("", "true") or ENTRY_RANGE_DECL_RE.match(args):
                continue
            out.append(Finding("entry-range-noalloc", "error", path, code.count("\n", 0, m.start()) + 1,
                               f"GetMetadataEntryValueRange({args}) does not allocate",
                               "take GetMetadataEntryValueRange() on the thread that owns the data"))
    return out


# ----------------------------------------------------------------------- selftest

# check -> {fixture path -> content}. A line carrying "// expect" must be reported, on that line, and
# nothing else may be; "// expect: TEXT" also pins TEXT in the message. Paths containing "Negative" hold
# correct code that must stay silent.
EXPECT_RE = re.compile(r"// expect(?::\s*(\S.*?))?\s*$")

SELFTEST = {
    "entry-range-noalloc": {
        "Plugin/Source/Mod/Private/EntryRangeDefects.cpp": (
            "void Defects(UPCGBasePointData* Data, UPCGBasePointData& Other, const bool bAllocate, const bool bFlag)\n"
            "{\n"
            "\tData->GetMetadataEntryValueRange(false)[0] = 1; // expect\n"
            "\tTPCGValueRange<int64> A = Data->GetMetadataEntryValueRange( false ); // expect\n"
            "\tTPCGValueRange<int64> B = Data->GetMetadataEntryValueRange(/*bAllocate=*/false); // expect: GetMetadataEntryValueRange(false)\n"
            "\tTPCGValueRange<int64> C = Data->GetMetadataEntryValueRange(bAllocate); // expect\n"
            "\tTPCGValueRange<int64> D = Data->GetMetadataEntryValueRange( // expect: GetMetadataEntryValueRange(false)\n"
            "\t\tfalse);\n"
            "\tTPCGValueRange<int64> E = Other.GetMetadataEntryValueRange(!bFlag); // expect\n"
            "\tTPCGValueRange<int64> G = Data->GetMetadataEntryValueRange(bAllocate && // expect: GetMetadataEntryValueRange(bAllocate && bFlag)\n"
            "\t\tbFlag);\n"
            "\tTPCGValueRange<int64> F = Data->GetMetadataEntryValueRange(IsAllocated(Data)); // expect: GetMetadataEntryValueRange(IsAllocated(Data))\n"
            "\tconst TCHAR Quote = '\"'; Data->GetMetadataEntryValueRange(false); // expect\n"
            "\tconst int32 Big = 1'000; Data->GetMetadataEntryValueRange(false); // expect\n"
            "\tconst TCHAR* Url = TEXT(\"http://x\"); Data->GetMetadataEntryValueRange(false); // expect\n"
            "}\n"
            "#define PCGEX_BAD_ENTRIES(_DATA) _DATA->GetMetadataEntryValueRange(false) // expect\n"),
        "Plugin/Source/Mod/Private/EntryRangeNegative.cpp": (
            "class FNegativePointData : public UPCGBasePointData\n"
            "{\n"
            "\tvirtual FPCGPointMetadataEntry::ValueRange GetMetadataEntryValueRange(bool bAllocate = true) override;\n"
            "};\n"
            "void Correct(UPCGBasePointData* Data)\n"
            "{\n"
            "\tData->GetMetadataEntryValueRange()[0] = 1;\n"
            "\tTPCGValueRange<int64> A = Data->GetMetadataEntryValueRange(true);\n"
            "\tTPCGValueRange<int64> B = Data->GetMetadataEntryValueRange( /*bAllocate=*/ true );\n"
            "\tTPCGValueRange<int64> C = Data->GetMetadataEntryValueRange(\n"
            "\t\ttrue);\n"
            "\tTConstPCGValueRange<int64> D = Data->GetConstMetadataEntryValueRange();\n"
            "\t// Data->GetMetadataEntryValueRange(false);\n"
            "\t/* Data->GetMetadataEntryValueRange(false); */\n"
            "\t/*\n"
            "\t * Data->GetMetadataEntryValueRange(false);\n"
            "\t */\n"
            "\tconst TCHAR* Doc = TEXT(\"GetMetadataEntryValueRange(false)\");\n"
            "\tconst TCHAR* Esc = TEXT(\"\\\" GetMetadataEntryValueRange(false) \\\"\");\n"
            "\tconst TCHAR* Raw = TEXT(R\"doc(\" GetMetadataEntryValueRange(false) )doc\");\n"
            "\tconst TCHAR Quote = '\"';\n"
            "\tMyGetMetadataEntryValueRange(false);\n"
            "\tData->GetMetadataEntryValueRangeCached(false);\n"
            "}\n"),
    },
}


def run_selftest():
    tmp = tempfile.mkdtemp(prefix="pcgexlint-")
    try:
        failed = []
        for name in sorted(CHECKS):
            fixtures = SELFTEST.get(name)
            if not fixtures:
                print(f"  NONE  {name} has no fixtures")
                failed.append(name)
                continue

            root = f"{tmp}/{name}".replace("\\", "/")
            want, texts = set(), {}
            for rel, content in fixtures.items():
                dst = f"{root}/{rel}"
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "w", encoding="utf-8", newline="\n") as fh:
                    fh.write(content)
                if "Negative" in rel:
                    continue
                for i, line in enumerate(content.split("\n")):
                    m = EXPECT_RE.search(line)
                    if m:
                        want.add((rel, i + 1))
                        if m.group(1):
                            texts[(rel, i + 1)] = m.group(1)

            messages = {}
            for f in CHECKS[name](Tree(root)):
                messages.setdefault((f.path[len(root) + 1:], f.line), []).append(f.message)
            got = set(messages)
            missed, extra = sorted(want - got), sorted(got - want)
            wrong = sorted(k for k, t in texts.items() if k in got and not any(t in msg for msg in messages[k]))
            ok = bool(want) and not missed and not extra and not wrong
            print(f"  {'ok  ' if ok else 'FAIL'}  {name} ({len(want & got)}/{len(want)})")
            for rel, line in missed:
                print(f"        missed      {rel}:{line}")
            for rel, line in extra:
                print(f"        over-fired  {rel}:{line}")
            for key in wrong:
                print(f"        wrong text  {key[0]}:{key[1]}  wanted '{texts[key]}', got {messages[key]}")
            if not ok:
                failed.append(name)

        if failed:
            print(f"\n{len(failed)} check(s) failed their fixtures: {', '.join(failed)}")
            return 1
        print(f"\nall {len(CHECKS)} check(s) report exactly their own cases and nothing in the correct code")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# --------------------------------------------------------------------------- mirror

def script_mirrors(explicit):
    """Other copies of this script: the same path relative to the workspace, in sibling workspaces.

    Layout assumed: <parent>/<workspace>/Plugins/<plugin>/Scripts/pcgex-lint.py.
    """
    me = os.path.abspath(__file__).replace("\\", "/")
    if explicit:
        return me, [os.path.abspath(explicit).replace("\\", "/")]
    parts = me.split("/")
    rel, workspace = "/".join(parts[-4:]), "/".join(parts[:-4])
    parent = os.path.dirname(workspace)
    found = []
    for d in sorted(os.listdir(parent)) if os.path.isdir(parent) else []:
        cand = f"{parent}/{d}/{rel}"
        if cand != me and os.path.isfile(cand):
            found.append(cand)
    return me, found


def run_check_mirror(explicit):
    me, mirrors = script_mirrors(explicit)
    if not mirrors:
        print("no mirror copy found; pass the other copy's path explicitly", file=sys.stderr)
        return 2
    with open(me, "rb") as fh:
        mine = fh.read()
    rc = 0
    for other in mirrors:
        with open(other, "rb") as fh:
            theirs = fh.read()
        if mine == theirs:
            print(f"identical: {other}")
            continue
        rc = 1
        diff = list(difflib.unified_diff(mine.decode("utf-8", "replace").splitlines(),
                                         theirs.decode("utf-8", "replace").splitlines(),
                                         me, other, lineterm=""))
        print(f"DIFFERS: {other}\n" + "\n".join(diff[:80]))
        if len(diff) > 80:
            print(f"... {len(diff) - 80} more diff lines")
    return rc


# --------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description="PCGEx runtime-correctness static checks")
    ap.add_argument("root", nargs="?", default=None,
                    help="source root (default: the plugin this script lives in)")
    ap.add_argument("--only", action="append", metavar="CHECK", help="run just this check")
    ap.add_argument("--list", action="store_true", help="describe every check")
    ap.add_argument("--selftest", action="store_true", help="prove every detector still fires")
    ap.add_argument("--check-mirror", nargs="?", const="", default=None, metavar="PATH",
                    help="fail unless every mirror copy of this script (PATH, or auto-discovered in "
                         "sibling workspaces) is byte-identical")
    ap.add_argument("--exclude", action="append", default=[], metavar="PATTERN",
                    help="skip paths containing PATTERN (repeatable); use for vendored plugins")
    ap.add_argument("--quiet", action="store_true", help="findings only")
    args = ap.parse_args()

    if args.list:
        for name, fn in sorted(CHECKS.items()):
            print(f"{name}  [{fn.severity}]\n    {fn.blurb}\n")
        return 0

    if args.selftest:
        return run_selftest()

    if args.check_mirror is not None:
        return run_check_mirror(args.check_mirror)

    root = args.root or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    root = root.replace("\\", "/").rstrip("/")
    if not os.path.isdir(root):
        print(f"not a directory: {root}", file=sys.stderr)
        return 2

    selected = args.only or sorted(CHECKS)
    unknown = [c for c in selected if c not in CHECKS]
    if unknown:
        print(f"unknown check(s): {', '.join(unknown)}", file=sys.stderr)
        return 2

    tree = Tree(root, args.exclude)
    if not args.quiet:
        print(f"{root}\n{len(tree.files)} source files\n")

    findings = []
    for name in selected:
        findings.extend(CHECKS[name](tree))

    for f in sorted(findings, key=lambda x: (x.check, x.path, x.line)):
        print(f"[{f.severity}] {f.check}: {f.message}\n    {f.path}:{f.line}"
              + (f"\n    -> {f.detail}" if f.detail else ""))

    if not args.quiet:
        print(f"\n{len(findings)} finding(s) across {len(selected)} check(s)")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
