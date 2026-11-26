#include "Lapis/UI/CodeEditor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_set>

#include "Lapis/Core/FileSystem.h"
#include "Lapis/Core/Log.h"
#include "Lapis/Platform/Input.h"
#include "Lapis/Platform/Window.h"

namespace Lapis {

namespace {

// ----------------------------------------------------------------- highlighting
enum class Tok { Text, Keyword, String, Comment, Number, Global, Punct };

const Vec4& TokColor(Tok t) {
    static const Vec4 colors[] = {
        {0.87f, 0.88f, 0.91f, 1}, // Text
        {0.80f, 0.52f, 0.92f, 1}, // Keyword
        {0.65f, 0.84f, 0.48f, 1}, // String
        {0.47f, 0.56f, 0.47f, 1}, // Comment
        {0.96f, 0.72f, 0.42f, 1}, // Number
        {0.42f, 0.76f, 0.98f, 1}, // Global (engine API)
        {0.70f, 0.72f, 0.78f, 1}, // Punct
    };
    return colors[(int)t];
}

const std::unordered_set<std::string>& LuauKeywords() {
    static const std::unordered_set<std::string> k = {
        "and", "break", "continue", "do", "else", "elseif", "end", "false", "for", "function", "if", "in", "local",
        "nil", "not", "or", "repeat", "return", "then", "true", "until", "while", "type", "export", "self"};
    return k;
}
const std::unordered_set<std::string>& PythonKeywords() {
    static const std::unordered_set<std::string> k = {
        "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del",
        "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
        "not", "or", "pass", "raise", "return", "try", "while", "with", "yield", "self"};
    return k;
}
const std::unordered_set<std::string>& EngineGlobals() {
    static const std::unordered_set<std::string> g = {
        "Scene", "Input", "Physics", "Vector3", "Color", "HUD", "File", "Perf", "Time", "App", "Debug", "vector",
        "math", "string", "table", "print", "warn", "lapis", "os", "pairs", "ipairs", "tostring", "tonumber"};
    return g;
}

struct Span { int Start, End; Tok Kind; };

// Tokenizes one line. `inBlock` carries a multi-line comment/string
// (Luau --[[ ]] / [[ ]], Python """ """) across lines.
std::vector<Span> Highlight(const std::string& line, bool python, int& inBlock) {
    std::vector<Span> spans;
    const int n = (int)line.size();
    int i = 0;
    const auto& keywords = python ? PythonKeywords() : LuauKeywords();
    auto closeBlock = [&](int from, Tok kind) {
        const char* closer = python ? "\"\"\"" : "]]";
        const size_t end = line.find(closer, (size_t)from);
        if (end == std::string::npos) { spans.push_back({from, n, kind}); return n; }
        spans.push_back({from, (int)end + (python ? 3 : 2), kind});
        inBlock = 0;
        return (int)end + (python ? 3 : 2);
    };
    if (inBlock) i = closeBlock(0, inBlock == 1 ? Tok::Comment : Tok::String);

    while (i < n) {
        const char c = line[i];
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        // comments
        if (!python && c == '-' && i + 1 < n && line[i + 1] == '-') {
            if (line.compare(i, 4, "--[[") == 0) { inBlock = 1; i = closeBlock(i, Tok::Comment); continue; }
            spans.push_back({i, n, Tok::Comment});
            break;
        }
        if (python && c == '#') { spans.push_back({i, n, Tok::Comment}); break; }
        // block strings
        if (!python && line.compare(i, 2, "[[") == 0) {
            inBlock = 2;
            const int start = i;
            const size_t end = line.find("]]", (size_t)i + 2);
            if (end == std::string::npos) { spans.push_back({start, n, Tok::String}); i = n; }
            else { spans.push_back({start, (int)end + 2, Tok::String}); inBlock = 0; i = (int)end + 2; }
            continue;
        }
        if (python && line.compare(i, 3, "\"\"\"") == 0) {
            inBlock = 2;
            const size_t end = line.find("\"\"\"", (size_t)i + 3);
            if (end == std::string::npos) { spans.push_back({i, n, Tok::String}); i = n; }
            else { spans.push_back({i, (int)end + 3, Tok::String}); inBlock = 0; i = (int)end + 3; }
            continue;
        }
        // strings
        if (c == '"' || c == '\'' || c == '`') {
            int j = i + 1;
            while (j < n && line[j] != c) { if (line[j] == '\\') ++j; ++j; }
            spans.push_back({i, std::min(j + 1, n), Tok::String});
            i = std::min(j + 1, n);
            continue;
        }
        // numbers
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)line[i + 1]))) {
            int j = i + 1;
            while (j < n && (std::isalnum((unsigned char)line[j]) || line[j] == '.' || line[j] == '_')) ++j;
            spans.push_back({i, j, Tok::Number});
            i = j;
            continue;
        }
        // identifiers
        if (std::isalpha((unsigned char)c) || c == '_') {
            int j = i + 1;
            while (j < n && (std::isalnum((unsigned char)line[j]) || line[j] == '_')) ++j;
            const std::string word = line.substr(i, j - i);
            Tok kind = Tok::Text;
            if (keywords.count(word)) kind = Tok::Keyword;
            else if (EngineGlobals().count(word)) kind = Tok::Global;
            spans.push_back({i, j, kind});
            i = j;
            continue;
        }
        spans.push_back({i, i + 1, Tok::Punct});
        ++i;
    }
    return spans;
}

int LeadingSpaces(const std::string& s) {
    int n = 0;
    while (n < (int)s.size() && s[n] == ' ') ++n;
    return n;
}

bool OpensBlock(const std::string& line, bool python) {
    std::string t = line;
    while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
    if (t.empty()) return false;
    if (python) return t.back() == ':';
    auto endsWith = [&](const char* s) { const size_t l = std::strlen(s); return t.size() >= l && t.compare(t.size() - l, l, s) == 0; };
    if (endsWith("then") || endsWith("do") || endsWith("else") || endsWith("{") || endsWith("repeat")) return true;
    if (t.find("function") != std::string::npos && t.back() == ')') return true;
    return false;
}

bool IsPython(const std::string& path) { return FileSystem::ExtensionOf(path) == ".py"; }

} // namespace

// =============================================================================
// documents
// =============================================================================
std::string CodeEditor::Join(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) { if (i) out += '\n'; out += lines[i]; }
    return out;
}

std::vector<std::string> CodeEditor::Split(const std::string& text) {
    std::vector<std::string> lines{""};
    for (char c : text) {
        if (c == '\r') continue;
        if (c == '\n') lines.emplace_back();
        else if (c == '\t') lines.back() += "    ";
        else lines.back() += c;
    }
    return lines;
}

bool CodeEditor::Open(const std::string& path, const std::string& displayName) {
    for (size_t i = 0; i < m_Docs.size(); ++i)
        if (m_Docs[i].Path == path) { m_Active = (int)i; m_Focused = true; return true; }
    auto text = FileSystem::ReadText(path);
    if (!text) { LAPIS_ERROR("Code editor: cannot open '{}'", path); return false; }
    Document d;
    d.Path = path;
    d.Name = displayName;
    d.Lines = Split(*text);
    m_Docs.push_back(std::move(d));
    m_Active = (int)m_Docs.size() - 1;
    m_Focused = true;
    return true;
}

bool CodeEditor::Save(int index) {
    if (index < 0) index = m_Active;
    if (index < 0 || index >= (int)m_Docs.size()) return false;
    Document& d = m_Docs[index];
    const std::string text = Join(d.Lines) + "\n";
    d.ErrorLine = -1;
    d.ErrorText.clear();
    if (Validate) {
        const std::string err = Validate(d.Path, text);
        if (!err.empty()) {
            const size_t colon = err.find(':');
            try { d.ErrorLine = std::stoi(err.substr(0, colon)) - 1; } catch (...) { d.ErrorLine = -1; }
            d.ErrorText = colon == std::string::npos ? err : err.substr(colon + 1);
        }
    }
    if (!FileSystem::WriteText(d.Path, text)) { d.ErrorText = "could not write the file"; return false; }
    d.Dirty = false;
    if (OnSaved) OnSaved(d.Path);
    return true;
}

void CodeEditor::Close(int index) {
    if (index < 0 || index >= (int)m_Docs.size()) return;
    m_Docs.erase(m_Docs.begin() + index);
    if (m_Active >= (int)m_Docs.size()) m_Active = (int)m_Docs.size() - 1;
    if (m_Docs.empty()) m_Focused = false;
}

bool CodeEditor::AnyDirty() const {
    for (const auto& d : m_Docs) if (d.Dirty) return true;
    return false;
}

// =============================================================================
// editing
// =============================================================================
void CodeEditor::PushUndo(Document& d) {
    d.Undo.push_back({d.Lines, d.Line, d.Col});
    if (d.Undo.size() > 200) d.Undo.erase(d.Undo.begin());
    d.Redo.clear();
    d.Dirty = true;
}

void CodeEditor::UndoStep(Document& d) {
    if (d.Undo.empty()) return;
    d.Redo.push_back({d.Lines, d.Line, d.Col});
    d.Lines = d.Undo.back().Lines; d.Line = d.Undo.back().Line; d.Col = d.Undo.back().Col;
    d.Undo.pop_back();
    d.Dirty = true;
    Collapse(d);
}

void CodeEditor::RedoStep(Document& d) {
    if (d.Redo.empty()) return;
    d.Undo.push_back({d.Lines, d.Line, d.Col});
    d.Lines = d.Redo.back().Lines; d.Line = d.Redo.back().Line; d.Col = d.Redo.back().Col;
    d.Redo.pop_back();
    d.Dirty = true;
    Collapse(d);
}

void CodeEditor::ClampCursor(Document& d) const {
    d.Line = Clamp(d.Line, 0, (int)d.Lines.size() - 1);
    d.Col = Clamp(d.Col, 0, (int)d.Lines[d.Line].size());
    d.AnchorLine = Clamp(d.AnchorLine, 0, (int)d.Lines.size() - 1);
    d.AnchorCol = Clamp(d.AnchorCol, 0, (int)d.Lines[d.AnchorLine].size());
}

std::string CodeEditor::SelectedText(const Document& d) const {
    if (!HasSelection(d)) return {};
    int l0 = d.AnchorLine, c0 = d.AnchorCol, l1 = d.Line, c1 = d.Col;
    if (l1 < l0 || (l1 == l0 && c1 < c0)) { std::swap(l0, l1); std::swap(c0, c1); }
    if (l0 == l1) return d.Lines[l0].substr(c0, c1 - c0);
    std::string out = d.Lines[l0].substr(c0);
    for (int l = l0 + 1; l < l1; ++l) out += "\n" + d.Lines[l];
    out += "\n" + d.Lines[l1].substr(0, c1);
    return out;
}

void CodeEditor::DeleteSelection(Document& d) {
    if (!HasSelection(d)) return;
    int l0 = d.AnchorLine, c0 = d.AnchorCol, l1 = d.Line, c1 = d.Col;
    if (l1 < l0 || (l1 == l0 && c1 < c0)) { std::swap(l0, l1); std::swap(c0, c1); }
    d.Lines[l0] = d.Lines[l0].substr(0, c0) + d.Lines[l1].substr(c1);
    d.Lines.erase(d.Lines.begin() + l0 + 1, d.Lines.begin() + l1 + 1);
    d.Line = d.AnchorLine = l0;
    d.Col = d.AnchorCol = c0;
}

void CodeEditor::InsertText(Document& d, const std::string& text) {
    DeleteSelection(d);
    const auto parts = Split(text);
    std::string& line = d.Lines[d.Line];
    const std::string tail = line.substr(d.Col);
    line = line.substr(0, d.Col) + parts[0];
    if (parts.size() == 1) {
        d.Col = (int)line.size();
        line += tail;
    } else {
        for (size_t i = 1; i < parts.size(); ++i) d.Lines.insert(d.Lines.begin() + d.Line + i, parts[i]);
        d.Line += (int)parts.size() - 1;
        d.Col = (int)d.Lines[d.Line].size();
        d.Lines[d.Line] += tail;
    }
    Collapse(d);
}

void CodeEditor::Backspace(Document& d) {
    if (HasSelection(d)) { DeleteSelection(d); return; }
    if (d.Col > 0) {
        // Remove a whole indentation step when only spaces precede the cursor.
        std::string& line = d.Lines[d.Line];
        int remove = 1;
        if (LeadingSpaces(line) >= d.Col && d.Col % 4 == 0) remove = 4;
        remove = std::min(remove, d.Col);
        line.erase(d.Col - remove, remove);
        d.Col -= remove;
    } else if (d.Line > 0) {
        d.Col = (int)d.Lines[d.Line - 1].size();
        d.Lines[d.Line - 1] += d.Lines[d.Line];
        d.Lines.erase(d.Lines.begin() + d.Line);
        --d.Line;
    }
    Collapse(d);
}

void CodeEditor::DeleteForward(Document& d) {
    if (HasSelection(d)) { DeleteSelection(d); return; }
    std::string& line = d.Lines[d.Line];
    if (d.Col < (int)line.size()) line.erase(d.Col, 1);
    else if (d.Line + 1 < (int)d.Lines.size()) { line += d.Lines[d.Line + 1]; d.Lines.erase(d.Lines.begin() + d.Line + 1); }
    Collapse(d);
}

void CodeEditor::NewLine(Document& d) {
    DeleteSelection(d);
    const std::string& cur = d.Lines[d.Line];
    int indent = LeadingSpaces(cur);
    if (OpensBlock(cur.substr(0, d.Col), IsPython(d.Path))) indent += 4;
    InsertText(d, "\n" + std::string(indent, ' '));
}

// =============================================================================
// input
// =============================================================================
void CodeEditor::HandleKeyboard(Document& d, Window& window) {
    const bool ctrl = Input::IsKeyDown(Key::Control);
    const bool shift = Input::IsKeyDown(Key::Shift);
    bool edited = false;

    auto move = [&](auto fn) {
        fn();
        ClampCursor(d);
        if (!shift) Collapse(d);
        m_Blink = 0.0f;
    };

    if (ctrl) {
        if (Input::WasKeyPressed(Key::S)) Save();
        if (Input::GetKeyPresses(Key::Z)) { if (shift) RedoStep(d); else UndoStep(d); }
        if (Input::GetKeyPresses(Key::Y)) RedoStep(d);
        if (Input::WasKeyPressed(Key::A)) { d.AnchorLine = 0; d.AnchorCol = 0; d.Line = (int)d.Lines.size() - 1; d.Col = (int)d.Lines.back().size(); }
        if (Input::WasKeyPressed(Key::C) && HasSelection(d)) window.SetClipboardText(SelectedText(d));
        if (Input::WasKeyPressed(Key::X) && HasSelection(d)) { window.SetClipboardText(SelectedText(d)); PushUndo(d); DeleteSelection(d); edited = true; }
        if (Input::GetKeyPresses(Key::V)) {
            const std::string clip = window.GetClipboardText();
            if (!clip.empty()) { PushUndo(d); InsertText(d, clip); edited = true; }
        }
    } else {
        const std::string& typed = Input::GetTypedText();
        if (!typed.empty()) {
            PushUndo(d);
            for (char c : typed) if ((unsigned char)c >= 32) InsertText(d, std::string(1, c));
            edited = true;
        }
    }

    for (int i = Input::GetKeyPresses(Key::Backspace); i > 0; --i) { PushUndo(d); Backspace(d); edited = true; }
    for (int i = Input::GetKeyPresses(Key::Delete); i > 0; --i) { PushUndo(d); DeleteForward(d); edited = true; }
    for (int i = Input::GetKeyPresses(Key::Enter); i > 0; --i) { PushUndo(d); NewLine(d); edited = true; }
    if (Input::GetKeyPresses(Key::Tab)) {
        PushUndo(d);
        if (shift) {
            const int remove = std::min(4, LeadingSpaces(d.Lines[d.Line]));
            d.Lines[d.Line].erase(0, remove);
            d.Col = std::max(0, d.Col - remove);
            Collapse(d);
        } else {
            InsertText(d, std::string(4 - d.Col % 4, ' '));
        }
        edited = true;
    }

    for (int i = Input::GetKeyPresses(Key::Left); i > 0; --i)
        move([&] { if (d.Col > 0) --d.Col; else if (d.Line > 0) { --d.Line; d.Col = (int)d.Lines[d.Line].size(); } });
    for (int i = Input::GetKeyPresses(Key::Right); i > 0; --i)
        move([&] { if (d.Col < (int)d.Lines[d.Line].size()) ++d.Col; else if (d.Line + 1 < (int)d.Lines.size()) { ++d.Line; d.Col = 0; } });
    for (int i = Input::GetKeyPresses(Key::Up); i > 0; --i) move([&] { --d.Line; });
    for (int i = Input::GetKeyPresses(Key::Down); i > 0; --i) move([&] { ++d.Line; });
    if (Input::GetKeyPresses(Key::Home)) move([&] {
        if (ctrl) { d.Line = 0; d.Col = 0; return; }
        const int ind = LeadingSpaces(d.Lines[d.Line]);
        d.Col = d.Col == ind ? 0 : ind;
    });
    if (Input::GetKeyPresses(Key::End)) move([&] {
        if (ctrl) d.Line = (int)d.Lines.size() - 1;
        d.Col = (int)d.Lines[d.Line].size();
    });
    if (Input::GetKeyPresses(Key::PageUp)) move([&] { d.Line -= 25; });
    if (Input::GetKeyPresses(Key::PageDown)) move([&] { d.Line += 25; });
    if (Input::WasKeyPressed(Key::Escape)) m_Focused = false;

    if (edited) { m_LastEditFrame = m_Frame; m_Blink = 0.0f; ClampCursor(d); }
}

// =============================================================================
// drawing
// =============================================================================
void CodeEditor::Draw(UIContext& ui, Window& window, float x, float y, float w, float h) {
    ++m_Frame;
    UIRenderer& r = ui.Renderer();
    const UIStyle& st = ui.Style;
    const Vec2 mouse = Input::GetMousePosition() / r.GetScale();
    const bool mouseInside = mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h;

    r.Shadow(x + 2, y + 4, w, h, 6.0f, 12.0f, st.ShadowColor);
    r.Rect(x, y, w, h, {0.10f, 0.105f, 0.12f, 0.98f}, 6.0f);
    r.RectOutline(x, y, w, h, m_Focused ? Vec4(st.Accent.x, st.Accent.y, st.Accent.z, 0.8f) : st.Border, 1.0f, 6.0f);

    // Tabs
    const float tabH = 28.0f;
    float tx = x + 6;
    int closeTab = -1;
    for (int i = 0; i < (int)m_Docs.size(); ++i) {
        const std::string label = m_Docs[i].Name + (m_Docs[i].Dirty ? " *" : "");
        const float tw = r.TextWidth(label) + 40.0f;
        const bool active = i == m_Active;
        const bool hover = mouse.x >= tx && mouse.x < tx + tw && mouse.y >= y + 4 && mouse.y < y + tabH;
        r.Rect(tx, y + 4, tw, tabH - 4, active ? Vec4(0.18f, 0.19f, 0.23f, 1) : hover ? st.Button : Vec4(0.13f, 0.135f, 0.155f, 1), 5.0f);
        r.Text(tx + 10, y + 4 + (tabH - 4 - r.LineHeight()) * 0.5f, label, active ? st.Text : st.TextDim);
        const float cx = tx + tw - 20;
        const bool hoverClose = hover && mouse.x >= cx;
        r.Text(cx + 4, y + 4 + (tabH - 4 - r.LineHeight()) * 0.5f, "x", hoverClose ? Vec4(1, 0.5f, 0.5f, 1) : st.TextDim);
        if (hover && Input::WasMousePressed(MouseButton::Left) && !ui.IsMessageOpen()) {
            if (hoverClose) closeTab = i; else { m_Active = i; m_Focused = true; }
        }
        tx += tw + 4;
    }
    if (closeTab >= 0) Close(closeTab);

    Document* d = Active();
    if (!d) {
        r.Text(x + 20, y + tabH + 20, "No script open.", st.TextDim);
        r.Text(x + 20, y + tabH + 44, "Select an entity with a script and press 'Edit script',", st.TextDim);
        r.Text(x + 20, y + tabH + 66, "or use File > New Script.", st.TextDim);
        return;
    }

    const float lineH = r.LineHeight(1.0f, 1) + 1.0f;
    const float charW = r.TextWidth("M", 1.0f, 1);
    const float statusH = 22.0f;
    const float textTop = y + tabH + 6;
    const float textH = h - tabH - 6 - statusH;
    const int digits = std::max(3, (int)std::to_string(d->Lines.size()).size());
    const float gutter = digits * charW + 18.0f;
    const float textX = x + gutter + 8;
    const int visibleLines = std::max(1, (int)(textH / lineH));

    // Mouse: focus, click to place the cursor, drag to select, wheel to scroll.
    if (Input::WasMousePressed(MouseButton::Left) && !ui.IsMessageOpen()) {
        m_Focused = mouseInside && mouse.y > y + tabH;
        if (m_Focused && mouse.y >= textTop && mouse.y < textTop + textH) {
            d->Line = (int)d->ScrollY + (int)((mouse.y - textTop) / lineH);
            d->Col = (int)std::round((mouse.x - textX) / charW);
            ClampCursor(*d);
            if (!Input::IsKeyDown(Key::Shift)) { d->AnchorLine = d->Line; d->AnchorCol = d->Col; }
            m_Dragging = true;
        }
    }
    if (!Input::IsMouseDown(MouseButton::Left)) m_Dragging = false;
    if (m_Dragging) {
        d->Line = (int)d->ScrollY + (int)std::floor((mouse.y - textTop) / lineH);
        d->Col = (int)std::round((mouse.x - textX) / charW);
        ClampCursor(*d);
    }
    if (mouseInside && Input::GetScrollDelta() != 0.0f)
        d->ScrollY = Clamp(d->ScrollY - Input::GetScrollDelta() * 3.0f, 0.0f, (float)std::max(0, (int)d->Lines.size() - 3));

    if (m_Focused) {
        const int line = d->Line, col = d->Col, count = (int)d->Lines.size();
        HandleKeyboard(*d, window);
        // Keep the cursor visible after keyboard movement / edits.
        if (d->Line != line || d->Col != col || (int)d->Lines.size() != count) {
            if (d->Line < (int)d->ScrollY) d->ScrollY = (float)d->Line;
            if (d->Line >= (int)d->ScrollY + visibleLines) d->ScrollY = (float)(d->Line - visibleLines + 1);
        }
    }
    m_Blink += 1.0f / 60.0f;

    // Selection range (ordered).
    int sl0 = d->AnchorLine, sc0 = d->AnchorCol, sl1 = d->Line, sc1 = d->Col;
    if (sl1 < sl0 || (sl1 == sl0 && sc1 < sc0)) { std::swap(sl0, sl1); std::swap(sc0, sc1); }
    const bool selection = HasSelection(*d);

    // Highlight state must be computed from the top of the file.
    const bool python = IsPython(d->Path);
    int block = 0;
    const int first = (int)d->ScrollY;
    for (int l = 0; l < first && l < (int)d->Lines.size(); ++l) Highlight(d->Lines[l], python, block);

    r.Rect(x + 1, textTop - 2, gutter, textH + 2, {0.085f, 0.09f, 0.105f, 1});
    for (int i = 0; i < visibleLines && first + i < (int)d->Lines.size(); ++i) {
        const int l = first + i;
        const float ly = textTop + i * lineH;
        const std::string& text = d->Lines[l];
        if (l == d->ErrorLine) r.Rect(textX - 4, ly, w - gutter - 14, lineH, {0.55f, 0.12f, 0.12f, 0.45f});
        else if (l == d->Line && m_Focused) r.Rect(textX - 4, ly, w - gutter - 14, lineH, {1, 1, 1, 0.04f});
        if (selection && l >= sl0 && l <= sl1) {
            const int a = l == sl0 ? sc0 : 0;
            const int b = l == sl1 ? sc1 : (int)text.size() + 1;
            r.Rect(textX + a * charW, ly, std::max(1, b - a) * charW, lineH, {0.25f, 0.45f, 0.85f, 0.45f});
        }
        const std::string num = std::to_string(l + 1);
        r.Text(x + gutter - 6 - num.size() * charW, ly, num, l == d->Line ? st.Text : Vec4(0.4f, 0.42f, 0.48f, 1), 1.0f, 1);
        const int maxCols = std::max(0, (int)((x + w - 10 - textX) / charW)); // clip at the panel edge
        for (const Span& s : Highlight(text, python, block)) {
            if (s.Start >= maxCols) break;
            const int end = std::min(s.End, maxCols);
            r.Text(textX + s.Start * charW, ly, std::string_view(text).substr(s.Start, end - s.Start), TokColor(s.Kind), 1.0f, 1);
        }
    }
    // Cursor
    if (m_Focused && std::fmod(m_Blink, 1.0f) < 0.6f && d->Line >= first && d->Line < first + visibleLines)
        r.Rect(textX + d->Col * charW - 1, textTop + (d->Line - first) * lineH + 1, 2, lineH - 2, {0.9f, 0.9f, 1.0f, 1});

    // Status line
    const float sy = y + h - statusH;
    r.Rect(x + 1, sy, w - 2, statusH - 1, {0.085f, 0.09f, 0.105f, 1});
    std::string status = Format("Ln {}, Col {}   |   {}   |   Ctrl+S save, Ctrl+Z undo, Esc leave editor",
                                d->Line + 1, d->Col + 1, python ? "Python" : "Luau");
    Vec4 statusColor = st.TextDim;
    if (!d->ErrorText.empty()) {
        status = Format("Error on line {}: {}", d->ErrorLine + 1, d->ErrorText);
        statusColor = {1.0f, 0.55f, 0.5f, 1};
    }
    r.Text(x + 10, sy + (statusH - r.LineHeight()) * 0.5f, status, statusColor);
}

} // namespace Lapis
