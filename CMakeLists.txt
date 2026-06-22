cmake_minimum_required(VERSION 3.22)

# 2. 定义项目名称
project(GPU_Scheduler)

# 3. 设置 C++ 编译器标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 开启 Release 模式下的最高级别性能优化 (-O3)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# 5. 自动搜集 src 目录下的所有源文件
file(GLOB_RECURSE SOURCE_FILES "src/*.cpp" "src/*.h")

# 6. 指定生成的可执行文件名称及对应的源文件
add_executable(main ${SOURCE_FILES})