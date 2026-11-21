cmake_minimum_required(VERSION 3.20)

project(umfeld-library-artnetmapper)

if (NOT TARGET umfeld-lib-interface)
    if (NOT DEFINED ENV{UMFELD_PATH})
        message(FATAL_ERROR "UMFELD_PATH not set. run: export UMFELD_PATH=/path/to/umfeld")
    endif ()
    set(UMFELD_PATH "$ENV{UMFELD_PATH}")
    add_subdirectory(${UMFELD_PATH} ${CMAKE_BINARY_DIR}/umfeld-lib-${PROJECT_NAME})
endif ()

if (WIN32)
    message(FATAL_ERROR "ArtNetMapper: Windows is not supported yet (macOS and Linux only).")
endif ()

set(ARTNETMAPPER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ArtNetMapperCore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ArtNetMapperUmfeld.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Discovery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Fixture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Sender.cpp"
)
file(GLOB_RECURSE ARTNETMAPPER_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

add_library(${PROJECT_NAME} STATIC ${ARTNETMAPPER_SOURCES} ${ARTNETMAPPER_HEADERS})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

target_include_directories(${PROJECT_NAME} PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)

find_package(Threads REQUIRED)
# umfeld-lib-interface gives access to the Umfeld headers; the sketch links Umfeld itself
target_link_libraries(${PROJECT_NAME} PUBLIC umfeld-lib-interface Threads::Threads)
