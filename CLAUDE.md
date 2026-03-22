# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

FFGL (FreeFrameGL) is the Resolume fork of the FreeFrame video effects plugin system — a C++ SDK for building real-time OpenGL video effects plugins (DLLs on Windows, bundles on macOS) that run inside Resolume Arena 7.0.3+.

## Building

### Windows (primary platform in this repo)

Open `build/windows/FFGLPlugins.sln` in Visual Studio 2017+ and build the desired project(s). Output DLLs land in `binaries/x64/{Debug,Release}/`.

To deploy: copy resulting `.dll` files to `%DOCUMENTS%/Resolume/Extra Effects`.

Command-line equivalent:
```
msbuild build/windows/FFGLPlugins.sln /p:Configuration=Release /p:Platform=x64
```

### macOS

Open `build/osx/FFGLPlugins.xcodeproj` in Xcode. For Apple Silicon universal builds, select "Any Mac (Apple Silicon, Intel)". Output `.bundle` files land in `binaries/release/`.

### CMake (cross-platform)

```bash
mkdir build_cmake && cd build_cmake
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
cmake --install . --prefix /path/to/install
```

Dependencies are managed via vcpkg (`vcpkg.json`): GLEW, libpng, zlib.

## Architecture

### SDK Library (`source/lib/`)

The SDK is built as a static library linked into each plugin. Three layers:

| Layer | Path | Purpose |
|---|---|---|
| `ffgl/` | Core | Protocol structs, `CFFGLPlugin` base class, plugin registration |
| `ffglex/` | Utilities | GLSL shader wrapper, FBO, screen quad, RAII OpenGL bindings |
| `ffglquickstart/` | High-level | Parameter management, plugin templates, shader utilities |

Master include: `source/lib/FFGLSDK.h`

### Plugin Type Hierarchy

```
CFFGLPlugin  (low-level, direct protocol control)
  └── ffglqs::Plugin  (quickstart: adds parameter system, shader helpers)
      ├── ffglqs::Effect   — processes one input texture
      ├── ffglqs::Source   — generates video from parameters/audio
      └── ffglqs::Mixer    — blends two input textures
```

### Plugin Implementation Pattern

Every plugin follows this structure:

1. Subclass `ffglqs::Effect`, `ffglqs::Source`, or `ffglqs::Mixer`
2. Declare parameters in the constructor via `AddParamRange()`, `AddParamOption()`, `AddParamText()`, etc.
3. Override `InitGL()` to compile shaders and allocate GL resources
4. Override `ProcessOpenGL()` (or `Update()`/`Render()` for quickstart) for per-frame rendering
5. Override `DeInitGL()` for cleanup
6. Register with a global `CFFGLPluginInfo` static — this auto-registers the plugin at load time

See `source/plugins/Add/` for the simplest mixer example and `source/plugins/Particles/` for an advanced compute shader example.

### Key Subsystems

- **Parameter system** (`ffglquickstart/FFGLParam*.h`): Typed wrappers for range, bool, option, text, event, FFT, trigger parameters. Parameters map to uniforms automatically in quickstart plugins.
- **Shader helper** (`ffglex/FFGLShader.h`): Compiles vertex/geometry/fragment GLSL, manages uniform locations.
- **RAII OpenGL bindings** (`ffglex/FFGLScoped*.h`): Scoped wrappers for binding/unbinding shaders, textures, samplers, FBOs, VAOs, buffers — prevents GL state leaks.
- **Audio access** (`ffglquickstart/FFGLAudio.h`): Per-frame FFT and audio data from the host.
- **Logging** (`ffgl/FFGLLog.h`): Writes to Resolume's log (7.3.1+).

### Adding a New Plugin

1. Create `source/plugins/YourPlugin/YourPlugin.h/.cpp`
2. On Windows: add a new `.vcxproj` to `build/windows/` and include it in `FFGLPlugins.sln`
3. On CMake: add a `CMakeLists.txt` in the plugin directory and `add_subdirectory()` from root
4. Choose a unique 4-character plugin ID and add it to `build/PluginIds.txt`
5. Output targets: `binaries/x64/{Debug,Release}/YourPlugin.dll`

### Dependencies

| Dep | Location | Notes |
|---|---|---|
| GLEW 2.1.0 | `deps/glew-2.1.0/` | OpenGL extension loading (Windows/Linux) |
| GLM | `deps/glm/` | Math library, header-only |
| tiny_obj_loader | `deps/tiny_obj_loader.h` | OBJ model loading, header-only |
| libpng / zlib | via vcpkg | Required only for `CustomThumbnail` plugin |

macOS uses native OpenGL and does not need GLEW.

## No Test Infrastructure

There are no unit tests or automated test frameworks. Plugins are validated by loading them in Resolume.
