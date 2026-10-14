cmake_minimum_required(VERSION 3.22)
project(AnimatedMojang LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden -Wall -Wextra")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-rpath,'$ORIGIN:$ORIGIN/../LeviLamroid'")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /home/devroom/LeviLamroid/include
    /usr/include
)

link_directories(
    /home/devroom/LeviLamroid/dist
    /system/lib64
    /data/data/com.termux/files/usr/lib
)

set(SOURCES
    src/mod/MyMod.cpp
    src/client/Client.cpp
    src/scene/NativeScene.cpp
    src/scene/CaveGlesRenderer.cpp
)

add_library(AnimatedMojang SHARED ${SOURCES})

set_target_properties(AnimatedMojang PROPERTIES
    OUTPUT_NAME "AnimatedMojang"
    PREFIX "lib"
    SUFFIX ".so"
)

target_link_libraries(AnimatedMojang PRIVATE /home/devroom/LeviLamroid/dist/liblevilamroid.so log dl GLESv2 EGL)
