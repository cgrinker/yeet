cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Force MSVC compiler on Windows
# if(WIN32)
#     set(CMAKE_C_COMPILER cl)
#     set(CMAKE_CXX_COMPILER cl)
    
# endif()

project(HelloWorld)

find_package(fmt CONFIG REQUIRED)
find_package(LLVM CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)


## LLVM
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(HandleLLVMOptions)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
llvm_map_components_to_libnames(llvm_libs Support Core IRReader OrcJit X86)


add_executable(main src/main.cpp)

target_link_libraries(main PRIVATE fmt::fmt-header-only)
target_link_libraries(main PRIVATE ${llvm_libs})
target_link_libraries(main PRIVATE cxxopts::cxxopts)
