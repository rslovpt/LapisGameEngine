# Lapis executables.

# LapisPlayer: the runtime that plays a project (and the exe packaged games use).
add_executable(LapisPlayer Player/Main.cpp)
target_link_libraries(LapisPlayer PRIVATE LapisEngine)
lapis_set_warnings(LapisPlayer)
if(WIN32)
    # Windowed app (no console); keep a standard main(). Use --console for logs.
    set_target_properties(LapisPlayer PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        target_link_options(LapisPlayer PRIVATE /ENTRY:mainCRTStartup)
    else()
        target_link_options(LapisPlayer PRIVATE -mwindows)
    endif()
endif()

# LapisPackager: validates a project and exports a standalone game folder.
add_executable(LapisPackager Packager/Main.cpp)
target_link_libraries(LapisPackager PRIVATE LapisEngine)
lapis_set_warnings(LapisPackager)
add_dependencies(LapisPackager LapisPlayer)

# LapisDiagnostics: unit tests + GPU/render checks on the target machine.
add_executable(LapisDiagnostics Diagnostics/Main.cpp $<TARGET_OBJECTS:LapisTestCases>)
target_link_libraries(LapisDiagnostics PRIVATE LapisEngine)
target_include_directories(LapisDiagnostics PRIVATE ${CMAKE_SOURCE_DIR}/Tests)
lapis_set_warnings(LapisDiagnostics)
