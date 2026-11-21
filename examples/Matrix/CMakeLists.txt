cmake_minimum_required(VERSION 3.20)

project(Matrix)
set(UMFELD_PATH "$ENV{UMFELD_PATH}")
set(ARTNETMAPPER_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../..") # the ArtNetMapper library folder

if (NOT DEFINED ENV{UMFELD_PATH})
    message(FATAL_ERROR "UMFELD_PATH not set. run: export UMFELD_PATH=/path/to/umfeld")
endif ()

# --------- no need to change anything below this line ------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(".")
file(GLOB SOURCE_FILES "*.cpp")
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

add_subdirectory(${UMFELD_PATH} ${CMAKE_BINARY_DIR}/umfeld-lib-${PROJECT_NAME})
add_umfeld_libs()

add_subdirectory(${ARTNETMAPPER_PATH} ${CMAKE_BINARY_DIR}/umfeld-library-artnetmapper-${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME} PRIVATE umfeld-library-artnetmapper)
