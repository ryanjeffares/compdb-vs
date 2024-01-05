cmake_minimum_required(VERSION 3.17)
project(compdb-vs)

include(FetchContent)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY      https://github.com/fmtlib/fmt.git
    GIT_TAG             master
    GIT_SHALLOW         ON
)
FetchContent_MakeAvailable(fmt)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(compdb-vs src/compdb-vs.cpp)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    target_compile_definitions(compdb-vs PRIVATE COMPDBVS_DEBUG)
endif()

target_compile_options(compdb-vs PRIVATE /std:c++20 /W4 /WX /external:templates- /external:W0)
target_include_directories(compdb-vs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third-party/)
target_link_libraries(compdb-vs PRIVATE fmt::fmt)

install(TARGETS compdb-vs)
