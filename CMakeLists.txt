cmake_minimum_required(VERSION 3.22)
project(AnimatedMojang VERSION 1.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Without a build type CMake compiles with NO optimization (-O0): stb_image PNG decoding, nlohmann::json and
# glm all ran 5-20x slower. Default to an optimized build; override with -DCMAKE_BUILD_TYPE=Debug if needed.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -Wall -Wextra")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /usr/include
)

link_directories(
    /system/lib64
    /data/data/com.termux/files/usr/lib
)

file(GLOB_RECURSE SOURCES
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.c"
)

add_library(AnimatedMojang SHARED ${SOURCES})

set_target_properties(AnimatedMojang PROPERTIES
    OUTPUT_NAME "AnimatedMojang"
    PREFIX "lib"
    SUFFIX ".so"
)

target_link_options(AnimatedMojang PRIVATE -Wl,--gc-sections)
target_link_libraries(AnimatedMojang PRIVATE log dl GLESv3 EGL)
