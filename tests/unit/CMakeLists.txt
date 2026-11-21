# Unit tests for the ArtNetMapper core. They build without Umfeld:
#
#   cmake -S tests/unit -B build-tests
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure

cmake_minimum_required(VERSION 3.20)
project(artnetmapper-unit-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIB "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(unit_tests
    unit_tests.cpp
    ${LIB}/src/ArtNetMapperCore.cpp
    ${LIB}/src/Discovery.cpp
    ${LIB}/src/Fixture.cpp
    ${LIB}/src/Sender.cpp
)
target_include_directories(unit_tests PRIVATE ${LIB}/include)
target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
find_package(Threads REQUIRED)
target_link_libraries(unit_tests PRIVATE Threads::Threads)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
