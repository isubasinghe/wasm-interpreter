set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

set (VM_DIR ${CMAKE_CURRENT_LIST_DIR})

file (GLOB_RECURSE VM_LIB_SRCS 
            ${VM_DIR}/src/*.c 
            ${VM_DIR}/src/*.cpp 
            ${VM_DIR}/api/*.c
            ${VM_DIR}/api/*.cpp)

add_library (vm ${VM_LIB_SRCS})
target_include_directories (vm PRIVATE ${VM_DIR}/inc ${VM_DIR})
target_link_libraries (vm PUBLIC spdlog::spdlog fmt::fmt)

install (TARGETS vm DESTINATION .)
