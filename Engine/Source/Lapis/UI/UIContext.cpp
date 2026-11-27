#include "Lapis/UI/UIContext.h"

#include <cmath>
#include <functional>

#include <algorithm>
#include <sstream>

#include "Lapis/Platform/Input.h"

namespace Lapis {

namespace {
    constexpr float PopupWidth = 250.0f;
}

void UIContext::BeginFrame(u32 width, u32 height, float scale) {
    m_R.Begin(width, height, scale);
    m_Width = m_R.GetWidth();
    m_Height = m_R.GetHeight();

    m_Mouse = Input::GetMousePosition() / m_R.GetScale();
    m_MouseDown = Input::IsMouseDown(MouseButton::Left);
    m_MousePressed = Input::WasMousePressed(MouseButton::Left);
    m_MouseReleased = Input::WasMouseReleased(MouseButton::Left);

    m_PrevHitRects = std::move(m_HitRects);
    m_HitRects.clear();
    m_PrevPopupRects = std::move(m_PopupRects);
    m_PopupRects.clear();

    m_WantsMouse = !m_OpenPath.empty() || IsMessageOpen();
    for (const Rect& r : m_PrevHitRects) if (r.Contains(m_Mouse)) m_WantsMouse = true;
    for (const Rect& r : m_PrevPopupRects) if (r.Contains(m_Mouse)) m_WantsMouse = true;

    m_MenuClickConsumed = false;
    m_MenuStack.clear();
    m_BuildingPopup = false;
}

void UIContext::EndFrame(u32 targetFBO) {
    // Clicking anywhere outside the open menus closes them.
    if (m_MousePressed && !m_OpenPath.empty() && !m_MenuClickConsumed) {
        bool inside = false;
        for (const Rect& r : m_PopupRects) if (r.Contains(m_Mouse)) inside = true;
        if (!inside) m_OpenPath.clear();
    }
    if (IsMessageOpen()) DrawMessage();
    m_R.End(targetFBO);
}

bool UIContext::Hovered(const Rect& r) const {
    if (IsMessageOpen()) return false;
    if (!m_BuildingPopup)
        for (const Rect& p : m_PrevPopupRects) if (p.Contains(m_Mouse)) return false;
    return r.Contains(m_Mouse);
}

bool UIContext::Clicked(const Rect& r) const { return m_MousePressed && Hovered(r); }

// =============================================================================
// Menu bar
// =============================================================================
bool UIContext::BeginMenuBar() {
    const Rect bar{0, 0, m_Width, Style.BarHeight};
    m_R.Rect(bar.X, bar.Y, bar.W, bar.H, Style.Bar);
    m_R.Rect(0, bar.H - 1, bar.W, 1, Style.Border);
    AddHitRect(bar);
    m_BarCursorX = 6.0f;
    m_InMenuBar = true;
    return true;
}

void UIContext::EndMenuBar() { m_InMenuBar = false; }

bool UIContext::BeginMenu(const std::string& label, bool enabled) {
    const size_t depth = m_MenuStack.size();
    const float textY = (Style.ItemHeight - m_R.LineHeight()) * 0.5f;

    if (depth == 0) {
        // Top-level entry on the bar.
        const float w = m_R.TextWidth(label) + 18.0f;
        const Rect r{m_BarCursorX, 0, w, Style.BarHeight};
        m_BarCursorX += w;
        const bool open = !m_OpenPath.empty() && m_OpenPath[0] == label;
        const bool hovered = enabled && Hovered(r);
        bool nowOpen = open;
        if (hovered && m_MousePressed) {
            nowOpen = !open;
            m_OpenPath.clear();
            if (nowOpen) m_OpenPath.push_back(label);
            m_MenuClickConsumed = true;
        } else if (hovered && !m_OpenPath.empty() && !open) {
            m_OpenPath = {label};   // slide between menus while one is open
            nowOpen = true;
        }
        if (nowOpen || hovered) m_R.Rect(r.X + 1, r.Y + 3, r.W - 2, r.H - 6, nowOpen ? Style.Active : Style.Button, Style.Rounding);
        m_R.Text(r.X + 9, (Style.BarHeight - m_R.LineHeight()) * 0.5f, label, enabled ? Style.Text : Style.TextDim);
        if (!nowOpen) return false;

        const size_t shadow = m_R.ReserveShadow();
        m_MenuStack.push_back({r.X, Style.BarHeight, PopupWidth, Style.BarHeight + 4, shadow, m_R.ReserveRect(), label});
        m_BuildingPopup = true;
        return true;
    }

    // Submenu entry inside a popup.
    MenuLevel& lvl = m_MenuStack.back();
    const Rect r{lvl.X + 4, lvl.CursorY, lvl.W - 8, Style.ItemHeight};
    lvl.CursorY += Style.ItemHeight;
    const bool open = m_OpenPath.size() > depth && m_OpenPath[depth] == label;
    const bool hovered = enabled && Hovered(r);
    if (hovered && !open) {
        m_OpenPath.resize(depth);
        m_OpenPath.push_back(label);
    }
    const bool nowOpen = enabled && m_OpenPath.size() > depth && m_OpenPath[depth] == label;
    if (hovered && m_MousePressed) m_MenuClickConsumed = true;
    if (nowOpen || hovered) m_R.Rect(r.X, r.Y, r.W, r.H, Style.Hover, Style.Rounding);
    m_R.Text(r.X + 22, r.Y + textY, label, enabled ? Style.Text : Style.TextDim);
    m_R.Text(r.X + r.W - 16, r.Y + textY, ">", Style.TextDim);
    if (!nowOpen) return false;

    const size_t shadow = m_R.ReserveShadow();
    m_MenuStack.push_back({lvl.X + lvl.W - 2, r.Y - 4, PopupWidth, r.Y, shadow, m_R.ReserveRect(), label});
    return true;
}

void UIContext::EndMenu() {
    if (m_MenuStack.empty()) return;
    const MenuLevel lvl = m_MenuStack.back();
    m_MenuStack.pop_back();
    const float h = lvl.CursorY - lvl.Y + 4;
    m_R.SetShadow(lvl.Shadow, lvl.X + 2, lvl.Y + 4, lvl.W, h, Style.PopupRounding, 10.0f, Style.ShadowColor);
    m_R.SetRect(lvl.Background, lvl.X, lvl.Y, lvl.W, h, Style.Popup, Style.PopupRounding);
    m_R.RectOutline(lvl.X, lvl.Y, lvl.W, h, Style.Border, 1.0f, Style.PopupRounding);
    m_PopupRects.push_back({lvl.X, lvl.Y, lvl.W, h});
    m_BuildingPopup = !m_MenuStack.empty();
}

bool UIContext::MenuItem(const std::string& label, const std::string& shortcut, bool checked, bool enabled) {
    if (m_MenuStack.empty()) return false;
    const size_t depth = m_MenuStack.size();
    MenuLevel& lvl = m_MenuStack.back();
    const Rect r{lvl.X + 4, lvl.CursorY, lvl.W - 8, Style.ItemHeight};
    lvl.CursorY += Style.ItemHeight;
    const float textY = r.Y + (Style.ItemHeight - m_R.LineHeight()) * 0.5f;

    const bool hovered = Hovered(r);
    if (hovered && m_OpenPath.size() > depth) m_OpenPath.resize(depth); // close sibling submenus
    if (hovered && enabled) m_R.Rect(r.X, r.Y, r.W, r.H, Style.Hover, Style.Rounding);
    if (checked) m_R.Rect(r.X + 7, r.Y + r.H * 0.5f - 4, 8, 8, Style.Accent, 2.0f);
    m_R.Text(r.X + 22, textY, label, enabled ? Style.Text : Style.TextDim);
    if (!shortcut.empty()) m_R.Text(r.X + r.W - 10 - m_R.TextWidth(shortcut), textY, shortcut, Style.TextDim);

    if (hovered && m_MousePressed) {
        m_MenuClickConsumed = true;
        if (enabled) { m_OpenPath.clear(); return true; }
    }
    return false;
}

void UIContext::MenuSeparator() {
    if (m_MenuStack.empty()) return;
    MenuLevel& lvl = m_MenuStack.back();
    m_R.Rect(lvl.X + 10, lvl.CursorY + 3, lvl.W - 20, 1, Style.Border);
    lvl.CursorY += 7;
}

// =============================================================================
// Panels
// =============================================================================
void UIContext::BeginPanel(const std::string& title, float x, float y, float w, float h) {
    m_Panel = {x, y, w, h};
    m_PanelTitle = title;
    m_LastRight = x;
    m_LastY = y;
    m_R.Rect(x, y, w, h, Style.Panel);
    m_R.Rect(x, y, w, Style.ItemHeight, Style.PanelHeader);
    m_R.Text(x + Style.Padding, y + (Style.ItemHeight - m_R.LineHeight()) * 0.5f, title, Style.Text);
    m_R.RectOutline(x, y, w, h, Style.Border);
    AddHitRect(m_Panel);
    m_CursorX = x + Style.Padding;
    m_CursorY = y + Style.ItemHeight + 6;
    m_LineHeight = Style.ItemHeight;
    m_SameLineX = -1;
}

void UIContext::EndPanel() { m_SameLineX = -1; }

void UIContext::SameLine(float spacing) { m_SameLineX = spacing; }

void UIContext::Spacing(float pixels) { m_CursorY += pixels; }

void UIContext::Label(const std::string& text, const Vec4* color) {
    if (m_CursorY + m_R.LineHeight() > m_Panel.Y + m_Panel.H) return; // clipped
    const float y = m_CursorY;
    m_R.Text(m_CursorX, y, text, color ? *color : Style.Text);
    m_LastRight = m_CursorX + m_R.TextWidth(text);
    m_LastY = y - (Style.ItemHeight - 2 - m_R.LineHeight()) * 0.5f;   // so a SameLine button lines up
    m_SameLineX = -1;
    m_CursorY += m_R.LineHeight() + 3;
}

bool UIContext::Button(const std::string& label, float width) {
    const float w = width > 0 ? width : m_R.TextWidth(label) + 18.0f;
    float x = m_CursorX, y = m_CursorY;
    if (m_SameLineX >= 0) { x = m_LastRight + m_SameLineX; y = m_LastY; m_SameLineX = -1; }
    if (y + Style.ItemHeight > m_Panel.Y + m_Panel.H) return false;
    const Rect r{x, y, w, Style.ItemHeight - 2};
    const bool hovered = Hovered(r);
    m_R.Rect(r.X, r.Y, r.W, r.H, hovered ? (m_MouseDown ? Style.Active : Style.Hover) : Style.Button, Style.Rounding);
    m_R.Text(r.X + (w - m_R.TextWidth(label)) * 0.5f, r.Y + (r.H - m_R.LineHeight()) * 0.5f, label, Style.Text);
    m_LastRight = x + w;
    m_LastY = y;
    m_CursorY = std::max(m_CursorY, y + Style.ItemHeight + 2);
    return hovered && m_MousePressed;
}

bool UIContext::Selectable(const std::string& label, bool selected) {
    if (m_CursorY + Style.ItemHeight > m_Panel.Y + m_Panel.H) return false;
    const Rect r{m_Panel.X + 2, m_CursorY, m_Panel.W - 4, Style.ItemHeight - 2};
    const bool hovered = Hovered(r);
    if (selected) m_R.Rect(r.X, r.Y, r.W, r.H, Style.Active, Style.Rounding);
    else if (hovered) m_R.Rect(r.X, r.Y, r.W, r.H, Style.Button, Style.Rounding);
    m_R.Text(m_CursorX, r.Y + (r.H - m_R.LineHeight()) * 0.5f, label, Style.Text);
    m_CursorY += Style.ItemHeight;
    return hovered && m_MousePressed;
}

bool UIContext::ButtonAt(const std::string& label, float x, float y, float w, float h, bool active) {
    const Rect r{x, y, w, h};
    AddHitRect(r);
    const bool hovered = Hovered(r);
    m_R.Rect(x, y, w, h, active ? Style.Active : hovered ? Style.Hover : Style.Button, Style.Rounding + 1);
    m_R.RectOutline(x, y, w, h, Style.Border, 1.0f, Style.Rounding + 1);
    m_R.Text(x + (w - m_R.TextWidth(label)) * 0.5f, y + (h - m_R.LineHeight()) * 0.5f, label, Style.Text);
    return hovered && m_MousePressed;
}

// =============================================================================
// Settings widgets
// =============================================================================
u64 UIContext::WidgetID(const std::string& label) const {
    return (u64)std::hash<std::string>{}(m_PanelTitle + "##" + label);
}

bool UIContext::RowBegin(float height, Rect& row) {
    row = {m_Panel.X + Style.Padding, m_CursorY, m_Panel.W - Style.Padding * 2 - (m_Scrolling ? 10.0f : 0.0f), height};
    m_CursorY += height + 3;
    m_LastHovered = false;
    if (m_Scrolling) return row.Y >= m_ScrollTop - 0.5f && row.Y + row.H <= m_ScrollBottom + 0.5f;
    return row.Y + row.H <= m_Panel.Y + m_Panel.H;
}

void UIContext::Heading(const std::string& text) {
    Rect row;
    const float h = m_R.LineHeight() + 8;
    if (!RowBegin(h, row)) return;
    m_R.Text(row.X, row.Y + 6, text, Style.Accent);
    m_R.Rect(row.X, row.Y + h - 2, row.W, 1, Style.Button);
}

bool UIContext::Tabs(const std::vector<std::string>& tabs, int& current) {
    const float h = Style.ItemHeight + 2;
    float x = m_Panel.X + Style.Padding;
    const float y = m_CursorY;
    bool changed = false;
    for (int i = 0; i < (int)tabs.size(); ++i) {
        const float w = m_R.TextWidth(tabs[i]) + 22;
        const Rect r{x, y, w, h};
        const bool hovered = Hovered(r), active = i == current;
        m_R.Rect(r.X, r.Y, r.W, r.H, active ? Style.Active : hovered ? Style.Hover : Style.Button, Style.Rounding);
        m_R.Text(r.X + 11, r.Y + (h - m_R.LineHeight()) * 0.5f, tabs[i], active ? Style.Text : Style.TextDim);
        if (hovered && m_MousePressed && !active) { current = i; changed = true; }
        x += w + 4;
    }
    m_CursorY = y + h + 8;
    return changed;
}

bool UIContext::Checkbox(const std::string& label, bool& value) {
    Rect row;
    if (!RowBegin(Style.ItemHeight, row)) return false;
    const bool hovered = Hovered(row);
    m_LastHovered = hovered;
    if (hovered) m_R.Rect(row.X - 4, row.Y, row.W + 8, row.H, Vec4(1, 1, 1, 0.04f), Style.Rounding);
    const float labelY = row.Y + (row.H - m_R.LineHeight()) * 0.5f;
    m_R.Text(row.X, labelY, label, Style.Text);
    const float bx = row.X + row.W * Style.LabelColumn, box = row.H - 8;
    m_R.Rect(bx, row.Y + 4, box, box, value ? Style.Accent : Style.Button, Style.Rounding);
    if (value) m_R.Text(bx + (box - m_R.TextWidth("x")) * 0.5f, labelY - 1, "x", Vec4(0.05f, 0.05f, 0.08f, 1));
    m_R.Text(bx + box + 8, labelY, value ? "On" : "Off", Style.TextDim);
    if (hovered && m_MousePressed) { value = !value; return true; }
    return false;
}

bool UIContext::SliderFloat(const std::string& label, float& value, float min, float max, float step, const std::string& display) {
    Rect row;
    const u64 id = WidgetID(label);
    if (!RowBegin(Style.ItemHeight, row)) { if (m_ActiveID == id && !m_MouseDown) m_ActiveID = 0; return false; }
    const bool hovered = Hovered(row);
    m_LastHovered = hovered;
    if (hovered) m_R.Rect(row.X - 4, row.Y, row.W + 8, row.H, Vec4(1, 1, 1, 0.04f), Style.Rounding);
    m_R.Text(row.X, row.Y + (row.H - m_R.LineHeight()) * 0.5f, label, Style.Text);

    const float valueW = 64.0f;
    const Rect track{row.X + row.W * Style.LabelColumn, row.Y + 4, row.W * (1.0f - Style.LabelColumn) - valueW - 6, row.H - 8};
    if (Hovered(track) && m_MousePressed) m_ActiveID = id;
    bool changed = false;
    if (m_ActiveID == id) {
        if (!m_MouseDown) m_ActiveID = 0;
        else {
            float t = Clamp((m_Mouse.x - track.X) / std::max(track.W, 1.0f), 0.0f, 1.0f);
            float v = min + t * (max - min);
            if (step > 0.0f) v = Clamp(min + std::round((v - min) / step) * step, min, max);
            if (v != value) { value = v; changed = true; }
        }
    }
    const float t = max > min ? Clamp((value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
    m_R.Rect(track.X, track.Y + track.H * 0.5f - 3, track.W, 6, Style.Button, 3);
    m_R.Rect(track.X, track.Y + track.H * 0.5f - 3, track.W * t, 6, Style.Accent, 3);
    const float knob = track.H;
    m_R.Rect(track.X + track.W * t - knob * 0.5f, track.Y, knob, knob, m_ActiveID == id ? Style.Text : Vec4(0.85f, 0.87f, 0.9f, 1), knob * 0.5f);
    m_R.Text(track.X + track.W + 10, row.Y + (row.H - m_R.LineHeight()) * 0.5f, display, Style.TextDim);
    return changed;
}

bool UIContext::Option(const std::string& label, int& value, const std::vector<std::string>& options,
                       const std::vector<std::string>& reasons, const std::vector<bool>& hidden) {
    if (options.empty()) return false;
    const int n = (int)options.size();
    auto isHidden = [&](int i) { return i < (int)hidden.size() && hidden[i]; };
    auto reason = [&](int i) { return i < (int)reasons.size() ? reasons[i] : std::string(); };
    value = Clamp(value, 0, n - 1);
    const bool unavailable = !reason(value).empty();

    Rect row;
    if (!RowBegin(Style.ItemHeight + (unavailable ? m_R.LineHeight() * 0.85f + 2 : 0.0f), row)) return false;
    const bool hovered = Hovered(row);
    m_LastHovered = hovered;
    if (hovered) m_R.Rect(row.X - 4, row.Y, row.W + 8, row.H, Vec4(1, 1, 1, 0.04f), Style.Rounding);
    const float h = Style.ItemHeight - 2;
    m_R.Text(row.X, row.Y + (h - m_R.LineHeight()) * 0.5f, label, Style.Text);

    const float x0 = row.X + row.W * Style.LabelColumn, w = row.W * (1.0f - Style.LabelColumn);
    const Rect left{x0, row.Y, h, h}, right{x0 + w - h, row.Y, h, h}, mid{x0 + h + 3, row.Y, w - 2 * h - 6, h};
    auto step = [&](int dir) {
        int v = value;
        for (int i = 0; i < n; ++i) {
            v = (v + dir + n) % n;
            if (!isHidden(v)) break;
        }
        if (v == value) return false;
        value = v;
        return true;
    };
    bool changed = false;
    for (const Rect* r : {&left, &right, &mid}) {
        const bool hov = Hovered(*r);
        m_R.Rect(r->X, r->Y, r->W, r->H, hov ? Style.Hover : Style.Button, Style.Rounding);
        if (hov && m_MousePressed) changed |= step(r == &left ? -1 : 1);
    }
    m_R.Text(left.X + (h - m_R.TextWidth("<")) * 0.5f, row.Y + (h - m_R.LineHeight()) * 0.5f, "<", Style.Text);
    m_R.Text(right.X + (h - m_R.TextWidth(">")) * 0.5f, row.Y + (h - m_R.LineHeight()) * 0.5f, ">", Style.Text);
    const std::string& text = options[value];
    m_R.Text(mid.X + std::max(4.0f, (mid.W - m_R.TextWidth(text)) * 0.5f), row.Y + (h - m_R.LineHeight()) * 0.5f, text,
             unavailable ? Style.TextDim : Style.Text);
    if (unavailable) m_R.Text(x0, row.Y + h + 2, reason(value), Vec4(0.95f, 0.7f, 0.35f, 1), 0.85f);
    return changed;
}

void UIContext::BeginScroll(float height, float& scroll) {
    m_Scrolling = true;
    m_ScrollValue = &scroll;
    m_ScrollTop = m_CursorY;
    m_ScrollBottom = std::min(m_CursorY + height, m_Panel.Y + m_Panel.H - 2);
    const Rect area{m_Panel.X, m_ScrollTop, m_Panel.W, m_ScrollBottom - m_ScrollTop};
    if (Hovered(area)) {
        const float wheel = Input::GetScrollDelta();
        if (wheel != 0.0f) scroll = std::max(0.0f, scroll - wheel * Style.ItemHeight * 3);
    }
    m_ScrollContentStart = m_CursorY;
    m_CursorY -= scroll;
}

void UIContext::EndScroll() {
    if (!m_Scrolling) return;
    const float content = m_CursorY + *m_ScrollValue - m_ScrollContentStart;
    const float view = m_ScrollBottom - m_ScrollTop;
    const float maxScroll = std::max(0.0f, content - view);
    *m_ScrollValue = Clamp(*m_ScrollValue, 0.0f, maxScroll);
    if (maxScroll > 0.0f) {
        const float x = m_Panel.X + m_Panel.W - 9;
        const float thumbH = std::max(24.0f, view * view / content);
        const float thumbY = m_ScrollTop + (view - thumbH) * (*m_ScrollValue / maxScroll);
        m_R.Rect(x, m_ScrollTop, 5, view, Style.Button, 2.5f);
        m_R.Rect(x, thumbY, 5, thumbH, Style.TextDim, 2.5f);
    }
    m_Scrolling = false;
    m_ScrollValue = nullptr;
    m_CursorY = m_ScrollBottom + 4;
}

// =============================================================================
// Modal message + misc
// =============================================================================
void UIContext::OpenMessage(const std::string& title, const std::string& text) {
    m_MessageTitle = title;
    m_MessageText = text;
    m_OpenPath.clear();
}

void UIContext::DrawMessage() {
    std::vector<std::string> lines;
    std::istringstream in(m_MessageText);
    std::string line;
    float textW = m_R.TextWidth(m_MessageTitle);
    while (std::getline(in, line)) { lines.push_back(line); textW = std::max(textW, m_R.TextWidth(line)); }

    const float w = std::max(320.0f, textW + 40.0f);
    const float h = Style.ItemHeight + 16 + lines.size() * (m_R.LineHeight() + 2) + 50;
    const float x = (m_Width - w) * 0.5f, y = (m_Height - h) * 0.5f;
    m_R.Rect(0, 0, m_Width, m_Height, {0, 0, 0, 0.45f});
    m_R.Shadow(x + 3, y + 6, w, h, 8.0f, 16.0f, Style.ShadowColor);
    m_R.Rect(x, y, w, h, Style.Popup, 8.0f);
    m_R.Rect(x, y, w, Style.ItemHeight + 8, Style.PanelHeader, 8.0f);
    m_R.Rect(x, y + Style.ItemHeight, w, 8, Style.Popup);
    m_R.RectOutline(x, y, w, h, Style.Border, 1.0f, 8.0f);
    m_R.Text(x + 12, y + (Style.ItemHeight - m_R.LineHeight()) * 0.5f, m_MessageTitle, Style.Text);
    float ty = y + Style.ItemHeight + 12;
    for (const auto& l : lines) { m_R.Text(x + 20, ty, l, Style.Text); ty += m_R.LineHeight() + 2; }

    const Rect ok{x + w - 100, y + h - 36, 84, 26};
    const bool hovered = ok.Contains(m_Mouse);
    m_R.Rect(ok.X, ok.Y, ok.W, ok.H, hovered ? Style.Hover : Style.Button, Style.Rounding + 1);
    m_R.Text(ok.X + (ok.W - m_R.TextWidth("OK")) * 0.5f, ok.Y + (ok.H - m_R.LineHeight()) * 0.5f, "OK", Style.Text);
    if ((hovered && m_MousePressed) || Input::WasKeyPressed(Key::Enter)) { m_MessageTitle.clear(); m_MessageText.clear(); }
}

void UIContext::ProgressDialog(const std::string& title, const std::string& step, float fraction) {
    fraction = Clamp(fraction, 0.0f, 1.0f);
    const float w = 460.0f, h = 128.0f;
    const float x = (m_Width - w) * 0.5f, y = (m_Height - h) * 0.5f;
    m_R.Rect(0, 0, m_Width, m_Height, {0, 0, 0, 0.35f});
    m_R.Shadow(x + 3, y + 6, w, h, 8.0f, 16.0f, Style.ShadowColor);
    m_R.Rect(x, y, w, h, Style.Popup, 8.0f);
    m_R.RectOutline(x, y, w, h, Style.Border, 1.0f, 8.0f);
    m_R.Text(x + 16, y + 14, title, Style.Text, 1.1f);
    const std::string pct = std::to_string((int)(fraction * 100.0f + 0.5f)) + "%";
    const float barX = x + 20, barY = y + 50, barW = w - 40, barH = 22;
    m_R.Rect(barX, barY, barW, barH, Style.Button, barH * 0.5f);
    if (fraction > 0.0f) m_R.Rect(barX, barY, std::max(barW * fraction, barH), barH, Style.Hover, barH * 0.5f);
    m_R.Text(barX + (barW - m_R.TextWidth(pct)) * 0.5f, barY + (barH - m_R.LineHeight()) * 0.5f, pct, Style.Text);
    std::string shown = step;
    while (shown.size() > 4 && m_R.TextWidth(shown) > barW) shown = "..." + shown.substr(4);
    m_R.Text(barX, barY + barH + 12, shown, Style.TextDim);
    AddHitRect({0, 0, m_Width, m_Height});
}

void UIContext::StatusBar(const std::string& text) {
    const float h = 22.0f, y = m_Height - h;
    m_R.Rect(0, y, m_Width, h, Style.Bar);
    m_R.Rect(0, y, m_Width, 1, Style.Border);
    m_R.Text(8, y + (h - m_R.LineHeight()) * 0.5f, text, Style.TextDim);
    AddHitRect({0, y, m_Width, h});
}

void UIContext::TextOverlay(float x, float y, const std::string& text, const Vec4& color, float scale, bool shadow) {
    if (shadow) m_R.Text(x + 1, y + 1, text, {0, 0, 0, color.w * 0.7f}, scale);
    m_R.Text(x, y, text, color, scale);
}

} // namespace Lapis
