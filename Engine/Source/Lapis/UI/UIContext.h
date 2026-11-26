#pragma once
// Immediate-mode UI widgets on top of UIRenderer: menu bar with dropdowns and
// submenus, panels with labels/buttons/selectables, toolbar buttons, modal
// message boxes and a status bar.
//
//   ui.BeginFrame(w, h);
//   if (ui.BeginMenuBar()) {
//       if (ui.BeginMenu("File")) {
//           if (ui.MenuItem("Save", "Ctrl+S")) Save();
//           if (ui.BeginMenu("Templates")) { if (ui.MenuItem("First Person")) ...; ui.EndMenu(); }
//           ui.EndMenu();
//       }
//       ui.EndMenuBar();
//   }
//   ui.EndFrame(fbo);

#include <string>
#include <vector>

#include "Lapis/Math/Math.h"
#include "Lapis/UI/UIRenderer.h"

namespace Lapis {

struct UIStyle {
    Vec4 Text{0.92f, 0.93f, 0.95f, 1.0f};
    Vec4 TextDim{0.6f, 0.62f, 0.66f, 1.0f};
    Vec4 Bar{0.11f, 0.115f, 0.13f, 0.98f};
    Vec4 Panel{0.13f, 0.135f, 0.155f, 0.94f};
    Vec4 PanelHeader{0.17f, 0.18f, 0.21f, 1.0f};
    Vec4 Popup{0.15f, 0.155f, 0.175f, 0.99f};
    Vec4 Hover{0.24f, 0.42f, 0.78f, 1.0f};
    Vec4 Active{0.2f, 0.35f, 0.65f, 1.0f};
    Vec4 Button{0.22f, 0.23f, 0.26f, 1.0f};
    Vec4 Border{0.05f, 0.05f, 0.06f, 1.0f};
    Vec4 Accent{0.35f, 0.65f, 1.0f, 1.0f};
    float BarHeight = 26.0f;
    float ItemHeight = 24.0f;
    float Padding = 8.0f;
};

class UIContext : NonCopyable {
public:
    explicit UIContext(UIRenderer& renderer) : m_R(renderer) {}

    // Mouse state comes from Lapis::Input.
    void BeginFrame(u32 width, u32 height);
    void EndFrame(u32 targetFBO);

    // True when the mouse is over UI (or a menu/modal is open): the game or
    // editor camera should ignore mouse input this frame.
    bool WantsMouse() const { return m_WantsMouse; }
    bool AnyMenuOpen() const { return !m_OpenPath.empty(); }

    // --- menu bar -----------------------------------------------------------------
    bool BeginMenuBar();
    void EndMenuBar();
    bool BeginMenu(const std::string& label, bool enabled = true);
    void EndMenu();
    bool MenuItem(const std::string& label, const std::string& shortcut = "", bool checked = false, bool enabled = true);
    void MenuSeparator();
    void CloseMenus() { m_OpenPath.clear(); }

    // --- panels -------------------------------------------------------------------
    void BeginPanel(const std::string& title, float x, float y, float w, float h);
    void EndPanel();
    void Label(const std::string& text, const Vec4* color = nullptr);
    bool Button(const std::string& label, float width = 0.0f);
    bool Selectable(const std::string& label, bool selected);
    void SameLine(float spacing = 6.0f);
    void Spacing(float pixels = 6.0f);

    // Standalone button at an absolute position (toolbars).
    bool ButtonAt(const std::string& label, float x, float y, float w, float h, bool active = false);

    // --- modal message box ----------------------------------------------------------
    void OpenMessage(const std::string& title, const std::string& text);
    bool IsMessageOpen() const { return !m_MessageTitle.empty(); }

    // --- misc -------------------------------------------------------------------
    void StatusBar(const std::string& text);
    void TextOverlay(float x, float y, const std::string& text, const Vec4& color, float scale = 1.0f, bool shadow = true);

    UIStyle Style;
    UIRenderer& Renderer() { return m_R; }

private:
    struct Rect { float X, Y, W, H; bool Contains(const Vec2& p) const { return p.x >= X && p.x < X + W && p.y >= Y && p.y < Y + H; } };
    struct MenuLevel { float X, Y, W, CursorY; size_t Background; std::string Label; };

    bool Hovered(const Rect& r) const;   // hover allowed for the current layer
    bool Clicked(const Rect& r) const;
    void AddHitRect(const Rect& r) { m_HitRects.push_back(r); }
    void DrawMessage();

    UIRenderer& m_R;
    Vec2 m_Mouse;
    bool m_MouseDown = false, m_MousePressed = false, m_MouseReleased = false;
    bool m_WantsMouse = false;
    u32  m_Width = 1, m_Height = 1;

    // Menus
    std::vector<std::string> m_OpenPath;        // labels of open menus, outermost first
    std::vector<MenuLevel>   m_MenuStack;
    std::vector<Rect>        m_PopupRects, m_PrevPopupRects;
    float m_BarCursorX = 0.0f;
    bool  m_InMenuBar = false;
    bool  m_MenuClickConsumed = false;
    bool  m_BuildingPopup = false;

    // Panels
    Rect  m_Panel{};
    float m_CursorX = 0, m_CursorY = 0, m_LineHeight = 0, m_SameLineX = -1;

    std::vector<Rect> m_HitRects, m_PrevHitRects;

    std::string m_MessageTitle, m_MessageText;
};

} // namespace Lapis
