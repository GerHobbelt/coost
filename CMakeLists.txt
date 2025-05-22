cmake_minimum_required(VERSION 3.12)

if(POLICY CMP0090)
    cmake_policy(SET CMP0090 NEW)
endif()

project(coost VERSION 4.0.0)

if(MSVC)
    enable_language(C CXX ASM_MASM)
else()
    enable_language(C CXX ASM)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(MSVC)
    add_compile_options(/fp:fast /EHsc)
    add_link_options(/SAFESEH:NO)
    if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
        string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    endif()
else()
    add_compile_options(-Wall -O2 -g -Wno-sign-compare -Wno-strict-aliasing)
    if(APPLE)
        add_compile_options(-fno-pie)
    endif()
endif()

add_subdirectory(src)

if(BUILD_ALL)
    enable_testing()
    add_subdirectory(src)
    add_subdirectory(gen)
    add_subdirectory(test)
    add_subdirectory(unitest)
endif()
