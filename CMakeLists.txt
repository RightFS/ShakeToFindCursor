# 设置最低 CMake 版本要求
cmake_minimum_required(VERSION 3.15)

# 项目名称和语言设置
project(ShakeToFindCursor 
        VERSION 1.0.0 
        LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 检查操作系统
if(NOT WIN32)
    message(FATAL_ERROR "This project only supports Windows platform")
endif()

# 设置输出目录
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 添加可执行文件
add_executable(${PROJECT_NAME} 
    main.cpp
)

# 设置Windows子系统
set_target_properties(${PROJECT_NAME} 
    PROPERTIES 
    WIN32_EXECUTABLE FALSE  # 设置为控制台应用程序
)

# 添加所需的 Windows 库
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    user32  # Windows User32 库
)

# 编译器特定选项
if(MSVC)
    # MSVC 特定编译选项
    target_compile_options(${PROJECT_NAME} 
        PRIVATE 
        /W4     # 警告级别
        /WX     # 将警告视为错误
        /MP     # 多处理器编译
        /EHsc   # 标准异常处理
    )
    
    # 禁用特定警告（如果需要）
    # target_compile_options(${PROJECT_NAME} PRIVATE /wd4100)
    
    # 使用 UTF-8 编码
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
else()
    # 其他编译器的选项（虽然项目只支持 Windows，但保留这部分以备将来扩展）
    target_compile_options(${PROJECT_NAME} 
        PRIVATE 
        -Wall 
        -Wextra 
        -Wpedantic
    )
endif()
