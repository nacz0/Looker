cmake_minimum_required(VERSION 3.16)
project(Looker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LOOKER_FASTENER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Fastener" CACHE PATH "Path to the local Fastener repository")

if(NOT EXISTS "${LOOKER_FASTENER_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Fastener was not found at ${LOOKER_FASTENER_DIR}. Set LOOKER_FASTENER_DIR to the Fastener repo.")
endif()

set(FST_BUILD_EXAMPLES OFF CACHE BOOL "Build Fastener examples" FORCE)
set(FST_BUILD_TESTS OFF CACHE BOOL "Build Fastener tests" FORCE)

add_subdirectory("${LOOKER_FASTENER_DIR}" "${CMAKE_BINARY_DIR}/fastener")

add_executable(looker
    src/main.cpp
    src/app.cpp
    src/file_system_model.cpp
)

target_include_directories(looker PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(looker PRIVATE fastener)

if(MSVC)
    target_compile_options(looker PRIVATE /W4 /permissive-)
else()
    target_compile_options(looker PRIVATE -Wall -Wextra -Wpedantic)
endif()
