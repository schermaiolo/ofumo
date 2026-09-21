# File     : CMakeLists.txt
# Platform : Linux / X11 / KDE Plasma Wayland
# Author   : Daniel Fridman (Schermaiolo)
#
# Build configuration for OFumo.

cmake_minimum_required(VERSION 3.16)
project(ofumo VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(OFUMO_ASSET_THRESHOLD "128" CACHE STRING "Monochrome threshold (0-255)")
set(OFUMO_ALPHA_THRESHOLD "128" CACHE STRING "Alpha threshold for XShape mask (0-255)")
set(OFUMO_SPRITE_WIDTH 48 CACHE STRING "Common sprite canvas width")
set(OFUMO_SPRITE_HEIGHT 60 CACHE STRING "Common sprite canvas height")

find_package(X11 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

if(NOT X11_Xext_LIB)
    message(FATAL_ERROR "Xext not found. On Arch install package: libxext")
endif()

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(KOISHI_ASSET_HEADERS)

function(ofumo_add_sprite NAME FILE)
    set(HEADER "${GENERATED_DIR}/koishi_${NAME}_xbm.h")

    add_custom_command(
        OUTPUT "${HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/png_to_xbm.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/koishi/${FILE}"
                "${HEADER}"
                --symbol "koishi_${NAME}"
                --canvas-width "${OFUMO_SPRITE_WIDTH}"
                --canvas-height "${OFUMO_SPRITE_HEIGHT}"
                --threshold "${OFUMO_ASSET_THRESHOLD}"
                --alpha-threshold "${OFUMO_ALPHA_THRESHOLD}"
        DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/koishi/${FILE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/png_to_xbm.py"
        COMMENT "Converting Koishi asset ${FILE}"
        VERBATIM
    )

    list(APPEND KOISHI_ASSET_HEADERS "${HEADER}")
    set(KOISHI_ASSET_HEADERS "${KOISHI_ASSET_HEADERS}" PARENT_SCOPE)
endfunction()

ofumo_add_sprite(idle idle.png)
ofumo_add_sprite(wink wink.png)
ofumo_add_sprite(left left.png)
ofumo_add_sprite(right right.png)
ofumo_add_sprite(up up.png)
ofumo_add_sprite(sleep0 sleep0.png)
ofumo_add_sprite(sleep1 sleep1.png)
ofumo_add_sprite(sleep2 sleep2.png)
ofumo_add_sprite(hide_hat hide_hat.png)

add_custom_target(ofumo_assets DEPENDS ${KOISHI_ASSET_HEADERS})

add_executable(ofumo
    src/ofumo.c
    src/kwin_bridge.c
    src/kwin_bridge.h
    ${KOISHI_ASSET_HEADERS}
)
add_dependencies(ofumo ofumo_assets)

target_include_directories(ofumo PRIVATE "${GENERATED_DIR}" ${X11_INCLUDE_DIR})
target_compile_definitions(ofumo PRIVATE _DEFAULT_SOURCE OFUMO_VERSION="${PROJECT_VERSION}")
target_compile_options(ofumo PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
target_link_libraries(ofumo PRIVATE ${X11_X11_LIB} ${X11_Xext_LIB} PkgConfig::LIBSYSTEMD m)

include(GNUInstallDirs)
install(TARGETS ofumo RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES docs/ofumo.1 DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
