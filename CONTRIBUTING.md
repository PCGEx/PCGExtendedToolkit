# Contributing
PCG Extended Toolkit (PCGEx) is a multiplatform plugin that is expected to work on `Linux`, `macOS` and `windows` at the very least. It is regularily packaged and pushed to FAB, and must pass certain validations to be published.  
At the time of writing, Unreal `5.7` and `5.8` are officially supported; hence, while making a PR please ensure that:
- The plugin compiles for the editor in those 3 versions
- The plugin can be packaged without error or warnings in a project for those 3 versions
- The plugin can be packaged *as a plugin* (this is different from packaging a project) for those 3 versions

Additionally, any contribution should not include or rely on any other third parties libraries to be integrated into the codebase. It's complex enough as is.

**Make sure to fork the main branch, don't PR on the per-version branch if they're current.**

## Non-exhaustive checklist

### Copyright notice
All files (.cpp, .h) **must** have a copyright notice at the very top, before any other text. *(FAB requires it and has automated checks)*

For new files:
```
// Copyright 2025 {Your Name} and contributors
// Released under the MIT license https://opensource.org/license/MIT/
```

Modifying existing files
```
// Copyright 2025 {Original Owner Name} and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// * {DD/MM/YY} {Your Name} {Short description of the contribtion}
```

### Enum
Make sure that enums that will displayed in the editor (i.e any enum that's used in a UPROPERTY or isn't in its own namespace) are prefixed `EPCGEx` and not just `E`. 
