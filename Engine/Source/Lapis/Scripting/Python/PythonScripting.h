#pragma once

// Python scripting (secondary language; Luau is the primary one).
//
// A Python interpreter (CPython 3.8+) is loaded at runtime from the user's
// Python installation, so the engine ships without it and works fine when
// Python isn't installed (Python scripts then report a clear error).
//
// Scripts: any script component whose path ends in ".py". The file defines a
// class deriving from lapis.Script:
//
//     from lapis import *
//     class Spinner(Script):
//         speed = 90
//         def on_start(self):           print("hello from", self.entity.name)
//         def on_update(self, dt):      self.entity.rotation = Vector3(0, time() * self.speed, 0)
//         def on_collision(self, other): ...
//
// See Docs/Scripting.md ("Python") for the full `lapis` module.
//
// PyPy: PyPy can't be embedded through the CPython C API (its C API layer is
// for extensions loaded *into* PyPy); PyPy support will use CFFI embedding
// (PLANS.txt, Python phase).

#include <string>
#include <unordered_map>

#include "Lapis/Core/Base.h"
#include "Lapis/Scene/Registry.h"

namespace Lapis {

class Scene;

namespace Python {

// Finds and starts the interpreter on first use. Thread: main thread only.
bool Initialize();
bool IsAvailable();
// "CPython 3.12.1 (C:\...\python312.dll)" or why Python isn't available.
const std::string& Status();
// Library search order (for the editor's Python settings / diagnostics).
std::string SearchDescription();

// Runs Python code in the `lapis` module context (tests, console).
bool RunString(const std::string& code, std::string* error = nullptr);

// Per-scene host for ".py" script components.
class Host : NonCopyable {
public:
    explicit Host(Scene& scene) : m_Scene(scene) {}
    ~Host();

    void StartEntity(EntityID id);
    void UpdateEntities(float dt);
    void DispatchCollision(EntityID self, EntityID other);
    void DestroyEntity(EntityID id);
    void DestroyAll();

private:
    struct Instance { void* Object = nullptr; std::string Path; long long Stamp = 0; };
    void Report(const std::string& message);
    bool Call(void* object, const char* method, EntityID arg = {}, bool withArg = false, float dt = -1.0f);

    Scene& m_Scene;
    std::unordered_map<u64, Instance> m_Instances;
    float m_ReloadTimer = 0.0f;
};

bool IsPythonScript(const std::string& path);

} // namespace Python
} // namespace Lapis
