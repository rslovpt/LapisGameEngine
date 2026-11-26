#pragma once
// Built-in code editor widget (used by the Lapis Editor for Luau scripts):
// tabs, line numbers, Luau/Python syntax highlighting, selection, undo/redo,
// clipboard, auto-indent, save with a compile check that marks the error line.

#include <functional>
#include <string>
#include <vector>

#include "Lapis/UI/UIContext.h"

namespace Lapis {

class Window;

class CodeEditor {
public:
    struct Document {
        std::string Path;                 // absolute path on disk
        std::string Name;                 // tab label
        std::vector<std::string> Lines{""};
        bool  Dirty = false;
        int   Line = 0, Col = 0;          // cursor
        int   AnchorLine = 0, AnchorCol = 0;
        float ScrollY = 0.0f;             // in lines
        int   ErrorLine = -1;             // 0-based, -1 = none
        std::string ErrorText;

        struct Snapshot { std::vector<std::string> Lines; int Line, Col; };
        std::vector<Snapshot> Undo, Redo;
    };

    // Opens (or focuses) a file. Returns false if it can't be read.
    bool Open(const std::string& path, const std::string& displayName);
    bool Save(int index = -1);           // -1 = active document
    void Close(int index);
    bool HasDocuments() const { return !m_Docs.empty(); }
    bool AnyDirty() const;

    // Draws the editor into the rectangle and handles input when focused.
    void Draw(UIContext& ui, Window& window, float x, float y, float w, float h);

    bool HasFocus() const { return m_Focused; }
    void SetFocus(bool focused) { m_Focused = focused; }

    Document* Active() { return m_Active >= 0 && m_Active < (int)m_Docs.size() ? &m_Docs[m_Active] : nullptr; }

    // Returns "" if the source is valid, otherwise "<line>:<message>" (1-based line).
    std::function<std::string(const std::string& path, const std::string& source)> Validate;
    std::function<void(const std::string& path)> OnSaved;

    // --- editing operations (public for tests) -------------------------------
    static std::string Join(const std::vector<std::string>& lines);
    static std::vector<std::string> Split(const std::string& text);
    void InsertText(Document& d, const std::string& text);
    void Backspace(Document& d);
    void DeleteForward(Document& d);
    void NewLine(Document& d);
    bool HasSelection(const Document& d) const { return d.Line != d.AnchorLine || d.Col != d.AnchorCol; }
    std::string SelectedText(const Document& d) const;
    void DeleteSelection(Document& d);
    void PushUndo(Document& d);
    void UndoStep(Document& d);
    void RedoStep(Document& d);

private:
    void HandleKeyboard(Document& d, Window& window);
    void ClampCursor(Document& d) const;
    void Collapse(Document& d) { d.AnchorLine = d.Line; d.AnchorCol = d.Col; }

    std::vector<Document> m_Docs;
    int   m_Active = -1;
    bool  m_Focused = false;
    bool  m_Dragging = false;
    float m_Blink = 0.0f;
    int   m_LastEditFrame = -100;
    int   m_Frame = 0;
};

} // namespace Lapis
