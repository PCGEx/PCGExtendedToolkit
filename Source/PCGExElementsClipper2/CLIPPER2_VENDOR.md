# Vendored Clipper2 -- provenance and local changes

`Public/Clipper2Lib/` + `Private/Clipper2Lib/` are a modified copy of Clipper2 (Boost license, see
`CLIPPER2_LICENSE`). **Never re-vendor over these files**: several local edits below are behaviour PCGEx relies on.
Port upstream changes by hand, hunk by hunk, and record them here. Bump `PCGEX_CLIPPER2_PATCH`
(`clipper.version.h`) with every change to the library.

The tree is also reformatted (braces, `using` instead of `typedef`, `constexpr`, `_v` traits), so a plain diff against
upstream is noise; compare token streams with comments and whitespace stripped.

## Base

- Upstream: AngusJohnson/Clipper2 `21ebba0` (release 2.0.1, 2025-12-20). That repository was archived on 2026-10-28.
- Successor: PolyClipping/Clipper2 (C++ only). It dropped the triangulation module; ours is PCGEx-maintained.

## Upstream / fork changes taken (patch 1)

| Source | Change |
|--------|--------|
| upstream `4da1564` (#1067) | `FixSelfIntersects`: no micro self-intersection branch; always `DoSplitOp` (Union of slivers returned phantom area). `AddPaths_` local renamed `allVertices`. |
| upstream `6a36be1` | Triangulation `SegsIntersect` endpoint test, `int ec = 0` (hand-ported earlier). |
| upstream `458dd85`, `46f6391` | Header hygiene: no stray `;` after member bodies, `delta == 0.0` instead of `!delta`. |
| fork `f97d65d` (#5) | `CheckSplitOwner`: `recursive_split` guard on emptied splits (infinite recursion, stack overflow). |
| fork `9b460cb` (#3) | Skip the intersection merge sort when the AEL is still sorted (`curr_x_at_top_`). |
| fork `7813a71`, `b496b45` (#2) | Local minima no longer queued as scanlines; `PopScanline` merges them via `PeekLocMinY`. |

Not taken: upstream `4da1564`'s triangulation `throw` -> `DoError` (a no-op without exceptions; see PCGEx fixes).

## PCGEx fixes with no upstream equivalent (patch 1)

- Offset (fork #9, open upstream): `DoGroupOffset` skips empty paths and resets `end_type_` per path, so a
  2-point `Joined` path no longer turns the paths after it into open paths.
- Triangulation (fork #6, #8; dropped upstream): `Delaunay` carries a `failed` flag. `AddPath`'s walk is bounded to
  one lap; a missing edge, a malformed triangle pair or a non-closing walk now return `TriangulateResult::fail`
  instead of hanging, asserting or running into undefined behaviour.

## Standing local edits

| Edit | Where |
|------|-------|
| Namespace `PCGExClipper2Lib`; include guards `PCGEX_CLIPPER_H_*` | all |
| `USINGZ` always on (`Point64` is x, y, z) | all |
| `UE_PI` instead of `PI`; `Math/UnrealMathUtility.h` | `clipper.h`, `clipper.offset.cpp` |
| `PCGEX_CLIPPER2_HI_PRECISION` (0), its min/max macros renamed and `#undef`'d | `clipper.core.h` |
| **Intersection Z** = nearer endpoint of the first segment (upstream writes 0) | `GetLineIntersectPt`, `clipper.core.h` |
| **`TriangulateWithHoles`, `TriangulatePolyTree`, `TriangulatePolyTreeD`** (+ `ZCallback64` parameter) | `clipper.triangulation.*` |
| `DoError` never throws; no `Clipper2Exception`; `CLIPPER2_MAX_DEC_PRECISION` fixed at 8 | `clipper.core.h` |
| `CLIPPER2_VERSION` / `PCGEX_CLIPPER2_PATCH` inside the namespace | `clipper.version.h` |
| `#undef` of the MSVC SSE `fmin`/`fmax`/`nearbyint` overrides at end of file (unity builds) | `clipper.engine.cpp` |
| Unreachable `break` after `return` removed in `IsContributingClosed` | `clipper.engine.cpp` |
| `clipper.minkowski.h` includes `clipper.engine.h` | `clipper.minkowski.h` |
| Upstream's C-ABI `clipper.export.h` removed | -- |

## Export (`PCGEXELEMENTSCLIPPER2_API`)

Class-level on `ReuseableDataContainer64`, `ClipperBase`, `Clipper64`, `ClipperD`, `ClipperOffset`, `RectClip64`,
`RectClipLines64` (`ClipperBase`'s vtable is keyed to its out-of-line destructor, so per-member export would miss it);
function-level on the triangulation entry points. Templates and header-only helpers carry no macro.
`ClipperBase` and `ReuseableDataContainer64` delete their copy operations: MSVC emits every non-deleted special member
of an exported class, and their `vector<unique_ptr>` members make the implicit copy ill-formed (C2280).

Consumers: depend on `PCGExElementsClipper2`, include `Clipper2Lib/clipper.h`. No ABI stability across PCGEx
versions. Do not put `using namespace PCGExClipper2Lib;` at global scope (`Point` / `Rect` collide with MacTypes).

## Known issues

- Fork #12 (open, no upstream fix): `Triangulate` can report success with overlapping triangles when a new edge
  passes exactly through a vertex. Repro: `PCGEx.Unit.Clipper2.Triangulate.EdgeThroughVertex` (disabled test).

## Patch history

| Patch | Date | Summary |
|-------|------|---------|
| 1 | 2026-11-04 | Fixes + fork perf commits + API export listed above. |

Tests: `PCGExtendedToolkitTest`, `Tests/Unit/Clipper2/PCGExClipper2LibTests.cpp` (`PCGEx.Unit.Clipper2.*`).
