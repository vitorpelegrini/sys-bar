cmake_minimum_required(VERSION 3.20)

# The version lives here and only here: it flows into version.h, the .exe resources, the installer
# and the release workflow.
project(sys-bar VERSION 1.0.0 LANGUAGES CXX RC)
set(SYSBAR_PROJECT_URL "https://github.com/vitorpelegrini/sys-bar")
set(SYSBAR_RELEASES_API "https://api.github.com/repos/vitorpelegrini/sys-bar/releases/latest")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: a single self-contained .exe, no Visual C++ Redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SYSBAR_BUILD_TESTS "Build the test suite" ON)
option(SYSBAR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI)" OFF)

configure_file(src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

function(sysbar_options target)
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_include_directories(${target} PRIVATE ${CMAKE_BINARY_DIR}/generated)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /utf-8 /EHsc /GS /guard:cf /Zc:__cplusplus>
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/O2 /GL /Gy>
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:${SYSBAR_WARNINGS_AS_ERRORS}>>:/WX>)
        target_link_options(${target} PRIVATE /guard:cf $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
    endif()
endfunction()

# Core (everything except the entry point), shared by the app and the tests.
add_library(sysbar_core STATIC
    src/app.cpp
    src/card.cpp
    src/format.cpp
    src/gfx.cpp
    src/hardware.cpp
    src/i18n.cpp
    src/json.cpp
    src/logic.cpp
    src/metrics.cpp
    src/renderer.cpp
    src/sensorfeed.cpp
    src/settings.cpp
    src/update.cpp
)
target_include_directories(sysbar_core PUBLIC src)
target_link_libraries(sysbar_core PUBLIC d2d1 dwrite dxgi gdi32 pdh shell32 ole32 advapi32 wtsapi32 winhttp bcrypt
                      iphlpapi setupapi)
sysbar_options(sysbar_core)

add_executable(sys-bar WIN32 src/main.cpp src/resource.rc src/app.manifest)
target_link_libraries(sys-bar PRIVATE sysbar_core)
sysbar_options(sys-bar)

if(SYSBAR_BUILD_TESTS)
    enable_testing()
    add_executable(sysbar-tests
        tests/test_main.cpp
        tests/test_format.cpp
        tests/test_gpu.cpp
        tests/test_hardware.cpp
        tests/test_i18n.cpp
        tests/test_logic.cpp
        tests/test_update.cpp
        tests/test_render.cpp
        tests/test_sensorfeed.cpp
        tests/test_system.cpp
        src/app.manifest)
    target_link_libraries(sysbar-tests PRIVATE sysbar_core windowscodecs)
    sysbar_options(sysbar-tests)
    add_test(NAME sysbar-tests COMMAND sysbar-tests --previews ${CMAKE_BINARY_DIR}/previews)
endif()
