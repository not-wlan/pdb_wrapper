cmake_minimum_required (VERSION 3.15)
project(llvm-pdb-wrapper)
find_package(LLVM REQUIRED CONFIG)
add_library(llvm-pdb-wrapper wrapper.cpp)

install(TARGETS llvm-pdb-wrapper DESTINATION .)