cmake_minimum_required(VERSION 3.7)
project(hidgui)

set(CMAKE_C_STANDARD 11)

set(SOURCE_FILES main.c)
add_executable(hidgui ${SOURCE_FILES})

find_package(SDL2 REQUIRED)

include_directories(
    ${SDL2_INCLUDE_DIRS}
)

INCLUDE_DIRECTORIES(${PROJECT_NAME} ${SDL2_INCLUDE_DIRS})
set(SDL2_LIBRARIES "-L${SDL2_LIBDIR}  -lSDL2")
TARGET_LINK_LIBRARIES(${PROJECT_NAME} ${SDL2_LIBRARIES})
