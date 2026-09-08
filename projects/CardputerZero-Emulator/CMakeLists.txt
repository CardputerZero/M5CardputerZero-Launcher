cmake_minimum_required(VERSION 3.16)
project(cardputer-zero-emu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
include(CTest)

# Paths — same repo, sibling projects
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(APPLAUNCH  ${REPO_ROOT}/projects/APPLaunch/main)
set(USERDEMO   ${REPO_ROOT}/projects/UserDemo/main)
set(CP0_LVGL   ${REPO_ROOT}/ext_components/cp0_lvgl)
set(APPLAUNCH_LVGL_OVERRIDE ${APPLAUNCH}/lvgl)
set(APPLAUNCH_EMU_ENTRY ${CMAKE_CURRENT_SOURCE_DIR}/src/app_launch_entry.cpp)

set(APPLAUNCH_GENERATED_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/generated/include)
set(APPLAUNCH_GENERATED_PAGE_APP ${APPLAUNCH_GENERATED_INCLUDE}/generated/page_app.h)

file(GLOB PAGE_APP_HEADERS
    RELATIVE ${APPLAUNCH}/ui
    CONFIGURE_DEPENDS
    ${APPLAUNCH}/ui/page_app/*.hpp
)
list(SORT PAGE_APP_HEADERS)
if(PAGE_APP_HEADERS)
    file(MAKE_DIRECTORY ${APPLAUNCH_GENERATED_INCLUDE}/generated)
    file(WRITE ${APPLAUNCH_GENERATED_PAGE_APP} "#pragma once\n\n")
    foreach(PAGE_APP_HEADER ${PAGE_APP_HEADERS})
        file(APPEND ${APPLAUNCH_GENERATED_PAGE_APP} "#include \"${PAGE_APP_HEADER}\"\n")
    endforeach()
else()
    message(FATAL_ERROR "No APPLaunch page_app headers found")
endif()

# ---------------------------------------------------------------------------
# SDL2 + Freetype (skip for Emscripten — uses its own ports)
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
    if(APPLE)
        if(EXISTS "/opt/homebrew/opt/sdl2")
            list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/sdl2")
        elseif(EXISTS "/usr/local/opt/sdl2")
            list(APPEND CMAKE_PREFIX_PATH "/usr/local/opt/sdl2")
        endif()
    endif()

    find_package(SDL2 QUIET)
    if(NOT SDL2_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL2 REQUIRED sdl2)
    endif()

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FREETYPE freetype2)
        pkg_check_modules(SDL2_MIXER SDL2_mixer)
    endif()
    if(NOT FREETYPE_FOUND)
        find_package(Freetype QUIET)
        if(Freetype_FOUND)
            set(FREETYPE_INCLUDE_DIRS ${FREETYPE_INCLUDE_DIRS})
            set(FREETYPE_LIBRARIES Freetype::Freetype)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# LVGL 9.5
# ---------------------------------------------------------------------------
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "" FORCE)
add_definitions(-DLV_CONF_INCLUDE_SIMPLE)
if(EMSCRIPTEN OR WIN32)
    add_definitions(-DLV_USE_FREETYPE=0)
endif()
if(EMSCRIPTEN)
    add_compile_options(-sUSE_SDL=2 -pthread)
endif()

# SDL2 parent include for <SDL2/SDL.h>
if(NOT EMSCRIPTEN)
    if(SDL2_FOUND AND TARGET SDL2::SDL2)
        get_target_property(_sdl2_inc SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
        if(_sdl2_inc)
            foreach(_dir ${_sdl2_inc})
                get_filename_component(_parent "${_dir}" DIRECTORY)
                include_directories("${_parent}")
            endforeach()
        endif()
    else()
        foreach(_dir ${SDL2_INCLUDE_DIRS})
            get_filename_component(_parent "${_dir}" DIRECTORY)
            include_directories("${_parent}")
        endforeach()
    endif()
    if(FREETYPE_INCLUDE_DIRS)
        include_directories(${FREETYPE_INCLUDE_DIRS})
    endif()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/lvgl ${CMAKE_CURRENT_BINARY_DIR}/lvgl)

# The native CardputerZero SDL runtime provides its own keyboard handler with
# key-up events, modifiers, and the cp0 keyboard event contract.  Do not also
# link LVGL's basic SDL keyboard driver on macOS: ELF linkers previously hid
# the duplicate symbol with --allow-multiple-definition, while ld64 rejects it.
if(APPLE AND NOT EMSCRIPTEN)
    get_target_property(LVGL_SRCS lvgl SOURCES)
    list(FILTER LVGL_SRCS EXCLUDE REGEX ".*/src/drivers/sdl/lv_sdl_keyboard\\.c$")
    set_property(TARGET lvgl PROPERTY SOURCES ${LVGL_SRCS})
endif()

file(GLOB APPLAUNCH_LVGL_FREETYPE_SRCS CONFIGURE_DEPENDS
    ${APPLAUNCH_LVGL_OVERRIDE}/lv_freetype*.c
)
if(APPLAUNCH_LVGL_FREETYPE_SRCS)
    get_target_property(LVGL_SRCS lvgl SOURCES)
    foreach(_override_src ${APPLAUNCH_LVGL_FREETYPE_SRCS})
        get_filename_component(_override_name ${_override_src} NAME)
        list(FILTER LVGL_SRCS EXCLUDE REGEX ".*/src/libs/freetype/${_override_name}$")
    endforeach()
    set_property(TARGET lvgl PROPERTY SOURCES ${LVGL_SRCS})
    target_sources(lvgl PRIVATE ${APPLAUNCH_LVGL_FREETYPE_SRCS})
    target_include_directories(lvgl BEFORE PRIVATE
        ${APPLAUNCH_LVGL_OVERRIDE}
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/lvgl/src/libs/freetype
    )
endif()

# ---------------------------------------------------------------------------
# Helper: common link libraries for app targets
# ---------------------------------------------------------------------------
macro(emu_link_deps target)
    target_link_libraries(${target} PRIVATE lvgl)
    if(TARGET SDL2::SDL2)
        target_link_libraries(${target} PRIVATE SDL2::SDL2)
    else()
        target_link_libraries(${target} PRIVATE ${SDL2_LIBRARIES})
    endif()
    if(FREETYPE_FOUND AND FREETYPE_LINK_LIBRARIES)
        target_link_libraries(${target} PRIVATE ${FREETYPE_LINK_LIBRARIES})
        target_link_directories(${target} PRIVATE ${FREETYPE_LIBRARY_DIRS})
    elseif(FREETYPE_LIBRARIES)
        target_link_libraries(${target} PRIVATE ${FREETYPE_LIBRARIES})
    else()
        target_link_directories(${target} PRIVATE /opt/homebrew/lib)
        target_link_libraries(${target} PRIVATE freetype)
    endif()
    find_package(Threads REQUIRED)
    target_link_libraries(${target} PRIVATE Threads::Threads)
endmacro()

# ===========================================================================
# Native-only targets (skipped for Emscripten)
# ===========================================================================
set(CP0_LVGL_APP_COMMON_SRCS
    ${CP0_LVGL}/src/commount.c
    ${CP0_LVGL}/src/commount.cpp
    ${CP0_LVGL}/src/cp0_esc_state.c
    ${CP0_LVGL}/src/cp0_font_service.cpp
    ${CP0_LVGL}/src/cp0_lvgl_app_page_assets.c
    ${CP0_LVGL}/src/cp0_network_api_contract.cpp
    ${CP0_LVGL}/src/cp0_status_ethernet.c
    ${CP0_LVGL}/src/cp0_status_lifecycle.cpp
    ${CP0_LVGL}/src/ui_app_page.cpp
)

if(NOT EMSCRIPTEN)

option(EMU_SKIP_APPLAUNCH "Skip APPLaunch (Unix-only deps)" OFF)

file(GLOB_RECURSE APPLAUNCH_UI_C CONFIGURE_DEPENDS ${APPLAUNCH}/ui/*.c)
file(GLOB_RECURSE APPLAUNCH_UI_CPP CONFIGURE_DEPENDS ${APPLAUNCH}/ui/*.cpp)
list(FILTER APPLAUNCH_UI_C EXCLUDE REGEX ".*/ui/lvgl/.*")
list(FILTER APPLAUNCH_UI_CPP EXCLUDE REGEX ".*/ui/lvgl/.*")
file(GLOB APPLAUNCH_KBD CONFIGURE_DEPENDS ${APPLAUNCH}/hal/keyboard_input.c)
file(GLOB APPLAUNCH_HAL_SDL_C CONFIGURE_DEPENDS ${APPLAUNCH}/hal/sdl/*.c)
file(GLOB APPLAUNCH_HAL_SDL_CPP CONFIGURE_DEPENDS ${APPLAUNCH}/hal/sdl/*.cpp)
file(GLOB CP0_LVGL_COMMON_SRCS CONFIGURE_DEPENDS
    ${CP0_LVGL}/src/*.c
    ${CP0_LVGL}/src/*.cpp
)
file(GLOB CP0_LVGL_SDL_SRCS CONFIGURE_DEPENDS
    ${CP0_LVGL_COMMON_SRCS}
    ${CP0_LVGL}/src/cp0/cp0_camera_viewport.cpp
    ${CP0_LVGL}/src/cp0/cp0_desktop_exec_policy.cpp
    ${CP0_LVGL}/src/cp0/cp0_process_commands.cpp
    ${CP0_LVGL}/src/cp0/cp0_process_lifecycle.cpp
    ${CP0_LVGL}/src/sdl/*.c
    ${CP0_LVGL}/src/sdl/*.cpp
)
set(CP0_LVGL_WIN32_SRCS
    src/cp0_web_runtime.cpp
    ${CP0_LVGL_APP_COMMON_SRCS}
    ${CP0_LVGL}/src/win32/cp0_hal_stubs_win32.c
)

# ---------------------------------------------------------------------------
# APP: APPLaunch shared lib (Unix only)
# ---------------------------------------------------------------------------
if(NOT EMU_SKIP_APPLAUNCH AND NOT WIN32)

add_library(APPLaunch SHARED
    ${APPLAUNCH_UI_C}
    ${APPLAUNCH_UI_CPP}
    ${APPLAUNCH_KBD}
    ${APPLAUNCH_HAL_SDL_C}
    ${APPLAUNCH_HAL_SDL_CPP}
    ${APPLAUNCH_EMU_ENTRY}
)
target_include_directories(APPLaunch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/lvgl
    ${REPO_ROOT}/ext_components/cp0_lvgl/include
    ${REPO_ROOT}/SDK/github_source/eventpp/include
    ${REPO_ROOT}/SDK/components/utilities/include
    ${REPO_ROOT}/ext_components/Miniaudio/include
    ${APPLAUNCH_GENERATED_INCLUDE}
    ${APPLAUNCH}/ui
    ${APPLAUNCH}/include
    ${APPLAUNCH}
    ${APPLAUNCH}/hal
)
if(NOT WIN32)
    target_include_directories(APPLaunch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/compat_stubs)
else()
    target_include_directories(APPLaunch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/compat)
endif()
target_compile_definitions(APPLaunch PRIVATE HAL_PLATFORM_SDL=1)
if(SDL2_MIXER_FOUND)
    target_compile_definitions(APPLaunch PRIVATE EMU_HAS_AUDIO=1)
    target_include_directories(APPLaunch PRIVATE ${SDL2_MIXER_INCLUDE_DIRS})
    target_link_libraries(APPLaunch PRIVATE ${SDL2_MIXER_LINK_LIBRARIES})
    target_link_directories(APPLaunch PRIVATE ${SDL2_MIXER_LIBRARY_DIRS})
endif()
if(WIN32)
    target_include_directories(APPLaunch BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/compat)
    target_compile_options(APPLaunch PRIVATE
        "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat_win.h"
        -Wno-c++11-narrowing)
else()
    target_compile_options(APPLaunch PRIVATE
        "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat.h"
        -Wno-c++11-narrowing)
endif()
if(APPLE)
    target_link_options(APPLaunch PRIVATE -undefined dynamic_lookup)
else()
    target_link_options(APPLaunch PRIVATE -Wl,--unresolved-symbols=ignore-all)
endif()
endif() # NOT EMU_SKIP_APPLAUNCH

# ---------------------------------------------------------------------------
# APP: UserDemo shared lib
# ---------------------------------------------------------------------------
file(GLOB_RECURSE USERDEMO_UI_C ${USERDEMO}/ui/*.c)

add_library(UserDemo SHARED ${USERDEMO_UI_C})
target_include_directories(UserDemo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${USERDEMO}/ui
    ${USERDEMO}/include
    ${USERDEMO}
)
target_compile_options(UserDemo PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat.h)
if(APPLE)
    target_link_options(UserDemo PRIVATE -undefined dynamic_lookup)
elseif(WIN32)
    target_link_libraries(UserDemo PRIVATE lvgl)
else()
    target_link_options(UserDemo PRIVATE -Wl,--unresolved-symbols=ignore-all)
endif()

# ---------------------------------------------------------------------------
# Emulator executable
# ---------------------------------------------------------------------------
if(WIN32 AND NOT EMU_SKIP_APPLAUNCH)
    add_executable(cardputer-zero-emu src/main.cpp src/device_skin.cpp src/keyboard_map.cpp
        ${APPLAUNCH_UI_C} ${APPLAUNCH_UI_CPP} ${APPLAUNCH_KBD}
        ${APPLAUNCH_HAL_SDL_C} ${APPLAUNCH_HAL_SDL_CPP}
        ${APPLAUNCH_EMU_ENTRY}
        ${CP0_LVGL_WIN32_SRCS})
    target_include_directories(cardputer-zero-emu PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compat
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/lvgl
        ${REPO_ROOT}/ext_components/cp0_lvgl/include
        ${REPO_ROOT}/SDK/github_source/eventpp/include
        ${REPO_ROOT}/SDK/components/utilities/include
        ${REPO_ROOT}/ext_components/Miniaudio/include
        ${APPLAUNCH_GENERATED_INCLUDE}
        ${APPLAUNCH}/ui ${APPLAUNCH}/include ${APPLAUNCH} ${APPLAUNCH}/hal)
    target_compile_options(cardputer-zero-emu PRIVATE
        "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat_win.h"
        -Wno-c++11-narrowing)
    target_compile_definitions(cardputer-zero-emu PRIVATE EMU_STATIC_APP=1 HAL_PLATFORM_SDL=1)
    target_link_options(cardputer-zero-emu PRIVATE -Wl,--allow-multiple-definition)
elseif(WIN32)
    add_executable(cardputer-zero-emu src/main.cpp src/device_skin.cpp src/keyboard_map.cpp ${USERDEMO_UI_C})
    target_include_directories(cardputer-zero-emu PRIVATE
        ${USERDEMO}/ui ${USERDEMO}/include ${USERDEMO})
    target_compile_definitions(cardputer-zero-emu PRIVATE EMU_STATIC_APP=1)
else()
    add_executable(cardputer-zero-emu src/main.cpp src/device_skin.cpp src/keyboard_map.cpp)
endif()
add_dependencies(cardputer-zero-emu lvgl lvgl_thorvg)
add_dependencies(cardputer-zero-emu UserDemo)
if(TARGET APPLaunch)
    add_dependencies(cardputer-zero-emu APPLaunch)
endif()
if(APPLE OR (UNIX AND NOT EMSCRIPTEN))
    set_target_properties(cardputer-zero-emu PROPERTIES ENABLE_EXPORTS ON)
endif()
if(UNIX AND NOT WIN32)
    target_sources(cardputer-zero-emu PRIVATE ${CP0_LVGL_SDL_SRCS})
    target_compile_definitions(cardputer-zero-emu PRIVATE HAL_PLATFORM_SDL=1)
    if(NOT APPLE)
        target_link_options(cardputer-zero-emu PRIVATE -Wl,--allow-multiple-definition)
    endif()
endif()

target_include_directories(cardputer-zero-emu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/lvgl
    ${CP0_LVGL}/include
    ${CP0_LVGL}/src/sdl
    ${REPO_ROOT}/SDK/github_source/eventpp/include
    ${REPO_ROOT}/ext_components/Miniaudio/include
)
if(NOT MSVC)
    target_compile_options(cardputer-zero-emu PRIVATE -Wno-c++11-narrowing)
endif()

# Force-load LVGL for dlopen'd apps
if(APPLE)
    target_link_libraries(cardputer-zero-emu PRIVATE
        -Wl,-force_load,$<TARGET_FILE:lvgl>
        -Wl,-force_load,$<TARGET_FILE:lvgl_thorvg>
    )
elseif(MSVC)
    target_link_libraries(cardputer-zero-emu PRIVATE lvgl lvgl_thorvg)
    target_link_options(cardputer-zero-emu PRIVATE
        /WHOLEARCHIVE:lvgl /WHOLEARCHIVE:lvgl_thorvg)
elseif(WIN32)
    target_link_libraries(cardputer-zero-emu PRIVATE lvgl lvgl_thorvg)
else()
    target_link_libraries(cardputer-zero-emu PRIVATE
        -Wl,--whole-archive $<TARGET_FILE:lvgl> $<TARGET_FILE:lvgl_thorvg> -Wl,--no-whole-archive
    )
endif()

# SDL2
if(TARGET SDL2::SDL2)
    target_link_libraries(cardputer-zero-emu PRIVATE SDL2::SDL2)
elseif(TARGET SDL2::SDL2-static)
    target_link_libraries(cardputer-zero-emu PRIVATE SDL2::SDL2-static)
else()
    target_include_directories(cardputer-zero-emu PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(cardputer-zero-emu PRIVATE ${SDL2_LIBRARIES})
endif()
if(TARGET SDL2::SDL2main)
    target_link_libraries(cardputer-zero-emu PRIVATE SDL2::SDL2main)
endif()

# SDL2_image
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2_IMAGE SDL2_image)
endif()
if(SDL2_IMAGE_FOUND)
    target_include_directories(cardputer-zero-emu PRIVATE ${SDL2_IMAGE_INCLUDE_DIRS})
    target_link_libraries(cardputer-zero-emu PRIVATE ${SDL2_IMAGE_LINK_LIBRARIES})
    target_link_directories(cardputer-zero-emu PRIVATE ${SDL2_IMAGE_LIBRARY_DIRS})
else()
    target_link_directories(cardputer-zero-emu PRIVATE /opt/homebrew/lib)
    target_link_libraries(cardputer-zero-emu PRIVATE SDL2_image)
endif()

# Freetype
if(FREETYPE_FOUND AND FREETYPE_LINK_LIBRARIES)
    target_link_libraries(cardputer-zero-emu PRIVATE ${FREETYPE_LINK_LIBRARIES})
    target_link_directories(cardputer-zero-emu PRIVATE ${FREETYPE_LIBRARY_DIRS})
else()
    target_link_directories(cardputer-zero-emu PRIVATE /opt/homebrew/lib)
    target_link_libraries(cardputer-zero-emu PRIVATE freetype)
endif()

# SDL2_mixer (optional)
if(SDL2_MIXER_FOUND)
    target_include_directories(cardputer-zero-emu PRIVATE ${SDL2_MIXER_INCLUDE_DIRS})
    target_link_libraries(cardputer-zero-emu PRIVATE ${SDL2_MIXER_LINK_LIBRARIES})
    target_link_directories(cardputer-zero-emu PRIVATE ${SDL2_MIXER_LIBRARY_DIRS})
    target_compile_definitions(cardputer-zero-emu PRIVATE EMU_HAS_AUDIO=1)
else()
    find_library(SDL2_MIXER_LIB SDL2_mixer)
    if(SDL2_MIXER_LIB)
        target_link_libraries(cardputer-zero-emu PRIVATE ${SDL2_MIXER_LIB})
        target_compile_definitions(cardputer-zero-emu PRIVATE EMU_HAS_AUDIO=1)
    endif()
endif()

# dl + threads
if(NOT WIN32)
    target_link_libraries(cardputer-zero-emu PRIVATE ${CMAKE_DL_LIBS})
    find_package(Threads REQUIRED)
    target_link_libraries(cardputer-zero-emu PRIVATE Threads::Threads)
else()
    # ui_app_hack.hpp calls gethostname() on Windows via <winsock2.h>.
    find_package(Threads REQUIRED)
    target_link_libraries(cardputer-zero-emu PRIVATE Threads::Threads)
    target_link_libraries(cardputer-zero-emu PRIVATE ws2_32)
endif()

# ---------------------------------------------------------------------------
# Post-build: copy apps and resources
# ---------------------------------------------------------------------------
add_custom_command(TARGET cardputer-zero-emu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:cardputer-zero-emu>/apps
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:UserDemo> $<TARGET_FILE_DIR:cardputer-zero-emu>/apps/
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:cardputer-zero-emu>/applications
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:cardputer-zero-emu>/images
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${REPO_ROOT}/projects/APPLaunch/APPLaunch/share/images $<TARGET_FILE_DIR:cardputer-zero-emu>/images
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:cardputer-zero-emu>/APPLaunch
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${REPO_ROOT}/projects/APPLaunch/APPLaunch/share $<TARGET_FILE_DIR:cardputer-zero-emu>/APPLaunch/share
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:cardputer-zero-emu>/assets
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/assets/device_skin.png $<TARGET_FILE_DIR:cardputer-zero-emu>/assets/device_skin.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/assets/startup.mp3 $<TARGET_FILE_DIR:cardputer-zero-emu>/assets/startup.mp3
    COMMENT "Copying apps and resources"
)
if(TARGET APPLaunch)
    add_custom_command(TARGET APPLaunch POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/apps
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:APPLaunch> ${CMAKE_CURRENT_BINARY_DIR}/apps/
        COMMENT "Copying APPLaunch"
    )
endif()

if(APPLE AND BUILD_TESTING)
    add_test(
        NAME emulator-macos-runtime
        COMMAND ${CMAKE_COMMAND} -E env
            SDL_VIDEODRIVER=dummy
            SDL_AUDIODRIVER=dummy
            EMU_TEST_CLICK_FRAME=10
            EMU_TEST_NAVIGATE_FRAME=10
            EMU_TEST_RESET_FRAME=20
            EMU_TEST_QUIT_FRAME=40
            $<TARGET_FILE:cardputer-zero-emu>
    )
    set_tests_properties(emulator-macos-runtime PROPERTIES
        WORKING_DIRECTORY $<TARGET_FILE_DIR:cardputer-zero-emu>
        PASS_REGULAR_EXPRESSION "\\[EMU\\] Host cp0 keyboard driver"
        FAIL_REGULAR_EXPRESSION "TEST (click )?(failed|missed|did not)"
        TIMEOUT 30
    )

    add_test(
        NAME emulator-macos-screensaver-wake
        COMMAND ${CMAKE_COMMAND} -E env
            SDL_VIDEODRIVER=dummy
            SDL_AUDIODRIVER=dummy
            EMU_DEBUG_INPUT=1
            EMU_SCREENSAVER_TIMEOUT_SECONDS=1
            EMU_TEST_NAVIGATE_FRAME=300
            EMU_TEST_QUIT_FRAME=350
            $<TARGET_FILE:cardputer-zero-emu>
    )
    set_tests_properties(emulator-macos-screensaver-wake PROPERTIES
        WORKING_DIRECTORY $<TARGET_FILE_DIR:cardputer-zero-emu>
        PASS_REGULAR_EXPRESSION
            "Keyboard filter key=106 state=1 handler=app consumed=1"
        TIMEOUT 30
    )
endif()

endif() # NOT EMSCRIPTEN

# ---------------------------------------------------------------------------
# Web (Emscripten) target
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    file(GLOB_RECURSE APPLAUNCH_UI_C_WEB   ${APPLAUNCH}/ui/*.c)
    file(GLOB_RECURSE APPLAUNCH_UI_CPP_WEB ${APPLAUNCH}/ui/*.cpp)
    file(GLOB         APPLAUNCH_KBD_WEB    ${APPLAUNCH}/hal/keyboard_input.c)
    file(GLOB         APPLAUNCH_HAL_SDL_C_WEB   ${APPLAUNCH}/hal/sdl/*.c)
    file(GLOB         APPLAUNCH_HAL_SDL_CPP_WEB ${APPLAUNCH}/hal/sdl/*.cpp)
    set(CP0_LVGL_WEB_SRCS
        src/cp0_web_runtime.cpp
        ${CP0_LVGL_APP_COMMON_SRCS}
        ${CP0_LVGL}/src/web/cp0_hal_stubs_web.c
    )

    add_executable(cardputer-zero-emu-web
        src/main_web.cpp
        src/device_skin.cpp
        src/keyboard_map.cpp
        ${APPLAUNCH_UI_C_WEB}
        ${APPLAUNCH_UI_CPP_WEB}
        ${APPLAUNCH_KBD_WEB}
        ${APPLAUNCH_HAL_SDL_C_WEB}
        ${APPLAUNCH_HAL_SDL_CPP_WEB}
        ${APPLAUNCH_EMU_ENTRY}
        ${CP0_LVGL_WEB_SRCS}
    )

    target_include_directories(cardputer-zero-emu-web PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compat_queue
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/compat_stubs
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/lvgl
        ${CP0_LVGL}/include
        ${REPO_ROOT}/SDK/github_source/eventpp/include
        ${REPO_ROOT}/SDK/components/utilities/include
        ${REPO_ROOT}/ext_components/Miniaudio/include
        ${APPLAUNCH_GENERATED_INCLUDE}
        ${APPLAUNCH}/ui
        ${APPLAUNCH}/include
        ${APPLAUNCH}
        ${APPLAUNCH}/hal
    )

    target_compile_options(cardputer-zero-emu-web PRIVATE
        -include ${CMAKE_CURRENT_SOURCE_DIR}/src/emu_compat.h
        -Wno-c++11-narrowing
        -DLV_USE_FREETYPE=0
        -DHAL_PLATFORM_SDL=1
    )

    target_link_libraries(cardputer-zero-emu-web PRIVATE lvgl)

    target_compile_options(cardputer-zero-emu-web PRIVATE -sUSE_SDL=2 -sUSE_SDL_IMAGE=2 -pthread)
    target_link_options(cardputer-zero-emu-web PRIVATE
        -sUSE_SDL=2
        -sUSE_SDL_IMAGE=2
        -pthread -sPROXY_TO_PTHREAD=0
        -sSDL2_IMAGE_FORMATS=["png"]
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets/device_skin.png@/assets/device_skin.png"
        "--preload-file=${REPO_ROOT}/projects/APPLaunch/APPLaunch/share/images@/images"
        --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html
    )

    set_target_properties(cardputer-zero-emu-web PROPERTIES
        SUFFIX ".html"
        OUTPUT_NAME "index"
    )
endif()
