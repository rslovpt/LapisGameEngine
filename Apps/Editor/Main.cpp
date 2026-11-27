// Lapis Editor - Unreal-style editor shell.
//
//   Menu bar: File / Edit / Window / Help (File > Templates, Settings, Export Game)
//   Toolbar:  Play / Stop (play-in-editor; the scene is restored on Stop)
//   Panels:   Hierarchy, Inspector, Settings
//   Viewport: editor fly camera (hold right mouse), click to select objects
//
// Usage: LapisEditor [--project <dir>] [--frames N] [--screenshot f.bmp]

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <mutex>
#include <thread>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>

#include "../Common/AppUtils.h"

#include "Lapis/Lapis.h"
#include "Lapis/Runtime/EntryPoint.h"
#include "Lapis/Runtime/Packager.h"
#include "Lapis/UI/CodeEditor.h"

#include "luacode.h"

namespace fs = std::filesystem;
using namespace Lapis;

namespace {

// --- persistent editor settings (LapisEditor.ini next to the exe) -------------
struct EditorSettings {
    std::string LastProject;
    // Editor viewport quality: editor only, never exported. Defaults to a
    // lighter "Medium" so the editor stays responsive.
    GraphicsSettings Viewport = DefaultViewport();
    bool PreviewGameGraphics = false;  // render the viewport with the project's game settings
    bool ShowStats = true;
    bool ShowHierarchy = true;
    bool ShowInspector = true;
    float CameraSpeed = 8.0f;
    float UIScale = 1.0f;     // multiplier on top of the monitor's DPI scale

    static fs::path File() { return fs::path(LapisApp::ExeDir()) / "LapisEditor.ini"; }

    void Load() {
        std::ifstream in(File());
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "LastProject") LastProject = v;
            else if (k.rfind("Viewport.", 0) == 0) {
                std::string value = v;
                const size_t hash = value.find('#');
                if (hash != std::string::npos) value = value.substr(0, hash);
                while (!value.empty() && std::isspace((unsigned char)value.back())) value.pop_back();
                Viewport.Set(k.substr(9), value);
            }
            else if (k == "PreviewGameGraphics") PreviewGameGraphics = v == "1";
            else if (k == "VSync") Viewport.VSync = v == "1";   // older ini files
            else if (k == "ShowStats") ShowStats = v == "1";
            else if (k == "ShowHierarchy") ShowHierarchy = v == "1";
            else if (k == "ShowInspector") ShowInspector = v == "1";
            else if (k == "CameraSpeed") { try { CameraSpeed = std::stof(v); } catch (...) {} }
            else if (k == "UIScale") { try { UIScale = Clamp(std::stof(v), 0.5f, 3.0f); } catch (...) {} }
        }
    }
    void Save() const {
        std::ofstream out(File());
        out << "LastProject=" << LastProject << "\nPreviewGameGraphics=" << PreviewGameGraphics << "\nShowStats=" << ShowStats
            << "\nShowHierarchy=" << ShowHierarchy << "\nShowInspector=" << ShowInspector << "\nCameraSpeed=" << CameraSpeed << "\nUIScale=" << UIScale << "\n";
        out << "# Editor viewport quality (editor only - not exported)\n";
        Viewport.Serialize(out, "Viewport.");
    }

    static GraphicsSettings DefaultViewport() {
        GraphicsSettings g = GraphicsSettings::FromPreset(GraphicsSettings::Preset::Medium);
        g.Fullscreen = false;
        return g;
    }
};

fs::path FindRepoFolder(const char* name) {
    const fs::path exe = LapisApp::ExeDir();
    for (const char* rel : {"..", "../..", "../../..", "."}) {
        const fs::path p = (exe / rel / name).lexically_normal();
        std::error_code ec;
        if (fs::is_directory(p, ec)) return p;
    }
    return (exe / ".." / name).lexically_normal();
}

std::string Fmt3(const Vec3& v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.2f, %.2f, %.2f", v.x, v.y, v.z);
    return buf;
}

} // namespace

class EditorApp : public Application {
public:
    EditorApp(const ApplicationSpec& spec, EditorSettings settings, std::string projectDir)
        : Application(spec), m_Settings(std::move(settings)), m_StartProject(std::move(projectDir)) {}

protected:
    // =========================================================================
    // lifecycle
    // =========================================================================
    bool OnInit() override {
        m_ProjectsRoot = FindRepoFolder("Projects");
        m_TemplatesRoot = FindRepoFolder("Templates");
        m_ExportRoot = FindRepoFolder("Export");
        if (!OpenProject(m_StartProject)) NewUntitledScene();
        ApplyEditorGraphics();

        // Built-in code editor: compile-check Luau on save (errors mark the line).
        m_Code.Validate = [](const std::string& path, const std::string& source) -> std::string {
            if (FileSystem::ExtensionOf(path) != ".luau") return {};
            size_t size = 0;
            char* bc = luau_compile(source.data(), source.size(), nullptr, &size);
            std::string err;
            if (bc && size > 0 && bc[0] == 0) err = std::string(bc + 1, size - 1); // ":<line>: <message>"
            std::free(bc);
            if (!err.empty() && err[0] == ':') err = err.substr(1);
            return err;
        };
        m_Code.OnSaved = [this](const std::string& path) {
            m_Status = "Saved " + fs::path(path).filename().string() + (m_Playing ? " - hot-reloading" : "");
        };
        return true;
    }

    void OnShutdown() override {
        if (m_Export.Thread.joinable()) m_Export.Thread.join();
        m_Settings.Save();
    }

    void OnScriptQuit() override { if (m_Playing) StopPlay(); }

    float GetUIScale() override { return GetWindow().GetContentScale() * m_Settings.UIScale; }

    Vec4 GetGameViewport() override {
        const float s = GetUIScale();
        const float top = GetUI().Style.BarHeight + ToolbarHeight, bottom = 22.0f;
        return {0, top, GetWindow().GetWidth() / s, GetWindow().GetHeight() / s - top - bottom};
    }

    const CameraView* GetCameraOverride() override {
        // While playing, look through the game's camera (if it has one).
        if (m_Playing && GetScene() && GetScene()->GetPrimaryCameraEntity()) return nullptr;
        return &m_EditorView;
    }

    // =========================================================================
    // per frame
    // =========================================================================
    void OnUpdate(float dt) override {
        const bool typing = m_ShowCode && m_Code.HasFocus();
        if (!typing) UpdateEditorCamera(dt);
        else UpdateEditorCamera(0.0f);

        // Shortcuts that also work while typing in the code editor.
        const bool ctrl = Input::IsKeyDown(Key::Control);
        if (Input::WasKeyPressed(Key::F5)) TogglePlay();
        if (ctrl && Input::WasKeyPressed(Key::E)) { m_ShowCode = !m_ShowCode; m_Code.SetFocus(m_ShowCode); }
        if (typing) return; // other keys go to the code editor

        if (m_Playing && Input::WasKeyPressed(Key::Escape) && !GetWindow().IsCursorLocked()) StopPlay();
        if (!m_Playing) {
            if (ctrl && Input::WasKeyPressed(Key::S)) SaveScene();
            if (ctrl && Input::GetKeyPresses(Key::Z)) { if (Input::IsKeyDown(Key::Shift)) Redo(); else Undo(); }
            if (ctrl && Input::GetKeyPresses(Key::Y)) Redo();
            if (Input::WasKeyPressed(Key::Delete)) DeleteSelected();
            if (Input::WasKeyPressed(Key::F)) FocusSelected();
        }
        if (Input::WasKeyPressed(Key::F1)) GetScene()->DebugDrawColliders = !GetScene()->DebugDrawColliders;

        // Gizmo drag has priority over click-to-select.
        if (!m_Playing) UpdateGizmo();
        if (!m_Playing && m_GizmoAxis < 0 && Input::WasMousePressed(MouseButton::Left) && !GetUI().WantsMouse()) PickAtMouse();
    }

    void OnRender() override {
        if (m_Playing) return;
        // Light icons so invisible point lights can be found in the viewport.
        GetScene()->GetRegistry().Each<LightComponent, TransformComponent>([&](EntityID, LightComponent& l, TransformComponent& t) {
            if (l.Type != LightType::Point) return;
            const Vec3 c = Normalize(l.Color) * 1.2f;
            GetRenderer().DrawWireSphere(t.WorldPosition(), 0.25f, {c.x, c.y, c.z, 1});
        });
        Entity e = GetScene()->GetEntity(m_Selected);
        if (!e) return;
        Renderer& r = GetRenderer();
        if (auto* mr = e.TryGet<MeshRendererComponent>(); mr && mr->MeshAsset)
            r.DrawAABB(mr->MeshAsset->GetBounds().Transformed(e.Transform().World), {1.0f, 0.6f, 0.1f, 1.0f});
        DrawGizmo(r, e);
        if (auto* l = e.TryGet<LightComponent>(); l && l->Type == LightType::Point)
            r.DrawWireSphere(e.Transform().WorldPosition(), 0.3f, {1, 1, 0.3f, 1});
    }

    void OnUI(UIContext& ui) override {
        const float w = ui.Width(), h = ui.Height();
        const float top = ui.Style.BarHeight + ToolbarHeight, bottom = 22.0f;

        if (m_Settings.ShowHierarchy && !m_Playing) DrawHierarchy(ui, 0, top, 250, h - top - bottom);
        if (m_Settings.ShowInspector && !m_Playing) DrawInspector(ui, w - 300, top, 300, h - top - bottom);
        if (m_ShowCode) {
            const float left = (m_Settings.ShowHierarchy && !m_Playing) ? 258.0f : 8.0f;
            const float right = (m_Settings.ShowInspector && !m_Playing) ? 308.0f : 8.0f;
            m_Code.Draw(ui, GetWindow(), left, top + 8, w - left - right, h - top - bottom - 16);
        }
        if (m_ShowSettings) DrawSettings(ui, w * 0.5f - 220, top + 30, 440, 400);
        const float gw = std::min(640.0f, w - 40), gh = std::min(600.0f, h - top - 60);
        if (m_ShowViewportQuality) DrawViewportQuality(ui, w * 0.5f - gw * 0.5f, top + 30, gw, gh);
        if (m_ShowGraphics) DrawGraphicsSettings(ui, w * 0.5f - gw * 0.5f, top + 30, gw, gh);
        DrawToolbar(ui, w);
        if (m_Settings.ShowStats && !m_ShowCode) DrawStats(ui, w);
        DrawMenuBar(ui);
        ui.StatusBar(StatusText());
        UpdateExport(ui);
    }

private:
    // =========================================================================
    // menu bar
    // =========================================================================
    void DrawMenuBar(UIContext& ui) {
        if (!ui.BeginMenuBar()) return;
        if (ui.BeginMenu("File")) {
            if (ui.MenuItem("New Scene", "", false, !m_Playing)) NewDefaultScene();
            if (ui.BeginMenu("Open Project")) {
                bool any = false;
                std::error_code ec;
                for (auto& entry : fs::directory_iterator(m_ProjectsRoot, ec)) {
                    if (!LapisApp::HasProject(entry.path())) continue;
                    any = true;
                    const std::string name = entry.path().filename().string();
                    if (ui.MenuItem(name, "", entry.path() == fs::path(m_Project.Directory))) OpenProject(entry.path().string());
                }
                if (!any) ui.MenuItem("(no projects in Projects/)", "", false, false);
                ui.EndMenu();
            }
            if (ui.MenuItem("Save Scene", "Ctrl+S", false, !m_Playing && !m_Project.Directory.empty())) SaveScene();
            if (ui.MenuItem("New Script...", "", false, !m_Project.Directory.empty())) NewScript();
            ui.MenuSeparator();
            if (ui.BeginMenu("Templates")) {
                if (ui.MenuItem("First Person Character")) CreateFromTemplate("FirstPerson");
                if (ui.MenuItem("Third Person Character")) CreateFromTemplate("ThirdPerson");
                if (ui.MenuItem("2D Platformer")) CreateFromTemplate("Platformer2D");
                ui.MenuItem("Top Down (coming soon)", "", false, false);
                ui.EndMenu();
            }
            ui.MenuSeparator();
            if (ui.MenuItem("Settings...")) m_ShowSettings = !m_ShowSettings;
            if (ui.MenuItem("Editor Viewport Quality...")) { m_ShowViewportQuality = !m_ShowViewportQuality; m_ShowGraphics = false; }
            if (ui.MenuItem("Export Game...", "", false, !m_Playing && !m_Project.Directory.empty() && !m_Export.Running)) ExportGame();
            if (ui.MenuItem("Open Project Folder", "", false, !m_Project.Directory.empty())) LapisApp::OpenInShell(m_Project.Directory);
            if (ui.MenuItem("Open Project in VS Code", "", false, !m_Project.Directory.empty())) {
                LapisApp::OpenInVSCode(m_Project.Directory);
                m_Status = "Opened in VS Code - saved scripts hot-reload while playing";
            }
            ui.MenuSeparator();
            if (ui.MenuItem("Exit", "Alt+F4")) Quit();
            ui.EndMenu();
        }
        if (ui.BeginMenu("Edit")) {
            if (ui.MenuItem(m_Playing ? "Stop" : "Play", "F5")) TogglePlay();
            ui.MenuSeparator();
            if (ui.MenuItem("Undo", "Ctrl+Z", false, !m_Playing && !m_UndoStack.empty())) Undo();
            if (ui.MenuItem("Redo", "Ctrl+Y", false, !m_Playing && !m_RedoStack.empty())) Redo();
            ui.MenuSeparator();
            if (ui.BeginMenu("Spawn", !m_Playing)) {
                if (ui.MenuItem("Cube")) SpawnPrimitive("Cube", false);
                if (ui.MenuItem("Sphere")) SpawnPrimitive("Sphere", false);
                if (ui.MenuItem("Physics Crate")) SpawnPrimitive("Cube", true);
                if (ui.MenuItem("Physics Ball")) SpawnPrimitive("Sphere", true);
                if (ui.MenuItem("Point Light")) SpawnLight();
                ui.EndMenu();
            }
            if (ui.MenuItem("Duplicate Selected", "", false, !m_Playing && GetScene()->GetEntity(m_Selected))) DuplicateSelected();
            if (ui.MenuItem("Delete Selected", "Del", false, !m_Playing && GetScene()->GetEntity(m_Selected))) DeleteSelected();
            if (ui.MenuItem("Focus Selected", "F", false, !m_Playing && GetScene()->GetEntity(m_Selected))) FocusSelected();
            ui.EndMenu();
        }
        if (ui.BeginMenu("Project", !m_Project.Directory.empty())) {
            if (ui.MenuItem("Game Graphics Settings...")) { m_ShowGraphics = !m_ShowGraphics; m_ShowViewportQuality = false; }
            if (ui.MenuItem("Open Project Folder")) LapisApp::OpenInShell(m_Project.Directory);
            if (ui.MenuItem("Export Game...", "", false, !m_Playing && !m_Export.Running)) ExportGame();
            ui.EndMenu();
        }
        if (ui.BeginMenu("Window")) {
            if (ui.MenuItem("Hierarchy", "", m_Settings.ShowHierarchy)) m_Settings.ShowHierarchy = !m_Settings.ShowHierarchy;
            if (ui.MenuItem("Inspector", "", m_Settings.ShowInspector)) m_Settings.ShowInspector = !m_Settings.ShowInspector;
            if (ui.MenuItem("Code Editor", "Ctrl+E", m_ShowCode)) { m_ShowCode = !m_ShowCode; m_Code.SetFocus(m_ShowCode); }
            if (ui.MenuItem("Stats Overlay", "", m_Settings.ShowStats)) m_Settings.ShowStats = !m_Settings.ShowStats;
            if (ui.MenuItem("Collider View", "F1", GetScene()->DebugDrawColliders)) GetScene()->DebugDrawColliders = !GetScene()->DebugDrawColliders;
            ui.EndMenu();
        }
        if (ui.BeginMenu("Help")) {
            if (ui.MenuItem("Controls")) ShowControls();
            if (ui.MenuItem("Scripting API (Docs)")) LapisApp::OpenInShell((FindRepoFolder("Docs") / "Scripting.md").string());
            if (ui.MenuItem("About Lapis")) {
                ui.OpenMessage("About Lapis Engine", std::string("Lapis Engine ") + EngineVersion +
                    "\nC++17 game engine with OpenGL 4.5 rendering,\nembedded Luau scripting and rigid body physics.");
            }
            ui.EndMenu();
        }
        ui.EndMenuBar();
    }

    // =========================================================================
    // panels
    // =========================================================================
    void DrawToolbar(UIContext& ui, float width) {
        const float y = ui.Style.BarHeight;
        UIRenderer& r = ui.Renderer();
        r.Rect(0, y, width, ToolbarHeight, {0.09f, 0.095f, 0.11f, 0.96f});
        const float bw = 92, x = width * 0.5f - bw - 4;
        if (ui.ButtonAt(m_Playing ? "Playing" : "Play", x, y + 6, bw, 26, m_Playing) && !m_Playing) StartPlay();
        if (ui.ButtonAt("Stop", x + bw + 8, y + 6, bw, 26) && m_Playing) StopPlay();
        if (!m_Playing) {
            const float gx = x - 3 * 78 - 30;
            if (ui.ButtonAt("Move", gx, y + 6, 74, 26, m_GizmoMode == GizmoMode::Move)) m_GizmoMode = GizmoMode::Move;
            if (ui.ButtonAt("Rotate", gx + 78, y + 6, 74, 26, m_GizmoMode == GizmoMode::Rotate)) m_GizmoMode = GizmoMode::Rotate;
            if (ui.ButtonAt("Scale", gx + 156, y + 6, 74, 26, m_GizmoMode == GizmoMode::Scale)) m_GizmoMode = GizmoMode::Scale;
        }
        const std::string title = m_Project.Name.empty() ? "No project" : m_Project.Name + "  -  " + GetScene()->GetName();
        r.Text(12, y + (ToolbarHeight - r.LineHeight()) * 0.5f, title, ui.Style.TextDim);
        if (m_Playing) r.Rect(0, y + ToolbarHeight - 2, width, 2, {0.2f, 0.8f, 0.3f, 1.0f});
    }

    void DrawHierarchy(UIContext& ui, float x, float y, float w, float h) {
        ui.BeginPanel("Hierarchy", x, y, w, h);
        Scene& scene = *GetScene();
        // Roots first, children indented under their parent.
        std::vector<std::pair<EntityID, int>> order;
        std::function<void(EntityID, int)> visit = [&](EntityID id, int depth) {
            order.push_back({id, depth});
            for (Entity c : scene.GetChildren(scene.GetEntity(id))) visit(c.GetID(), depth + 1);
        };
        scene.GetRegistry().EachEntity([&](EntityID id) {
            if (scene.GetRegistry().Get<TransformComponent>(id).Parent.IsNull()) visit(id, 0);
        });
        for (auto& [id, depth] : order) {
            Entity e = scene.GetEntity(id);
            std::string label = std::string(depth * 3, ' ') + e.GetName();
            if (e.Has<CameraComponent>()) label += "  [cam]";
            else if (e.Has<LightComponent>()) label += "  [light]";
            if (ui.Selectable(label, id == m_Selected)) m_Selected = id;
        }
        ui.EndPanel();
    }

    void DrawInspector(UIContext& ui, float x, float y, float w, float h) {
        ui.BeginPanel("Inspector", x, y, w, h);
        Entity e = GetScene()->GetEntity(m_Selected);
        if (!e) {
            ui.Label("Nothing selected.", &ui.Style.TextDim);
            ui.Label("Click an object in the viewport", &ui.Style.TextDim);
            ui.Label("or in the Hierarchy.", &ui.Style.TextDim);
            ui.EndPanel();
            return;
        }
        auto& t = e.Transform();
        ui.Label(e.GetName());
        ui.Spacing(4);

        auto axisRow = [&](const char* name, Vec3 value, float step, auto apply) {
            ui.Label(std::string(name) + ":  " + Fmt3(value), &ui.Style.TextDim);
            const char* axes[3] = {"X", "Y", "Z"};
            for (int i = 0; i < 3; ++i) {
                if (i > 0) ui.SameLine(10);
                if (ui.Button(std::string("-") + axes[i], 38)) { RecordUndo(); value[i] -= step; apply(value); }
                ui.SameLine(2);
                if (ui.Button(std::string("+") + axes[i], 38)) { RecordUndo(); value[i] += step; apply(value); }
            }
            ui.Spacing(4);
        };
        axisRow("Position", t.Position, 0.5f, [&](const Vec3& v) { t.Position = v; });
        axisRow("Rotation", t.GetEulerDegrees(), 15.0f, [&](const Vec3& v) { t.SetEulerDegrees(v); });
        axisRow("Scale", t.Scale, 0.25f, [&](const Vec3& v) { t.Scale = Max(v, Vec3(0.05f)); });

        ui.Label("Components:", &ui.Style.TextDim);
        if (auto* mr = e.TryGet<MeshRendererComponent>()) ui.Label("  Mesh Renderer (" + mr->MeshName + ")");
        if (e.Has<ColliderComponent>()) ui.Label(std::string("  Collider (") + (e.Get<ColliderComponent>().Shape == ColliderShape::Sphere ? "sphere" : "box") + ")");
        if (auto* rb = e.TryGet<RigidBodyComponent>()) ui.Label(std::string("  Rigid Body (") + (rb->Type == BodyType::Dynamic ? "dynamic" : rb->Type == BodyType::Static ? "static" : "kinematic") + ")");
        if (e.Has<LightComponent>()) ui.Label("  Light");
        if (e.Has<CameraComponent>()) ui.Label("  Camera");
        if (auto* sc = e.TryGet<LuauScriptComponent>()) {
            ui.Label("  Luau Script: " + sc->ScriptPath);
            if (ui.Button("Edit script")) OpenScript(sc->ScriptPath);
            ui.SameLine();
            if (ui.Button("Edit in VS Code")) LapisApp::OpenInVSCode(m_Project.Path(sc->ScriptPath));
        }
        ui.Spacing(6);
        if (ui.Button("Duplicate")) DuplicateSelected();
        ui.SameLine();
        if (ui.Button("Delete")) DeleteSelected();
        ui.EndPanel();
    }

    void OnScriptGraphics(const GraphicsSettings& settings) override {
        GraphicsSettings g = settings;
        g.Fullscreen = false;          // a game's settings menu must not take over the editor
        ApplyGraphicsSettings(g);
        m_Status = "A script changed the graphics settings (restored when play stops)";
    }

    // Renders the viewport with either the editor's own quality or (preview)
    // the project's game settings. Fullscreen never applies to the editor.
    void ApplyEditorGraphics() {
        GraphicsSettings g = m_Settings.PreviewGameGraphics && !m_Project.Directory.empty() ? m_Project.Graphics : m_Settings.Viewport;
        g.Fullscreen = false;
        ApplyGraphicsSettings(g);
    }

    // Shared body of both quality panels, generated from the settings
    // reflection table. Returns true if anything changed.
    struct GraphicsPanelState { int Tab = 0; float Scroll = 0.0f; };
    bool DrawGraphicsFields(UIContext& ui, GraphicsSettings& gs, GraphicsPanelState& state, float bottom, bool game) {
        const GPUInfo& gpu = GetRenderer().GetGPUInfo();
        const GraphicsAPI api = GetRenderer().GetAPI();
        bool changed = false;

        ui.Label("Preset:", &ui.Style.TextDim);
        ui.SameLine(8);
        for (auto preset : {GraphicsSettings::Preset::Low, GraphicsSettings::Preset::Medium, GraphicsSettings::Preset::High, GraphicsSettings::Preset::Ultra}) {
            if (ui.Button(GraphicsSettings::PresetName(preset), 80)) { gs.ApplyPreset(preset); changed = true; }
            ui.SameLine(4);
        }
        if (ui.Button("Defaults", 80)) { gs = game ? GraphicsSettings::FromPreset(GraphicsSettings::Preset::High) : EditorSettings::DefaultViewport(); changed = true; }
        ui.Spacing(2);

        const std::vector<std::string> tabs = GraphicsSettings::Categories();
        if (ui.Tabs(tabs, state.Tab)) state.Scroll = 0.0f;
        const std::string& category = tabs[Clamp(state.Tab, 0, (int)tabs.size() - 1)];

        const char* help = nullptr;
        ui.BeginScroll(bottom - ui.CursorY() - ui.Style.ItemHeight, state.Scroll);
        if (game && category == "Display") {
            static const u32 sizes[][2] = {{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
            std::vector<std::string> names;
            int current = 0;
            for (int i = 0; i < 6; ++i) {
                names.push_back(Format("{} x {}", sizes[i][0], sizes[i][1]));
                if (sizes[i][0] == m_Project.Width && sizes[i][1] == m_Project.Height) current = i;
            }
            if (ui.Option("Window size", current, names)) { m_Project.Width = sizes[current][0]; m_Project.Height = sizes[current][1]; changed = true; }
            if (ui.LastItemHovered()) help = "Size of the game window when not fullscreen.";
        }
        for (const auto& f : GraphicsSettings::Fields()) {
            if (category != f.Category) continue;
            if (!game && std::string(f.Key) == "Fullscreen") continue;   // the editor is never fullscreen
            switch (f.Type) {
                case GraphicsSettings::FieldType::Bool: {
                    bool v = gs.GetNumber(f) != 0.0f;
                    if (ui.Checkbox(f.Label, v)) { gs.SetNumber(f, v ? 1.0f : 0.0f); changed = true; }
                    break;
                }
                case GraphicsSettings::FieldType::Float: {
                    float v = gs.GetNumber(f);
                    std::string display;
                    const std::string key = f.Key;
                    if (key == "RenderScale" || f.Max <= 2.0f) display = Format("{}%", (int)std::lround(v * 100));
                    else display = Format("{} m", (int)std::lround(v));
                    if (key == "Gamma" || key == "BloomThreshold") display = Format("{}", std::lround(v * 100) / 100.0f);
                    if (key == "Temperature") display = Format("{}", (int)std::lround(v * 100));
                    if (key == "RenderScale" && v != 1.0f)
                        display += Format(" ({}x{})", (u32)std::lround(GetWindow().GetWidth() * v), (u32)std::lround(GetWindow().GetHeight() * v));
                    if (ui.SliderFloat(f.Label, v, f.Min, f.Max, f.Step, display)) { gs.SetNumber(f, v); changed = true; }
                    break;
                }
                case GraphicsSettings::FieldType::Int: {
                    float v = gs.GetNumber(f);
                    if (ui.SliderFloat(f.Label, v, f.Min, f.Max, 1.0f, std::to_string((int)v))) { gs.SetNumber(f, v); changed = true; }
                    break;
                }
                case GraphicsSettings::FieldType::Enum: {
                    std::vector<std::string> options, reasons;
                    std::vector<bool> hidden;
                    for (int i = 0; i < (int)f.Options.size(); ++i) {
                        bool hide = false;
                        std::string why = GraphicsSettings::OptionUnavailable(f, i, gpu, api, &hide);
                        if (!why.empty() && !hide) why += game ? " - the game falls back automatically" : " - using the fallback";
                        options.push_back(f.Options[i]);
                        reasons.push_back(why);
                        hidden.push_back(hide);
                    }
                    int v = (int)gs.GetNumber(f);
                    if (ui.Option(f.Label, v, options, reasons, hidden)) { gs.SetNumber(f, (float)v); changed = true; }
                    break;
                }
            }
            if (ui.LastItemHovered() && f.Help && *f.Help) help = f.Help;
        }
        if (category == "Frame Generation") {
            ui.Heading("This computer");
            ui.Label("GPU: " + gpu.Name, &ui.Style.TextDim);
            ui.Label(Format("Vendor: {}    Renderer: OpenGL", GPUInfo::VendorString(gpu.Vendor)), &ui.Style.TextDim);
            ui.Label(gpu.NVIDIA_RTX ? "NVIDIA RTX detected: DLSS options are shown." : "DLSS options appear only on NVIDIA RTX GPUs.", &ui.Style.TextDim);
            ui.Label("FSR 3 / XeSS / DLSS run on the DX12 & Vulkan renderers (Phase 12).", &ui.Style.TextDim);
        }
        ui.EndScroll();
        ui.Label(help ? help : " ", &ui.Style.TextDim);
        return changed;
    }

    void DrawGraphicsSettings(UIContext& ui, float x, float y, float w, float h) {
        ui.BeginPanel("Game Graphics Settings  -  " + m_Project.Name, x, y, w, h);
        ui.Label("Baked into the exported game as its defaults (project.lapis). Players can", &ui.Style.TextDim);
        ui.Label("change them in-game if your scripts use the Luau Graphics library.", &ui.Style.TextDim);
        if (DrawGraphicsFields(ui, m_Project.Graphics, m_GamePanel, y + h - 66, true)) {
            m_Project.Save();
            if (m_Settings.PreviewGameGraphics) ApplyEditorGraphics();
            m_Status = "Game graphics settings saved to project.lapis";
        }
        bool preview = m_Settings.PreviewGameGraphics;
        if (ui.Checkbox("Preview in the editor viewport", preview)) {
            m_Settings.PreviewGameGraphics = preview;
            m_Settings.Save();
            ApplyEditorGraphics();
        }
        if (ui.Button("Close", 90)) m_ShowGraphics = false;
        ui.EndPanel();
    }

    void DrawViewportQuality(UIContext& ui, float x, float y, float w, float h) {
        ui.BeginPanel("Editor Viewport Quality", x, y, w, h);
        ui.Label("Editor only - never exported. Keep it lighter than the game for a fast editor.", &ui.Style.TextDim);
        ui.Label(m_Settings.PreviewGameGraphics ? "Currently previewing the GAME settings (Project > Game Graphics Settings)."
                                                : "The game's own quality: Project > Game Graphics Settings.", &ui.Style.TextDim);
        if (DrawGraphicsFields(ui, m_Settings.Viewport, m_ViewportPanel, y + h - 36, false)) {
            m_Settings.Save();
            ApplyEditorGraphics();
        }
        if (ui.Button("Close", 90)) m_ShowViewportQuality = false;
        ui.EndPanel();
    }

    void DrawSettings(UIContext& ui, float x, float y, float w, float h) {
        ui.BeginPanel("Settings (general)", x, y, w, h);
        ui.Label("Graphics API:  OpenGL 4.5 core", &ui.Style.Text);
        ui.Label("DirectX 12 / Vulkan: planned (see PLANS.txt, Phase 12)", &ui.Style.TextDim);
        ui.Spacing(6);
        if (ui.Button("Editor viewport quality...", 220)) { m_ShowViewportQuality = true; m_ShowSettings = false; }
        if (ui.Button(std::string("Stats overlay: ") + (m_Settings.ShowStats ? "On" : "Off"), 180)) m_Settings.ShowStats = !m_Settings.ShowStats;
        ui.Label(Format("UI size: {}%  (display scale {}%)", (int)(m_Settings.UIScale * 100 + 0.5f),
                        (int)(GetWindow().GetContentScale() * 100 + 0.5f)), &ui.Style.Text);
        if (ui.Button("Smaller", 90)) m_Settings.UIScale = std::max(0.5f, m_Settings.UIScale - 0.1f);
        ui.SameLine();
        if (ui.Button("Larger", 90)) m_Settings.UIScale = std::min(3.0f, m_Settings.UIScale + 0.1f);
        ui.SameLine();
        if (ui.Button("Reset", 90)) m_Settings.UIScale = 1.0f;
        ui.Label("Editor camera speed: " + std::to_string((int)m_Settings.CameraSpeed) + "  (mouse wheel while flying)", &ui.Style.TextDim);
        if (ui.Button("Slower", 90)) m_Settings.CameraSpeed = std::max(1.0f, m_Settings.CameraSpeed * 0.75f);
        ui.SameLine();
        if (ui.Button("Faster", 90)) m_Settings.CameraSpeed = std::min(100.0f, m_Settings.CameraSpeed * 1.33f);
        ui.Spacing(6);
        ui.Label("The game's own graphics: Project > Game Graphics Settings.", &ui.Style.TextDim);
        ui.Spacing(6);
        if (ui.Button("Close", 90)) { m_ShowSettings = false; m_Settings.Save(); }
        ui.EndPanel();
    }

    void DrawStats(UIContext& ui, float width) {
        const RenderStats& s = GetRenderer().GetStats();
        const std::string text = Format("{} FPS   {} draws   {} tris   {} entities", (int)(GetTime().FPS + 0.5f), s.DrawCalls,
                                        s.Triangles, GetScene()->EntityCount());
        const float x = width - ui.Renderer().TextWidth(text) - (m_Settings.ShowInspector && !m_Playing ? 312 : 12);
        ui.TextOverlay(x, ui.Style.BarHeight + 44, text, {0.85f, 0.95f, 0.85f, 1});
    }

    std::string StatusText() const {
        if (m_Playing) return "PLAYING - F5 or Stop to return to the editor (Esc releases the mouse first)";
        if (!m_Status.empty()) return m_Status;
        return "WASD / Q E: move camera   |   Hold right mouse: look   |   Left click: select   |   F5: Play   |   Ctrl+S: Save";
    }

    void ShowControls() {
        GetUI().OpenMessage("Editor controls",
            "Viewport:  hold Right Mouse + move = look,  WASD = move,  Q / E = down / up\n"
            "           Shift = fast,  mouse wheel = fly speed,  Left click = select\n"
            "Keys:      F5 = Play / Stop,  Ctrl+S = Save scene,  Del = delete,  F = focus,\n"
            "           F1 = collider view,  Ctrl+E = code editor\n"
            "Code:      Ctrl+S save (hot-reloads while playing), Ctrl+Z / Ctrl+Y undo / redo,\n"
            "           Ctrl+C / X / V clipboard, Tab / Shift+Tab indent, Esc leaves the editor\n"
            "Play mode: the game's own controls apply; Esc releases the mouse,\n"
            "           Esc again (or F5 / Stop) returns to the editor.");
    }

    // =========================================================================
    // editor camera + picking
    // =========================================================================
    void UpdateEditorCamera(float dt) {
        const bool flying = !m_Playing || !GetScene()->GetPrimaryCameraEntity();
        if (flying) {
            const bool rmb = Input::IsMouseDown(MouseButton::Right);
            if (rmb && !m_Flying && !GetUI().WantsMouse()) { m_Flying = true; GetWindow().SetCursorLocked(true); }
            if (!rmb && m_Flying) { m_Flying = false; GetWindow().SetCursorLocked(false); }
            if (m_Flying) {
                const Vec2 d = Input::GetMouseDelta();
                m_Yaw -= d.x * 0.12f;
                m_Pitch = Clamp(m_Pitch - d.y * 0.12f, -89.0f, 89.0f);
                const float scroll = Input::GetScrollDelta();
                if (scroll != 0) m_Settings.CameraSpeed = Clamp(m_Settings.CameraSpeed * (1.0f + scroll * 0.15f), 1.0f, 100.0f);
            }
            // Keyboard movement works any time (not only while looking with
            // the right mouse button); Ctrl is reserved for shortcuts.
            if (!Input::IsKeyDown(Key::Control) && !GetUI().IsMessageOpen()) {
                const Quat rot = Quat::FromEuler({Radians(m_Pitch), Radians(m_Yaw), 0});
                Vec3 move(0.0f);
                if (Input::IsKeyDown(Key::W) || Input::IsKeyDown(Key::Up))    move += rot * Vec3::Forward();
                if (Input::IsKeyDown(Key::S) || Input::IsKeyDown(Key::Down))  move -= rot * Vec3::Forward();
                if (Input::IsKeyDown(Key::D) || Input::IsKeyDown(Key::Right)) move += rot * Vec3::Right();
                if (Input::IsKeyDown(Key::A) || Input::IsKeyDown(Key::Left))  move -= rot * Vec3::Right();
                if (Input::IsKeyDown(Key::E)) move += Vec3::Up();
                if (Input::IsKeyDown(Key::Q)) move -= Vec3::Up();
                if (LengthSq(move) > 0) m_CamPos += Normalize(move) * m_Settings.CameraSpeed * (Input::IsKeyDown(Key::Shift) ? 3.0f : 1.0f) * dt;
            }
        }
        const Quat rot = Quat::FromEuler({Radians(m_Pitch), Radians(m_Yaw), 0});
        const Mat4 world = ComposeTRS(m_CamPos, rot, Vec3(1.0f));
        m_EditorView.View = Inverse(world);
        m_EditorView.Projection = Perspective(Radians(65.0f), GetWindow().GetAspect(), 0.1f, 1000.0f);
        m_EditorView.Position = m_CamPos;
    }

    // ---- transform gizmo: drag an axis handle to move / rotate / scale ----------
    enum class GizmoMode { Move, Rotate, Scale };

    float GizmoSize(const Vec3& at) const { return std::max(0.5f, Distance(m_CamPos, at) * 0.15f); }

    // Screen position (pixels) of a world point; false if behind the camera.
    bool ToScreen(const Vec3& p, Vec2& out) const {
        const Vec4 c = m_EditorView.ViewProjection() * Vec4(p, 1.0f);
        if (c.w <= 0.01f) return false;
        out = {(c.x / c.w * 0.5f + 0.5f) * GetWindowConst().GetWidth(), (1.0f - (c.y / c.w * 0.5f + 0.5f)) * GetWindowConst().GetHeight()};
        return true;
    }
    const Window& GetWindowConst() const { return const_cast<EditorApp*>(this)->GetWindow(); }

    int HoveredAxis(const Vec3& origin, float size) const {
        Vec2 o;
        if (!ToScreen(origin, o)) return -1;
        const Vec2 m = Input::GetMousePosition();
        int best = -1;
        float bestDist = 10.0f * GetWindowConst().GetContentScale();
        for (int a = 0; a < 3; ++a) {
            Vec3 dir(0.0f); dir[a] = 1.0f;
            Vec2 tip;
            if (!ToScreen(origin + dir * size, tip)) continue;
            const Vec2 seg = tip - o;
            const float len2 = Dot(seg, seg);
            if (len2 < 1.0f) continue;
            const float t = Clamp(Dot(m - o, seg) / len2, 0.0f, 1.0f);
            const float d = Length(m - (o + seg * t));
            if (d < bestDist) { bestDist = d; best = a; }
        }
        return best;
    }

    void UpdateGizmo() {
        Entity e = GetScene()->GetEntity(m_Selected);
        if (!e) { m_GizmoAxis = -1; return; }
        GetScene()->UpdateTransforms();
        auto& t = e.Transform();
        const Vec3 origin = t.WorldPosition();
        const float size = GizmoSize(origin);
        m_GizmoHover = GetUI().WantsMouse() ? -1 : HoveredAxis(origin, size);

        if (m_GizmoAxis < 0 && m_GizmoHover >= 0 && Input::WasMousePressed(MouseButton::Left)) {
            RecordUndo();
            m_GizmoAxis = m_GizmoHover;
            m_GizmoLastMouse = Input::GetMousePosition();
        }
        if (m_GizmoAxis >= 0 && !Input::IsMouseDown(MouseButton::Left)) { m_GizmoAxis = -1; return; }
        if (m_GizmoAxis < 0) return;

        // Mouse movement projected on the axis' screen direction = world units.
        Vec3 dir(0.0f); dir[m_GizmoAxis] = 1.0f;
        Vec2 o, tip;
        if (!ToScreen(origin, o) || !ToScreen(origin + dir * size, tip)) return;
        const Vec2 screenAxis = tip - o;
        const float pixelsPerUnit = std::max(Length(screenAxis) / size, 1e-3f);
        const Vec2 mouse = Input::GetMousePosition();
        const float along = Dot(mouse - m_GizmoLastMouse, Normalize(screenAxis)) / pixelsPerUnit;
        m_GizmoLastMouse = mouse;
        const bool snap = Input::IsKeyDown(Key::Control);

        switch (m_GizmoMode) {
            case GizmoMode::Move:
                t.Position[m_GizmoAxis] += along;
                if (snap) t.Position[m_GizmoAxis] = std::round(t.Position[m_GizmoAxis] * 2.0f) / 2.0f;
                break;
            case GizmoMode::Rotate: {
                Vec3 euler = t.GetEulerDegrees();
                euler[m_GizmoAxis] += along * 45.0f;
                if (snap) euler[m_GizmoAxis] = std::round(euler[m_GizmoAxis] / 15.0f) * 15.0f;
                t.SetEulerDegrees(euler);
                break;
            }
            case GizmoMode::Scale:
                t.Scale[m_GizmoAxis] = std::max(0.05f, t.Scale[m_GizmoAxis] + along);
                if (snap) t.Scale[m_GizmoAxis] = std::max(0.25f, std::round(t.Scale[m_GizmoAxis] * 4.0f) / 4.0f);
                break;
        }
        t.PrevPosition = t.Position;
    }

    void DrawGizmo(Renderer& r, Entity e) {
        const Vec3 origin = e.Transform().WorldPosition();
        const float size = GizmoSize(origin);
        const Vec4 colors[3] = {{1, 0.25f, 0.25f, 1}, {0.3f, 1, 0.3f, 1}, {0.35f, 0.55f, 1, 1}};
        for (int a = 0; a < 3; ++a) {
            Vec3 dir(0.0f); dir[a] = 1.0f;
            const bool hot = a == m_GizmoAxis || (m_GizmoAxis < 0 && a == m_GizmoHover);
            const Vec4 c = hot ? Vec4(1, 1, 0.3f, 1) : colors[a];
            const Vec3 tip = origin + dir * size;
            r.DrawLine(origin, tip, c);
            // handle at the tip: arrow (move), ring (rotate), box (scale)
            const float hs = size * 0.08f;
            if (m_GizmoMode == GizmoMode::Move) {
                Vec3 side(0.0f); side[(a + 1) % 3] = 1.0f;
                Vec3 side2(0.0f); side2[(a + 2) % 3] = 1.0f;
                for (const Vec3& s : {side, -side, side2, -side2}) r.DrawLine(tip, tip - dir * hs * 2.0f + s * hs, c);
            } else if (m_GizmoMode == GizmoMode::Scale) {
                r.DrawAABB(AABB::FromCenterExtents(tip, Vec3(hs)), c);
            } else {
                r.DrawWireSphere(tip, hs, c);
            }
        }
    }

    void PickAtMouse() {
        const Vec2 m = Input::GetMousePosition();
        const float nx = m.x / GetWindow().GetWidth() * 2.0f - 1.0f;
        const float ny = 1.0f - m.y / GetWindow().GetHeight() * 2.0f;
        const Mat4 inv = Inverse(m_EditorView.ViewProjection());
        const Vec3 nearP = inv.TransformPoint({nx, ny, -1.0f});
        const Vec3 farP = inv.TransformPoint({nx, ny, 1.0f});
        const Ray ray{nearP, Normalize(farP - nearP)};

        Scene& scene = *GetScene();
        scene.UpdateTransforms();
        EntityID best;
        float bestT = 1e30f;
        scene.GetRegistry().Each<MeshRendererComponent, TransformComponent>([&](EntityID id, MeshRendererComponent& mr, TransformComponent& t) {
            if (!mr.MeshAsset) return;
            float hit = 0;
            if (RayIntersectsAABB(ray, mr.MeshAsset->GetBounds().Transformed(t.World), hit) && hit < bestT) { bestT = hit; best = id; }
        });
        // Lights/cameras without meshes: small pick spheres.
        scene.GetRegistry().Each<TransformComponent>([&](EntityID id, TransformComponent& t) {
            if (!scene.GetRegistry().Has<LightComponent>(id) && !scene.GetRegistry().Has<CameraComponent>(id)) return;
            float hit = 0;
            if (RayIntersectsSphere(ray, Sphere{t.WorldPosition(), 0.4f}, hit) && hit < bestT) { bestT = hit; best = id; }
        });
        m_Selected = best;
    }

    void FocusSelected() {
        Entity e = GetScene()->GetEntity(m_Selected);
        if (!e) return;
        const Vec3 target = e.Transform().WorldPosition();
        const Quat rot = Quat::FromEuler({Radians(m_Pitch), Radians(m_Yaw), 0});
        m_CamPos = target - (rot * Vec3::Forward()) * 6.0f;
    }

    void ResetEditorCamera() {
        m_CamPos = {0, 5, 14};
        m_Yaw = 0;
        m_Pitch = -15;
        // Start where the game camera is, if there is one.
        if (Entity cam = GetScene()->GetPrimaryCameraEntity()) {
            GetScene()->UpdateTransforms();
            m_CamPos = cam.Transform().WorldPosition();
            const Vec3 f = Normalize(cam.Transform().World.TransformDirection(Vec3::Forward()));
            m_Yaw = Degrees(std::atan2(-f.x, -f.z));
            m_Pitch = Degrees(std::asin(Clamp(f.y, -1.0f, 1.0f)));
        }
    }

    // =========================================================================
    // play mode
    // =========================================================================
    // Snapshot-based undo: every editing action records the scene before it.
    void RecordUndo() {
        if (m_Playing) return;
        m_UndoStack.push_back(SceneSerializer::Serialize(*GetScene()));
        if (m_UndoStack.size() > 100) m_UndoStack.erase(m_UndoStack.begin());
        m_RedoStack.clear();
    }

    void RestoreSnapshot(const std::string& text) {
        const std::string selectedName = GetScene()->GetEntity(m_Selected) ? GetScene()->GetEntity(m_Selected).GetName() : "";
        auto scene = MakeScene();
        SceneSerializer::Deserialize(*scene, text);
        SetScene(scene);
        m_Selected = {};
        if (!selectedName.empty()) if (Entity e = scene->FindEntityByName(selectedName)) m_Selected = e.GetID();
    }

    void Undo() {
        if (m_Playing || m_UndoStack.empty()) return;
        m_RedoStack.push_back(SceneSerializer::Serialize(*GetScene()));
        RestoreSnapshot(m_UndoStack.back());
        m_UndoStack.pop_back();
        m_Status = "Undo";
    }

    void Redo() {
        if (m_Playing || m_RedoStack.empty()) return;
        m_UndoStack.push_back(SceneSerializer::Serialize(*GetScene()));
        RestoreSnapshot(m_RedoStack.back());
        m_RedoStack.pop_back();
        m_Status = "Redo";
    }

    void TogglePlay() { if (m_Playing) StopPlay(); else StartPlay(); }

    void StartPlay() {
        if (m_Playing) return;
        m_Snapshot = SceneSerializer::Serialize(*GetScene());
        m_Playing = true;
        m_Status.clear();
        GetUI().CloseMenus();
        if (m_Flying) { m_Flying = false; GetWindow().SetCursorLocked(false); }
        GetScene()->OnStart();
        if (!m_Project.MainScript.empty()) GetScene()->GetScripts().RunFile(m_Project.MainScript);
        LAPIS_INFO("Editor: Play");
    }

    void StopPlay() {
        if (!m_Playing) return;
        const size_t scriptErrors = GetScene()->GetRegistry().Count<LuauScriptComponent>() > 0 ? GetScene()->GetScripts().GetErrors().size() : 0;
        GetScene()->OnStop();
        GetWindow().SetCursorLocked(false);
        auto restored = MakeScene();
        SceneSerializer::Deserialize(*restored, m_Snapshot);
        SetScene(restored);
        m_Playing = false;
        ApplyEditorGraphics();   // undo any Graphics.Apply() made by the game while playing
        if (!GetScene()->GetEntity(m_Selected)) m_Selected = {};
        m_Status = scriptErrors ? Format("Stopped. {} script error(s) - see the log (Logs/LapisEditor.log).", scriptErrors) : "Stopped - scene restored.";
        LAPIS_INFO("Editor: Stop");
    }

    // =========================================================================
    // projects / scenes
    // =========================================================================
    Ref<Scene> MakeScene() {
        auto scene = CreateRef<Scene>(m_Project.Name.empty() ? "Untitled" : m_Project.Name);
        scene->SetAssetRoot(m_Project.Directory.empty() ? "." : m_Project.Directory);
        return scene;
    }

    bool OpenProject(const std::string& dir) {
        if (dir.empty()) return false;
        auto project = Project::Load(dir);
        if (!project) { m_Status = "Could not open project: " + dir; return false; }
        if (m_Playing) StopPlay();
        m_Project = *project;
        if (m_Settings.PreviewGameGraphics) ApplyEditorGraphics();
        m_UndoStack.clear();
        m_RedoStack.clear();
        auto scene = MakeScene();
        if (!SceneSerializer::Load(*scene, m_Project.Path(m_Project.StartupScene)))
            m_Status = "Startup scene missing - created an empty scene";
        SetScene(scene);
        m_Selected = {};
        ResetEditorCamera();
        m_Settings.LastProject = fs::absolute(dir).lexically_normal().string();
        m_Settings.Save();
        GetWindow().SetTitle("Lapis Editor - " + m_Project.Name);
        m_Spec.Window.Title = "Lapis Editor - " + m_Project.Name;
        if (m_Status.empty() || m_Status.rfind("Could not", 0) == 0) m_Status = "Opened project '" + m_Project.Name + "'";
        LAPIS_INFO("Editor: opened project '{}' ({})", m_Project.Name, m_Project.Directory);
        return true;
    }

    void NewUntitledScene() {
        m_Project = Project{};
        NewDefaultScene();
    }

    void NewDefaultScene() {
        auto scene = MakeScene();
        Entity cam = scene->CreateEntity("Camera");
        cam.Add<CameraComponent>();
        cam.Transform().Position = {0, 4, 12};
        cam.Transform().SetEulerDegrees({-15, 0, 0});
        Entity sun = scene->CreateEntity("Sun");
        sun.Transform().SetEulerDegrees({-50, 35, 0});
        auto& l = sun.Add<LightComponent>();
        l.Type = LightType::Directional;
        l.Intensity = 3.2f;
        Entity ground = scene->CreateEntity("Ground");
        ground.Transform().Position = {0, -0.5f, 0};
        ground.Transform().Scale = {40, 1, 40};
        auto& mr = ground.Add<MeshRendererComponent>();
        mr.MeshName = "Cube";
        mr.MeshAsset = scene->GetAssets().GetMesh("Cube");
        mr.MaterialAsset = Material::Create("Ground", {0.8f, 0.8f, 0.8f, 1}, 0.9f);
        mr.MaterialAsset->AlbedoMap = scene->GetAssets().GetTexture("Checker");
        mr.MaterialAsset->AlbedoMap->SetPath("Checker");
        mr.MaterialAsset->Tiling = {10, 10};
        ground.Add<ColliderComponent>();
        SetScene(scene);
        m_Selected = {};
        ResetEditorCamera();
        m_Status = "New scene";
    }

    void SaveScene() {
        if (m_Project.Directory.empty()) { GetUI().OpenMessage("Save Scene", "Open or create a project first\n(File > Templates or File > Open Project)."); return; }
        const std::string path = m_Project.Path(m_Project.StartupScene);
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        m_Status = SceneSerializer::Save(*GetScene(), path) ? "Saved " + m_Project.StartupScene : "Save FAILED: " + path;
    }

    void CreateFromTemplate(const std::string& templateName) {
        const fs::path src = m_TemplatesRoot / templateName;
        if (!LapisApp::HasProject(src)) {
            GetUI().OpenMessage("Templates", "Template not found:\n" + src.string());
            return;
        }
        std::error_code ec;
        fs::create_directories(m_ProjectsRoot, ec);
        fs::path dst = m_ProjectsRoot / templateName;
        for (int i = 2; fs::exists(dst); ++i) dst = m_ProjectsRoot / (templateName + "_" + std::to_string(i));
        fs::copy(src, dst, fs::copy_options::recursive, ec);
        if (ec) { GetUI().OpenMessage("Templates", "Could not copy the template:\n" + ec.message()); return; }
        if (auto p = Project::Load(dst.string())) {
            p->Name = dst.filename().string();
            p->Save();
        }
        OpenProject(dst.string());
        GetUI().OpenMessage("New project from template",
            "Created project '" + dst.filename().string() + "' from the " + templateName + " template.\n"
            "Folder: " + dst.string() + "\n\nPress Play (F5) to try it.");
    }

    void ExportGame() {
        if (m_Project.Directory.empty() || m_Export.Running) return;
        SaveScene();
        m_Export.Running = true;
        m_Export.Done = false;
        m_Export.Progress = 0.0f;
        m_Export.Thread = std::thread([this, project = m_Project]() {
            const fs::path out = m_ExportRoot / Packager::SafeName(project.Name);
            auto result = Packager::Package(project, Packager::DefaultRuntimePath(), out.string(),
                [this](float fraction, const std::string& step) {
                    m_Export.Progress = fraction;
                    std::lock_guard<std::mutex> lock(m_Export.Mutex);
                    m_Export.Step = step;
                });
            std::lock_guard<std::mutex> lock(m_Export.Mutex);
            m_Export.Result = std::move(result);
            m_Export.Done = true;
        });
    }

    void UpdateExport(UIContext& ui) {
        if (!m_Export.Running) return;
        if (m_Export.Done) {
            m_Export.Thread.join();
            m_Export.Running = false;
            const auto& result = m_Export.Result;
            if (result.Ok) {
                ui.OpenMessage("Export Game", "Exported '" + m_Project.Name + "' (" + std::to_string(result.FilesCopied) +
                    " content files).\n\n" + result.ExePath + "\n\nDouble-click the .exe to play. The folder opens now.");
                LapisApp::OpenInShell(result.OutputDir);
            } else {
                std::string text = "Export failed:\n";
                for (size_t i = 0; i < result.Errors.size() && i < 8; ++i) text += "  - " + result.Errors[i] + "\n";
                ui.OpenMessage("Export Game", text);
            }
            return;
        }
        std::string step;
        { std::lock_guard<std::mutex> lock(m_Export.Mutex); step = m_Export.Step; }
        ui.ProgressDialog("Exporting '" + m_Project.Name + "'", step, m_Export.Progress);
    }

    void OpenScript(const std::string& relative) {
        if (m_Code.Open(m_Project.Path(relative), fs::path(relative).filename().string())) m_ShowCode = true;
    }

    void NewScript() {
        const fs::path dir = fs::path(m_Project.Directory) / "Scripts";
        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path file = dir / "NewScript.luau";
        for (int i = 2; fs::exists(file); ++i) file = dir / ("NewScript" + std::to_string(i) + ".luau");
        const std::string name = file.stem().string();
        FileSystem::WriteText(file.string(),
            "-- " + name + ".luau\n"
            "-- Attach this script to an entity (Inspector) or spawn with Script = \"Scripts/" + file.filename().string() + "\".\n"
            "local " + name + " = {}\n\n"
            "function " + name + ":OnStart()\n    print(\"Hello from \" .. self.Entity.Name)\nend\n\n"
            "function " + name + ":OnUpdate(dt: number)\nend\n\n"
            "return " + name + "\n");
        OpenScript("Scripts/" + file.filename().string());
        // Attach it to the selected entity if it has no script yet.
        if (Entity e = GetScene()->GetEntity(m_Selected); e && !e.Has<LuauScriptComponent>())
            e.Add<LuauScriptComponent>().ScriptPath = "Scripts/" + file.filename().string();
    }

    // =========================================================================
    // editing
    // =========================================================================
    Vec3 SpawnPoint() const {
        const Quat rot = Quat::FromEuler({Radians(m_Pitch), Radians(m_Yaw), 0});
        Vec3 p = m_CamPos + (rot * Vec3::Forward()) * 6.0f;
        p.y = std::max(p.y, 0.5f);
        return p;
    }

    void SpawnPrimitive(const std::string& mesh, bool physics) {
        RecordUndo();
        Scene& s = *GetScene();
        Entity e = s.CreateEntity(physics ? (mesh == "Cube" ? "Crate" : "Ball") : mesh);
        e.Transform().Position = SpawnPoint();
        auto& mr = e.Add<MeshRendererComponent>();
        mr.MeshName = mesh;
        mr.MeshAsset = s.GetAssets().GetMesh(mesh);
        mr.MaterialAsset = Material::Create(mesh, {0.85f, 0.85f, 0.88f, 1}, 0.5f);
        if (physics) {
            e.Add<ColliderComponent>().Shape = mesh == "Sphere" ? ColliderShape::Sphere : ColliderShape::Box;
            e.Add<RigidBodyComponent>();
        }
        m_Selected = e.GetID();
    }

    void SpawnLight() {
        RecordUndo();
        Entity e = GetScene()->CreateEntity("Point Light");
        e.Transform().Position = SpawnPoint() + Vec3(0, 1.5f, 0);
        e.Add<LightComponent>();
        m_Selected = e.GetID();
    }

    void DuplicateSelected() {
        RecordUndo();
        Scene& s = *GetScene();
        Entity src = s.GetEntity(m_Selected);
        if (!src) return;
        // Copy component values first: adding to a pool may move its storage.
        TransformComponent t = src.Get<TransformComponent>();
        std::optional<MeshRendererComponent> mr;
        std::optional<ColliderComponent> col;
        std::optional<RigidBodyComponent> rb;
        std::optional<LightComponent> light;
        std::optional<std::string> script;
        if (auto* c = src.TryGet<MeshRendererComponent>()) mr = *c;
        if (auto* c = src.TryGet<ColliderComponent>()) col = *c;
        if (auto* c = src.TryGet<RigidBodyComponent>()) rb = *c;
        if (auto* c = src.TryGet<LightComponent>()) light = *c;
        if (auto* c = src.TryGet<LuauScriptComponent>()) script = c->ScriptPath;

        Entity copy = s.CreateEntity(src.GetName());
        t.Position += Vec3(1.5f, 0, 0);
        copy.Get<TransformComponent>() = t;
        if (mr) {
            if (mr->MaterialAsset) mr->MaterialAsset = CreateRef<Material>(*mr->MaterialAsset); // own material
            copy.Add<MeshRendererComponent>(*mr);
        }
        if (col) copy.Add<ColliderComponent>(*col);
        if (rb) copy.Add<RigidBodyComponent>(*rb);
        if (light) copy.Add<LightComponent>(*light);
        if (script) copy.Add<LuauScriptComponent>().ScriptPath = *script;
        m_Selected = copy.GetID();
    }

    void DeleteSelected() {
        Entity e = GetScene()->GetEntity(m_Selected);
        if (!e) return;
        RecordUndo();
        GetScene()->DestroyEntity(e);
        GetScene()->FlushPendingDestroys(); // edit mode has no update loop to do it
        m_Selected = {};
    }

    static constexpr float ToolbarHeight = 38.0f;

    struct ExportJob {
        std::thread Thread;
        std::atomic<bool> Running{false}, Done{false};
        std::atomic<float> Progress{0.0f};
        std::mutex Mutex;
        std::string Step;
        Packager::Result Result;
    } m_Export;

    EditorSettings m_Settings;
    std::string m_StartProject;
    Project m_Project;
    fs::path m_ProjectsRoot, m_TemplatesRoot, m_ExportRoot;

    bool m_Playing = false;
    bool m_ShowSettings = false;
    bool m_ShowGraphics = false;
    bool m_ShowViewportQuality = false;
    GraphicsPanelState m_GamePanel, m_ViewportPanel;
    bool m_ShowCode = false;
    CodeEditor m_Code;
    std::string m_Snapshot;
    std::vector<std::string> m_UndoStack, m_RedoStack;
    std::string m_Status;
    EntityID m_Selected;

    CameraView m_EditorView;
    GizmoMode m_GizmoMode = GizmoMode::Move;
    int  m_GizmoAxis = -1, m_GizmoHover = -1;
    Vec2 m_GizmoLastMouse;
    Vec3 m_CamPos{0, 5, 14};
    float m_Yaw = 0, m_Pitch = -15;
    bool m_Flying = false;
};

Application* Lapis::CreateApplication(int argc, char** argv) {
    LapisApp::Args args(argc, argv);
    if (args.Has("--console")) LapisApp::AttachConsole();
    if (args.Has("--headless")) LapisApp::SetEnv("LAPIS_HEADLESS", "1");

    EditorSettings settings;
    settings.Load();
    std::string project = args.Value("--project");
    if (project.empty() && LapisApp::HasProject(settings.LastProject)) project = settings.LastProject;
    if (project.empty()) project = LapisApp::FindProject("");

    ApplicationSpec spec;
    spec.Window.Title = "Lapis Editor";
    spec.Window.Width = (u32)std::stoul(args.Value("--width", "1600"));
    spec.Window.Height = (u32)std::stoul(args.Value("--height", "900"));
    spec.Window.VSync = settings.Viewport.VSync;
    spec.MSAASamples = settings.Viewport.MSAASamples();
    spec.LogFile = (fs::path(LapisApp::ExeDir()) / "Logs" / "LapisEditor.log").string();
    spec.MaxFrames = std::stoull(args.Value("--frames", "0"));
    spec.ScreenshotOnExit = args.Value("--screenshot");
    spec.ShowStatsInTitle = false;
    spec.AutoStartScene = false;
    return new EditorApp(spec, settings, project);
}
