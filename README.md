# Lapis Game Engine

Lapis is a from-scratch 3D game engine in C++17, structured like Unreal: an
engine library, an **editor**, a game **player/runtime**, a **packager** that
exports standalone games, and embedded **Luau** scripting for gameplay.
Rendering uses **OpenGL 4.5 core** (DirectX 12 / Vulkan are planned).

See [`PLANS.txt`](PLANS.txt) for the roadmap/checklist and
[`Docs/Scripting.md`](Docs/Scripting.md) for the Luau API.

## Quick start (Windows)

1. Install **Visual Studio 2022 Community** with the workload
   **"Desktop development with C++"** (CMake comes with it).
2. Double-click **`LapisEditor.bat`**.
   The first run compiles everything (a few minutes), then the editor opens.
3. In the editor: **File > Templates** (First Person Character, Third Person
   Character or 2D Platformer), then press
   **Play** (F5). **File > Export Game...** creates `Export\<Game>\<Game>.exe`.

Other one-click files:

| File | What it does |
|---|---|
| `LapisEditor.bat` | build (if needed) + open the editor |
| `PlaySandbox.bat` | build + play the Sandbox sample without the editor |
| `RunDiagnostics.bat` | build + run all checks, opens `bin\Logs\diagnostics_report.txt` |
| `Build.bat` | just build; programs go to `bin\`, log to `bin\Logs\build_log.txt` |

## Editor

* **Menu bar**: File (New Scene, Open Project, Save, Templates, Settings,
  Export Game), Edit (Play/Stop, Spawn, Duplicate, Delete), Window, Help.
* **Play-in-editor**: Play runs the game inside the editor; Stop restores the
  scene exactly as it was.
* **Viewport**: hold right mouse to fly (WASD, Q/E, Shift), left click selects.
* **Hierarchy / Inspector** panels; "Open script in external editor" opens the
  Luau file in your default editor (e.g. VS Code).

## Engine features

- **Renderer**: HDR forward renderer, directional + 16 point lights, PCF
  shadow maps, procedural sky with reflections, 4x MSAA, ACES tone mapping,
  frustum culling, debug drawing, 2D UI/HUD with a baked font
- **Scripting**: Luau 0.748 (vendored), sandboxed per-script environments,
  OnStart/OnUpdate/OnCollision/OnDestroy, Entity/Scene/Input/Physics/HUD APIs
- **Physics**: rigid bodies, box/sphere colliders, triggers, friction,
  restitution, sleeping, raycasts, collision events
- **Scene**: ECS with generational IDs, transform hierarchy, readable `.scene`
  files, projects (`project.lapis`)
- **Assets**: OBJ models, TGA/BMP textures, cached asset manager
- **Platform**: Win32 + WGL on Windows; X11 + EGL (or headless EGL) on Linux
- **No external dependencies** besides the vendored Luau source

## Repository layout

```
Engine/Source/Lapis/   engine library (Core, Math, Platform, Renderer, Scene,
                       Physics, Assets, Scripting, UI, Runtime)
Apps/                  LapisEditor, LapisPlayer, LapisPackager, LapisDiagnostics
Projects/Sandbox/      sample game project (Luau gameplay)
Templates/             project templates (File > Templates)
Tests/                 unit tests (also run by LapisDiagnostics)
Tools/Testing/         Linux test rig: play-tests in a real window with
                       synthesized input, editor click-through test,
                       Windows compile check
ThirdParty/            Luau (MIT), DejaVu font license
Docs/                  scripting reference
```

## Building on Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
build/bin/LapisTests                                   # unit tests
build/bin/LapisEditor --project Projects/Sandbox       # needs X11
Tools/Testing/playtest.sh build/bin Projects/Sandbox   # automated play-test (Xvfb)
Tools/Testing/editortest.sh build/bin                  # automated editor test
Tools/Testing/windows_check.sh                         # compile-check Windows code
```

Set `LAPIS_HEADLESS=1` to render offscreen without a display.
