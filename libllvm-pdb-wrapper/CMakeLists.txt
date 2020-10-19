cmake_minimum_required (VERSION 3.13)
project(llvm-pdb-wrapper)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_library(llvm-pdb-wrapper wrapper.cpp)
target_link_libraries(llvm-pdb-wrapper ${llvm_libs})

install(TARGETS llvm-pdb-wrapper DESTINATION .)