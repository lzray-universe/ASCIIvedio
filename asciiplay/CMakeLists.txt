cmake_minimum_required(VERSION 3.16)
project(asciiplay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Threads REQUIRED)
find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL SWSCALE SWRESAMPLE)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

file(GLOB SRC_FILES
    src/*.cpp
)

add_executable(asciiplay ${SRC_FILES})

target_include_directories(asciiplay PRIVATE src)

target_link_libraries(asciiplay PRIVATE miniaudio Threads::Threads ${FFMPEG_LIBRARIES})

target_include_directories(asciiplay PRIVATE ${FFMPEG_INCLUDE_DIRS})

target_compile_definitions(asciiplay PRIVATE ${FFMPEG_DEFINITIONS})

if (MSVC)
    target_compile_options(asciiplay PRIVATE /W4 /permissive- /EHsc)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(asciiplay PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS asciiplay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
