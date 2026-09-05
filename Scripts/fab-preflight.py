#!/usr/bin/env python
"""
FAB pre-flight: static checks for defects a local Windows editor build cannot surface.

FAB compiles configurations the dev box never does -- a non-editor target, and Clang on
Mac/Linux, from a clean Intermediate (so Unity Build groups .cpp files differently). Every
FAB-only failure this plugin has hit falls into that description. Each check below encodes
one such failure that actually happened, or one the coding standard already forbids.

    python Scripts/fab-preflight.py                 # this plugin
    python Scripts/fab-preflight.py ../.. --exclude VoxelPlugin   # all sibling plugins
    python Scripts/fab-preflight.py --list
    python Scripts/fab-preflight.py --only nsdmi-default-arg
    python Scripts/fab-preflight.py --selftest      # prove every detector still fires
    python Scripts/fab-preflight.py --check-mirror  # every sibling workspace's copy is byte-identical
    python Scripts/fab-preflight.py --diff-trees ../../../PCGExWorkbench_57/Plugins/PCGExtendedToolkit . \
        --ignore SomeKnownDelta                     # port drift between two plugin trees

Exit code is 1 if any error-severity finding is reported, so CI can gate on it.

A detector that silently stops matching is worse than no detector -- it reports "clean" over a
real defect. --selftest builds a throwaway tree containing one instance of every defect and
fails if any check misses its own case. Run it whenever you touch a pattern in this file.

Deliberately absent (verified against UE 5.8 UnrealBuildTool, Configuration/Rules/CppCompileWarnings.cs):
  -Wunused-lambda-capture, -Wunused-private-field, -Wunused-variable, -Winconsistent-missing-override,
  -Wsign-compare and -Wtautological-compare all default to WarningLevel.Off, so UBT passes -Wno-* for
  them and they cannot fail a FAB build. Do not add detectors for them. -Wall -Werror is still on
  (Platform/Clang/ClangToolChain.cs), so -Wall members and Clang's default-on diagnostics that UBT
  does not silence remain FAB-only failures: those are what clang-wall and ctor-reorder cover.
  Access-control violations (calling a protected virtual through a base pointer) are diagnosed by
  every compiler on the dev box too, so they are not FAB-only and are not checked here.
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
HDR_EXT = (".h", ".hpp", ".inl")


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


def strip_comments(text):
    """Blank out comments but keep line numbering intact."""
    text = re.sub(r'/\*.*?\*/', lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


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
        self.headers = sorted(p for p in walk(root, HDR_EXT) if not self._skip(p))
        self.sources = sorted(p for p in walk(root, (".cpp",)) if not self._skip(p))
        self.text = {}
        self.clean = {}

        # basename -> [real paths], for resolving #include "..." the way UBT does
        self.by_name = {}
        for h in self.headers:
            self.by_name.setdefault(os.path.basename(h).lower(), []).append(h)

        self._index_modules()
        self._closures = {}

    def _skip(self, path):
        low = path.lower()
        return any(e in low for e in self.exclude)

    def body(self, path):
        if path not in self.text:
            self.text[path] = read(path)
        return self.text[path]

    def stripped(self, path):
        if path not in self.clean:
            self.clean[path] = strip_comments(self.body(path))
        return self.clean[path]

    # -- modules -----------------------------------------------------------

    def _index_modules(self):
        """module name -> (dir, host type). Host type comes from the owning .uplugin."""
        self.module_dir = {}
        for dp, dn, fn in os.walk(self.root):
            dn[:] = [d for d in dn if d not in SKIP_DIRS]
            for f in fn:
                if f.endswith(".Build.cs"):
                    self.module_dir[f[:-len(".Build.cs")]] = dp.replace("\\", "/")

        self.module_type = {}
        for up in walk(self.root, (".uplugin",)):
            for m in re.finditer(r'"Name"\s*:\s*"([^"]+)"\s*,\s*"Type"\s*:\s*"([^"]+)"',
                                 read(up).replace("\n", " ")):
                self.module_type[m.group(1)] = m.group(2)

    def module_of(self, path):
        best, best_len = None, -1
        for name, d in self.module_dir.items():
            if path.startswith(d + "/") and len(d) > best_len:
                best, best_len = name, len(d)
        return best

    def always_editor(self, path):
        """True if this module can never be compiled with WITH_EDITOR=0.

        Per Engine/Source/Runtime/Projects/Private/ModuleDescriptor.cpp only Editor /
        EditorNoCommandlet gate on TargetType == Editor. UncookedOnly gates on
        !bBuildRequiresCookedData, so it still reaches non-editor targets and stays checked.

        Do not try to infer this from a "UnrealEd" dependency: Runtime modules routinely add
        it under `if (Target.bBuildEditor)`, which does not make them editor-only.
        """
        return self.module_type.get(self.module_of(path)) in ("Editor", "EditorNoCommandlet")

    # -- include graph -----------------------------------------------------

    def resolve(self, inc):
        cands = self.by_name.get(os.path.basename(inc).lower(), [])
        for c in cands:
            if c.lower().endswith("/" + inc.lower().lstrip("./")):
                return c
        return cands[0] if len(cands) == 1 else None

    def closure(self, path):
        """Every project header reachable from path, itself included."""
        if path in self._closures:
            return self._closures[path]
        self._closures[path] = {path}          # cycle guard
        acc = {path}
        for inc in re.findall(r'^\s*#\s*include\s+"([^"]+)"', self.stripped(path), re.M):
            r = self.resolve(inc)
            if r and r != path:
                acc |= self.closure(r)
        self._closures[path] = acc
        return acc


# ---------------------------------------------------------------------- primitives

IF_RE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')
ED_RE = re.compile(r'\bWITH_EDITOR(ONLY_DATA)?\b')
CLS_RE = re.compile(
    r'^\s*(?:class|struct)\s+(?:[A-Z_0-9]+_API\s+)?([A-Za-z_]\w*)\s*(?:final\s*)?(?::[^;{]*)?\s*\{?\s*$')


def editor_guard_map(lines):
    """line index -> is it inside an editor-only #if."""
    out, stack = [], []
    for ln in lines:
        m = IF_RE.match(ln)
        if m:
            d, rest = m.group(1), m.group(2)
            if d in ("if", "ifdef", "ifndef"):
                stack.append(bool(ED_RE.search(rest)) and d != "ifndef")
            elif d == "elif" and stack:
                stack[-1] = bool(ED_RE.search(rest))
            elif d == "else" and stack:
                stack[-1] = False
            elif d == "endif" and stack:
                stack.pop()
        out.append(any(stack))
    return out


def logical_lines(lines):
    """Yield (first_line_index, joined_text), collapsing backslash continuations.

    Multi-line #define bodies are the norm in this codebase; matching per physical line
    attributes a macro's body to the wrong construct.
    """
    i = 0
    while i < len(lines):
        start, buf = i, lines[i]
        while buf.rstrip().endswith("\\") and i + 1 < len(lines):
            buf = buf.rstrip()[:-1] + " " + lines[i + 1]
            i += 1
        yield start, buf
        i += 1


def class_scopes(lines):
    """Yield (line_index, line, enclosing_class, nested_types, rel_depth) walking a header's class bodies.

    nested_types maps an already-closed inner type name -> whether it has default member
    initializers. rel_depth is 1 for a line directly in the class body, 2+ inside an inline
    method body. A class body is only entered once its '{' is seen; the brace commonly sits
    on the line after 'class X', and popping before that loses every body in the file.
    """
    stack, nested, depth = [], {}, 0
    for i, ln in enumerate(lines):
        if ln.lstrip().startswith("#"):
            continue
        m = CLS_RE.match(ln)
        if m and not ln.rstrip().endswith(";"):
            stack.append([m.group(1), depth, False, False])   # name, depth, has_nsdmi, opened
        elif stack and stack[-1][3]:
            yield i, ln, stack[-1], nested.get(stack[-1][0], {}), depth - stack[-1][1]
        o, c = ln.count("{"), ln.count("}")
        if stack and not stack[-1][3] and o > 0:
            stack[-1][3] = True
        depth += o - c
        while stack and stack[-1][3] and depth <= stack[-1][1]:
            done = stack.pop()
            if stack:
                nested.setdefault(stack[-1][0], {})[done[0]] = done[2]


NS_RE = re.compile(r'^\s*namespace\s+([A-Za-z_][\w:]*)\s*\{?\s*$')


def namespace_scopes(lines):
    """Yield (line_index, line, namespace_path) for every line at namespace (or global) scope.

    Class and function bodies are opaque. A namespace or class header whose '{' sits on the
    next line is still attributed to its scope.
    """
    stack, pending = [], None
    for i, ln in enumerate(lines):
        if ln.lstrip().startswith("#"):
            continue
        m = NS_RE.match(ln)
        c = CLS_RE.match(ln)
        if m:
            pending = ("ns", m.group(1))
        elif c and not ln.rstrip().endswith(";"):
            pending = ("cls", c.group(1))
        elif all(k == "ns" for k, _ in stack):
            yield i, ln, "::".join(n for _, n in stack)
        for ch in ln:
            if ch == "{":
                stack.append(pending or ("blk", None))
                pending = None
            elif ch == "}" and stack:
                stack.pop()


def join_parens(lines, i, limit=30):
    """Join lines[i:] until parentheses balance; returns (text, last_index).

    Multi-line parameter lists are common; matching a signature per physical line misses them.
    """
    buf, j = lines[i], i
    while buf.count("(") > buf.count(")") and j + 1 < len(lines) and j - i < limit:
        j += 1
        buf = buf.rstrip() + " " + lines[j].strip()
    return buf, j


def ctor_init_list(text, pos):
    """From a constructor's name at text[pos], return its member-initializer names in order.

    None when the constructor is only declared (';' before any body) or has no initializer list.
    Angle brackets are tracked so 'TMap<FName, int32>(...)' in the list does not split on its comma.
    """
    n, depth, i = len(text), 0, text.find("(", pos)
    if i < 0:
        return None
    while i < n:                              # skip the parameter list
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    i += 1
    m = re.match(r'\s*(?:const\s*|noexcept\s*|override\s*|final\s*)*', text[i:])
    i += m.end()
    if i >= n or text[i] != ":" or text[i + 1:i + 2] == ":":
        return None
    i += 1
    start, pd, bd, ad = i, 0, 0, 0
    items = []
    while i < n:
        ch = text[i]
        if ch in "([":
            pd += 1
        elif ch in ")]":
            pd -= 1
        elif ch == "<" and pd == 0 and bd == 0:
            ad += 1
        elif ch == ">" and ad > 0 and text[i - 1] != "-":
            ad -= 1
        elif ch == "{":
            if pd == 0 and ad == 0 and bd == 0:
                items.append(text[start:i])
                break
            bd += 1
        elif ch == "}":
            bd -= 1
        elif ch == "," and pd == 0 and bd == 0 and ad == 0:
            items.append(text[start:i])
            start = i + 1
        elif ch == ";" and pd == 0 and bd == 0:
            return None
        i += 1
    names = []
    for it in items:
        m = re.match(r'\s*([A-Za-z_][\w:]*)\s*(?:<[^{(]*>)?\s*[({]', it)
        if m:
            names.append(m.group(1).split("::")[-1])
    return names


MEMBER_RE = re.compile(
    r'^\s*(?!(?:return|using|typedef|friend|static|virtual|template|public|private|protected|enum|class|'
    r'struct|namespace|case|else|if|for|while|delete|new|goto|UPROPERTY|UFUNCTION|GENERATED_BODY|'
    r'GENERATED_USTRUCT_BODY|GENERATED_UCLASS_BODY)\b)'
    r'(?:mutable\s+)?[A-Za-z_][\w:<>,\s\*&]*?\s+\*?&?\s*([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*(?::\s*\d+\s*)?(?:;|=[^=]|\{)')


def type_definers(tree):
    """type name -> set of headers that define it (class/struct with a body)."""
    if not hasattr(tree, "_definers"):
        d = {}
        for h in tree.headers:
            for sym in DEFINES_RE.findall(tree.stripped(h)):
                d.setdefault(sym, set()).add(h)
        tree._definers = d
    return tree._definers


# ------------------------------------------------------------------------- checks

CHECKS = {}


def check(name, severity, blurb):
    def wrap(fn):
        fn.check_name, fn.severity, fn.blurb = name, severity, blurb
        CHECKS[name] = fn
        return fn
    return wrap


BASE_RE = re.compile(
    r'^\s*(?:class|struct)\s+(?:[A-Z_0-9]+_API\s+)?[A-Za-z_]\w*\s*(?:final\s*)?:\s*'
    r'(?:public\s+|protected\s+|private\s+)?([A-Za-z_]\w*)')
DEFINES_RE = re.compile(r'^\s*(?:class|struct)\s+(?:[A-Z_0-9]+_API\s+)?([A-Za-z_]\w*)\s*(?::|\{)', re.M)
MACRO_DEF_RE = re.compile(r'^\s*#\s*define\s+(PCGEX_[A-Za-z_0-9]+)', re.M)


@check("missing-include", "error",
       "A reflected header's base class or PCGEX macro is not reachable through its includes. "
       "Unity Build supplies it from a neighbouring .cpp; a clean build does not.")
def check_missing_include(tree):
    defines, macros = {}, {}
    for h in tree.headers:
        t = tree.stripped(h)
        for sym in DEFINES_RE.findall(t):
            defines.setdefault(sym, set()).add(h)
        for mac in MACRO_DEF_RE.findall(t):
            macros.setdefault(mac, set()).add(h)

    out = []
    for h in tree.headers:
        t = tree.stripped(h)
        if ".generated.h" not in t:
            continue
        cl = tree.closure(h)
        for base in set(BASE_RE.findall(t)):
            if not base.startswith(("UPCGEx", "FPCGEx", "IPCGEx")):
                continue
            owners = defines.get(base)
            if owners and not (owners & cl):
                out.append(Finding("missing-include", "error", h, 0,
                                   f"base class '{base}' is only forward-declared here",
                                   f'add #include "{rel_include(tree, sorted(owners)[0])}"'))
        for mac in sorted(set(re.findall(r'\b(PCGEX_[A-Z][A-Z_0-9]*)\s*\(', t))):
            owners = macros.get(mac)
            if owners and not (owners & cl):
                out.append(Finding("missing-include", "error", h, 0,
                                   f"macro '{mac}' is used but never included",
                                   f'add #include "{rel_include(tree, sorted(owners)[0])}"'))
    return out


def rel_include(tree, path):
    """Best-effort include spelling: the part after a Public/ or Private/ root."""
    for marker in ("/Public/", "/Private/", "/Classes/"):
        if marker in path:
            return path.split(marker, 1)[1]
    return os.path.basename(path)


DECL_RE = re.compile(
    r'^\s*(?:virtual\s+|static\s+|explicit\s+)*[A-Za-z_][\w:<>,\s\*&]*?\b([A-Za-z_]\w*)\s*'
    r'\([^;{]*\)\s*(?:const\s*)?(?:override\s*|final\s*)*;\s*$')
DEF_RE = re.compile(
    r'^\s*(?:template\s*<[^>]*>\s*)?(?:[A-Za-z_][\w:<>,\s\*&]*?\s+)?([A-Za-z_]\w*)::([A-Za-z_~]\w*)\s*\(')


@check("editor-guard", "error",
       "A member declared under #if WITH_EDITOR is defined without one. With WITH_EDITOR=0 the "
       "declaration vanishes and the orphaned definition is a syntax error (C2039 + C2270).")
def check_editor_guard(tree):
    decls = {}
    for h in tree.headers:
        lines = tree.stripped(h).split("\n")
        guarded = editor_guard_map(lines)
        for i, ln, scope, _nested, _rel in class_scopes(lines):
            d = DECL_RE.match(ln)
            if d:
                decls[(scope[0], d.group(1))] = guarded[i]

    out = []
    for c in tree.sources:
        if tree.always_editor(c):
            continue
        lines = tree.stripped(c).split("\n")
        guarded = editor_guard_map(lines)
        depth = 0
        for i, ln in enumerate(lines):
            # only a qualified name at brace depth 0 is a definition; nested ones are call sites
            m = DEF_RE.match(ln) if depth == 0 else None
            depth += ln.count("{") - ln.count("}")
            if not m or ln.rstrip().endswith(";"):
                continue
            if decls.get((m.group(1), m.group(2))) and not guarded[i]:
                out.append(Finding("editor-guard", "error", c, i + 1,
                                   f"{m.group(1)}::{m.group(2)} is declared #if WITH_EDITOR "
                                   "but defined unguarded",
                                   "wrap the definition in #if WITH_EDITOR / #endif"))
    return out


NSDMI_RE = re.compile(r'^\s*(?!return\b)[A-Za-z_][\w:<>,\s\*&]*\s+([A-Za-z_]\w*)\s*(?:=|\{)[^;]*;\s*$')
DEFARG_RE = re.compile(r'=\s*([A-Za-z_]\w*)\s*(?:\(\s*\)|\{\s*\})')


@check("nsdmi-default-arg", "error",
       "A nested type's default member initializers are needed for a default ARGUMENT of the "
       "enclosing class. They are late-parsed at the end of the outermost class, so they are not "
       "available yet. MSVC accepts this; Clang rejects it.")
def check_nsdmi_default_arg(tree):
    out = []
    for h in tree.headers:
        lines = tree.stripped(h).split("\n")
        for i, ln, scope, nested, _rel in class_scopes(lines):
            if NSDMI_RE.match(ln) and "(" not in ln.split("=")[0]:
                scope[2] = True
            for m in DEFARG_RE.finditer(ln):
                # A default ARGUMENT only: the '=' must sit inside a parameter list. A default
                # MEMBER INITIALIZER ('T Member = T();') is fine -- it is late-parsed alongside
                # the nested type's own initializers, in declaration order.
                head = ln[:m.start()]
                if head.count("(") - head.count(")") <= 0:
                    continue
                if nested.get(m.group(1)):
                    out.append(Finding("nsdmi-default-arg", "error", h, i + 1,
                                       f"'{m.group(1)}()' as a default argument while "
                                       f"'{scope[0]}' is still being defined",
                                       "split into an overload that forwards the default "
                                       "from the .cpp"))
    return out


@check("extern-template-api", "error",
       "A live 'extern template class' of an _API class template. It suppresses implicit "
       "instantiation on Clang while MSVC links via the import table -- Windows-clean, Mac-broken. "
       "See the rule block at the top of PCGExCoreMacros.h.")
def check_extern_template_api(tree):
    exported = set()
    for h in tree.headers:
        for m in re.finditer(r'^\s*(?:template\s*<[^>]*>\s*)?class\s+[A-Z_0-9]+_API\s+([A-Za-z_]\w*)',
                             tree.stripped(h), re.M):
            exported.add(m.group(1))

    extern_cls = re.compile(r'extern\s+template\s+class\s+([A-Za-z_]\w*)\s*<')
    out = []
    for h in tree.headers:
        lines = tree.stripped(h).split("\n")
        # These headers redefine one macro name (PCGEX_TPL) several times with #undef between,
        # so a definition only applies until its #undef. Only 'extern template class' counts --
        # function templates are exempt and their invocations stay live.
        active = {}
        for idx, ln in logical_lines(lines):
            d = re.match(r'^\s*#\s*define\s+([A-Za-z_]\w*)', ln)
            if d:
                m = extern_cls.search(ln)
                if m and m.group(1) in exported:
                    active[d.group(1)] = (m.group(1), idx + 1)
                else:
                    active.pop(d.group(1), None)
                continue
            u = re.match(r'^\s*#\s*undef\s+([A-Za-z_]\w*)', ln)
            if u:
                active.pop(u.group(1), None)
                continue
            if ln.lstrip().startswith("#"):
                continue
            m = extern_cls.search(ln)
            if m and m.group(1) in exported:
                out.append(Finding("extern-template-api", "error", h, idx + 1,
                                   f"extern template class of exported '{m.group(1)}'",
                                   "remove it; the _API on the class definition does the export"))
                continue
            for name, (tpl, dline) in active.items():
                if re.search(r'\b' + re.escape(name) + r'\s*[),]', ln):
                    out.append(Finding("extern-template-api", "error", h, idx + 1,
                                       f"macro {name} (externs exported '{tpl}', "
                                       f"defined line {dline}) is invoked",
                                       "keep the invocation commented out"))
    return out


@check("unity-collision", "warn",
       "Anonymous namespace or file-scope static in a .cpp. Unity Build concatenates a module's "
       ".cpp files into one TU, where anonymous namespaces merge -- two same-named helpers become "
       "a redefinition (C2084). Use a named namespace matching the file.")
def check_unity_collision(tree):
    out = []
    for c in tree.sources:
        lines = tree.stripped(c).split("\n")
        depth = 0
        for i, ln in enumerate(lines):
            if depth == 0 and re.match(r'^\s*namespace\s*\{?\s*$', ln):
                # 'namespace' alone, or 'namespace {' -- either way it is anonymous
                nxt = lines[i + 1].strip() if i + 1 < len(lines) else ""
                if ln.rstrip().endswith("{") or nxt.startswith("{"):
                    out.append(Finding("unity-collision", "warn", c, i + 1,
                                       "anonymous namespace at file scope",
                                       "use a named namespace matching the file"))
            if depth == 0 and re.match(r'^\s*static\s+[A-Za-z_][\w:<>,\s\*&]*\s+[A-Za-z_]\w*\s*\(', ln):
                out.append(Finding("unity-collision", "warn", c, i + 1,
                                   "file-scope static function",
                                   "use a named namespace matching the file"))
            depth += ln.count("{") - ln.count("}")
    return out


@check("msvc-only", "error",
       "An MSVC-only extension. Clang and GCC reject these under -Werror.")
def check_msvc_only(tree):
    patterns = [(r'\b__forceinline\b', "__forceinline", "use FORCEINLINE"),
                (r'^\s*#\s*pragma\s+warning\b', "#pragma warning",
                 "use THIRD_PARTY_INCLUDES_START / _END")]
    out = []
    for p in tree.headers + tree.sources:
        lines = tree.stripped(p).split("\n")
        for i, ln in enumerate(lines):
            for rx, what, fix in patterns:
                if re.search(rx, ln):
                    out.append(Finding("msvc-only", "error", p, i + 1, what, fix))
    return out


@check("include-case", "error",
       "An #include whose spelling differs in case from the file on disk. Windows does not care; "
       "Mac and Linux do.")
def check_include_case(tree):
    exact = {h.rsplit("/", 1)[-1]: h for h in tree.headers}
    out = []
    for p in tree.headers + tree.sources:
        for m in re.finditer(r'^\s*#\s*include\s+"([^"]+)"', tree.stripped(p), re.M):
            inc = m.group(1)
            base = os.path.basename(inc)
            if base in exact:
                continue
            hits = tree.by_name.get(base.lower(), [])
            if hits:
                line = tree.stripped(p)[:m.start()].count("\n") + 1
                out.append(Finding("include-case", "error", p, line,
                                   f'#include "{inc}" does not match the file on disk',
                                   f"real name is {os.path.basename(hits[0])}"))
    return out


@check("generated-last", "error",
       ".generated.h must be the final #include in a reflected header; UHT requires it.")
def check_generated_last(tree):
    out = []
    for h in tree.headers:
        incs = re.findall(r'^\s*#\s*include\s+"([^"]+)"', tree.stripped(h), re.M)
        gen = [i for i, v in enumerate(incs) if v.endswith(".generated.h")]
        if gen and gen[0] != len(incs) - 1:
            out.append(Finding("generated-last", "error", h, 0,
                               f'"{incs[gen[0]]}" is not the last include',
                               f'followed by "{incs[gen[0] + 1]}"'))
    return out


# Getters whose return type is routinely only forward-declared at the call site. Each entry:
# (defining engine header, type name, regex of a deref of the getter's result).
FWD_DEREFS = [
    ("UObject/Package.h", "UPackage",
     re.compile(r'\b(GetOutermost|GetPackage|GetTransientPackage)\s*\(\s*\)\s*->')),
]


@check("fwd-decl-deref", "warn",
       "A getter returning a commonly forward-declared engine type (UPackage) is dereferenced, "
       "but the defining header is nowhere in the file's include closure. MSVC editor builds get "
       "the definition transitively; FAB's clean Clang build may only see the forward declaration "
       "(C2027 'use of undefined type'). Warn-level: a transitive engine include can legitimately "
       "supply it, but a direct include is the only spelling a clean build guarantees.")
def check_fwd_decl_deref(tree):
    inc_res = {hdr: re.compile(r'^\s*#\s*include\s+["<]' + re.escape(hdr) + r'[">]', re.M)
               for hdr, _, _ in FWD_DEREFS}
    out = []
    for p in tree.headers + tree.sources:
        t = tree.stripped(p)
        cl = None
        for hdr, ty, rx in FWD_DEREFS:
            hits = list(rx.finditer(t))
            if not hits:
                continue
            if cl is None:
                cl = tree.closure(p)
            if any(inc_res[hdr].search(tree.stripped(f)) for f in cl):
                continue
            for m in hits:
                out.append(Finding("fwd-decl-deref", "warn", p,
                                   t[:m.start()].count("\n") + 1,
                                   f"{m.group(1)}()-> needs the complete {ty} type, and "
                                   f'"{hdr}" is not in the include closure',
                                   f'add #include "{hdr}"'))
    return out


CLASS_TPL_RE = re.compile(
    r'\b(TSubclassOf|TSoftClassPtr)\s*<\s*(?:class\s+)?([A-Za-z_]\w*)\s*>\s*(?:const\s+)?&?\s*([A-Za-z_]\w*)\b')
# Callees whose matching parameter is UClass*: passing the wrapper converts through operator UClass*().
CLASS_ARG_CALLEES = (r'(?:NewObject|SpawnActor|SpawnActorDeferred|IsChildOf|IsA|Cast|CastChecked|ExactCast|'
                     r'GetDefault|GetMutableDefault|CreateDefaultSubobject|StaticLoadClass|LoadClass)')


def complete_type_use(text, name):
    """Regex for the uses of a TSubclassOf/TSoftClassPtr variable that instantiate T::StaticClass().

    Verified against 5.8 Templates/SubclassOf.h and UObject/SoftObjectPtr.h: Get(), operator*,
    operator->, operator UClass*, GetDefaultObject() and LoadSynchronous() all compare against
    T::StaticClass(); construction, copy and assignment do not.
    """
    n = re.escape(name)
    return re.compile(
        rf'\b{n}\s*(?:\.\s*(?:Get|GetDefaultObject|LoadSynchronous)\s*\(|->)'
        rf'|[!*]\s*{n}\b'
        rf'|\bif\s*\(\s*{n}\s*\)'
        rf'|\b{n}\s*(?:&&|\|\|)|(?:&&|\|\|)\s*{n}\s*\)'
        rf'|\b{CLASS_ARG_CALLEES}\s*(?:<[^>]*>)?\s*\([^;]*\b{n}\b')


@check("subclassof-incomplete", "error",
       "A TSubclassOf<T> / TSoftClassPtr<T> is dereferenced (Get, ->, bool test, GetDefaultObject, "
       "passed as UClass*) while T is only forward-declared: those paths call T::StaticClass(), which "
       "needs the complete type (C2027 inside SubclassOf.h). Only project types are checked -- an "
       "engine T can be completed by a transitive engine include, a project T cannot.")
def check_subclassof_incomplete(tree):
    definers = type_definers(tree)
    out = []
    for p in tree.headers + tree.sources:
        t = tree.stripped(p)
        hits = list(CLASS_TPL_RE.finditer(t))
        if not hits:
            continue
        cl, seen = None, set()
        for m in hits:
            tpl, ty, name = m.groups()
            owners = definers.get(ty)
            if not owners or (ty, name) in seen:
                continue
            if cl is None:
                cl = tree.closure(p)
            if owners & cl:
                continue
            seen.add((ty, name))
            if complete_type_use(t, name).search(t):
                out.append(Finding("subclassof-incomplete", "error", p, t[:m.start()].count("\n") + 1,
                                   f"{tpl}<{ty}> '{name}' is used through T::StaticClass() but "
                                   f"'{ty}' is only forward-declared here",
                                   f'add #include "{rel_include(tree, sorted(owners)[0])}"'))
    return out


FREE_KW = (r'(?!\s*(?:return|if|else|for|while|switch|using|typedef|friend|template|class|struct|enum|'
           r'namespace|delete|new|throw|case)\b)')
FREE_DECL_RE = re.compile(
    r'^\s*' + FREE_KW + r'(?:[A-Z_0-9]+_API\s+)?(?:extern\s+|inline\s+|static\s+|constexpr\s+)*'
    r'[A-Za-z_][\w:<>,\s\*&]*?\s+\*?&?\s*([A-Za-z_]\w*)\s*\(([^;{]*)\)\s*(?:const\s*)?(?:noexcept\s*)?;\s*$')
FREE_DEF_RE = re.compile(
    r'^\s*' + FREE_KW + r'(?:[A-Z_0-9]+_API\s+)?(?:inline\s+|static\s+|constexpr\s+)*'
    r'[A-Za-z_][\w:<>,\s\*&]*?\s+\*?&?\s*(?:([A-Za-z_][\w:]*)::)?([A-Za-z_]\w*)\s*\(([^;{]*)\)\s*'
    r'(?:const\s*)?(?:noexcept\s*)?\{?\s*$')


def free_functions(tree, paths, want_def):
    """(namespace, name) -> [(guarded, path, line)] for namespace-scope prototypes or definitions.

    A variable defined with constructor arguments ('FAutoConsoleCommand X(TEXT(...))') has the
    shape of a prototype; string literals in the argument list tell the two apart.
    """
    out = {}
    for p in paths:
        lines = tree.stripped(p).split("\n")
        guarded = editor_guard_map(lines)
        for i, ln, ns in namespace_scopes(lines):
            if "(" not in ln:
                continue
            text, last = join_parens(lines, i)
            if want_def:
                m = FREE_DEF_RE.match(text)
                if not m:
                    continue
                nxt = next((x.strip() for x in lines[last + 1:last + 3] if x.strip()), "")
                if not (text.rstrip().endswith("{") or nxt.startswith("{")):
                    continue
                qual, name, args = m.group(1), m.group(2), m.group(3)
                full = "::".join(x for x in (ns, qual) if x)
            else:
                m = FREE_DECL_RE.match(text)
                if not m:
                    continue
                name, args, full = m.group(1), m.group(2), ns
            if '"' in args or "TEXT(" in args:
                continue
            out.setdefault((full, name), []).append((guarded[i], p, i + 1))
    return out


@check("editor-guard-free", "error",
       "A namespace-scope function declared outside #if WITH_EDITOR but defined only inside one. The "
       "exported declaration promises a symbol the non-editor DLL never contains: any unguarded call "
       "links on the dev box (editor target) and fails to link on FAB's non-editor target. Error when "
       "such a call exists in a non-editor module, warning when every caller is guarded (latent). The "
       "reverse split (declared guarded, defined unguarded) compiles but ships dead code: warning.")
def check_editor_guard_free(tree):
    decls = free_functions(tree, tree.headers, want_def=False)
    defs = free_functions(tree, [c for c in tree.sources if not tree.always_editor(c)], want_def=True)
    out = []
    for key in sorted(set(decls) & set(defs)):
        ns, name = key
        for dg, dpath, dline in decls[key]:
            if tree.always_editor(dpath):
                continue
            for fg, fpath, fline in defs[key]:
                if dg == fg:
                    continue
                if dg and not fg:
                    out.append(Finding("editor-guard-free", "warn", fpath, fline,
                                       f"{ns + '::' if ns else ''}{name} is declared #if WITH_EDITOR "
                                       f"({os.path.basename(dpath)}:{dline}) but defined unguarded",
                                       "wrap the definition in #if WITH_EDITOR / #endif"))
                    continue
                callers = unguarded_callers(tree, name, {(dpath, dline), (fpath, fline)})
                sev = "error" if callers else "warn"
                where = (f"; unguarded call at {callers[0][0]}:{callers[0][1]}" if callers
                         else "; no unguarded caller yet (latent)")
                out.append(Finding("editor-guard-free", sev, dpath, dline,
                                   f"{ns + '::' if ns else ''}{name} is declared unguarded but defined "
                                   f"only #if WITH_EDITOR ({os.path.basename(fpath)}:{fline}){where}",
                                   "guard the declaration (and every call) with #if WITH_EDITOR"))
    return out


def unguarded_callers(tree, name, skip):
    call_re = re.compile(r'\b' + re.escape(name) + r'\s*\(')
    hits = []
    for p in tree.headers + tree.sources:
        if tree.always_editor(p):
            continue
        t = tree.stripped(p)
        if not call_re.search(t):
            continue
        lines = t.split("\n")
        guarded = editor_guard_map(lines)
        for i, ln in enumerate(lines):
            if guarded[i] or (p, i + 1) in skip or not call_re.search(ln):
                continue
            if ln.lstrip().startswith("#") or FREE_DECL_RE.match(ln) or FREE_DEF_RE.match(ln):
                continue
            hits.append((p, i + 1))
    return hits


# Bool contexts Clang diagnoses: a condition, an assertion macro argument, or a logical operand.
BOOL_OPEN = (r'(?:\bif\s*\(|\bwhile\s*\(|\b(?:check|checkf|ensure|ensureMsgf|ensureAlways|ensureAlwaysMsgf|'
             r'verify|verifyf)\s*\(|&&|\|\||!)\s*')
BOOL_CLOSE = r'\s*(?:\)|&&|\|\||\?|,)'
# Engine functions carrying FUNCTION_NON_NULL_RETURN_START (5.8 UObjectGlobals.h, Casts.h, Field.h).
NONNULL_CALLEE_RE = re.compile(r'\b(NewObject|CastChecked|CastFieldChecked|GetOwnerChecked)\s*<')
WALL_PATTERNS = [
    (re.compile(BOOL_OPEN + r'this' + BOOL_CLOSE),
     "'this' tested for null (-Wundefined-bool-conversion)", "drop the test; 'this' cannot be null"),
    (re.compile(BOOL_OPEN + r'&\s*[A-Za-z_][\w.]*(?:\[[^\]]*\])?' + BOOL_CLOSE),
     "address-of tested for null (-Wpointer-bool-conversion)", "an address is never null; test the value"),
    # Leading operand of a logical expression: 'return &X && ...', 'b = &X || ...', 'Foo(&X && ...)'.
    (re.compile(r'(?:^|[(=,]|\breturn\b)\s*&\s*[A-Za-z_][\w.]*(?:\[[^\]]*\])?\s*(?:&&|\|\|)', re.M),
     "address-of used as a logical operand (-Wpointer-bool-conversion)", "an address is never null; test the value"),
]


@check("clang-wall", "error",
       "A Clang default-on diagnostic that -Werror promotes and UBT does not silence: a null test on "
       "'this', on an address-of expression, on a FUNCTION_NON_NULL_RETURN callee (NewObject, "
       "CastChecked) or on a dereferenced TObjectIterator. MSVC says nothing; FAB's Clang build fails.")
def check_clang_wall(tree):
    out = []
    for p in tree.headers + tree.sources:
        t = tree.stripped(p)
        for rx, what, fix in WALL_PATTERNS:
            for m in rx.finditer(t):
                out.append(Finding("clang-wall", "error", p, t[:m.start()].count("\n") + 1, what, fix))
        opener = re.compile(BOOL_OPEN + r'$')
        for m in NONNULL_CALLEE_RE.finditer(t):
            if not opener.search(t[max(0, m.start() - 40):m.start()]):
                continue
            end = balanced_call_end(t, m.end())
            if end is not None and re.match(BOOL_CLOSE, t[end:]):
                out.append(Finding("clang-wall", "error", p, t[:m.start()].count("\n") + 1,
                                   f"{m.group(1)}<>() result tested for null (-Wpointer-bool-conversion)",
                                   "the callee is returns_nonnull; the test is always true"))
        for it in set(re.findall(r'\bTObjectIterator\s*<[^>]*>\s+([A-Za-z_]\w*)', t)):
            rx = re.compile(BOOL_OPEN + r'\*\s*' + re.escape(it) + BOOL_CLOSE)
            for m in rx.finditer(t):
                out.append(Finding("clang-wall", "error", p, t[:m.start()].count("\n") + 1,
                                   f"*{it} tested for null (-Wpointer-bool-conversion)",
                                   "TObjectIterator::operator* is returns_nonnull; test the iterator itself"))
    return out


def balanced_call_end(text, pos):
    """Index just past the ')' closing the call whose '<' or '(' starts at text[pos]."""
    depth, i, n = 0, pos, len(text)
    while i < n:
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        elif ch in ";{}":
            return None
        i += 1
    return None


CTOR_HDR_RE = re.compile(r'^\s*(?:explicit\s+|constexpr\s+|inline\s+|FORCEINLINE\s+)*([A-Za-z_]\w*)\s*\(')
CTOR_CPP_RE = re.compile(r'^[ \t]*(?:[A-Za-z_][\w:<>]*[ \t]+)?([A-Za-z_]\w*)::\1\s*\(', re.M)


def class_members(tree):
    """class name -> ordered direct data members, or None when two same-named classes disagree."""
    if hasattr(tree, "_members"):
        return tree._members
    seen = {}
    for h in tree.headers:
        lines = tree.stripped(h).split("\n")
        per = {}
        for i, ln, scope, _nested, rel in class_scopes(lines):
            if rel != 1:
                continue
            m = MEMBER_RE.match(ln)
            if m:
                per.setdefault(scope[0], []).append(m.group(1))
        for name, members in per.items():
            if name in seen and seen[name] != members:
                seen[name] = None
            else:
                seen.setdefault(name, members)
    tree._members = seen
    return seen


def reorder_finding(members, inits, path, line):
    idx = {m: k for k, m in enumerate(members)}
    order = [(idx[n], n) for n in inits if n in idx]
    for k in range(1, len(order)):
        prev = max(order[:k])
        if order[k][0] < prev[0]:
            return Finding("ctor-reorder", "error", path, line,
                           f"'{order[k][1]}' is initialized after '{prev[1]}' but declared before it "
                           "(-Wreorder-ctor)", "list initializers in declaration order")
    return None


@check("ctor-reorder", "error",
       "A constructor initializer list out of member declaration order. Clang's -Wreorder-ctor is in "
       "-Wall (error under -Werror); MSVC's equivalent C5038 is off at the level UBT uses, so the dev "
       "box never sees it.")
def check_ctor_reorder(tree):
    members = class_members(tree)
    out = []
    for h in tree.headers:
        t = tree.stripped(h)
        lines = t.split("\n")
        offsets, acc = [], 0
        for ln in lines:
            offsets.append(acc)
            acc += len(ln) + 1
        for i, ln, scope, _nested, rel in class_scopes(lines):
            m = CTOR_HDR_RE.match(ln)
            if rel != 1 or not m or m.group(1) != scope[0] or not members.get(scope[0]):
                continue
            inits = ctor_init_list(t, offsets[i] + m.start(1))
            f = reorder_finding(members[scope[0]], inits or [], h, i + 1)
            if f:
                out.append(f)
    for c in tree.sources:
        t = tree.stripped(c)
        for m in CTOR_CPP_RE.finditer(t):
            if not members.get(m.group(1)):
                continue
            inits = ctor_init_list(t, m.start(1))
            f = reorder_finding(members[m.group(1)], inits or [], c, t[:m.start()].count("\n") + 1)
            if f:
                out.append(f)
    return out


# Member-shaped declarations ONLY: a reference/pointer parameter, a TArray<T>& out-param,
# or a TArray<T>* member is fine with a forward declaration — flagging those invites
# "fixes" that create include cycles (BitmaskCommon <-> BitmaskDetails, 2026-08-25).
VALUE_MEMBER_RE = re.compile(r'^\s*(?:const\s+)?(FPCGEx\w+)\s+\w+\s*(?:=|;|\{)')
CONTAINER_MEMBER_RE = re.compile(r'^\s*(?:const\s+)?T(?:Array|Set)<\s*(FPCGEx\w+)\s*>\s+\w+\s*(?:=|;|\{)')


@check("value-member-include", "error",
       "A reflected header declares a BY-VALUE member (plain or container) of a project type while "
       "only a forward declaration is reachable through its includes. The shared PCH supplies the "
       "definition in normal builds; a no-PCH build fails with C3646 'unknown override specifier'.")
def check_value_member_include(tree):
    defines = {}
    for h in tree.headers:
        for sym in DEFINES_RE.findall(tree.stripped(h)):
            defines.setdefault(sym, set()).add(h)

    out = []
    for h in tree.headers:
        t = tree.stripped(h)
        if ".generated.h" not in t:
            continue
        cl = None
        for i, ln in enumerate(t.split("\n")):
            names = set()
            for rx in (VALUE_MEMBER_RE, CONTAINER_MEMBER_RE):
                m = rx.match(ln)
                if m:
                    names.add(m.group(1))
            for ty in sorted(names):
                owners = defines.get(ty)
                if not owners:
                    continue
                if cl is None:
                    cl = tree.closure(h)
                if owners & cl:
                    continue
                out.append(Finding("value-member-include", "error", h, i + 1,
                                   f"by-value use of '{ty}' with only a forward declaration in reach",
                                   f'add #include "{rel_include(tree, sorted(owners)[0])}"'))
    return out


# Logging macros whose expansion names a log category their own header never declares. PCGE_LOG and
# PCGE_LOG_C (PCGElement.h) expand to UE_LOG(LogPCG, ...); LogPCG is declared in PCGModule.h only.
# On the dev box a Unity neighbour or the shared PCH supplies it; a clean -NoPCH -DisableUnity build
# (FAB, or BuildPlugin -StrictIncludes) fails with C2065 / 'use of undeclared identifier LogPCG'
# (Valency 5.7, Windows and Mac, 2026-09-04). Suppliers: the owning header plus the engine headers
# verified against 5.7 PCG/Public to include it directly.
LOG_CATEGORY_USES = [
    (re.compile(r'\b(?:PCGE_LOG|PCGE_LOG_C)\s*\(|\bLogPCG\b'), "LogPCG", "PCGModule.h",
     ("PCGModule.h", "Metadata/PCGMetadataAttributeTpl.h", "Helpers/PCGSettingsHelpers.h",
      "Helpers/PCGPointDataPartition.h", "Data/PCGToolData.h")),
]


@check("log-category-include", "error",
       "A logging macro (PCGE_LOG, PCGE_LOG_C) or LogPCG is used while nothing in the file's include "
       "closure brings in PCGModule.h, which declares the category. Unity Build and the shared PCH "
       "supply it from a neighbour on the dev box; FAB's clean build reports C2065 'LogPCG'.")
def check_log_category_include(tree):
    table = []
    for rx, sym, owner, suppliers in LOG_CATEGORY_USES:
        alts = "|".join(re.escape(s) for s in suppliers)
        table.append((rx, sym, owner,
                      re.compile(r'^\s*#\s*include\s+["<](?:[^">]*/)?(?:' + alts + r')[">]', re.M)))
    out = []
    for p in tree.headers + tree.sources:
        t = tree.stripped(p)
        cl = None
        for rx, sym, owner, inc_re in table:
            m = rx.search(t)
            if not m:
                continue
            if cl is None:
                cl = tree.closure(p)
            if any(inc_re.search(tree.stripped(f)) for f in cl):
                continue
            out.append(Finding("log-category-include", "error", p, t[:m.start()].count("\n") + 1,
                               f"{sym} is needed by '{m.group(0).rstrip('( ')}' but \"{owner}\" is not "
                               "in the include closure",
                               f'add #include "{owner}"'))
    return out


# Global identifiers <MacTypes.h> (CoreServices, reached through any CoreFoundation / Cocoa include
# on Mac) already defines as typedefs or structs. A global-scope namespace, type, alias, function or
# macro with one of these names is "redefinition as a different kind of symbol" on Mac alone
# ('namespace Style = PCGExValencyWidgets::Style;', 5.7 Mac, 2026-09-04). Nested scopes are safe.
MAC_RESERVED = frozenset("""
    Boolean Byte SignedByte Ptr Handle Size Fixed Fract UnsignedFixed ShortFixed Float32 Float64
    Float80 Float96 Duration AbsoluteTime OptionBits ItemCount ByteCount ByteOffset LogicalAddress
    PhysicalAddress OSErr OSStatus OSType ResType FourCharCode ScriptCode LangCode RegionCode
    Str15 Str27 Str31 Str32 Str63 Str255 StringPtr StringHandle ConstStringPtr UniChar UniCharPtr
    UniCharCount Style StyleParameter StyleField TimeValue TimeScale TimeBase TimeRecord Point Rect
    FixedPoint FixedRect ProcessSerialNumber NumVersion VersRec WideChar
""".split())
MAC_GLOBAL_DECL_RE = re.compile(
    r'^\s*(?:namespace\s+([A-Za-z_]\w*)\s*(?:=|\{|$)'
    r'|(?:class|struct|union|enum(?:\s+(?:class|struct))?)\s+(?:[A-Z_0-9]+_API\s+)?([A-Za-z_]\w*)\s*(?:final\b|:|\{|;|$)'
    r'|using\s+([A-Za-z_]\w*)\s*='
    r'|typedef\s+[^;]*?\b([A-Za-z_]\w*)\s*;'
    r'|(?:[A-Z_0-9]+_API\s+)?(?:static\s+|inline\s+|extern\s+|constexpr\s+)*'
    r'[A-Za-z_][\w:<>,\s\*&]*?\s+\*?&?\s*([A-Za-z_]\w*)\s*\()')


@check("mac-reserved-global", "error",
       "A global-scope namespace, type, alias, function or macro named like a <MacTypes.h> typedef "
       "(Style, Point, Rect, Size, Handle, Ptr, Boolean, Duration, ...). Every Mac TU sees those typedefs "
       "through CoreFoundation, so the declaration is 'redefinition as a different kind of symbol' on "
       "Mac only. Windows and Linux compile it fine.")
def check_mac_reserved_global(tree):
    out = []
    for p in tree.headers + tree.sources:
        lines = tree.stripped(p).split("\n")
        depth = 0
        for i, ln in enumerate(lines):
            if ln.lstrip().startswith("#"):
                d = re.match(r'^\s*#\s*define\s+([A-Za-z_]\w*)', ln)
                if d and d.group(1) in MAC_RESERVED:
                    out.append(Finding("mac-reserved-global", "error", p, i + 1,
                                       f"macro '{d.group(1)}' rewrites the <MacTypes.h> typedef of the same name",
                                       "rename the macro"))
                continue
            if depth == 0:
                m = MAC_GLOBAL_DECL_RE.match(ln)
                if m:
                    name = next(g for g in m.groups() if g)
                    if name in MAC_RESERVED:
                        out.append(Finding("mac-reserved-global", "error", p, i + 1,
                                           f"global '{name}' collides with the <MacTypes.h> typedef of the same name",
                                           "rename it (only the global scope clashes; nested declarations are fine)"))
            depth += ln.count("{") - ln.count("}")
    return out


FUNCREF_ALIAS_RE = re.compile(r'\busing\s+([A-Za-z_]\w*)\s*=\s*TFunctionRef\s*<')


@check("functionref-dangling", "error",
       "A TFunctionRef local (or an alias of one) initialized from a lambda expression. TFunctionRef's "
       "constructor is UE_LIFETIMEBOUND (5.8 Templates/Function.h), so the lambda temporary it binds dies "
       "at the ';' and Clang reports -Wdangling (error under -Werror). MSVC says nothing.")
def check_functionref_dangling(tree):
    aliases = set()
    for p in tree.headers + tree.sources:
        aliases.update(FUNCREF_ALIAS_RE.findall(tree.stripped(p)))
    names = "|".join([r'TFunctionRef\s*<[^;=\n]*>'] + [re.escape(a) for a in sorted(aliases)])
    rx = re.compile(r'^\s*(?:const\s+)?(?:[A-Za-z_][\w:]*::)?(' + names + r')\s+([A-Za-z_]\w*)\s*(?:=|\(|\{)\s*\[',
                    re.M)
    out = []
    for p in tree.headers + tree.sources:
        t = tree.stripped(p)
        for m in rx.finditer(t):
            out.append(Finding("functionref-dangling", "error", p, t[:m.start()].count("\n") + 1,
                               f"'{m.group(2)}' ({m.group(1).split('<')[0].strip()}) is bound to a lambda temporary",
                               "store the lambda in an 'auto' local first, then bind the TFunctionRef to that"))
    return out


# Symbols CoreMinimal.h stopped supplying transitively in 5.8 (C7568 / C2065 on a clean build), with
# the header that owns them. Owner paths verified against the 5.8 engine tree.
IWYU_SYMBOLS = [
    ("GetObjectsWithOuter", "UObject/UObjectHash.h"),
    ("FArchiveObjectCrc32", "Serialization/ArchiveObjectCrc32.h"),
    ("FStructuredArchiveFromArchive", "Serialization/StructuredArchiveAdapters.h"),
    ("FPropertyEditorModule", "PropertyEditorModule.h"),
    ("TStaticArray", "Containers/StaticArray.h"),
]


@check("iwyu-symbol", "warn",
       "A symbol from the IWYU table is used but its owning header is nowhere in the file's include "
       "closure. 5.8 tightened CoreMinimal.h; what the editor build still gets transitively, a clean "
       "build may not (C7568 'assumed function template', C2065). Warn-level: another engine header in "
       "the closure can legitimately supply it; a direct include is the only guaranteed spelling.")
def check_iwyu_symbol(tree):
    table = [(sym, hdr, re.compile(r'\b' + sym + r'\b'),
              re.compile(r'^\s*#\s*include\s+["<](?:[^">]*/)?' + re.escape(hdr) + r'[">]', re.M))
             for sym, hdr in IWYU_SYMBOLS]
    out = []
    for p in tree.headers + tree.sources:
        t = tree.stripped(p)
        cl = None
        for sym, hdr, sym_re, inc_re in table:
            m = sym_re.search(t)
            if not m:
                continue
            if cl is None:
                cl = tree.closure(p)
            if any(inc_re.search(tree.stripped(f)) for f in cl):
                continue
            out.append(Finding("iwyu-symbol", "warn", p, t[:m.start()].count("\n") + 1,
                               f'{sym} is used but "{hdr}" is not in the include closure',
                               f'add #include "{hdr}"'))
    return out


USTRUCT_RE = re.compile(
    r'^\s*USTRUCT\b[^\n]*\n(?:[^\n]*\n){0,2}?\s*struct\s+(?:[A-Z_0-9]+_API\s+)?([A-Za-z_]\w*)'
    r'(?:\s*(?:final\s*)?:\s*(?:public\s+)?([A-Za-z_]\w*))?', re.M)
UPROP_RE = re.compile(r'^\s*UPROPERTY\s*\(([^\n]*)$', re.M)


@check("instanced-in-instancedstruct", "warn",
       "A USTRUCT with an Instanced UPROPERTY that is only ever held through FInstancedStruct / "
       "TInstancedStruct. UStruct::InstanceSubobjectTemplates (5.8 Class.cpp) descends only properties "
       "that ContainsInstancedObjectProperty(); FInstancedStruct has no reflected members, so the flag "
       "never propagates through it and instances built from a template share the subobject instead of "
       "getting their own. Warn-level: dynamic subobject instancing on the owner class would still cover it.")
def check_instanced_in_instancedstruct(tree):
    ustruct, children, holders, wrapped, inst = {}, {}, set(), set(), {}
    wrap_re = re.compile(r'\b(?:TInstancedStruct|InitializeAs|Make|MakeInstancedStruct|Emplace)\s*<\s*([A-Za-z_]\w*)\s*>'
                         r'|BaseStruct\s*=\s*"[^"]*?([A-Za-z_]\w*)"')
    for h in tree.headers:
        t = tree.stripped(h)
        for m in USTRUCT_RE.finditer(t):
            ustruct[m.group(1)] = (h, t[:m.start()].count("\n") + 1)
            if m.group(2):
                children.setdefault(m.group(2), set()).add(m.group(1))
        lines = t.split("\n")
        for m in UPROP_RE.finditer(t):
            i = t[:m.start()].count("\n")
            member = next((x for x in lines[i + 1:i + 4] if x.strip()), "")
            if "InstancedStruct" not in member:
                holders.update(re.findall(r'\b([A-Za-z_]\w*)\b', member.split("=")[0]))
        for a, b in wrap_re.findall(t):
            wrapped.add(a or b)
        for i, ln, scope, _nested, rel in class_scopes(lines):
            if rel == 1 and ln.lstrip().startswith("UPROPERTY") and re.search(r'\bInstanced\b', ln):
                inst[scope[0]] = i + 1

    def held(name, seen=frozenset()):
        if name in holders:
            return True
        return any(held(c, seen | {name}) for c in children.get(name, ()) if c not in seen)

    out = []
    for name in sorted(inst):
        if name not in ustruct or held(name) or name not in wrapped:
            continue
        h, line = ustruct[name]
        out.append(Finding("instanced-in-instancedstruct", "warn", h, line,
                           f"USTRUCT {name} has an Instanced UPROPERTY (line {inst[name]}) but is only "
                           "reached through FInstancedStruct",
                           "hold it in a reflected UPROPERTY, or give the owner its own subobject handling"))
    return out


DEPRECATED_MEMBER_RE = re.compile(
    r'^\s*(?:mutable\s+)?[A-Za-z_][\w:<>,\s\*&]*?\s+\*?\s*([A-Za-z_]\w*_DEPRECATED)\s*(?:\[[^\]]*\]\s*)?(?:;|=[^=]|\{)')
DEPRECATED_TOKEN_RE = re.compile(r'\b([A-Za-z_]\w*_DEPRECATED)\b')


@check("deprecated-unconsumed", "warn",
       "A *_DEPRECATED member with no reference anywhere outside its own declaration. A deprecated "
       "property exists to be migrated in PostLoad / OnHostPostLoad; one nobody reads is either a "
       "forgotten migration (data silently dropped on re-save) or dead weight to delete.")
def check_deprecated_unconsumed(tree):
    decls, refs, pasted = {}, {}, []
    for p in tree.headers + tree.sources:
        lines = tree.stripped(p).split("\n")
        for i, ln in enumerate(lines):
            d = DEPRECATED_MEMBER_RE.match(ln)
            if d and p.endswith(HDR_EXT):
                decls.setdefault(d.group(1), (p, i + 1))
                continue
            for tok in DEPRECATED_TOKEN_RE.findall(ln):
                refs[tok] = refs.get(tok, 0) + 1
        # A migration macro builds the name by token pasting ('Radius##_AXIS##Input_DEPRECATED');
        # each macro parameter becomes a wildcard.
        for _i, ln in logical_lines(lines):
            m = re.match(r'^\s*#\s*define\s+\w+\s*\(([^)]*)\)', ln)
            if not m or "##" not in ln:
                continue
            params = {x.strip() for x in m.group(1).split(",")}
            for tok in re.findall(r'[\w#]*##[\w#]*', ln):
                if "_DEPRECATED" in tok:
                    pasted.append(re.compile("^" + "".join(
                        ".*" if part in params else re.escape(part) for part in tok.split("##")) + "$"))
    out = []
    for name, (p, line) in sorted(decls.items(), key=lambda kv: kv[1]):
        if refs.get(name) or any(rx.match(name) for rx in pasted):
            continue
        out.append(Finding("deprecated-unconsumed", "warn", p, line,
                           f"{name} is never read", "migrate it in PostLoad, or delete it"))
    return out


# ----------------------------------------------------------------------- selftest

SELFTEST = {
    "Test.uplugin": (
        '{"Modules":[\n'
        '{"Name":"ModA","Type":"Runtime"},\n'
        '{"Name":"ModB","Type":"Runtime"},\n'
        '{"Name":"ModEd","Type":"Editor"}\n'
        ']}\n'),
    "ModEd/ModEd.Build.cs": "// module marker\n",
    "ModEd/Public/EditorOnlyThing.h": (
        "#pragma once\n"
        "struct FPCGExElsewhere\n"
        "{\n"
        "\tint V = 0;\n"
        "};\n"),
    "ModA/ModA.Build.cs": "// module marker\n",
    "ModA/Public/PCGExBase.h": (
        "#pragma once\n"
        "#define PCGEX_NODE_COLOR_NAME(_C) _C\n"
        "class MODA_API UPCGExSettings : public UPCGSettings\n"
        "{\n"
        "};\n"
        "template <typename T> class MODA_API TExported\n"
        "{\n"
        "\tT V;\n"
        "};\n"
        "struct MODA_API FPCGExMisc\n"
        "{\n"
        "\tint V = 0;\n"
        "};\n"
        "extern template class TExported<int>;\n"),
    "ModB/ModB.Build.cs": "// module marker\n",
    "ModB/Public/PCGExBad.h": (
        "#pragma once\n"
        "#include \"PCGExBad.generated.h\"\n"
        "#include \"Extra.h\"\n"
        "class UPCGExBadSettings : public UPCGExSettings\n"
        "{\n"
        "\tvirtual FLinearColor GetNodeTitleColor() const { return PCGEX_NODE_COLOR_NAME(X); }\n"
        "#if WITH_EDITOR\n"
        "\tvirtual void EditorOnly() const;\n"
        "#endif\n"
        "\tstruct FOpts\n"
        "\t{\n"
        "\t\tbool bFlag = false;\n"
        "\t};\n"
        "\tstatic void Draw(int A, const FOpts& O = FOpts());\n"
        "\tFPCGExMisc MiscValue;\n"
        "};\n"),
    "ModB/Public/Extra.h": "#pragma once\n",
    "ModB/Private/PCGExBad.cpp": (
        "#include \"PCGExBad.h\"\n"
        "#include \"pcgexbase.h\"\n"
        "#include \"EditorOnlyThing.h\"\n"
        "namespace\n"
        "{\n"
        "\tint Helper() { return 0; }\n"
        "}\n"
        "static int OtherHelper() { return 2; }\n"
        "__forceinline int Fast() { return 1; }\n"
        "void UPCGExBadSettings::EditorOnly() const\n"
        "{\n"
        "\tGetClass()->GetOutermost()->GetFName();\n"
        "}\n"),
    # subclassof-incomplete: UPCGExSettings lives in ModA/PCGExBase.h, which is not included.
    "ModB/Public/PCGExIncomplete.h": (
        "#pragma once\n"
        "#include \"Templates/SubclassOf.h\"\n"
        "class UPCGExSettings;\n"
        "struct FPCGExIncompleteHolder\n"
        "{\n"
        "\tTSubclassOf<UPCGExSettings> SettingsClass;\n"
        "\tbool IsSet() const { return SettingsClass.Get() != nullptr; }\n"
        "};\n"),
    # editor-guard-free: Register is declared unguarded / defined guarded, with an unguarded caller
    # (error); Unregister is declared guarded / defined unguarded (warn).
    "ModB/Public/PCGExFree.h": (
        "#pragma once\n"
        "namespace PCGExFree\n"
        "{\n"
        "\tMODB_API void Register();\n"
        "#if WITH_EDITOR\n"
        "\tMODB_API void Unregister();\n"
        "#endif\n"
        "}\n"),
    "ModB/Private/PCGExFree.cpp": (
        "#include \"PCGExFree.h\"\n"
        "void PCGExFreeBoot()\n"
        "{\n"
        "\tPCGExFree::Register();\n"
        "}\n"
        "#if WITH_EDITOR\n"
        "namespace PCGExFree\n"
        "{\n"
        "\tvoid Register()\n"
        "\t{\n"
        "\t}\n"
        "}\n"
        "#endif\n"
        "namespace PCGExFree\n"
        "{\n"
        "\tvoid Unregister()\n"
        "\t{\n"
        "\t}\n"
        "}\n"),
    # clang-wall + ctor-reorder.
    "ModB/Public/PCGExWall.h": (
        "#pragma once\n"
        "#include \"UObject/UObjectIterator.h\"\n"
        "struct FPCGExWall\n"
        "{\n"
        "\tint32 A = 0;\n"
        "\tint32 B = 0;\n"
        "\tFPCGExWall() : B(1), A(2)\n"
        "\t{\n"
        "\t}\n"
        "\tbool Check(const FPCGExWall& Other) const\n"
        "\t{\n"
        "\t\tif (this) { return true; }\n"
        "\t\tif (&Other) { return false; }\n"
        "\t\treturn &Other && Other.A > 0;\n"
        "\t}\n"
        "};\n"),
    "ModB/Private/PCGExWall.cpp": (
        "#include \"PCGExWall.h\"\n"
        "void PCGExWallScan()\n"
        "{\n"
        "\tfor (TObjectIterator<UObject> It; It; ++It)\n"
        "\t{\n"
        "\t\tif (*It) { }\n"
        "\t}\n"
        "\tif (!NewObject<UObject>()) { }\n"
        "\tcheck(CastChecked<UObject>(nullptr));\n"
        "}\n"),
    # iwyu-symbol.
    "ModB/Private/PCGExIwyu.cpp": (
        "#include \"PCGExWall.h\"\n"
        "void PCGExIwyuScan(UObject* Outer)\n"
        "{\n"
        "\tTArray<UObject*> Out;\n"
        "\tGetObjectsWithOuter(Outer, Out);\n"
        "}\n"),
    # log-category-include: PCGE_LOG_C with no PCGModule.h anywhere in reach.
    "ModB/Private/PCGExLog.cpp": (
        "#include \"PCGExBase.h\"\n"
        "void PCGExLogScan(FPCGContext* Ctx)\n"
        "{\n"
        "\tPCGE_LOG_C(Error, GraphAndLog, Ctx, FTEXT(\"no rules\"));\n"
        "}\n"),
    # mac-reserved-global: one finding per declaration shape (namespace alias, type, function).
    "ModB/Private/PCGExMac.cpp": (
        "#include \"PCGExBase.h\"\n"
        "namespace Style = PCGExNegative;\n"
        "struct Point\n"
        "{\n"
        "\tint X = 0;\n"
        "};\n"
        "int Size();\n"),
    # functionref-dangling: through an alias and through the spelled-out template.
    "ModB/Private/PCGExDangling.cpp": (
        "#include \"PCGExBase.h\"\n"
        "using FPCGExVisitor = TFunctionRef<void(int)>;\n"
        "void PCGExDanglingScan()\n"
        "{\n"
        "\tFPCGExVisitor V = [](int) {};\n"
        "\tTFunctionRef<void(int)> W = [](int) {};\n"
        "\tV(1);\n"
        "\tW(2);\n"
        "}\n"),
    # instanced-in-instancedstruct + deprecated-unconsumed.
    "ModB/Public/PCGExPayload.h": (
        "#pragma once\n"
        "#include \"PCGExPayload.generated.h\"\n"
        "USTRUCT()\n"
        "struct FPCGExPayload\n"
        "{\n"
        "\tGENERATED_BODY()\n"
        "\tUPROPERTY(Instanced)\n"
        "\tTObjectPtr<UObject> Sub;\n"
        "\tUPROPERTY()\n"
        "\tint32 Legacy_DEPRECATED = 0;\n"
        "};\n"
        "UCLASS()\n"
        "class UPCGExPayloadHost : public UObject\n"
        "{\n"
        "\tGENERATED_BODY()\n"
        "\tUPROPERTY()\n"
        "\tTInstancedStruct<FPCGExPayload> Payload;\n"
        "};\n"),

    # Everything below is CORRECT code. Any finding pointing at a "Negative" path means a
    # detector over-fires. These mirror the real false positives this tool has produced.
    "ModB/Public/PCGExNegative.h": (
        "#pragma once\n"
        "#include \"PCGExBase.h\"\n"
        "#include \"UObject/Package.h\"\n"
        "#include \"Templates/SubclassOf.h\"\n"
        "#include \"PCGExNegative.generated.h\"\n"
        "namespace PCGExNegative\n"
        "{\n"
        "\tinline const FName SlotId = FName(TEXT(\"Slot\"));\n"
        "\tMODB_API void Plain();\n"
        "#if WITH_EDITOR\n"
        "\tMODB_API void Guarded();\n"
        "\tMODB_API void GuardedMultiLine(int A,\n"
        "\t                               int B);\n"
        "#endif\n"
        "\tnamespace Style\n"
        "\t{\n"
        "\t\tinline int Pad() { return 4; }\n"
        "\t}\n"
        "}\n"
        "class UPCGExNegativeSettings : public UPCGExSettings\n"
        "{\n"
        "\tvirtual FLinearColor GetNodeTitleColor() const { return PCGEX_NODE_COLOR_NAME(X); }\n"
        "#if WITH_EDITOR\n"
        "\tvirtual void EditorOnly() const;\n"
        "#endif\n"
        "\tstruct FOpts\n"
        "\t{\n"
        "\t\tbool bFlag = false;\n"
        "\t};\n"
        "\tFOpts Defaults = FOpts();\n"
        "\tFPCGExMisc NegMisc;\n"
        "\tTArray<FPCGExMisc> NegMiscArr;\n"
        "\tTArray<FPCGExElsewhere>* ElsewherePtr = nullptr;\n"
        "\tstatic void DrawMany(const TArray<FPCGExElsewhere>& In);\n"
        "\tstatic void Draw(int A, const FOpts& O);\n"
        "\tTSubclassOf<UPCGExSettings> SettingsClass;\n"
        "\tbool IsSet() const { return SettingsClass.Get() != nullptr; }\n"
        "\tint32 A = 0;\n"
        "\tTMap<FName, int32> Lookup;\n"
        "\tint32 B = 0;\n"
        "\tint32 NegLegacy_DEPRECATED = 0;\n"
        "\tint32 NegPasteXInput_DEPRECATED = 0;\n"
        "\tint32 Style = 0;\n"
        "\tUPCGExNegativeSettings() : A(1), Lookup(TMap<FName, int32>()), B(2)\n"
        "\t{\n"
        "\t}\n"
        "\tbool Same(const UPCGExNegativeSettings& Other) const\n"
        "\t{\n"
        "\t\tif (&Other == this) { return true; }\n"
        "\t\tUObject* O = NewObject<UObject>();\n"
        "\t\treturn O && Other.A > 0 && !Lookup.IsEmpty();\n"
        "\t}\n"
        "};\n"
        "USTRUCT()\n"
        "struct FPCGExNegPayload\n"
        "{\n"
        "\tGENERATED_BODY()\n"
        "\tUPROPERTY(Instanced)\n"
        "\tTObjectPtr<UObject> Sub;\n"
        "};\n"
        "UCLASS()\n"
        "class UPCGExNegHost : public UObject\n"
        "{\n"
        "\tGENERATED_BODY()\n"
        "\tUPROPERTY()\n"
        "\tFPCGExNegPayload Direct;\n"
        "\tUPROPERTY()\n"
        "\tTInstancedStruct<FPCGExNegPayload> Wrapped;\n"
        "};\n"
        "template <typename T> class MODB_API TNegExported\n"
        "{\n"
        "\tT V;\n"
        "};\n"
        "#define PCGEX_TPL_NEG(_T) extern template class TNegExported<_T>;\n"
        "\t//PCGEX_FOREACH(PCGEX_TPL_NEG)\n"
        "#undef PCGEX_TPL_NEG\n"
        "#define PCGEX_TPL_NEG(_T) \\\n"
        "\textern template void TNegExported<_T>::Set(const _T&);\n"
        "\tPCGEX_FOREACH(PCGEX_TPL_NEG)\n"
        "#undef PCGEX_TPL_NEG\n"),
    # A forward-declared T that is only stored, never dereferenced, is legal.
    "ModB/Public/PCGExNegativeStore.h": (
        "#pragma once\n"
        "#include \"Templates/SubclassOf.h\"\n"
        "class UPCGExSettings;\n"
        "struct FPCGExNegativeStore\n"
        "{\n"
        "\tTSubclassOf<UPCGExSettings> SettingsClass;\n"
        "\tvoid Set(TSubclassOf<UPCGExSettings> InClass) { SettingsClass = InClass; }\n"
        "};\n"),
    "ModB/Private/PCGExNegative.cpp": (
        "#include \"PCGExNegative.h\"\n"
        "#include \"PCGModule.h\"\n"
        "#include \"UObject/UObjectHash.h\"\n"
        "#if WITH_EDITOR\n"
        "#include \"EditorOnlyThing.h\"\n"
        "#endif\n"
        "namespace PCGExNegative\n"
        "{\n"
        "\tint Helper() { return 0; }\n"
        "\tvoid Plain()\n"
        "\t{\n"
        "\t\tTArray<UObject*> Out;\n"
        "\t\tGetObjectsWithOuter(nullptr, Out);\n"
        "\t\tPCGE_LOG_C(Error, GraphAndLog, nullptr, FTEXT(\"covered\"));\n"
        "\t\tauto NegFn = [](int) {};\n"
        "\t\tTFunctionRef<void(int)> NegRef = NegFn;\n"
        "\t\tNegRef(Style::Pad());\n"
        "\t}\n"
        "\tFAutoConsoleCommand Cmd(TEXT(\"pcgex.Neg\"), TEXT(\"help\"), FConsoleCommandDelegate());\n"
        "}\n"
        "#if WITH_EDITOR\n"
        "namespace PCGExNegative\n"
        "{\n"
        "\tvoid Guarded()\n"
        "\t{\n"
        "\t}\n"
        "\tvoid GuardedMultiLine(int A,\n"
        "\t                      int B)\n"
        "\t{\n"
        "\t}\n"
        "}\n"
        "#define NEG_MIGRATE(_AXIS) \\\n"
        "\tNegPaste##_AXIS##Input_DEPRECATED = 0;\n"
        "void UPCGExNegativeSettings::EditorOnly() const\n"
        "{\n"
        "\tGetClass()->GetOutermost()->GetFName();\n"
        "\tNegLegacy_DEPRECATED = 0;\n"
        "\tNEG_MIGRATE(X)\n"
        "\tPCGExNegative::Guarded();\n"
        "}\n"
        "#undef NEG_MIGRATE\n"
        "#endif\n"),
}

# check -> minimum findings on the defect fixtures. A multi-pattern detector must fire once per
# pattern; "fires at all" would let a single dead pattern hide behind its siblings.
SELFTEST_EXPECT = {
    "missing-include": 1, "editor-guard": 1, "nsdmi-default-arg": 1, "extern-template-api": 1,
    "unity-collision": 2, "msvc-only": 1, "include-case": 1, "generated-last": 1, "fwd-decl-deref": 1,
    "subclassof-incomplete": 1, "editor-guard-free": 2, "clang-wall": 6, "ctor-reorder": 1,
    "iwyu-symbol": 1, "instanced-in-instancedstruct": 1, "deprecated-unconsumed": 1,
    "value-member-include": 1, "log-category-include": 1, "mac-reserved-global": 3,
    "functionref-dangling": 2,
}


def run_selftest():
    tmp = tempfile.mkdtemp(prefix="fabpreflight-")
    try:
        for rel, content in SELFTEST.items():
            dst = os.path.join(tmp, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "w", encoding="utf-8") as fh:
                fh.write(content)
        tree = Tree(tmp.replace("\\", "/"))
        found = {name: CHECKS[name](tree) for name in sorted(CHECKS)}
        over = [f for fs in found.values() for f in fs if "Negative" in f.path]
        unlisted = sorted(set(CHECKS) - set(SELFTEST_EXPECT))

        missed = []
        for name, want in sorted(SELFTEST_EXPECT.items()):
            got = sum(1 for f in found.get(name, ()) if "Negative" not in f.path)
            ok = got >= want
            print(f"  {'ok  ' if ok else 'MISS'}  {name} ({got}/{want})")
            if not ok:
                missed.append(name)
        if missed:
            print(f"\n{len(missed)} detector(s) fired fewer times than their fixtures demand: "
                  f"{', '.join(missed)}")
        if unlisted:
            print(f"\ncheck(s) with no selftest expectation: {', '.join(unlisted)}")
        for f in over:
            print(f"\nover-fired on correct code: {f.check}: {f.message}\n"
                  f"    {os.path.basename(f.path)}:{f.line}")
        if missed or over or unlisted:
            return 1
        print(f"\nall {len(SELFTEST_EXPECT)} detectors fire on their own case, "
              f"none fire on the correct-code fixtures")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ------------------------------------------------------------------- mirror / drift

def script_mirrors(explicit):
    """Other copies of this script: the same path relative to the workspace, in sibling workspaces.

    Layout assumed: <parent>/<workspace>/Plugins/<plugin>/Scripts/fab-preflight.py.
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


DIFF_EXTS = HDR_EXT + (".cpp", ".cs", ".uplugin")
# Relative-path substrings that are expected to differ between the 5.7 and 5.8 trees.
DIFF_TREES_DEFAULT_IGNORE = (
    "PCGExSkinnedMeshCollection",   # skinned-mesh collection is 5.8-only (no 5.7 counterpart)
)


def run_diff_trees(a, b, ignore):
    a, b = (os.path.abspath(x).replace("\\", "/").rstrip("/") for x in (a, b))
    for root in (a, b):
        if not os.path.isdir(root):
            print(f"not a directory: {root}", file=sys.stderr)
            return 2
    patterns = tuple(p.lower() for p in DIFF_TREES_DEFAULT_IGNORE + tuple(ignore))

    def index(root):
        out = {}
        for p in walk(root, DIFF_EXTS):
            rel = p[len(root):].lstrip("/")
            if not any(pat in rel.lower() for pat in patterns):
                out[rel] = p
        return out

    ia, ib = index(a), index(b)
    only_a, only_b = sorted(set(ia) - set(ib)), sorted(set(ib) - set(ia))
    differ, cosmetic = [], []
    for rel in sorted(set(ia) & set(ib)):
        with open(ia[rel], "rb") as fa, open(ib[rel], "rb") as fb:
            ra, rb = fa.read(), fb.read()
        if ra == rb:
            continue
        if read(ia[rel]).replace("\r\n", "\n") == read(ib[rel]).replace("\r\n", "\n"):
            cosmetic.append(rel)     # BOM or line endings only; MSVC decodes the two differently
        else:
            differ.append(rel)
    for title, items in (("only in A", only_a), ("only in B", only_b), ("content differs", differ),
                         ("BOM / line-ending only", cosmetic)):
        print(f"{title}: {len(items)}")
        for rel in items:
            print(f"    {rel}")
    print(f"\nA = {a}\nB = {b}\n{len(ia)} vs {len(ib)} files compared, "
          f"{len(patterns)} ignore pattern(s)")
    return 1 if only_a or only_b or differ else 0


# --------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description="FAB pre-flight static checks")
    ap.add_argument("root", nargs="?", default=None,
                    help="source root (default: the plugin this script lives in)")
    ap.add_argument("--only", action="append", metavar="CHECK", help="run just this check")
    ap.add_argument("--list", action="store_true", help="describe every check")
    ap.add_argument("--selftest", action="store_true", help="prove every detector still fires")
    ap.add_argument("--check-mirror", nargs="?", const="", default=None, metavar="PATH",
                    help="fail unless every mirror copy of this script (PATH, or auto-discovered in "
                         "sibling workspaces) is byte-identical")
    ap.add_argument("--diff-trees", nargs=2, metavar=("A", "B"),
                    help="list source files that differ between two plugin trees (port drift)")
    ap.add_argument("--ignore", action="append", default=[], metavar="PATTERN",
                    help="--diff-trees: also skip relative paths containing PATTERN (repeatable)")
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

    if args.diff_trees:
        return run_diff_trees(args.diff_trees[0], args.diff_trees[1], args.ignore)

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
        print(f"{root}\n{len(tree.headers)} headers, {len(tree.sources)} sources, "
              f"{len(tree.module_dir)} modules\n")

    findings, errors = [], 0
    for name in selected:
        found = CHECKS[name](tree)
        findings.extend(found)
        errors += sum(1 for f in found if f.severity == "error")

    for f in sorted(findings, key=lambda x: (x.severity != "error", x.check, x.path, x.line)):
        where = f"{f.path}:{f.line}" if f.line else f.path
        print(f"[{f.severity}] {f.check}: {f.message}\n    {where}"
              + (f"\n    -> {f.detail}" if f.detail else ""))

    if not args.quiet:
        warns = len(findings) - errors
        print(f"\n{errors} error(s), {warns} warning(s) across {len(selected)} check(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
