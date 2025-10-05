cmake_minimum_required(VERSION 3.10)
project(tetris_drm CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Используем pkg-config для поиска libdrm
find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)

set(SOURCES
    tetris_drm.cpp
)

# Создаем исполняемый файл
add_executable(${PROJECT_NAME} ${SOURCES})

# Линкуем с libdrm и pthread
target_link_libraries(${PROJECT_NAME}
    PkgConfig::DRM
    pthread
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${DRM_INCLUDE_DIRS}
)

message(STATUS "Found DRM include dirs: ${DRM_INCLUDE_DIRS}")
message(STATUS "Found DRM libraries: ${DRM_LIBRARIES}")
message(STATUS "Found DRM version: ${DRM_VERSION}")
