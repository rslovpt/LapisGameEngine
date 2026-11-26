# Lapis Game Engine

Lapis is a from-scratch 3D game engine written in C++17 that renders with
**OpenGL 4.5 (core profile)**. It is structured like a real engine: a static
library (`LapisEngine`) that games link against, plus a demo game (`Sandbox`)
and a test suite.

It has **no third-party dependencies**. The Win32 window, the OpenGL function
loader, math, physics, and image/model loaders all live in this repository.

See [`PLANS.txt`](PLANS.txt) for the roadmap and checklist.

## Features

- **Core**: logging, assertions, timers, file IO, event system
- **Math**: vectors, matrices, quaternions, AABB/ray/plane/frustum
- **Platform**: Win32 window + WGL OpenGL 4.5 context, keyboard/mouse input,
  headless EGL backend for automated tests on Linux
- **Renderer**: shaders, buffers, textures, framebuffers, Blinn-Phong lighting
  (directional + point lights), PCF shadow mapping, procedural sky, HDR +
  tone mapping, debug lines
- **Scene**: entity component system, transform hierarchy, native C++ scripts
- **Physics**: rigid bodies, sphere/box colliders, impulse-based collision
  response with friction, raycasts
- **Assets**: OBJ models, TGA/BMP textures, cached asset manager

## Building (Windows)

Requirements: Visual Studio 2019/2022 (Desktop development with C++) and
CMake 3.16+ (bundled with Visual Studio).

```bat
cmake -S . -B build
cmake --build build --config Release
build\bin\Release\Sandbox.exe
```

Or open the folder directly in Visual Studio ("Open a local folder"); it
detects `CMakeLists.txt` automatically. Pick `Sandbox.exe` as the startup item.

### Sandbox controls

| Input                  | Action                         |
|------------------------|--------------------------------|
| Right mouse (hold)     | Mouse look                     |
| W A S D                | Move                           |
| Q / E                  | Down / up                      |
| Shift                  | Move faster                    |
| Left mouse / Space     | Throw a sphere / drop a box    |
| R                      | Reset the physics playground   |
| F1                     | Toggle debug collider drawing  |
| Esc                    | Quit                           |

## Building and testing (Linux, headless)

There's no Linux window yet. The engine library, unit tests, and an offscreen
render test still build and run through EGL:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The render test writes `render_test.ppm`, one frame of the test scene.

## Repository layout

```
Engine/Source/Lapis/
    Core/       logging, asserts, time, files, events
    Math/       vectors, matrices, quaternions, geometry
    Platform/   window + input (Win32, headless EGL)
    Renderer/   OpenGL loader, RHI objects, scene renderer
    Scene/      ECS, components, scene, scripts
    Physics/    rigid bodies, colliders, solver
    Assets/     OBJ/TGA/BMP loaders, asset manager
    Runtime/    Application, main loop, entry point
Sandbox/        demo game built on the engine
Tests/          unit tests + headless render test
```

## Making a game

```cpp
#include <Lapis/Lapis.h>
#include <Lapis/Runtime/EntryPoint.h>

class MyGame : public Lapis::Application {
public:
    MyGame() : Application({"My Game", 1280, 720}) {}
    void OnInit() override { /* build your scene */ }
    void OnUpdate(float dt) override { /* game logic */ }
};

Lapis::Application* Lapis::CreateApplication() { return new MyGame(); }
```
