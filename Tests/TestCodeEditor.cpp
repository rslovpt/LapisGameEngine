#include "TestFramework.h"

#include "Lapis/UI/CodeEditor.h"

using namespace Lapis;

namespace {
    CodeEditor::Document Doc(const std::string& text) {
        CodeEditor::Document d;
        d.Path = "test.luau";
        d.Lines = CodeEditor::Split(text);
        return d;
    }
}

LAPIS_TEST(CodeEditor, SplitJoinTabs) {
    auto lines = CodeEditor::Split("a\r\n\tb\nc");
    CHECK(lines.size() == 3);
    CHECK(lines[1] == "    b");
    CHECK(CodeEditor::Join(lines) == "a\n    b\nc");
}

LAPIS_TEST(CodeEditor, InsertMultilineAndUndo) {
    CodeEditor ed;
    auto d = Doc("hello world");
    d.Col = d.AnchorCol = 5;
    ed.PushUndo(d);
    ed.InsertText(d, ",\nnew");
    CHECK(CodeEditor::Join(d.Lines) == "hello,\nnew world");
    CHECK(d.Line == 1 && d.Col == 3);
    ed.UndoStep(d);
    CHECK(CodeEditor::Join(d.Lines) == "hello world");
    ed.RedoStep(d);
    CHECK(CodeEditor::Join(d.Lines) == "hello,\nnew world");
}

LAPIS_TEST(CodeEditor, AutoIndentAndBackspace) {
    CodeEditor ed;
    auto d = Doc("function Foo:OnUpdate(dt)");
    d.Col = d.AnchorCol = (int)d.Lines[0].size();
    ed.NewLine(d);
    CHECK(d.Lines[1] == "    ");          // indented after function(...)
    ed.InsertText(d, "if x then");
    ed.NewLine(d);
    CHECK(d.Lines[2] == "        ");      // nested block
    ed.Backspace(d);                       // removes a whole indent step
    CHECK(d.Lines[2] == "    ");
    ed.Backspace(d);
    ed.Backspace(d);                       // joins with the previous line
    CHECK(d.Lines.size() == 2);
    CHECK(d.Lines[1] == "    if x then");
}

LAPIS_TEST(CodeEditor, SelectionCopyDelete) {
    CodeEditor ed;
    auto d = Doc("one\ntwo\nthree");
    d.AnchorLine = 0; d.AnchorCol = 1;
    d.Line = 2; d.Col = 2;
    CHECK(ed.SelectedText(d) == "ne\ntwo\nth");
    ed.DeleteSelection(d);
    CHECK(CodeEditor::Join(d.Lines) == "oree");
    CHECK(!ed.HasSelection(d));
    ed.InsertText(d, "X");
    CHECK(CodeEditor::Join(d.Lines) == "oXree");
}
