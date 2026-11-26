# LapisEngine - the engine runtime as a static library.

file(GLOB_RECURSE LAPIS_ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)

# Platform backends are only compiled on their own platform.
if(WIN32)
    list(FILTER LAPIS_ENGINE_SOURCES EXCLUDE REGEX ".*/Platform/(Linux|Headless)/.*")
else()
    list(FILTER LAPIS_ENGINE_SOURCES EXCLUDE REGEX ".*/Platform/Win32/.*")
endif()

add_library(LapisEngine STATIC ${LAPIS_ENGINE_SOURCES})

target_include_directories(LapisEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
lapis_set_warnings(LapisEngine)
target_link_libraries(LapisEngine PUBLIC LuauLib)

if(WIN32)
    target_compile_definitions(LapisEngine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(LapisEngine PUBLIC opengl32 gdi32 user32)
else()
    # EGL / libGL are dlopen()ed at runtime; X11 is optional (headless fallback).
    target_link_libraries(LapisEngine PUBLIC ${CMAKE_DL_LIBS})
    find_package(X11)
    if(X11_FOUND)
        target_compile_definitions(LapisEngine PRIVATE LAPIS_HAS_X11=1)
        target_include_directories(LapisEngine PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(LapisEngine PUBLIC ${X11_LIBRARIES})
        message(STATUS "Lapis: X11 window backend enabled")
    else()
        message(STATUS "Lapis: X11 not found - Linux builds are headless only")
    endif()
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES ${LAPIS_ENGINE_SOURCES})
