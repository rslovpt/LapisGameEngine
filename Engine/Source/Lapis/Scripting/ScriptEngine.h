#pragma once
// Embedded Luau scripting.
//
// A script file returns a table of callbacks; each entity with a
// LuauScriptComponent gets its own instance (a table whose __index is the
// script table), so per-entity state lives on `self`:
//
//   -- Content/Scripts/Spinner.luau
//   local Spinner = {}
//   Spinner.Speed = 90
//   function Spinner:OnStart() print("hello from", self.Entity.Name) end
//   function Spinner:OnUpdate(dt)
//       local r = self.Entity.Rotation
//       self.Entity.Rotation = r + Vector3.new(0, self.Speed * dt, 0)
//   end
//   function Spinner:OnCollision(other) end
//   return Spinner
//
// See Docs/Scripting.md for the full API.

#include <string>
#include <unordered_map>
#include <vector>

#include "Lapis/Core/Base.h"
#include "Lapis/Scene/Registry.h"

struct lua_State;

namespace Lapis {

class Scene;

struct LuauScriptComponent {
    std::string ScriptPath;      // relative to the scene's asset root
    int  InstanceRef = -1;       // registry ref of the per-entity instance table
    bool Started = false;
    bool Failed = false;
};

class ScriptEngine : NonCopyable {
public:
    explicit ScriptEngine(Scene* scene);
    ~ScriptEngine();

    // Runs a chunk of code in its own sandboxed environment. Returns false
    // on compile/runtime errors (which are logged and recorded).
    bool RunString(const std::string& source, const std::string& chunkName = "=string");
    bool RunFile(const std::string& path);

    // Lifecycle hooks driven by the Scene.
    void StartEntity(EntityID id);
    void UpdateEntities(float dt);
    void DispatchCollision(EntityID self, EntityID other);
    void DestroyEntity(EntityID id);

    lua_State* GetState() const { return m_L; }
    Scene*     GetScene() const { return m_Scene; }

    const std::vector<std::string>& GetErrors() const { return m_Errors; }
    void ReportError(const std::string& message);

    // Set by App.Quit() from scripts; polled by the runtime.
    bool QuitRequested = false;
    std::string RequestedTitle;

private:
    int  LoadModule(const std::string& path);           // returns registry ref or -1
    bool CallMethod(int instanceRef, const char* method, int nargs, EntityID entityForErrors);

    lua_State* m_L = nullptr;
    Scene*     m_Scene = nullptr;
    std::unordered_map<std::string, int> m_Modules;      // path -> registry ref of script table
    std::vector<std::string> m_Errors;
};

} // namespace Lapis
