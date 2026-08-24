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

Exit code is 1 if any error-severity finding is reported, so CI can gate on it.

A detector that silently stops matching is worse than no detector -- it reports "clean" over a
real defect. --selftest builds a throwaway tree containing one instance of every defect and
fails if any check misses its own case. Run it whenever you touch a pattern in this file.
"""

import argparse
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
    """Yield (line_index, line, enclosing_class, nested_types) walking a header's class bodies.

    nested_types maps an already-closed inner type name -> whether it has default member
    initializers. A class body is only entered once its '{' is seen; the brace commonly sits
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
            yield i, ln, stack[-1], nested.get(stack[-1][0], {})
        o, c = ln.count("{"), ln.count("}")
        if stack and not stack[-1][3] and o > 0:
            stack[-1][3] = True
        depth += o - c
        while stack and stack[-1][3] and depth <= stack[-1][1]:
            done = stack.pop()
            if stack:
                nested.setdefault(stack[-1][0], {})[done[0]] = done[2]


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
        for i, ln, scope, _nested in class_scopes(lines):
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
        for i, ln, scope, nested in class_scopes(lines):
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


# ----------------------------------------------------------------------- selftest

SELFTEST = {
    "Test.uplugin": (
        '{"Modules":[\n'
        '{"Name":"ModA","Type":"Runtime"},\n'
        '{"Name":"ModB","Type":"Runtime"},\n'
        '{"Name":"ModEd","Type":"Editor"}\n'
        ']}\n'),
    "ModEd/ModEd.Build.cs": "// module marker\n",
    "ModEd/Public/EditorOnlyThing.h": "#pragma once\n",
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

    # Everything below is CORRECT code. Any finding pointing at a "Negative" path means a
    # detector over-fires. These mirror the real false positives this tool has produced.
    "ModB/Public/PCGExNegative.h": (
        "#pragma once\n"
        "#include \"PCGExBase.h\"\n"
        "#include \"UObject/Package.h\"\n"
        "#include \"PCGExNegative.generated.h\"\n"
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
        "\tstatic void Draw(int A, const FOpts& O);\n"
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
    "ModB/Private/PCGExNegative.cpp": (
        "#include \"PCGExNegative.h\"\n"
        "#if WITH_EDITOR\n"
        "#include \"EditorOnlyThing.h\"\n"
        "#endif\n"
        "namespace PCGExNegative\n"
        "{\n"
        "\tint Helper() { return 0; }\n"
        "}\n"
        "#if WITH_EDITOR\n"
        "void UPCGExNegativeSettings::EditorOnly() const\n"
        "{\n"
        "\tGetClass()->GetOutermost()->GetFName();\n"
        "}\n"
        "#endif\n"),
}

SELFTEST_EXPECT = {
    "missing-include", "editor-guard", "nsdmi-default-arg", "extern-template-api",
    "unity-collision", "msvc-only", "include-case", "generated-last", "fwd-decl-deref",
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
        fired = {n for n, fs in found.items() if fs}
        over = [f for fs in found.values() for f in fs if "Negative" in f.path]

        for name in sorted(SELFTEST_EXPECT):
            print(f"  {'ok  ' if name in fired else 'MISS'}  {name}")
        missed = SELFTEST_EXPECT - fired
        if missed:
            print(f"\n{len(missed)} detector(s) failed to fire: {', '.join(sorted(missed))}")
        for f in over:
            print(f"\nover-fired on correct code: {f.check}: {f.message}\n"
                  f"    {os.path.basename(f.path)}:{f.line}")
        if missed or over:
            return 1
        print(f"\nall {len(SELFTEST_EXPECT)} detectors fire on their own case, "
              f"none fire on the correct-code fixtures")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# --------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description="FAB pre-flight static checks")
    ap.add_argument("root", nargs="?", default=None,
                    help="source root (default: the plugin this script lives in)")
    ap.add_argument("--only", action="append", metavar="CHECK", help="run just this check")
    ap.add_argument("--list", action="store_true", help="describe every check")
    ap.add_argument("--selftest", action="store_true", help="prove every detector still fires")
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
