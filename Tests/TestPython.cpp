#include "TestFramework.h"

#include <filesystem>
#include <fstream>
#include <thread>

#include "Lapis/Scene/Scene.h"
#include "Lapis/Scripting/Python/PythonScripting.h"
#include "Lapis/Scripting/ScriptEngine.h"

using namespace Lapis;

namespace {
    // Python is optional: on machines without it these tests just check the error path.
    std::string WriteScript(const std::string& dir, const std::string& name, const std::string& code) {
        std::filesystem::create_directories(dir + "/Scripts");
        std::ofstream(dir + "/Scripts/" + name) << code;
        return "Scripts/" + name;
    }
}

LAPIS_TEST(Python, ScriptLifecycleAndApi) {
    const std::string dir = (std::filesystem::temp_directory_path() / "lapis_py_test").string();
    std::filesystem::remove_all(dir);
    const std::string path = WriteScript(dir, "Mover.py", R"(
from lapis import *

class Mover(Script):
    speed = 2.0
    def on_start(self):
        self.started = True
        self.hits = 0
        self.ball = spawn("PyBall", "Sphere", position=(0, 5, 0), physics="Dynamic", color=Color.red)
        print("Mover started on", self.entity.name)
    def on_update(self, dt):
        p = self.entity.position
        self.entity.position = p + Vector3(self.speed * dt, 0, 0)
        assert find("Ground") is not None and find("Nope") is None
        assert isinstance(time(), float) and key_down("W") in (True, False)
    def on_collision(self, other):
        self.hits += 1
)");
    Scene scene;
    scene.SetAssetRoot(dir);
    Entity ground = scene.CreateEntity("Ground");
    ground.Transform().Scale = {20, 1, 20};
    ground.Transform().Position = {0, -0.5f, 0};
    ground.Add<ColliderComponent>();
    Entity mover = scene.CreateEntity("Mover");
    mover.Add<LuauScriptComponent>().ScriptPath = path;

    scene.OnStart();
    if (!Python::IsAvailable()) {
        // No Python on this machine: a clear error instead of a crash.
        CHECK(!scene.GetScripts().GetErrors().empty());
        std::filesystem::remove_all(dir);
        return;
    }
    for (int i = 0; i < 60; ++i) { scene.OnFixedUpdate(1.0f / 60.0f); scene.OnUpdate(1.0f / 60.0f); }
    CHECK(scene.GetScripts().GetErrors().empty());
    CHECK_NEAR(mover.Transform().Position.x, 2.0f);                 // moved 1 s at 2 units/s
    Entity ball = scene.FindEntityByName("PyBall");
    CHECK(ball && ball.Transform().Position.y < 4.9f);              // spawned with physics, falling

    // Errors are reported with a traceback, not thrown into the engine.
    std::string error;
    CHECK(Python::RunString("x = 1 + 1"));
    CHECK(!Python::RunString("raise ValueError('boom')", &error));
    CHECK(error.find("ValueError") != std::string::npos);

    // Hot reload: changing the file restarts the script with the new code.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WriteScript(dir, "Mover.py", "from lapis import *\nclass Mover(Script):\n    def on_update(self, dt):\n        self.entity.position = Vector3(42, 0, 0)\n");
    std::filesystem::last_write_time(dir + "/Scripts/Mover.py", std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2));
    for (int i = 0; i < 40; ++i) scene.OnUpdate(1.0f / 60.0f);
    CHECK_NEAR(mover.Transform().Position.x, 42.0f);

    // A broken script reports an error and the scene keeps running.
    Entity bad = scene.CreateEntity("Bad");
    bad.Add<LuauScriptComponent>().ScriptPath = WriteScript(dir, "Bad.py", "from lapis import *\nclass Bad(Script):\n    def on_update(self, dt):\n        undefined_name()\n");
    const size_t before = scene.GetScripts().GetErrors().size();
    scene.OnUpdate(1.0f / 60.0f);
    CHECK(scene.GetScripts().GetErrors().size() > before);
    CHECK(scene.GetScripts().GetErrors().back().find("undefined_name") != std::string::npos);
    scene.OnStop();
    std::filesystem::remove_all(dir);
}
