cmake_minimum_required(VERSION 3.22.1)
project(sortfree_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(sortfree_native SHARED
    main.cpp
    ply_loader.cpp
)

find_library(log-lib log)
find_library(android-lib android)
find_library(vulkan-lib vulkan)

# NDK r26+ normally ships shaderc in some setups; optional.
find_library(shaderc-lib shaderc_shared)

if(shaderc-lib)
    target_compile_definitions(sortfree_native PRIVATE HAS_SHADERC=1)
    target_link_libraries(sortfree_native ${shaderc-lib})
endif()

target_link_libraries(sortfree_native
    native_app_glue
    ${log-lib}
    ${android-lib}
    ${vulkan-lib}
)
