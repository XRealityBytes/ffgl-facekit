# ModelViewer FFGL Plugin — Setup Guide

A static OBJ loader for Resolume with full transform, camera, lighting,
material, vertex FX, and render mode controls.

---

## 1. Dependencies

Two header-only libraries need to be placed in the `deps/` folder of the FFGL repo.

### TinyOBJ Loader
Single-header OBJ parser.

```
https://github.com/tinyobjloader/tinyobjloader
```

Download `tiny_obj_loader.h` and place it at:
```
<repo>/deps/tiny_obj_loader.h
```

### GLM (OpenGL Mathematics)
Header-only matrix/vector math library.

```
https://github.com/g-truc/glm
```

Download and place the `glm/` folder at:
```
<repo>/deps/glm/
```

Both are also available via vcpkg if you prefer:
```json
// vcpkg.json (already exists in the repo root — add these)
{
  "dependencies": [
    "glm",
    "tinyobjloader"
  ]
}
```

---

## 2. Adding to the Xcode Project (macOS)

1. Copy the `ModelViewer/` folder into `<repo>/source/plugins/`
2. Open `<repo>/build/osx/FFGLPlugins.xcodeproj`
3. Duplicate an existing target (e.g. Gradients) and rename it `ModelViewer`
4. Under **Build Phases > Compile Sources**: remove the old `.cpp`, add `ModelViewer.cpp`
5. Under **Build Settings > Packaging > Info.plist**: set to `FFGLPlugin-Info.plist`
6. Under **Build Settings > Header Search Paths**: add `$(SRCROOT)/../../deps`
   (so `#include "tiny_obj_loader.h"` and `#include <glm/glm.hpp>` resolve)
7. Build → the `.bundle` lands in `<repo>/binaries/debug/`
8. Copy `.bundle` → `~/Documents/Resolume/Extra Effects/`

---

## 3. Adding to Visual Studio (Windows)

1. Copy the `ModelViewer/` folder into `<repo>/source/plugins/`
2. Duplicate `Gradients.vcxproj` → rename to `ModelViewer.vcxproj`
3. Open `ModelViewer.vcxproj` in a text editor:
   - Delete the `<ProjectGuid>` line (VS will generate a new one)
   - Replace the Gradients `.cpp`/`.h` `<ClCompile>`/`<ClInclude>` entries
     with the ModelViewer ones
4. Open `FFGLPlugins.sln` → right-click Solution → Add > Existing Project
5. In **Project Properties > C/C++ > Additional Include Directories** add:
   `$(SolutionDir)..\..\deps`
6. Build → `.dll` lands in `<repo>/binaries/x64/Debug/`
7. Copy `.dll` → `%USERPROFILE%\Documents\Resolume\Extra Effects\`

---

## 4. Setting the OBJ Path

Currently `objPath` is a public member set before `initialise()` runs.

**For development**, the quickest approach is to hardcode it temporarily:

```cpp
// At the bottom of the ModelViewer constructor:
objPath = "/Users/yourname/models/mymodel.obj";   // macOS
objPath = "C:/models/mymodel.obj";                 // Windows
```

**For a proper plugin**, the recommended approach is to add a file-path
parameter using FFGL's `FF_TYPE_FILE` parameter type:

```cpp
// In constructor
AddParam( FFParam( "objFile" )
    .WithDisplayName( "OBJ File" )
    .WithType( FF_TYPE_FILE ) );

// In initialise()
objPath = GetParam( "objFile" ).GetString();
```

Resolume will then show a file browser for this parameter.

---

## 5. OBJ File Requirements

- Must have **normals** (`vn` lines). If your OBJ lacks them, re-export
  from Blender with "Export Normals" checked, or run it through:
  ```
  meshlabserver -i input.obj -o output.obj -s compute_normals.mlx
  ```
- **Triangulate** before export — FFGL draws `GL_TRIANGLES` and TinyOBJ
  does not auto-triangulate quads.
- Keep polygon count reasonable for real-time. 50k–200k tris is comfortable;
  above 500k you may see frame drops depending on the machine.

---

## 6. Parameter Reference

| Group      | Parameter         | Range          | Notes                                      |
|------------|-------------------|----------------|--------------------------------------------|
| Transform  | Rotate X/Y/Z      | -180 → 180°    | Great for BPM-sync spin                    |
| Transform  | Scale             | 0.01 → 5       |                                            |
| Transform  | Translate X/Y     | -2 → 2         |                                            |
| Camera     | FOV               | 10 → 120°      | Wide vs telephoto                          |
| Camera     | Camera Distance   | 0.5 → 20       | Dolly                                      |
| Camera     | Orbit X/Y         | ±90 / ±180°    | Arc camera around model                    |
| Lighting   | Light Dir X/Y     | -1 → 1         | 2D direction; Z is computed automatically  |
| Lighting   | Light Color       | colour picker  |                                            |
| Lighting   | Ambient           | 0 → 1          |                                            |
| Lighting   | Specular          | 0 → 1          |                                            |
| Lighting   | Shininess         | 1 → 128        | Higher = tighter highlight                 |
| Material   | Base Color        | colour picker  | Multiplied over the mesh                   |
| Material   | Wireframe         | 0 → 1          | Blends in wireframe overlay as second pass |
| Material   | Wireframe Color   | colour picker  |                                            |
| Material   | Fresnel           | 0 → 3          | Rim glow based on view angle               |
| Material   | Fresnel Color     | colour picker  |                                            |
| Vertex FX  | Explode           | 0 → 2          | Pushes faces out along normals             |
| Vertex FX  | Wave Amplitude    | 0 → 1          | Sine deformation along X axis              |
| Vertex FX  | Wave Frequency    | 0 → 20         |                                            |
| Vertex FX  | Wave Speed        | 0 → 10         | Auto-animates via internal timer           |
| Render     | Shading Mode      | dropdown       | Phong / Flat / Normals / Unlit             |
| Render     | Blend Mode        | dropdown       | Normal / Additive                          |
| Output     | BG Opacity        | 0 → 1          | 0 = transparent, composites over layer     |
| Output     | BG Color          | colour picker  |                                            |

---

## 7. Tips for Live Use in Resolume

- **BPM-sync Rotate Y**: Map to a BPM-divided LFO for a constant spin.
- **Audio-react Explode**: Map to a kick drum envelope — faces burst out on
  every hit and snap back.
- **Audio-react Wave Amplitude**: Low frequencies driving wave height gives
  a nice breathing/pulsing effect.
- **Wireframe at 0.3–0.5 + Additive blend**: Classic live visual look.
- **BG Opacity = 0**: The plugin outputs RGBA with alpha, so it composites
  cleanly over video or other generators in the Resolume layer stack.
- **Normals shading mode**: Looks great as a standalone visual, especially
  combined with colour FX layers on top.

---

## 8. Next Steps / Extensions

- **Multiple OBJ support**: Load several meshes, switch between them with an
  option param.
- **Texture mapping**: Load a PNG alongside the OBJ, bind as `GL_TEXTURE_2D`,
  sample in the fragment shader.
- **Instance rendering**: Use `glDrawElementsInstanced` to render many copies
  with per-instance offsets for crowd/particle-like looks.
- **Shadow pass**: Render depth from the light's perspective first, then use
  the depth texture for shadow sampling in the main pass.
- **Animated mesh** (next major step): Load GLTF with Assimp, add bone matrix
  interpolation and a skinning vertex shader.
