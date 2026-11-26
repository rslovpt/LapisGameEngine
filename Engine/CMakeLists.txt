# LapisEngine - the engine runtime as a static library.

file(GLOB_RECURSE LAPIS_ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)

# Platform backends are only compiled on their own platform.
if(WIN32)
    list(FILTER LAPIS_ENGINE_SOURCES EXCLUDE REGEX ".*/Platform/Headless/.*")
else()
    list(FILTER LAPIS_ENGINE_SOURCES EXCLUDE REGEX ".*/Platform/Win32/.*")
endif()

add_library(LapisEngine STATIC ${LAPIS_ENGINE_SOURCES})

target_include_directories(LapisEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
lapis_set_warnings(LapisEngine)

if(WIN32)
    target_compile_definitions(LapisEngine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(LapisEngine PUBLIC opengl32 gdi32 user32)
else()
    # The headless backend dlopen()s libEGL / libGL at runtime.
    target_link_libraries(LapisEngine PUBLIC ${CMAKE_DL_LIBS})
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Source FILES ${LAPIS_ENGINE_SOURCES})
