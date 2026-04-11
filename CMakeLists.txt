cmake_minimum_required(VERSION 3.27)
project(compdb-vs)

include(CTest)
include(FetchContent)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW ON
)
FetchContent_MakeAvailable(json)

add_library(compdb-vs-lib ${CMAKE_CURRENT_SOURCE_DIR}/src/compdb-vs.cpp)
add_executable(compdb-vs-tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/compdb-vs-tests.cpp)
add_executable(compdb-vs ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    target_compile_definitions(compdb-vs PRIVATE COMPDBVS_DEBUG)
endif()

set(COMPDBVS_COMPILE_OPTIONS /std:c++latest /W4 /WX /external:templates- /external:W0)

target_compile_options(compdb-vs-lib PRIVATE ${COMPDBVS_COMPILE_OPTIONS})
target_compile_options(compdb-vs-tests PRIVATE ${COMPDBVS_COMPILE_OPTIONS})
target_compile_options(compdb-vs PRIVATE ${COMPDBVS_COMPILE_OPTIONS})

target_link_libraries(compdb-vs-tests PRIVATE compdb-vs-lib)
target_link_libraries(compdb-vs PRIVATE compdb-vs-lib nlohmann_json)

target_include_directories(compdb-vs-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third-party)

install(TARGETS compdb-vs)

add_test(NAME TestAll COMMAND compdb-vs-tests)

