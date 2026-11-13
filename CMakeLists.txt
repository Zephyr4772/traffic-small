cmake_minimum_required(VERSION 3.16)
project(SmartTrafficCity LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/engine/src/*.cpp)

add_executable(traffic ${ENGINE_SOURCES})
target_include_directories(traffic PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)

if(MSVC)
  target_compile_options(traffic PRIVATE /W4 /permissive-)
else()
  target_compile_options(traffic PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  target_link_libraries(traffic PRIVATE ws2_32 shell32)
endif()

# Link the C++ runtime statically so traffic.exe runs on any Windows PC without MinGW DLLs.
if(MINGW)
  target_link_options(traffic PRIVATE -static)
endif()

# Put the executable in build/ regardless of generator.
set_target_properties(traffic PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
