#pragma once
// Include exactly once, in the game's main .cpp, after defining
// Lapis::CreateApplication().

#include "Lapis/Runtime/Application.h"

int main(int argc, char** argv) {
    Lapis::Application* app = Lapis::CreateApplication(argc, argv);
    if (!app) return 1;
    const int code = app->Run();
    delete app;
    return code;
}
