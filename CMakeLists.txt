cmake_minimum_required(VERSION 3.17)
project(compdb-vs)

include(CTest)
include(FetchContent)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY      https://github.com/fmtlib/fmt.git
    GIT_TAG             master
    GIT_SHALLOW         ON
)
FetchContent_MakeAvailable(fmt)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(compdb-vs-lib src/compdb-vs.cpp)
add_executable(compdb-vs-tests tests/compdb-vs-tests.cpp)
add_executable(compdb-vs src/main.cpp)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    target_compile_definitions(compdb-vs PRIVATE COMPDBVS_DEBUG)
endif()

set(COMPDBVS_COMPILE_OPTIONS /std:c++20 /W4 /WX /external:templates- /external:W0 /external:I ${CMAKE_CURRENT_SOURCE_DIR}/third-party)

target_compile_options(compdb-vs-lib PRIVATE ${COMPDBVS_COMPILE_OPTIONS})
target_compile_options(compdb-vs-tests PRIVATE ${COMPDBVS_COMPILE_OPTIONS})
target_compile_options(compdb-vs PRIVATE ${COMPDBVS_COMPILE_OPTIONS})

target_include_directories(compdb-vs-lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third-party/)
target_include_directories(compdb-vs-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third-party/)
target_include_directories(compdb-vs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third-party/)

target_link_libraries(compdb-vs-lib PRIVATE fmt::fmt)
target_link_libraries(compdb-vs-tests PRIVATE fmt::fmt compdb-vs-lib)
target_link_libraries(compdb-vs PRIVATE fmt::fmt compdb-vs-lib)

install(TARGETS compdb-vs)

add_test(NAME TestAll COMMAND compdb-vs-tests)

