cmake_minimum_required(VERSION 3.20)
project(TimeoutKill LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_definitions(-DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# 主程序源码
add_executable(TimeoutKill main.cpp)

# 子系统：统一 WINDOWS（命令行模式按需 AllocConsole）
target_link_options(TimeoutKill PRIVATE /SUBSYSTEM:WINDOWS)

# 禁止链接器默认嵌入 manifest
target_link_options(TimeoutKill PRIVATE /MANIFEST:NO)

# Release 全套体积/内存优化
target_compile_options(TimeoutKill PRIVATE
    $<$<CONFIG:Release>:/O1 /GL>
)
target_link_options(TimeoutKill PRIVATE
    $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /LTCG /HEAP:0x1000,0x1000 /STACK:0x10000>
)

target_link_libraries(TimeoutKill PRIVATE user32 kernel32 shell32 comctl32)

if(MSVC)
    target_compile_options(TimeoutKill PRIVATE /W4 /utf-8)
    target_compile_definitions(TimeoutKill PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
    )
endif()

set_target_properties(TimeoutKill PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
)

# 构建后用 mt.exe 嵌入 UAC manifest（仅 Release）
add_custom_command(TARGET TimeoutKill POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DCONFIG=$<CONFIG> -DSRC=$<TARGET_FILE:TimeoutKill>
        -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/TimeoutKill.manifest
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake_post_build.cmake
    COMMENT "Post-build: manifest + copy (Release only)..."
)
