# Unit tests (no GPU needed). The test cases live in an object library so
# LapisDiagnostics can run the very same tests on the user's machine.
file(GLOB LAPIS_TEST_CASES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Test*.cpp)
list(REMOVE_ITEM LAPIS_TEST_CASES ${CMAKE_CURRENT_SOURCE_DIR}/TestMain.cpp)

add_library(LapisTestCases OBJECT ${LAPIS_TEST_CASES} TestFramework.h)
target_link_libraries(LapisTestCases PUBLIC LapisEngine)
target_include_directories(LapisTestCases PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
lapis_set_warnings(LapisTestCases)

add_executable(LapisTests TestMain.cpp $<TARGET_OBJECTS:LapisTestCases>)
target_link_libraries(LapisTests PRIVATE LapisEngine)
target_include_directories(LapisTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
lapis_set_warnings(LapisTests)
add_test(NAME LapisTests COMMAND LapisTests)
