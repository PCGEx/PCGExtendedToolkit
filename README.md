<p align="center">
<img src="https://img.shields.io/badge/UE-5.8%20·%205.7-darkgreen" alt="Supports 5.8 5.7 and earlier versions down to 5.3" />
<a href="https://github.com/PCGEx/PCGExtendedToolkit/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License" /></a>
<a href="https://github.com/PCGEx/PCGExtendedToolkit/stargazers"><img src="https://img.shields.io/github/stars/PCGEx/PCGExtendedToolkit?style=social" alt="Stars" /></a>
<a href="https://github.com/PCGEx/PCGExtendedToolkit/network/members"><img src="https://img.shields.io/github/forks/PCGEx/PCGExtendedToolkit?style=social" alt="Forks" /></a>
<a href="https://deepwiki.com/Nebukam/PCGExtendedToolkit"><img src="https://deepwiki.com/badge.svg" alt="Deep Wiki" /></a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/Nebukam/PCGExtendedToolkit/refs/heads/docs/_sources/smol-logo.png" alt="PCGEx Logo">
</p>

<h1 align="center">PCG Extended Toolkit (PCGEx)</h1>

<p align="center">
  <strong>Node & feature ecosystem for advanced PCG in Unreal Engine</strong><br>
  Graph theory, pathfinding, spatial queries, asset management, and more.
</p>

<p align="center">
  <a href="https://pcgex.gitbook.io/pcgex">Documentation</a> •
  <a href="https://pcgex.gitbook.io/pcgex/working-with-pcgex/getting-started/installation">Installation</a> •
  <a href="https://pcgex.gitbook.io/pcgex/changelogs">Changelogs</a> •
  <a href="https://discord.gg/mde2vC5gbE">Discord</a> •
  <a href="https://www.patreon.com/c/pcgex">Support on Patreon</a>
</p>

---

## What is PCGEx?

### A comprehensive node ecosystem that extend Unreal Engine's PCG framework with structure, connectivity, and spatial intelligence. 
Build graph networks from points. Pathfind through them. Compose filters and heuristics as reusable sub-nodes. Stage assets. Sample across datasets. Manipulate paths. Streamline the sorting, fusion, partitioning, and attribute work that complex workflows demand. 

_PCGEx is **[fully documented](https://pcgex.gitbook.io/pcgex)** and **production-ready**._

---

## Getting Started
- **[Getting Started](https://pcgex.gitbook.io/pcgex/getting-started)** / [Installation](https://pcgex.gitbook.io/pcgex/getting-started/installation) / Epic' [FAB](https://www.fab.com/listings/3f0bea1c-7406-4441-951b-8b2ca155f624)
- **[Working with PCGEx](https://pcgex.gitbook.io/pcgex/working-with-pcgex/)**
- [Node library](https://pcgex.gitbook.io/pcgex/node-library/overview) + [Example Project](https://pcgex.gitbook.io/pcgex/getting-started/example-project)
  

> AI Assistants : Gitbook [llms.txt](https://pcgex.gitbook.io/pcgex/llms.txt) and [llms-full.txt](https://pcgex.gitbook.io/pcgex/llms-full.txt) has you covered.

### Support
- **[Documentation](https://pcgex.gitbook.io/pcgex)**  •  Everything is there.
- **[Discord Server](https://discord.gg/mde2vC5gbE)**  •  Community support

> PCGEx is actively developed for the latest `5.x` version and most updates are backported to `5.x-1` if Epic's APIs aren't too widely diverging.

## PCGEx Pro
PCGEx also has a small ecosystem of more uniquely targeted plugins that build on top of the core plugin, under the ["PCGEx Pro"](https://pcgex.gitbook.io/pcgex/pro) umbrella :

→ **[PCGEx + ZoneGraph](https://pcgex.gitbook.io/pcgex/zone-graph)**  •  Generate ZoneGraph roads & polygons from clusters  
→ **[PCGEx + Valency](https://pcgex.gitbook.io/pcgex/valency)**  •  Constraint solving pipeline (WFC + free-form connector grammar)  
→ **[PCGEx + Cluster Sketch](https://pcgex.gitbook.io/pcgex/cluster-sketch)**  •  Create clusters by hand with per-element data layers

--- 

### Branches
```diff
- main
Compiles against latest launcher engine binaries
(this an unstable branch)

+ 5.x
Compiles against that version of the engine
(these are stable branches)

! `FAB-5.x`
Served to FAB and have some features disabled
(no PCHs, no tooling/cherry-picking scripts);
they can be unstable during submission windows.

```

> Note that PCGEx is actively maintained only for `5.7+`.

---

## Support the project

PCGEx is free and open source under the MIT license. If it's useful to your work, consider:

- ⭐ **Starring** the repository
- 💬 **Joining** the [Discord community](https://discord.gg/mde2vC5gbE)
- ❤️ **Supporting** on [Patreon](https://www.patreon.com/c/pcgex)

---

## Acknowledgments

### Supporters
Check out the [Supporters page](https://pcgex.gitbook.io/pcgex/supporters) on Gitbook!

### Third-Party Libraries

- **[delaunator-cpp](https://github.com/delfrrr/delaunator-cpp)** → Fast Delaunay triangulation
- **[Clipper2](https://github.com/AngusJohnson/Clipper2)** → Polygon clipping and offsetting (modified C++ port, v2.0.1) by Angus Johnson

### Special Thanks

| | |
|---|---|
| **[@MikeC](https://github.com/mikec316)**, **[@TyrannicGoat](https://github.com/mharris382)**, **[@EdBoucher](https://github.com/EdBoucher)** | Reckless experiments, feedback, and suggestions that shaped the plugin into what it is today |
| **[@Amathlog](https://github.com/Amathlog)** | Epic Games staff, invaluable PCG framework guidance |
| **[@Erlandys](https://github.com/Erlandys)** | Advanced C++ insights |
| **[@Syscrusher](https://github.com/sna-scourtney)** / [Sine Nomine Associates](https://sinenomine.net/) | Linux support |
| **[@staminajim](https://github.com/staminajim), [@MaximeDup](https://github.com/MaximeDup)**, **[@EmSeta](https://github.com/EmSeta)** | macOS compatibility |

And all the [contributors](https://github.com/Nebukam/PCGExtendedToolkit/graphs/contributors) who make this project better! ❤️

---

## License

**MIT License**  •  Free for personal and commercial use. Attribution appreciated but not required.
