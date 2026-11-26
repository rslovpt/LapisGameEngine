# Unit tests (no GPU needed).
file(GLOB LAPIS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Test*.cpp)

add_executable(LapisTests ${LAPIS_TEST_SOURCES} TestFramework.h)
target_link_libraries(LapisTests PRIVATE LapisEngine)
lapis_set_warnings(LapisTests)
add_test(NAME LapisTests COMMAND LapisTests)
