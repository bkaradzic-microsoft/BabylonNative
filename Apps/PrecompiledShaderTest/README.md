# PrecompiledShaderTest

A cross-platform headless app that validates the **ShaderTool → ShaderCache →
NativeEngine** pipeline by rendering a scene using pre-compiled shaders and
saving the result as a PNG screenshot.

## Overview

The app has two parts: a **JavaScript** scene that describes what to render, and
a **C++** host that loads pre-compiled shaders, creates a graphics device,
executes the scene script, and saves the rendered frame as a PNG.

### Pipeline

1. `npm run build` in `JavaScript/` bundles the scene script and extracts
   preprocessed shader source files. The scene includes a WebGL2 GPU particle
   system: its `gpuUpdateParticles` program is captured together with its
   transform feedback varyings, which Babylon Native runs as a generated compute
   shader, and its render program is captured with the
   attributes it reads per instance.
2. At CMake build time, **ShaderTool** compiles the shader source files into a
   binary cache (`shaders.bin`).
3. At run time, the app loads `shaders.bin` into **ShaderCache**, renders the
   scene to an offscreen texture, and writes `output.png`.

## Building

### 1. Prepare the JavaScript assets

```bash
cd JavaScript
npm install
npm run build
```

This produces:
- `JavaScript/dist/index.js` — bundled scene script
- `JavaScript/dist/shaders/<name>/vertex.fx` — preprocessed vertex shaders
- `JavaScript/dist/shaders/<name>/fragment.fx` — preprocessed fragment shaders
- `JavaScript/dist/shaders/<name>/instanced.txt` — attributes the pair reads per instance
- `JavaScript/dist/shaders/<name>/transformFeedback.fx` — transform feedback vertex shader (optional)
- `JavaScript/dist/shaders/<name>/transformFeedback.txt` — its captured varyings

### 2. Build with CMake

Configure and build the `PrecompiledShaderTest` target as part of the normal
BabylonNative CMake build. The `CMakeLists.txt` invokes **ShaderTool** to
compile the shader files into `shaders.bin` and copies all assets next to
the executable.

