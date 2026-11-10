cmake_minimum_required(VERSION 3.22.1)

project(instrument_core
    VERSION 1.0.7
    LANGUAGES CXX
)

if(ANDROID AND NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This target requires ANDROID_ABI=arm64-v8a (ELF64 AArch64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# Target 1: libinstrument_core.so (entry.cpp + Dear ImGui GLES3 HUD + exports.map)
# ============================================================================
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

add_library(instrument_core SHARED
    entry.cpp
    imgui_gles3_hud.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(instrument_core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_compile_definitions(instrument_core
    PRIVATE
        IMGUI_IMPL_OPENGL_ES3
        IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)

target_compile_options(instrument_core
    PRIVATE
        -O3
        -fno-exceptions
        -fno-rtti
        -fPIC
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -ffunction-sections
        -fdata-sections
        -fno-stack-protector
        -fomit-frame-pointer
        -Wall
        -Wextra
        -Wpedantic
        $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>:-march=armv8-a+simd>
)

target_link_options(instrument_core
    PRIVATE
        -Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/exports.map
        -Wl,--gc-sections
        -Wl,--exclude-libs,ALL
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,--hash-style=gnu
        -flto=thin
)

set_target_properties(instrument_core PROPERTIES
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
)

if(ANDROID)
    find_library(ANDROID_LOG_LIB log REQUIRED)
    find_library(ANDROID_EGL_LIB EGL REQUIRED)
    find_library(ANDROID_GLESV3_LIB GLESv3 REQUIRED)
    find_library(ANDROID_NATIVE_LIB android REQUIRED)
    target_link_libraries(instrument_core
        PRIVATE
            ${ANDROID_EGL_LIB}
            ${ANDROID_GLESV3_LIB}
            ${ANDROID_NATIVE_LIB}
            ${ANDROID_LOG_LIB}
            dl
    )
else()
    find_package(Threads REQUIRED)
    target_link_libraries(instrument_core
        PRIVATE
            Threads::Threads
            ${CMAKE_DL_LIBS}
    )
endif()

# ============================================================================
# Target 2 (Optional): libue4_diagnostics.so (lib_main.cpp unified suite)
# ============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib_main.cpp")
    add_library(ue4_diagnostics SHARED
        lib_main.cpp
    )

    target_include_directories(ue4_diagnostics
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_options(ue4_diagnostics
        PRIVATE
            -O3
            -fno-exceptions
            -fno-rtti
            -fPIC
            -fvisibility=hidden
            -fvisibility-inlines-hidden
            -ffunction-sections
            -fdata-sections
            -fno-stack-protector
            -fomit-frame-pointer
            -Wall
            -Wextra
            -Wpedantic
            $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>:-march=armv8-a+simd>
    )

    target_link_options(ue4_diagnostics
        PRIVATE
            -Wl,--gc-sections
            -Wl,--exclude-libs,ALL
            -Wl,-z,relro
            -Wl,-z,now
            -Wl,--hash-style=gnu
            -flto=thin
    )

    if(ANDROID)
        target_link_libraries(ue4_diagnostics
            PRIVATE
                ${ANDROID_LOG_LIB}
                dl
        )
    else()
        target_link_libraries(ue4_diagnostics
            PRIVATE
                Threads::Threads
                ${CMAKE_DL_LIBS}
        )
    endif()
endif()

