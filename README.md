![UE 5.7](https://img.shields.io/badge/UE-5.7-darkgreen) ![5.6](https://img.shields.io/badge/5.6-darkgreen) ![5.5](https://img.shields.io/badge/5.5-gray) ![5.4](https://img.shields.io/badge/5.4-gray) ![5.3](https://img.shields.io/badge/5.3-gray) [![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/Nebukam/PCGExtendedToolkit)

<p align="center">
  <img src="https://raw.githubusercontent.com/Nebukam/PCGExtendedToolkit/refs/heads/docs/_sources/smol-logo.png" alt="PCGEx Logo">
</p>

<h1 align="center">PCG Extended Toolkit (PCGEx)</h1>

<p align="center">
  <strong>200+ nodes for advanced procedural generation in Unreal Engine</strong><br>
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

### PCGEx is a **low-level, use-case agnostic toolkit** extending Unreal Engine's PCG framework with **200+ nodes**.

Vanilla PCG excels at scattering and rule-based placement. PCGEx adds what's missing: **structure**. Build graphs from points, find paths through them, analyze topology, and work with explicit connections—not just proximity. Delaunay, Voronoi, MST, convex hulls, A* pathfinding, and more.

Graph theory is just the headline. PCGEx is also a comprehensive data manipulation toolkit: spatial queries, sampling, blending, path operations, polygon booleans, asset management, filtering, sorting—the low-level primitives that vanilla PCG doesn't provide. Reusable sub-nodes (filters, heuristics, blenders) plug into operations to keep your graphs clean. 

_It doesn't solve specific problems for you. _It gives you the tools to solve them yourself._

---

## Getting Started

### Installation

PCGEx is available through multiple channels:

- **[FAB](https://www.fab.com/listings/3f0bea1c-7406-4441-951b-8b2ca155f624)** — Epic's official marketplace
- **[Source](https://github.com/Nebukam/PCGExtendedToolkit)** — Build from GitHub

See the [Installation Guide](https://pcgex.gitbook.io/pcgex/working-with-pcgex/getting-started/installation) for detailed instructions.

### Example Project

The best way to learn PCGEx is through the **[Example Project](https://pcgex.gitbook.io/pcgex/working-with-pcgex/getting-started/example-project)**, which contains hundred of annotated graphs and complex examples demonstrating PCGEx capabilities.

<img width="1256" height="902" alt="image" src="https://github.com/user-attachments/assets/017164af-ac4c-4ff2-b0ae-a76a32d40ed2" />

### Documentation

- **[Gitbook Documentation](https://pcgex.gitbook.io/pcgex)** — Comprehensive guides and tutorials
- **[Discord Server](https://discord.gg/mde2vC5gbE)** — Community support and discussion

---

## Key Features

### Clusters & Graphs
The heart of PCGEx. Transform points into connected networks via Delaunay, Voronoi, convex hulls, MST, and custom builders. Every connection is data you can query, filter, refine, and build upon. This is what vanilla PCG can't do.

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/09d29b95-b7f5-4e77-87b0-56d1b4b7d86e" />


### Filter Ecosystem
Define selection logic once, reuse everywhere. AND/OR composition, attribute tests, spatial queries, bitmasks—all as portable sub-nodes that plug into operations. No more duplicating filter chains across your graph.

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/556df727-c17a-4cb7-835b-653a8411d74e" />

### Asset Collections
Curate meshes, actors, and data assets with weighted distribution, tags, and per-entry property overrides. Define a collection once, use it consistently everywhere. The asset management layer vanilla PCG lacks.

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/6f1c453b-8010-496f-bdf8-5a809e36849d" />


### Paths
Smooth, simplify, subdivide, cut, fuse, offset, bevel. Tangent operations for clean curves. Convert freely between points, paths, and splines. A complete path manipulation toolbox.

<img width="3840" height="1080" alt="image" src="https://github.com/user-attachments/assets/698a107c-6429-4310-89b1-ddf52c032f27" />


### Pathfinding
A*/Djikstra routing through your clusters with pluggable heuristics. Weight by distance, slope, attributes, or custom logic. Find optimal paths when you need them, _without a single loop_.

<img width="3840" height="1080" alt="image" src="https://github.com/user-attachments/assets/7686097c-ee02-42b4-85bc-dfc9e763a265" />


### Spatial Operations
**Point fusion** is foundational—merge nearby points with attribute blending. Beyond that: Lloyd relaxation, bin packing, octree queries, bounds analysis. Power tools for when you need them.

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/1e36d60e-0830-4a5c-aa6c-ad99fc0f542d" />


### Sampling & Blending
Transfer data between point sets, surfaces, splines, textures. Configurable weighting and falloff. The glue that connects disparate data sources.

<img width="3840" height="1080" alt="image" src="https://github.com/user-attachments/assets/664a0725-9e76-4593-9e1e-81dc7814199d" />


### Tensors & Vector Fields
Spatial effectors that influence transforms. Stack them for complex directional fields—orienting objects, extruding paths, guiding growth.

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/85bd53fa-c9f1-42d7-a278-ec3da68b1804" />


### Probing
Build clusters from connection rules—define how points should connect based on spatial relationships and let PCGEx figure out the graph.

<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/ffcf4a0f-f05b-4c66-bd83-7165379ada3c" />


### Supporting Tools
**Shapes** — 2D/3D primitives, Clipper2 polygon booleans.  
**Topology** — Boundary detection, flood fill, island analysis.  
**Noise** — 3D procedural noise for natural variation.  
**Bridges** — Convert between meshes, clusters, and paths.  
**Utilities** — Sorting, partitioning, attributes, hashes, so much more.  


<img width="3840" height="1080" alt="image" src="https://github.com/user-attachments/assets/487c9a31-7af2-4e10-b10c-919d92c80c70" />



---

## Interop Plugins

PCGEx has companion plugins for specialized integrations:

| Plugin | Description |
|--------|-------------|
| **[PCGEx + ZoneGraph](https://github.com/Nebukam/PCGExtendedToolkitZoneGraph)** | Generate ZoneGraph data for AI navigation from PCGEx clusters |
| **[PCGEx + Watabou](https://github.com/Nebukam/PCGExtendedToolkitWatabou)** | Import procedural maps from Watabou's generators |

---

## For C++ Developers

PCGEx is designed for extensibility. The plugin provides a robust framework for creating custom PCG nodes with:

- **Processor Pattern** — Per-input processing with automatic parallelization across threads
- **Factory System** — Pluggable operations (filters, blenders, samplers) via a Settings → Factory → Operation pipeline
- **Data Facades** — Type-safe, cached attribute access with thread-safe buffer management
- **Cluster Infrastructure** — Full graph/topology data structures ready for custom algorithms

### Architecture

The plugin is organized into **core modules** (foundational infrastructure) and **element modules** (node implementations):

**Core Modules:**
```
PCGExCore          → Data facades, threading primitives, macros, containers
PCGExGraphs        → Graph/cluster structures, node/edge topology
PCGExFilters       → Composable filter system with manager orchestration
PCGExBlending      → Attribute blending with multiple blend modes
PCGExCollections   → Asset collection management, weighted picking
PCGExFoundations   → Polylines, tangents, geometric primitives
PCGExProperties    → Unified property system across modules
PCGExMatching      → Pattern matching framework
PCGExHeuristics    → Heuristic calculations for pathfinding
PCGExNoise3D       → Procedural noise
```

All processing runs **off the game thread** with pre-allocated buffers and parallel-safe patterns. Custom nodes inherit these capabilities automatically by extending the appropriate base classes.

See [CONTRIBUTING.md](https://github.com/Nebukam/PCGExtendedToolkit/blob/main/CONTRIBUTING.md) for development guidelines.

---

## Support the Project

PCGEx is free and open source under the MIT license. If it's useful to your work, consider:

- ⭐ **Starring** the repository
- 💬 **Joining** the [Discord community](https://discord.gg/mde2vC5gbE)
- ❤️ **Supporting** on [Patreon](https://www.patreon.com/c/pcgex)

---

## Acknowledgments

### Supporters
Check out the [Supporters page](https://pcgex.gitbook.io/pcgex/supporters) on Gitbook!

Special thanks to [Sine Nomine Associates](https://sinenomine.net/) for generously providing and maintaining the automated Linux build infrastructure.

### Special Thanks

| | |
|---|---|
| **[@MikeC](https://github.com/mikec316)** | Reckless experiments, feedback, and suggestions that shaped the plugin into what it is today |
| **[@Amathlog](https://github.com/Amathlog)** | Epic Games staff, invaluable PCG framework guidance |
| **[@Erlandys](https://github.com/Erlandys)** | Advanced C++ insights |
| **[@Syscrusher](https://github.com/sna-scourtney)** | Linux support and maintenance |
| **[@staminajim](https://github.com/staminajim), [@MaximeDup](https://github.com/MaximeDup)** and **[@EmSeta](https://github.com/EmSeta)** | macOS compatibility |

And all the [contributors](https://github.com/Nebukam/PCGExtendedToolkit/graphs/contributors) who make this project better! ❤️

### Third-Party Libraries

- **[delaunator-cpp](https://github.com/delfrrr/delaunator-cpp)** — Fast Delaunay triangulation
- **[Clipper2](https://github.com/AngusJohnson/Clipper2)** — Polygon clipping and offsetting (modified C++ port, v2.0.1) by Angus Johnson

---

## License

**MIT License** — Free for personal and commercial use. Attribution appreciated but not required.
