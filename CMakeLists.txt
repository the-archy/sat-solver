cmake_minimum_required(VERSION 3.20)

project(sat_solver LANGUAGES CXX)

add_executable(sat-solver
  main.cpp
  dimacs.cpp
  solver.cpp
)

target_compile_features(sat-solver PRIVATE cxx_std_20)
set_target_properties(sat-solver PROPERTIES
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(sat-solver PRIVATE /W4 /permissive-)
else()
  target_compile_options(sat-solver PRIVATE -Wall -Wextra -Wpedantic)
endif()
