# PCSX2's GS as a shared library behind a small C interface (include/pcsx2gs_api.h).
# GPL-3.0+, from github.com/PCSX2/pcsx2 commit 2954ebe6965669219f1bbd256e958fbd8f83a85d.
#
# Adapted from urban-reign-recomp (thirdparty/pcsx2-gs, GPL-3.0, thiagordsfx), which builds the DX11/DX12 renderers
# with MSVC. This version builds the OpenGL device (hardware renderer) and the software renderer, so the same code
# runs on Windows (WGL) and Linux (EGL, including surfaceless contexts for headless test runs), with clang/GCC and
# llvm-mingw. The runtime loads the library at run time (dlopen / LoadLibrary), so PCSX2's ImGui, fmt and glad never
# meet the runtime's copies.
cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pcsx2_gs_standalone LANGUAGES C CXX)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(P "${CMAKE_CURRENT_SOURCE_DIR}/../pcsx2")

add_library(pcsx2_flags INTERFACE)
target_compile_features(pcsx2_flags INTERFACE cxx_std_20)
target_compile_definitions(pcsx2_flags INTERFACE
    _M_X86=1 __SSE4_1__=1 ENABLE_OPENGL=1 PCSX2_CORE=1
    IMGUI_USER_CONFIG="pcsx2gs_imconfig.h" ST_NO_EXCEPTION_HANDLING)
if(WIN32)
    target_compile_definitions(pcsx2_flags INTERFACE
        WINVER=0x0A00 _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX _UNICODE UNICODE
        _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS PCSX2GS_NO_D3D)
    if(NOT MSVC)
        # MinGW: Windows SDK headers in lower case, and the few MSVC-only pieces PCSX2 assumes.
        target_include_directories(pcsx2_flags INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/compat/mingw")
    endif()
endif()
target_compile_options(pcsx2_flags INTERFACE -msse4.1 -fno-strict-aliasing -w -fvisibility=hidden)
target_include_directories(pcsx2_flags INTERFACE
    "${P}" "${P}/pcsx2" "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${P}/3rdparty/fmt/include" "${P}/3rdparty/imgui/include" "${P}/3rdparty/xbyak"
    "${P}/3rdparty/fast_float/include" "${P}/3rdparty/cpuinfo/include" "${P}/3rdparty/include"
    "${P}/3rdparty/glad/include")

# --- third party ---
add_library(pcsx2_fmt STATIC "${P}/3rdparty/fmt/src/format.cc" "${P}/3rdparty/fmt/src/os.cc")
target_link_libraries(pcsx2_fmt PRIVATE pcsx2_flags)

add_library(pcsx2_imgui STATIC
    "${P}/3rdparty/imgui/src/imgui.cpp" "${P}/3rdparty/imgui/src/imgui_draw.cpp"
    "${P}/3rdparty/imgui/src/imgui_tables.cpp" "${P}/3rdparty/imgui/src/imgui_widgets.cpp"
    "${P}/3rdparty/imgui/src/imgui_stdlib.cpp")
target_link_libraries(pcsx2_imgui PRIVATE pcsx2_flags)

if(WIN32)
    add_library(pcsx2_glad STATIC "${P}/3rdparty/glad/src/gl.c" "${P}/3rdparty/glad/src/wgl.c")
else()
    add_library(pcsx2_glad STATIC "${P}/3rdparty/glad/src/gl.c" "${P}/3rdparty/glad/src/egl.c")
endif()
target_include_directories(pcsx2_glad PUBLIC "${P}/3rdparty/glad/include")
set_target_properties(pcsx2_glad PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)

set(CPUINFO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(CPUINFO_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(CPUINFO_BUILD_MOCK_TESTS OFF CACHE BOOL "" FORCE)
set(CPUINFO_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(CPUINFO_BUILD_PKG_CONFIG OFF CACHE BOOL "" FORCE)
set(CPUINFO_LIBRARY_TYPE static CACHE STRING "" FORCE)
add_subdirectory("${P}/3rdparty/cpuinfo" "${CMAKE_CURRENT_BINARY_DIR}/cpuinfo" EXCLUDE_FROM_ALL)
set_target_properties(cpuinfo PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- common/ (PCSX2's utility library) ---
set(COMMON_SOURCES
    AlignedMalloc.cpp Assertions.cpp Console.cpp DynamicLibrary.cpp Error.cpp FileSystem.cpp HostSys.cpp
    MemorySettingsInterface.cpp MD5Digest.cpp Perf.cpp ProgressCallback.cpp ReadbackSpinManager.cpp Semaphore.cpp
    SettingsWrapper.cpp SmallString.cpp StringUtil.cpp TextureDecompress.cpp Timer.cpp WindowInfo.cpp)
if(WIN32)
    list(APPEND COMMON_SOURCES Windows/WinThreads.cpp Windows/WinHostSys.cpp Windows/WinMisc.cpp)
else()
    list(APPEND COMMON_SOURCES Linux/LnxThreads.cpp Linux/LnxHostSys.cpp)
endif()
list(TRANSFORM COMMON_SOURCES PREPEND "${P}/common/")
if(NOT WIN32)
    list(APPEND COMMON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/compat/LnxMiscNoDBus.cpp")
endif()
add_library(pcsx2_common STATIC ${COMMON_SOURCES})
target_link_libraries(pcsx2_common PRIVATE pcsx2_flags cpuinfo)

# --- GS: hardware renderer on the OpenGL device, and the software renderer ---
set(GS_SOURCES
    GS/GS.cpp GS/GSClut.cpp GS/GSDrawingContext.cpp GS/GSDrawingEnvironment.cpp GS/GSLocalMemory.cpp
    GS/GSPerfMon.cpp GS/GSRingHeap.cpp GS/GSState.cpp GS/GSTables.cpp GS/GSUtil.cpp GS/GSVector.cpp GS/MultiISA.cpp
    GS/Renderers/Common/GSDevice.cpp GS/Renderers/Common/GSDirtyRect.cpp GS/Renderers/Common/GSFunctionMap.cpp
    GS/Renderers/Common/GSRenderer.cpp GS/Renderers/Common/GSTexture.cpp GS/Renderers/Common/GSVertexTrace.cpp
    GS/Renderers/Null/GSRendererNull.cpp GS/Renderers/HW/GSHwHack.cpp GS/Renderers/HW/GSRendererHW.cpp
    GS/Renderers/HW/GSTextureCache.cpp GS/Renderers/SW/GSTextureCacheSW.cpp
    GS/GSBlock.cpp GS/GSLocalMemoryMultiISA.cpp GS/GSXXH.cpp GS/Renderers/Common/GSVertexTraceFMM.cpp
    GS/Renderers/HW/GSRendererHWMultiISA.cpp GS/Renderers/SW/GSDrawScanline.cpp GS/Renderers/SW/GSRasterizer.cpp
    GS/Renderers/SW/GSRendererSW.cpp GS/Renderers/SW/GSDrawScanlineCodeGenerator.all.cpp
    GS/Renderers/SW/GSSetupPrimCodeGenerator.all.cpp
    GS/Renderers/OpenGL/GLContext.cpp GS/Renderers/OpenGL/GLProgram.cpp GS/Renderers/OpenGL/GLShaderCache.cpp
    GS/Renderers/OpenGL/GLState.cpp GS/Renderers/OpenGL/GLStreamBuffer.cpp GS/Renderers/OpenGL/GSDeviceOGL.cpp
    GS/Renderers/OpenGL/GSTextureOGL.cpp
    GS/Renderers/HW/GSTextureReplacements.cpp)
if(WIN32)
    list(APPEND GS_SOURCES GS/Renderers/OpenGL/GLContextWGL.cpp)
else()
    list(APPEND GS_SOURCES GS/Renderers/OpenGL/GLContextEGL.cpp)
endif()
list(TRANSFORM GS_SOURCES PREPEND "${P}/pcsx2/")
list(APPEND GS_SOURCES compat/GSTextureReplacementLoaders.cpp)
add_library(pcsx2_gs STATIC ${GS_SOURCES})
target_link_libraries(pcsx2_gs PRIVATE pcsx2_flags)
target_link_libraries(pcsx2_gs PUBLIC pcsx2_common pcsx2_fmt pcsx2_imgui pcsx2_glad cpuinfo)
if(WIN32)
    target_link_libraries(pcsx2_gs PUBLIC opengl32 gdi32 winmm ole32 windowscodecs pathcch onecore) # onecore: VirtualAlloc2/MapViewOfFile3 (Windows 10 1803+)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(pcsx2_gs PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_library(pcsx2gs_api INTERFACE)
target_include_directories(pcsx2gs_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# pcsx2gs (.dll / .so): the GS behind include/pcsx2gs_api.h. Next to it at run time: resources/ (shaders, OSD font).
add_library(pcsx2gs SHARED compat/Bridge.cpp compat/HostStubs.cpp compat/Pcsx2ConfigGS.cpp)
target_link_libraries(pcsx2gs PRIVATE pcsx2_flags pcsx2gs_api pcsx2_gs)
target_compile_definitions(pcsx2gs PRIVATE PCSX2GS_BUILD=1)
if(NOT WIN32)
    target_link_options(pcsx2gs PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined)
elseif(NOT MSVC)
    # MinGW: carry libc++/libunwind inside the DLL, so it needs nothing beyond Windows' own DLLs.
    target_link_options(pcsx2gs PRIVATE -static)
endif()
set_target_properties(pcsx2gs PROPERTIES PREFIX "")
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set_target_properties(pcsx2gs PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ps2xRuntime"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ps2xRuntime")
endif()
add_custom_command(TARGET pcsx2gs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:pcsx2gs>/pcsx2gs_resources")
