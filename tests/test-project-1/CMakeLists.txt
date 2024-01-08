cmake_minimum_required(VERSION 3.27)
project(test-project-1)

add_library(test-project-lib src/lib/lib.cpp)
add_executable(test-project-exe src/main.cpp)

target_compile_options(test-project-lib PRIVATE /std:c++latest /W4 /WX)
target_compile_options(test-project-exe PRIVATE /std:c++latest /W4 /WX)

target_include_directories(test-project-lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(test-project-exe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(test-project-exe PRIVATE test-project-lib)

