cmake_minimum_required (VERSION 3.14)

project(wasm-vm)
set(CMAKE_BUILD_TYPE Debug)

include(vm_lib.cmake)

include_directories (inc .)
enable_testing()
find_package(doctest REQUIRED)

# --- Main executable --- #
add_executable (wasm-vm main.cpp)
target_link_libraries (wasm-vm vm -lm)

add_executable(jit_test tests/jit_test.cpp)
target_link_libraries(jit_test vm doctest::doctest -lm)
add_test(NAME jit_test COMMAND jit_test)

add_executable(jit_locals_test tests/jit_locals_test.cpp)
target_link_libraries(jit_locals_test vm doctest::doctest -lm)
add_test(NAME jit_locals_test COMMAND jit_locals_test)

add_custom_command(
  TARGET wasm-vm
  POST_BUILD
  COMMAND ${CMAKE_COMMAND}
  ARGS -E create_symlink $<TARGET_FILE:wasm-vm> ../wasm-vm
)
