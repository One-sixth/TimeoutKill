cmake_minimum_required(VERSION 3.20)
project(TimeoutKill LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_definitions(-DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 主程序源码
add_executable(TimeoutKill main.cpp)

# 子系统设置
# Release: WINDOWS 子系统（使用 wWinMain 入口）
# Debug: CONSOLE 子系统（需要 wmain 入口）
target_link_options(TimeoutKill PRIVATE
    $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
    $<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>
)

# 禁止链接器默认嵌入 manifest
target_link_options(TimeoutKill PRIVATE /MANIFEST:NO)

target_link_libraries(TimeoutKill PRIVATE user32 kernel32 shell32 comctl32)

if(MSVC)
    target_compile_options(TimeoutKill PRIVATE /W4 /utf-8)
    target_compile_definitions(TimeoutKill PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        $<$<CONFIG:Debug>:CONSOLE_BUILD>
    )
endif()

set_target_properties(TimeoutKill PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
)

# 构建后用 mt.exe 嵌入 UAC manifest（仅 Release）
add_custom_command(TARGET TimeoutKill POST_BUILD
    COMMAND mt.exe -manifest ${CMAKE_CURRENT_SOURCE_DIR}/TimeoutKill.manifest -outputresource:$<TARGET_FILE:TimeoutKill>
    COMMENT "Embedding UAC manifest..."
)
