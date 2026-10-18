cmake_minimum_required(VERSION 3.20)

project(MarioParty2Recomp
    VERSION 0.1.0
    DESCRIPTION "Mario Party 2 PC port via static recompilation"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # Debug builds are too slow for the audio thread to keep up.
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING
        "Building with GCC. N64Recomp output is validated with Clang and MSVC; "
        "use Clang (clang-cl on Windows) to avoid miscompiled game code.")
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(MP2_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(MP2_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated" CACHE PATH
    "Output of tools/gen_syms.py, N64Recomp and RSPRecomp (made from your ROM)")
set(MP2_FUNCS_DIR "${MP2_GENERATED_DIR}/RecompiledFuncs")

foreach(sub N64ModernRuntime rt64 RecompFrontend)
    if(NOT EXISTS "${MP2_LIB_DIR}/${sub}/CMakeLists.txt")
        message(FATAL_ERROR "Submodule recomp/lib/${sub} is missing. Run: git submodule update --init --recursive")
    endif()
endforeach()

if(NOT EXISTS "${MP2_FUNCS_DIR}/recomp_overlays.inl" OR NOT EXISTS "${MP2_GENERATED_DIR}/rom_info.h")
    message(FATAL_ERROR
        "The recompiled game code has not been generated yet.\n"
        "Run the build script first (it needs your ROM):\n"
        "  Windows: recomp\\tools\\build_windows.ps1 -Rom <path to rom>\n"
        "  Linux:   recomp/tools/build_linux.sh <path to rom>")
endif()

# ---------------------------------------------------------------------------
#  Third-party libraries
# ---------------------------------------------------------------------------

if(NOT WIN32)
    find_package(SDL2 REQUIRED)
endif()

set(RT64_STATIC ON CACHE BOOL "" FORCE)

# On Linux, make plume's window handle an SDL_Window* (what ultramodern hands
# the renderer) instead of an X11 pair. Must be set at directory scope before
# anything is added so every target agrees on the type.
if(UNIX AND NOT APPLE)
    set(RT64_SDL_WINDOW_VULKAN ON CACHE BOOL "" FORCE)
    add_compile_definitions(PLUME_SDL_VULKAN_ENABLED RT64_SDL_WINDOW_VULKAN)
endif()

add_subdirectory("${MP2_LIB_DIR}/rt64" EXCLUDE_FROM_ALL)
add_subdirectory("${MP2_LIB_DIR}/N64ModernRuntime" EXCLUDE_FROM_ALL)

if(WIN32)
    # RecompFrontend expects SDL2 where FetchContent would have put it; point
    # it at the copy RT64 vendors so there is only one SDL2 in the process.
    set(sdl2_SOURCE_DIR "${MP2_LIB_DIR}/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
    # SDL 2.26's SDL_endian.h redefines _m_prefetch, which current Clang has
    # as a builtin.
    add_compile_definitions(__PRFCHWINTRIN_H)
endif()

# recompui compiles its shaders with RT64's build_*_shader functions, whose
# DXC variables are set inside RT64's directory only. Re-declare them here.
set(MP2_RT64_DIR "${MP2_LIB_DIR}/rt64")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(MP2_DXC_ARCH arm64)
else()
    set(MP2_DXC_ARCH x64)
endif()
if(WIN32)
    set(DXC "${MP2_RT64_DIR}/src/contrib/dxc/bin/x64/dxc.exe")
else()
    set(DXC "${CMAKE_COMMAND}" -E env
        "LD_LIBRARY_PATH=${MP2_RT64_DIR}/src/contrib/dxc/lib/${MP2_DXC_ARCH}"
        "${MP2_RT64_DIR}/src/contrib/dxc/bin/${MP2_DXC_ARCH}/dxc-linux")
endif()
set(DXC_COMMON_OPTS "-I${MP2_RT64_DIR}/src")
set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_3")
set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_3" "-fvk-invert-y")

set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH "${MP2_LIB_DIR}/N64ModernRuntime" CACHE PATH "" FORCE)
set(RECOMP_FRONTEND_RT64_PATH "${MP2_LIB_DIR}/rt64" CACHE PATH "" FORCE)
add_subdirectory("${MP2_LIB_DIR}/RecompFrontend" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
#  Recompiled game code (generated from your ROM; never committed)
# ---------------------------------------------------------------------------

file(GLOB MP2_RECOMPILED_SOURCES CONFIGURE_DEPENDS
    "${MP2_FUNCS_DIR}/*.c"
    "${MP2_FUNCS_DIR}/lookup.cpp"
    "${MP2_FUNCS_DIR}/mp2_recomp_stubs.cpp")

set(MP2_RSP_SOURCE "${MP2_FUNCS_DIR}/aspMain_rsp.cpp")
if(NOT EXISTS "${MP2_RSP_SOURCE}")
    message(FATAL_ERROR "${MP2_RSP_SOURCE} is missing: RSPRecomp has not run.")
endif()
list(APPEND MP2_RECOMPILED_SOURCES "${MP2_RSP_SOURCE}")

# The RSP vector unit emulation uses SSSE3/SSE4.1 intrinsics.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i.86)$")
    if(MSVC)
        set_source_files_properties("${MP2_RSP_SOURCE}" PROPERTIES COMPILE_OPTIONS "/clang:-mssse3;/clang:-msse4.1")
    else()
        set_source_files_properties("${MP2_RSP_SOURCE}" PROPERTIES COMPILE_OPTIONS "-mssse3;-msse4.1")
    endif()
endif()

add_library(mp2_recompiled STATIC ${MP2_RECOMPILED_SOURCES})
target_include_directories(mp2_recompiled PUBLIC
    "${MP2_LIB_DIR}/N64ModernRuntime/N64Recomp/include"
    "${MP2_LIB_DIR}/N64ModernRuntime/librecomp/include"
    "${MP2_LIB_DIR}/N64ModernRuntime/ultramodern/include"
    "${MP2_LIB_DIR}/N64ModernRuntime/thirdparty/sse2neon"
    "${MP2_FUNCS_DIR}")
# Machine-generated code: no warnings, no strict aliasing, wrapping overflow.
if(MSVC)
    target_compile_options(mp2_recompiled PRIVATE /w)
else()
    target_compile_options(mp2_recompiled PRIVATE -fno-strict-aliasing -fwrapv -w)
endif()

# ---------------------------------------------------------------------------
#  The port
# ---------------------------------------------------------------------------

set(MP2_SOURCES
    src/main.cpp
    src/platform.cpp
    src/frontend.cpp
    src/overlays.cpp)

add_executable(mp2-recomp ${MP2_SOURCES})

target_include_directories(mp2-recomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${MP2_GENERATED_DIR}"
    "${MP2_FUNCS_DIR}"
    "${MP2_LIB_DIR}/RecompFrontend/recompui/include"
    "${MP2_LIB_DIR}/RecompFrontend/recompinput/include"
    "${MP2_LIB_DIR}/rt64/include"
    "${MP2_LIB_DIR}/rt64/src"
    "${MP2_LIB_DIR}/rt64/src/contrib"
    "${MP2_LIB_DIR}/rt64/src/contrib/plume"
    "${MP2_LIB_DIR}/rt64/src/contrib/imgui"
    "${MP2_LIB_DIR}/rt64/src/contrib/hlslpp/include"
    "${MP2_LIB_DIR}/rt64/src/contrib/mupen64plus-core/src/api"
    "${MP2_LIB_DIR}/N64ModernRuntime/N64Recomp/include"
    "${MP2_LIB_DIR}/N64ModernRuntime/librecomp/include"
    "${MP2_LIB_DIR}/N64ModernRuntime/ultramodern/include")

target_compile_definitions(mp2-recomp PRIVATE
    MP2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MP2_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MP2_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    HLSL_CPU
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

target_link_libraries(mp2-recomp PRIVATE recompui recompinput rt64 ultramodern librecomp)

if(WIN32)
    set(MP2_SDL2_DIR "${MP2_LIB_DIR}/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
    target_compile_definitions(mp2-recomp PRIVATE NOMINMAX)
    target_include_directories(mp2-recomp PRIVATE
        "${MP2_SDL2_DIR}/include"
        "${MP2_LIB_DIR}/rt64/src/contrib/dxc/inc")
    target_link_directories(mp2-recomp PRIVATE "${MP2_SDL2_DIR}/lib/x64")
    target_link_libraries(mp2-recomp PRIVATE SDL2 winmm mp2_recompiled)
    # lld-link fails the build over missing .pdb files of the prebuilt freetype.
    target_link_options(mp2-recomp PRIVATE /ignore:4099)
    set_target_properties(mp2-recomp PROPERTIES WIN32_EXECUTABLE FALSE)
    # The executable will not start without these beside it.
    add_custom_command(TARGET mp2-recomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MP2_SDL2_DIR}/lib/x64/SDL2.dll"
                "${MP2_LIB_DIR}/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
                "${MP2_LIB_DIR}/rt64/src/contrib/dxc/bin/x64/dxil.dll"
                "$<TARGET_FILE_DIR:mp2-recomp>")
else()
    if(TARGET SDL2::SDL2)
        target_link_libraries(mp2-recomp PRIVATE SDL2::SDL2)
    else()
        target_include_directories(mp2-recomp PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(mp2-recomp PRIVATE ${SDL2_LIBRARIES})
    endif()
    # The recompiled code and librecomp call into each other.
    target_link_libraries(mp2-recomp PRIVATE -Wl,--start-group mp2_recompiled librecomp -Wl,--end-group)
endif()

# Fonts and icons the menus need, next to the executable.
set(MP2_ASSET_DIR "$<TARGET_FILE_DIR:mp2-recomp>/assets")
set(MP2_RMLUI_ASSETS "${MP2_LIB_DIR}/RecompFrontend/recompui/lib/RmlUi/Samples/assets")
add_custom_command(TARGET mp2-recomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MP2_ASSET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/recomp.rcss"
            "${MP2_RMLUI_ASSETS}/LatoLatin-Regular.ttf"
            "${MP2_RMLUI_ASSETS}/LatoLatin-Bold.ttf"
            "${MP2_RMLUI_ASSETS}/LatoLatin-Italic.ttf"
            "${MP2_RMLUI_ASSETS}/NotoEmoji-Regular.ttf"
            "${MP2_ASSET_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons" "${MP2_ASSET_DIR}/icons"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MP2_ASSET_DIR}/promptfont"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/promptfont/promptfont.ttf"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/promptfont/LICENSE.txt"
            "${MP2_ASSET_DIR}/promptfont"
    COMMENT "Copying menu fonts and icons")
