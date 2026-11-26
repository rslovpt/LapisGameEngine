#include "TestFramework.h"

#include <filesystem>

#include "Lapis/Core/FileSystem.h"
#include "Lapis/Scene/Scene.h"
#include "Lapis/Scripting/ScriptEngine.h"

using namespace Lapis;

namespace {
    std::string TempDir() {
        auto dir = std::filesystem::temp_directory_path() / "lapis_script_tests";
        std::filesystem::create_directories(dir);
        return dir.string();
    }
    void WriteScript(const std::string& dir, const std::string& name, const std::string& code) {
        FileSystem::WriteText((std::filesystem::path(dir) / name).string(), code);
    }
}

LAPIS_TEST(Luau, RunStringAndErrors) {
    Scene scene;
    ScriptEngine& se = scene.GetScripts();
    CHECK(se.RunString("local v = Vector3.new(1, 2, 3); assert(v.x == 1 and v.z == 3)"));
    CHECK(se.RunString("assert(Vector3.Length(Vector3.new(3, 4, 0)) == 5)"));
    CHECK(se.GetErrors().empty());
    CHECK(!se.RunString("this is not luau"));          // compile error
    CHECK(!se.RunString("error('boom')"));             // runtime error
    CHECK(se.GetErrors().size() == 2);
    // Each chunk gets its own environment: assigning a global doesn't leak...
    CHECK(se.RunString("Scene = nil"));
    CHECK(se.RunString("assert(Scene ~= nil and Scene.Find ~= nil)"));
    // ...and built-in libraries are read-only.
    CHECK(!se.RunString("Scene.Find = nil"));
    CHECK(!se.RunString("math.pi = 3"));
}

LAPIS_TEST(Luau, EntityApiFromScripts) {
    Scene scene;
    Entity e = scene.CreateEntity("Player");
    e.Transform().Position = {1, 2, 3};
    ScriptEngine& se = scene.GetScripts();
    CHECK(se.RunString(R"(
        local p = Scene.Find("Player")
        assert(p ~= nil and p.Name == "Player")
        assert(p.Position == Vector3.new(1, 2, 3))
        p.Position = p.Position + Vector3.new(0, 10, 0)
        p.Rotation = Vector3.new(0, 90, 0)
        p.Name = "Hero"
        assert(Scene.Find("Missing") == nil)
        local spawned = Scene.Spawn({ Name = "Rock", Position = Vector3.new(5, 0, 0), Physics = "Dynamic", Mass = 3 })
        assert(spawned.Name == "Rock" and spawned.Velocity == Vector3.zero)
    )"));
    CHECK(NearlyEqual(e.Transform().Position, Vec3(1, 12, 3)));
    CHECK_NEAR(e.Transform().GetEulerDegrees().y, 90.0f);
    CHECK(e.GetName() == "Hero");
    Entity rock = scene.FindEntityByName("Rock");
    CHECK(rock && rock.Has<RigidBodyComponent>() && rock.Has<ColliderComponent>());
    CHECK(rock && rock.Get<RigidBodyComponent>().Mass == 3.0f);
}

LAPIS_TEST(Luau, ScriptComponentLifecycle) {
    const std::string dir = TempDir();
    WriteScript(dir, "Mover.luau", R"(
        local Mover = {}
        Mover.Speed = 2
        function Mover:OnStart()
            self.started = true
            self.Entity.Position = Vector3.new(0, 0, 0)
        end
        function Mover:OnUpdate(dt)
            self.Entity.Position = self.Entity.Position + Vector3.new(self.Speed * dt, 0, 0)
        end
        function Mover:OnDestroy()
            local marker = Scene.Spawn({ Name = "DestroyedMarker" })
        end
        return Mover
    )");
    WriteScript(dir, "Broken.luau", "local t = {} function t:OnUpdate() error('runtime failure') end return t");

    Scene scene;
    scene.SetAssetRoot(dir);
    Entity a = scene.CreateEntity("A");
    a.Add<LuauScriptComponent>().ScriptPath = "Mover.luau";
    Entity b = scene.CreateEntity("B");
    b.Add<LuauScriptComponent>().ScriptPath = "Mover.luau";
    Entity broken = scene.CreateEntity("Broken");
    broken.Add<LuauScriptComponent>().ScriptPath = "Broken.luau";

    scene.OnStart();
    for (int i = 0; i < 10; ++i) scene.OnUpdate(0.1f);
    CHECK_NEAR(a.Transform().Position.x, 2.0f);
    CHECK_NEAR(b.Transform().Position.x, 2.0f);
    CHECK(scene.GetScripts().GetErrors().size() == 10); // one per frame from Broken, engine keeps going

    scene.DestroyEntity(a);
    scene.OnUpdate(0.1f);
    CHECK(scene.FindEntityByName("DestroyedMarker"));
}

LAPIS_TEST(Luau, CollisionCallbacks) {
    const std::string dir = TempDir();
    WriteScript(dir, "Ball.luau", R"(
        local Ball = {}
        function Ball:OnCollision(other)
            if other.Name == "Floor" then self.Entity.Name = "Landed" end
        end
        return Ball
    )");
    Scene scene;
    scene.SetAssetRoot(dir);
    Entity floor = scene.CreateEntity("Floor");
    floor.Transform().Scale = {10, 1, 10};
    floor.Transform().Position = {0, -0.5f, 0};
    floor.Add<ColliderComponent>();
    Entity ball = scene.CreateEntity("Ball");
    ball.Transform().Position = {0, 2, 0};
    ball.Add<ColliderComponent>().Shape = ColliderShape::Sphere;
    ball.Add<RigidBodyComponent>();
    ball.Add<LuauScriptComponent>().ScriptPath = "Ball.luau";
    scene.OnStart();
    for (int i = 0; i < 120; ++i) { scene.OnFixedUpdate(1.0f / 60.0f); scene.OnUpdate(1.0f / 60.0f); }
    CHECK(ball.GetName() == "Landed");
}

LAPIS_TEST(Luau, PhysicsRaycastFromScript) {
    Scene scene;
    Entity wall = scene.CreateEntity("Wall");
    wall.Transform().Position = {0, 0, -10};
    wall.Add<ColliderComponent>();
    CHECK(scene.GetScripts().RunString(R"(
        local hit = Physics.Raycast(Vector3.new(0, 0, 0), Vector3.new(0, 0, -1), 100)
        assert(hit ~= nil, "expected a hit")
        assert(hit.Entity.Name == "Wall")
        assert(math.abs(hit.Distance - 9.5) < 0.01)
        assert(Physics.Raycast(Vector3.new(0, 0, 0), Vector3.new(0, 1, 0), 100) == nil)
    )"));
}

LAPIS_TEST(Luau, FileLibrarySandboxed) {
    const std::string dir = TempDir();
    Scene scene;
    scene.SetAssetRoot(dir);
    std::filesystem::remove_all(std::filesystem::path(dir) / "Saved");
    ScriptEngine& se = scene.GetScripts();
    CHECK(se.RunString(R"(
        assert(File.Write("logs/run.csv", "a,b\n"))
        assert(File.Append("logs/run.csv", "1,2\n"))
        assert(File.Read("logs/run.csv") == "a,b\n1,2\n")
        assert(File.Exists("logs/run.csv"))
        assert(not File.Exists("nope.txt"))
        assert(File.Read("nope.txt") == nil)
        local names = File.List("logs")
        assert(#names == 1 and names[1] == "run.csv")
        local p = Perf.Get()
        assert(p.Entities ~= nil and p.FPS ~= nil)
    )"));
    CHECK(FileSystem::Exists((std::filesystem::path(dir) / "Saved" / "logs" / "run.csv").string()));
    // Escaping the save folder is refused.
    CHECK(!se.RunString("File.Write('../escape.txt', 'x')"));
    CHECK(!se.RunString("File.Write('/tmp/escape.txt', 'x')"));
    CHECK(!se.RunString("File.Read('C:/Windows/win.ini')"));
}
