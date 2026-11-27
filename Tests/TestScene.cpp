#include "TestFramework.h"

#include "Lapis/Scene/Scene.h"
#include "Lapis/Scene/ScriptableEntity.h"

using namespace Lapis;

namespace {
    struct Health { int Value = 100; };
    struct Tag {};
}

LAPIS_TEST(ECS, CreateDestroyGenerations) {
    Registry r;
    EntityID a = r.Create();
    EntityID b = r.Create();
    CHECK(r.IsValid(a) && r.IsValid(b));
    CHECK(r.AliveCount() == 2);
    r.Destroy(a);
    CHECK(!r.IsValid(a));
    EntityID c = r.Create(); // reuses a's slot with a new generation
    CHECK(c.Index == a.Index);
    CHECK(c.Generation != a.Generation);
    CHECK(!r.IsValid(a));
    CHECK(r.IsValid(c));
}

LAPIS_TEST(ECS, Components) {
    Registry r;
    EntityID e = r.Create();
    r.Add<Health>(e).Value = 42;
    CHECK(r.Has<Health>(e));
    CHECK(!r.Has<Tag>(e));
    CHECK(r.Get<Health>(e).Value == 42);
    CHECK(r.TryGet<Tag>(e) == nullptr);
    r.Remove<Health>(e);
    CHECK(!r.Has<Health>(e));
}

LAPIS_TEST(ECS, SwapRemoveKeepsOthers) {
    Registry r;
    EntityID ids[5];
    for (int i = 0; i < 5; ++i) { ids[i] = r.Create(); r.Add<Health>(ids[i]).Value = i; }
    r.Destroy(ids[1]);
    r.Remove<Health>(ids[3]);
    CHECK(r.Get<Health>(ids[0]).Value == 0);
    CHECK(r.Get<Health>(ids[2]).Value == 2);
    CHECK(r.Get<Health>(ids[4]).Value == 4);
    CHECK(r.Count<Health>() == 3);
}

LAPIS_TEST(ECS, EachVisitsMatching) {
    Registry r;
    for (int i = 0; i < 10; ++i) {
        EntityID e = r.Create();
        r.Add<Health>(e).Value = i;
        if (i % 2 == 0) r.Add<Tag>(e);
    }
    int count = 0, sum = 0;
    r.Each<Health, Tag>([&](EntityID, Health& h, Tag&) { ++count; sum += h.Value; });
    CHECK(count == 5);
    CHECK(sum == 0 + 2 + 4 + 6 + 8);
}

LAPIS_TEST(ECS, EachSafeWhileDestroying) {
    Registry r;
    for (int i = 0; i < 6; ++i) r.Add<Health>(r.Create());
    int visited = 0;
    r.Each<Health>([&](EntityID e, Health&) { ++visited; r.Destroy(e); });
    CHECK(visited == 6);
    CHECK(r.AliveCount() == 0);
}

LAPIS_TEST(Scene, HierarchyTransforms) {
    Scene scene;
    Entity parent = scene.CreateEntity("Parent");
    Entity child = scene.CreateEntity("Child");
    scene.SetParent(child, parent);
    parent.Transform().Position = {10, 0, 0};
    parent.Transform().Rotation = Quat::FromAxisAngle(Vec3::Up(), HALF_PI);
    child.Transform().Position = {1, 0, 0};
    scene.UpdateTransforms();
    // Parent rotated +90 deg about Y: child's local +X maps to world -Z.
    CHECK(NearlyEqual(child.Transform().WorldPosition(), Vec3(10, 0, -1)));
    CHECK(scene.GetChildren(parent).size() == 1);

    scene.SetParent(parent, child); // cycle -> refused
    CHECK(parent.Transform().Parent.IsNull());
}

LAPIS_TEST(Scene, FindAndDeferredDestroy) {
    Scene scene;
    Entity a = scene.CreateEntity("Alpha");
    Entity b = scene.CreateEntity("Beta");
    scene.SetParent(b, a);
    CHECK(scene.FindEntityByName("Beta") == b);
    scene.OnStart();
    scene.DestroyEntity(a);
    CHECK(a.IsValid());               // still alive until end of frame
    scene.OnUpdate(0.016f);
    CHECK(!a.IsValid());
    CHECK(!b.IsValid());              // children go too
    CHECK(!scene.FindEntityByName("Alpha"));
}

namespace {
    struct Counter : ScriptableEntity {
        static inline int Created = 0, Updated = 0, Destroyed = 0;
        void OnCreate() override { ++Created; }
        void OnUpdate(float) override { ++Updated; Transform().Position.x += 1.0f; }
        void OnDestroy() override { ++Destroyed; }
    };
}

LAPIS_TEST(Scene, NativeScripts) {
    Counter::Created = Counter::Updated = Counter::Destroyed = 0;
    {
        Scene scene;
        Entity e = scene.CreateEntity("Scripted");
        e.Add<NativeScriptComponent>().Bind<Counter>();
        scene.OnStart();
        scene.OnUpdate(0.1f);
        scene.OnUpdate(0.1f);
        CHECK(Counter::Created == 1);
        CHECK(Counter::Updated == 2);
        CHECK_NEAR(e.Transform().Position.x, 2.0f);
        scene.DestroyEntity(e);
        scene.OnUpdate(0.1f);
        CHECK(Counter::Destroyed == 1);
    }
}

#include "Lapis/Runtime/Project.h"
#include "Lapis/Scene/SceneSerializer.h"
#include "Lapis/Scripting/ScriptEngine.h"

LAPIS_TEST(Scene, SerializeRoundTrip) {
    Scene a("RoundTrip");
    a.Env.FogDensity = 0.02f;
    a.GetPhysics().Settings.Gravity = {0, -5, 0};
    Entity parent = a.CreateEntity("Parent \"quoted\"");
    parent.Transform().Position = {1, 2, 3};
    parent.Transform().SetEulerDegrees({10, 20, 0});
    auto& mr = parent.Add<MeshRendererComponent>();
    mr.MeshName = "Sphere";
    mr.MaterialAsset = Material::Create("m", {0.1f, 0.2f, 0.3f, 1}, 0.7f, 1.0f);
    parent.Add<ColliderComponent>().Shape = ColliderShape::Sphere;
    parent.Add<RigidBodyComponent>().Mass = 4.0f;
    Entity child = a.CreateEntity("Child");
    a.SetParent(child, parent);
    child.Add<LightComponent>().Range = 7.0f;
    child.Add<CameraComponent>().Settings.FieldOfView = Radians(75.0f);
    child.Add<LuauScriptComponent>().ScriptPath = "Scripts/My Script.luau";

    const std::string text = SceneSerializer::Serialize(a);
    Scene b;
    std::string err;
    CHECK(SceneSerializer::Deserialize(b, text, &err, false));
    CHECK(b.GetName() == "RoundTrip");
    CHECK_NEAR(b.Env.FogDensity, 0.02f);
    CHECK_NEAR(b.GetPhysics().Settings.Gravity.y, -5.0f);
    Entity p2 = b.FindEntityByName("Parent \"quoted\"");
    Entity c2 = b.FindEntityByName("Child");
    CHECK(p2 && c2);
    if (!p2 || !c2) return;
    CHECK(NearlyEqual(p2.Transform().Position, Vec3(1, 2, 3)));
    CHECK(NearlyEqual(p2.Transform().GetEulerDegrees(), Vec3(10, 20, 0), 1e-2f));
    CHECK(p2.Get<MeshRendererComponent>().MeshName == "Sphere");
    CHECK_NEAR(p2.Get<MeshRendererComponent>().MaterialAsset->Roughness, 0.7f);
    CHECK(p2.Get<ColliderComponent>().Shape == ColliderShape::Sphere);
    CHECK_NEAR(p2.Get<RigidBodyComponent>().Mass, 4.0f);
    CHECK(c2.Transform().Parent == p2.GetID());
    CHECK_NEAR(c2.Get<LightComponent>().Range, 7.0f);
    CHECK_NEAR(Degrees(c2.Get<CameraComponent>().Settings.FieldOfView), 75.0f);
    CHECK(c2.Get<LuauScriptComponent>().ScriptPath == "Scripts/My Script.luau");

    Scene bad;
    CHECK(!SceneSerializer::Deserialize(bad, "Transform position=1,2,3\n", &err, false)); // component before entity
    CHECK(!SceneSerializer::Deserialize(bad, "Entity name=\"unterminated\n", &err, false));
}

LAPIS_TEST(Scene, ProjectFile) {
    auto p = Project::Parse("# c\nName=My Game\nStartupScene=Scenes/A.scene\nWidth=800\nVSync=false\nMSAA=2\n", "/games/x");
    CHECK(p.has_value());
    if (!p) return;
    CHECK(p->Name == "My Game");
    CHECK(p->Width == 800 && p->Height == 720);
    CHECK(!p->Graphics.VSync && p->Graphics.MSAASamples() == 2);   // legacy keys still load
    p->Graphics.Bloom = false;
    p->Graphics.RenderScale = 0.75f;
    p->Graphics.Tonemapper = 2;
    auto again = Project::Parse(p->Serialize(), p->Directory);
    CHECK(again && again->Name == "My Game" && again->StartupScene == "Scenes/A.scene");
    CHECK(again && again->Graphics == p->Graphics);
}

LAPIS_TEST(Graphics, SettingsReflection) {
    GraphicsSettings g;
    // Every field is reachable by key, round-trips through text and clamps.
    for (const auto& f : GraphicsSettings::Fields()) {
        CHECK(GraphicsSettings::FindField(f.Key) == &f);
        GraphicsSettings copy;
        CHECK(copy.Set(f.Key, g.Get(f.Key)));
        CHECK(copy.GetNumber(f) == g.GetNumber(f));
    }
    CHECK(g.Set("RenderScale", "5"));
    CHECK(g.RenderScale == 2.0f);
    CHECK(g.Set("Tonemapper", "reinhard"));
    CHECK(g.Tonemapper == 2);
    CHECK(!g.Set("NoSuchSetting", "1"));
    CHECK(!g.Set("Bloom", "maybe"));
    CHECK(GraphicsSettings::Categories().size() == 5);

    // Presets are ordered by cost.
    const auto low = GraphicsSettings::FromPreset(GraphicsSettings::Preset::Low);
    const auto ultra = GraphicsSettings::FromPreset(GraphicsSettings::Preset::Ultra);
    CHECK(low.ShadowMapSize() < ultra.ShadowMapSize() && low.MSAASamples() < ultra.MSAASamples());
}

LAPIS_TEST(Graphics, HardwareFallbacks) {
    GPUInfo amd;
    amd.Vendor = GPUVendor::AMD;
    amd.MaxAnisotropy = 16.0f;
    GPUInfo rtx = amd;
    rtx.Vendor = GPUVendor::NVIDIA;
    rtx.NVIDIA_RTX = true;
    const auto* up = GraphicsSettings::FindField("Upscaler");
    bool hidden = false;
    // DLSS is hidden without an RTX GPU, and needs DX12/Vulkan even with one.
    CHECK(!GraphicsSettings::OptionUnavailable(*up, GraphicsSettings::UpscaleDLSS, amd, GraphicsAPI::OpenGL, &hidden).empty() && hidden);
    CHECK(!GraphicsSettings::OptionUnavailable(*up, GraphicsSettings::UpscaleDLSS, rtx, GraphicsAPI::OpenGL, &hidden).empty() && !hidden);
    // Vendor SDKs aren't integrated yet: still unavailable on DX12 / Vulkan.
    CHECK(!GraphicsSettings::OptionUnavailable(*up, GraphicsSettings::UpscaleDLSS, rtx, GraphicsAPI::Vulkan).empty());
    CHECK(!GraphicsSettings::OptionUnavailable(*up, GraphicsSettings::UpscaleFSR3, amd, GraphicsAPI::DirectX12).empty());

    GraphicsSettings g;
    g.Upscaler = GraphicsSettings::UpscaleXeSS;
    g.FrameGeneration = GraphicsSettings::FrameGenFSR3;
    CHECK(!g.ResolveForHardware(amd, GraphicsAPI::OpenGL).empty());
    CHECK(g.Upscaler == GraphicsSettings::UpscaleSpatial && g.FrameGeneration == GraphicsSettings::FrameGenOff);
    CHECK(g.ResolveForHardware(amd, GraphicsAPI::OpenGL).empty()); // nothing left to change
}
