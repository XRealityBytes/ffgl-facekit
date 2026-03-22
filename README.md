This is the Resolume fork of the FFGL repository. It is up to date and has Visual Studio and Xcode projects to compile 64 bit plugins that can be loaded by Resolume 7.0.3 and up.  

**Note for macOS developers:** *Resolume 7.11.0 has added native ARM support. This means that on Apple Silicon it will run as a native ARM process. Native ARM processes cannot load x86_64 based plugins. To enable your plugin to be loaded you should build it as a universal build. If your Xcode is up-to-date enough you can choose to build for "Any Mac (Apple Silicon, Intel)" instead of "My Mac" in the top left corner. Please read the [apple developer documentation](https://developer.apple.com/documentation/apple-silicon/building-a-universal-macos-binary) for more information about universal builds.*

The master branch is used for continued development. It will contain the latest features, fixes and bugs. Plugins compiled with the master branch will work in Resolume 7.3.1 and up.
If you do not want to be affected by the latest bugs you can use one of the stable releases. eg FFGL 2.2, which is the most recent released version of the sdk. Plugin development for Resolume 7.0.0/7.0.1/7.0.2 is no longer supported by this repository. These versions are very old and there are many newer versions that users can update to.

You can find some help to get started with FFGL plugin development on the [wiki](https://github.com/resolume/ffgl/wiki).

Also more examples are available on this [repo](https://github.com/flyingrub/ffgl/tree/more/).

## Master branch changes since FFGL 2.2
- Replaced glload by glew, enabling OpenGL 4.6 extensions to be used inside plugins. Plugins may need to add deps/glew.props to their project's property pages for them to link to the binary.
- Implemented parameter display names. Parameter names are used as identification during serialization, display names can be used to override the name that is shown in the ui. The display name can also be changed dynamically by raising a display name changed event. (Requires Resolume 7.4.0 and up)
- Implemented value change events. Plugins can change their own parameter values and make the host pick up the change. See the new Events example on how to do this. (Requires Resolume 7.4.0 and up)
- Implemented dynamic option elements. Plugins can add/remove/rename option elements on the fly. (Requires Resolume 7.4.1 and up)

*You can suggest a change by creating an issue. In the issue describe the problem that has to be solved and if you want, a suggestion on how it could be solved.*

## FaceKit plugin

`source/plugins/FaceKit` is a Resolume FFGL source plugin that renders a 3D face mesh and applies FaceKit-style morph targets from OSC. It supports:

- ICT FaceKit OBJ directories, including expression and identity morphs
- `.gltf` and `.glb` face meshes with morph targets
- cross-platform OSC input on Windows and macOS
- macOS universal builds for Intel and Apple Silicon

### FaceKit parameters

- `modelPath`: file parameter for a model asset
- `modelFormat`: `Auto`, `ICT FaceKit OBJ`, or `glTF / GLB`
- `modelStatus`: load result and warnings
- `meshToggle01` ... `meshToggle48`: dynamic on/off toggles for loaded mesh components
- `renderLevel`: legacy coarse subset for ICT FaceKit meshes, default `Full`
- `oscPort`: UDP listen port, default `7400`
- `oscAgent`: `Any`, `Agent A`, or `Agent B` filter for agent-scoped OSC streams
- `oscStatus`: current OSC receiver state and packet count

`modelPath` is exposed as an FFGL file parameter, so Resolume can show a file browser and accept file drag/drop. `oscPort` remains a text parameter. Mesh toggles are hidden until a model is loaded, then renamed to the discovered component names.

### Supported model layouts

#### ICT FaceKit OBJ

Point `modelPath` at either:

- `generic_neutral_mesh.obj`
- any `.obj` file inside the ICT FaceKit asset directory

The loader expects the standard ICT naming scheme alongside the neutral mesh:

- `generic_neutral_mesh.obj`
- the 53 expression OBJ files
- the 100 identity OBJ files

If identity morphs are missing, rendering still works and OSC identity messages are ignored.

The ICT loader also enumerates the built-in FaceKit submeshes such as face, teeth, eyeballs, lacrimals, occlusion, and eyelashes so they can be toggled individually in Resolume.

#### glTF / GLB

Point `modelPath` at a `.gltf` or `.glb` file. The current loader expects:

- triangle primitives
- `POSITION` data on the base mesh
- morph target `POSITION` deltas
- morph names in `mesh.extras.targetNames` or `mesh.targetNames`

All mesh instances and triangle primitives found in the active glTF scene are loaded as components. Each component gets a dynamic toggle in the `Meshes` parameter group, so separate face parts such as eyes, teeth, lashes, or attached accessories can be enabled or disabled per plugin instance.

Current glTF limitations:

- identity morphs are not supported
- if target names are missing, the mesh can still load, but `/facekit/blendshape/{name}` mapping is unavailable

### OSC input

The plugin listens on UDP and accepts the following OSC addresses:

- `/Avatar-A/arkit52`
- `/Avatar-B/arkit52`
  - 52 floats in standard ARKit order, followed by the sender's `frameIndex` and `timestamp`
  - this matches the outbound stream used by `D-VoidBot`
  - `oscAgent` can be used to make an instance respond only to agent A or only to agent B when both are sending
- `/facekit/blendshapes`
  - 53 floats, in the canonical FaceKit order listed below
- `/facekit/blendshape/{name}`
  - 1 float
  - `{name}` is matched against the canonical names below after normalization
  - normalization lowercases the name, removes punctuation, and collapses `left/right` to `l/r`
- `/facekit/identity`
  - 100 floats
  - only applies to ICT FaceKit OBJ models
- `/facekit/identity/{index}`
  - 1 float
  - zero-based identity index
- `/facekit/headpose`
  - 6 floats: `tx ty tz rx ry rz`
- `/facekit/reset`
  - resets blendshapes, identity weights, and headpose to zero

Canonical blendshape order for `/facekit/blendshapes`:

```text
browDown_L browDown_R browInnerUp_L browInnerUp_R browOuterUp_L browOuterUp_R
cheekPuff_L cheekPuff_R cheekSquint_L cheekSquint_R eyeBlink_L eyeBlink_R
eyeLookDown_L eyeLookDown_R eyeLookIn_L eyeLookIn_R eyeLookOut_L eyeLookOut_R
eyeLookUp_L eyeLookUp_R eyeSquint_L eyeSquint_R eyeWide_L eyeWide_R
jawForward jawLeft jawOpen jawRight mouthClose mouthDimple_L mouthDimple_R
mouthFrown_L mouthFrown_R mouthFunnel mouthLeft mouthLowerDown_L mouthLowerDown_R
mouthPress_L mouthPress_R mouthPucker mouthRight mouthRollLower mouthRollUpper
mouthShrugLower mouthShrugUpper mouthSmile_L mouthSmile_R mouthStretch_L
mouthStretch_R mouthUpperUp_L mouthUpperUp_R noseSneer_L noseSneer_R
```

Examples:

- `/Avatar-A/arkit52 <52 floats> <frameIndex:int32> <timestamp:double>`
- `/facekit/blendshape/jawOpen 0.8`
- `/facekit/blendshape/mouthSmileLeft 1.0`
- `/facekit/headpose 0 0 0 0 20 0`
- `/facekit/reset`

Outgoing OSC telemetry is intentionally not enabled in this version. Incoming control is the production path.

### Component toggles in Resolume

FaceKit is still a single FFGL source, so Resolume sees one composited video output per plugin instance. That means component toggles can isolate parts of the model inside an instance, but they do not create separate native Resolume layers automatically.

To apply different Resolume effect chains to different parts of the same model:

- add multiple `FaceKit` source instances
- point them at the same model and OSC port
- enable different `Meshes` toggles in each instance

Example: one instance can render only the face skin, while a second instance renders only eyes or lashes with a different Resolume effect stack.

### Building FaceKit

#### Windows Visual Studio

- Open `build/windows/FFGLPlugins.sln`
- Build the `FaceKit` project for `x64`
- The plugin is written to `binaries/x64/Release/FaceKit.dll` or `binaries/x64/Debug/FaceKit.dll`

The Visual Studio project is wired for the new cross-platform loader and OSC layer, but Windows compilation was not validated in this macOS-only environment.

#### macOS Xcode

- Open `build/osx/FFGLPlugins.xcodeproj`
- Select the `FaceKit` scheme
- For a universal plugin, build for `Any Mac`
- The plugin is written to `binaries/release/FaceKit.bundle` or `binaries/debug/FaceKit.bundle`

Validated command-line build:

```sh
HOME=/tmp/ffgl-facekit-home \
xcodebuild \
  -project build/osx/FFGLPlugins.xcodeproj \
  -scheme FaceKit \
  -configuration Release \
  -destination 'generic/platform=macOS' \
  -derivedDataPath /tmp/ffgl-facekit-xcode-universal-default-dd \
  build
```

#### CMake

FaceKit is now part of `source/plugins/CMakeLists.txt` and builds as target `ffgl-plugin-facekit`.

Validated macOS universal build:

```sh
cmake -S . -B /tmp/ffgl-facekit-cmake-universal \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64'

cmake --build /tmp/ffgl-facekit-cmake-universal \
  --config Release \
  --target ffgl-plugin-facekit
```

On macOS, the built bundle is written to:

```text
/tmp/ffgl-facekit-cmake-universal/source/plugins/FaceKit/FaceKit.bundle
```

## Quickstart

Below are the first steps you need to create and test an FFGL plugin for Resolume. This assumes you have experience with git and C++ development.

### Mac

- Go to `<repo>/build/osx`, open `FFGLPlugins.xcodeproj`
- Create a compilation target for your plugin:
	- Select the Xcode project (top of the tree)
	- Duplicate a target and rename it
	- Remove the old plugin-specific files under Build Phases > Compile Sources (e.g. if you duplicated Gradients, remove `FFGLGradients.cpp`)
	- Duplicating a target in Xcode creates and assigns a new `xx copy-Info.plist` file, but we don't want that. Go to Build Settings > Packaging > Info.plist and change the file name to `FFGLPlugin-Info.plist`.  
	- Find the reference to the newly created `xx copy-Info.plist` file in the Xcode Project Navigator (probably all the way down the panel) and remove it there. When asked, choose Move to Trash.
- In Finder, duplicate a plugin folder and rename the files. Choose a corresponding plugin type, e.g. copy `AddSubtract` if you want to build an Effect plugin or `Gradients` if you want to build a Source plugin.
- Drag the new folder into the Xcode project. You will be asked to which target you want to add them, add them to your new target.
- Go to the target's Build Phases again and make sure there are no resources under the Copy Bundle Resources phase.
- Replace the class names to match your new plugin name and rename the elements in the PluginInfo struct
- Fix up the Build scheme:
	- When duplicating a target, a Build Scheme was also created. Next to the play and stop buttons, click the schemes dropdown and select Manage Schemes. 
	- Rename the scheme that was auto-created (e.g. "Gradient copy")
	- Select it in the scheme drop down.
- Press play (Cmd+B) to compile.
- Copy the resulting `.bundle` file from `<repo>/binaries/debug` to `~/Documents/Resolume/Extra Effects` and start Arena to test it.

### Windows 

This assumes you use Visual Studio 2017

- Go to `<repo>/build/windows`, duplicate a `.vcxproj` and the corresponding `.vcxproj.filters` file, and rename them.
- Open `FFGLPlugins.sln`. Then right-click the Solution in the solution explorer (top of the tree), and choose Add > Existing Project and select the new file.
- Remove the original `.cpp` and `.h` source files from the newly added project, i.e. if you duplicated `Gradient.vcxproj`, remove `FFGLGradients.h` and `FFGLGradients.cpp`
- In Explorer, go to `<repo>/source/`, duplicate a plugin folder and rename the files. Choose a corresponding plugin type, i.e. copy `AddSubtract` if you want to build an Effect plugin or `Gradients` if you want to build a Source plugin.
- Add the new source files to the project by dragging them into Visual Studio, onto your new project.
- If you want to start the build with Visual Studio's Build command (F5), right-click the project and select Set as Startup Project. Altenatively, you can right-click the project and select Build.
- After building, find the resulting `.dll` file in `\binaries\x64\Debug`. Copy it to `<user folder>/Documents/Resolume/Extra Effects`
