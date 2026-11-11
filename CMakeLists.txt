cmake_minimum_required(VERSION 3.22.1)

project(instrument_core
    VERSION 1.0.9
    LANGUAGES CXX
)

if(ANDROID AND NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This target requires ANDROID_ABI=arm64-v8a (ELF64 AArch64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Support both structured layout (jni/core, jni/telemetry, jni/hud) and flat layout
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/jni/core/entry.cpp")
    set(JNI_ROOT      "${CMAKE_CURRENT_SOURCE_DIR}/jni")
    set(CORE_DIR      "${JNI_ROOT}/core")
    set(TELEMETRY_DIR "${JNI_ROOT}/telemetry")
    set(HUD_DIR       "${JNI_ROOT}/hud")
    set(IMGUI_DIR     "${JNI_ROOT}/third_party/imgui")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/entry.cpp")
    set(JNI_ROOT      "${CMAKE_CURRENT_SOURCE_DIR}")
    set(CORE_DIR      "${JNI_ROOT}/core")
    set(TELEMETRY_DIR "${JNI_ROOT}/telemetry")
    set(HUD_DIR       "${JNI_ROOT}/hud")
    set(IMGUI_DIR     "${JNI_ROOT}/third_party/imgui")
else()
    set(JNI_ROOT      "${CMAKE_CURRENT_SOURCE_DIR}")
    set(CORE_DIR      "${CMAKE_CURRENT_SOURCE_DIR}")
    set(TELEMETRY_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(HUD_DIR       "${CMAKE_CURRENT_SOURCE_DIR}")
    set(IMGUI_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
endif()

set(COMMON_INCLUDE_DIRS
    "${CORE_DIR}"
    "${TELEMETRY_DIR}"
    "${HUD_DIR}"
    "${JNI_ROOT}"
)

# ============================================================================
# Target 1: libinstrument_core.so (Headless UI-Free Zygisk Backend + IPC Core)
# ============================================================================
add_library(instrument_core SHARED
    "${CORE_DIR}/entry.cpp"
)

target_include_directories(instrument_core
    PRIVATE
        ${COMMON_INCLUDE_DIRS}
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
        -Wl,--version-script,${CORE_DIR}/exports.map
        -Wl,--gc-sections
        -Wl,--exclude-libs,ALL
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,--hash-style=gnu
        -flto=thin
)

set_target_properties(instrument_core PROPERTIES
    LINK_DEPENDS "${CORE_DIR}/exports.map"
)

if(ANDROID)
    find_library(ANDROID_LOG_LIB log REQUIRED)
    target_link_libraries(instrument_core
        PRIVATE
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
# Target 2: libdbdm_overlay_ui.so (Isolated Standalone Overlay UI Library)
# ============================================================================
if(EXISTS "${IMGUI_DIR}/imgui.cpp")
    set(IMGUI_SOURCES
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    )

    add_library(dbdm_overlay_ui SHARED
        "${HUD_DIR}/imgui_gles3_hud.cpp"
        ${IMGUI_SOURCES}
    )

    target_include_directories(dbdm_overlay_ui
        PRIVATE
            ${COMMON_INCLUDE_DIRS}
            "${IMGUI_DIR}"
            "${IMGUI_DIR}/backends"
    )

    target_compile_definitions(dbdm_overlay_ui
        PRIVATE
            IMGUI_IMPL_OPENGL_ES3
            IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    )

    target_compile_options(dbdm_overlay_ui
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
            $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>:-march=armv8-a+simd>
    )

    target_link_options(dbdm_overlay_ui
        PRIVATE
            -Wl,--gc-sections
            -Wl,--exclude-libs,ALL
            -Wl,-z,relro
            -Wl,-z,now
            -Wl,--hash-style=gnu
            -flto=thin
    )

    if(ANDROID)
        find_library(ANDROID_EGL_LIB EGL REQUIRED)
        find_library(ANDROID_GLESV3_LIB GLESv3 REQUIRED)
        find_library(ANDROID_NATIVE_LIB android REQUIRED)
        target_link_libraries(dbdm_overlay_ui
            PRIVATE
                ${ANDROID_EGL_LIB}
                ${ANDROID_GLESV3_LIB}
                ${ANDROID_NATIVE_LIB}
                ${ANDROID_LOG_LIB}
                dl
        )
    endif()
endif()

# ============================================================================
# Target 3 (Optional): libue4_diagnostics.so (lib_main.cpp unified suite)
# ============================================================================
if(EXISTS "${CORE_DIR}/lib_main.cpp")
    add_library(ue4_diagnostics SHARED
        "${CORE_DIR}/lib_main.cpp"
    )

    target_include_directories(ue4_diagnostics
        PRIVATE
            ${COMMON_INCLUDE_DIRS}
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

# ============================================================================
# Target 4: dbdm_telemetry_daemon (Root-Assisted Live Telemetry & IPC Bridge)
# ============================================================================
add_executable(dbdm_telemetry_daemon
    "${CORE_DIR}/entry.cpp"
)

target_include_directories(dbdm_telemetry_daemon
    PRIVATE
        ${COMMON_INCLUDE_DIRS}
)

target_compile_definitions(dbdm_telemetry_daemon
    PRIVATE
        DBDM_STANDALONE_DAEMON=1
)

target_compile_options(dbdm_telemetry_daemon
    PRIVATE
        -O3
        -fno-exceptions
        -fno-rtti
        -fPIE
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -ffunction-sections
        -fdata-sections
        -fno-stack-protector
        -fomit-frame-pointer
        $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},aarch64>:-march=armv8-a+simd>
)

target_link_options(dbdm_telemetry_daemon
    PRIVATE
        -pie
        -Wl,--gc-sections
        -Wl,-z,relro
        -Wl,-z,now
        -flto=thin
)

if(ANDROID)
    target_link_libraries(dbdm_telemetry_daemon
        PRIVATE
            ${ANDROID_LOG_LIB}
            dl
    )
endif()
