# kzvu - PCSX2's VU1 (microVU x86-64 recompiler + VU interpreter) as a static library.
#
# From KillzoneRecomp (github.com/drakolordx7/KillzoneRecomp, ext/kzvu, GPL-3.0+), with this CMake file rewritten for
# clang/GCC on Linux and llvm-mingw on Windows (the original supports MSVC only). The shims and src are unchanged.
# PCSX2 source: KZVU_PCSX2_DIR, a subset of PCSX2 at 646df006a (the commit kzvu was verified against).
cmake_minimum_required(VERSION 3.24)
project(kzvu C CXX)

set(KZVU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(KZVU_PCSX2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pcsx2-vu" CACHE PATH "PCSX2 source tree (microVU)")
get_filename_component(KZVU_PCSX2_DIR "${KZVU_PCSX2_DIR}" ABSOLUTE)
set(P2 "${KZVU_PCSX2_DIR}")
set(P2SRC "${P2}/pcsx2")
set(P3 "${P2}/3rdparty")
if(NOT EXISTS "${P2SRC}/x86/microVU.cpp")
    message(FATAL_ERROR "kzvu: PCSX2 source not found at ${P2}")
endif()

add_library(kzvu_flags INTERFACE)
target_compile_features(kzvu_flags INTERFACE cxx_std_20)
target_compile_definitions(kzvu_flags INTERFACE _M_X86=1)
if(MINGW)
    # Windows.h / VersionHelpers.h spelled as the PCSX2 sources include them (llvm-mingw ships lowercase names).
    target_include_directories(kzvu_flags INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../pcsx2-gs/compat/mingw")
endif()
if(WIN32)
    target_compile_definitions(kzvu_flags INTERFACE WINVER=0x0A00 _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX
        _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS _UNICODE UNICODE)
endif()
target_compile_options(kzvu_flags INTERFACE -msse4.1 -fno-strict-aliasing -fPIC -w
    "$<$<COMPILE_LANGUAGE:CXX>:-include;${KZVU_ROOT}/shim/include/kzvu_force.h>")
target_include_directories(kzvu_flags INTERFACE
    "${KZVU_ROOT}/shim/include" "${P2}" "${P2SRC}" "${P3}/include" "${P3}/fmt/include" "${P3}/fast_float/include")

add_library(kzvu_common OBJECT
    ${P2}/common/Assertions.cpp ${P2}/common/Console.cpp ${P2}/common/Error.cpp ${P2}/common/FileSystem.cpp
    ${P2}/common/Perf.cpp ${P2}/common/StringUtil.cpp ${P2}/common/SmallString.cpp ${P2}/common/Timer.cpp
    ${P2}/common/emitter/bmi.cpp ${P2}/common/emitter/fpu.cpp ${P2}/common/emitter/groups.cpp
    ${P2}/common/emitter/jmp.cpp ${P2}/common/emitter/legacy.cpp ${P2}/common/emitter/legacy_sse.cpp
    ${P2}/common/emitter/movs.cpp ${P2}/common/emitter/simd.cpp ${P2}/common/emitter/x86emitter.cpp
    ${P3}/fmt/src/format.cc)
if(NOT WIN32)
    target_sources(kzvu_common PRIVATE ${P2}/common/AlignedMalloc.cpp)
endif()
target_link_libraries(kzvu_common PRIVATE kzvu_flags)

set(KZVU_VU_SRCS
    ${KZVU_ROOT}/shim/unity/KzvuMicroVU.cpp ${P2SRC}/VUops.cpp ${P2SRC}/VUflags.cpp
    ${P2SRC}/VU1microInterp.cpp ${P2SRC}/VU0microInterp.cpp)
file(GLOB KZVU_SHIM_SRCS CONFIGURE_DEPENDS "${KZVU_ROOT}/shim/*.cpp")
add_library(kzvu_vu OBJECT ${KZVU_VU_SRCS} ${KZVU_SHIM_SRCS})
target_link_libraries(kzvu_vu PRIVATE kzvu_flags)
target_include_directories(kzvu_vu PRIVATE "${KZVU_ROOT}/include")
set_source_files_properties(${KZVU_VU_SRCS} PROPERTIES COMPILE_OPTIONS "-include;${KZVU_ROOT}/shim/include/kzvu_prefix.h")

add_library(kzvu STATIC "${KZVU_ROOT}/src/kzvu.cpp" $<TARGET_OBJECTS:kzvu_common> $<TARGET_OBJECTS:kzvu_vu>)
target_link_libraries(kzvu PRIVATE kzvu_flags)
set_source_files_properties("${KZVU_ROOT}/src/kzvu.cpp" PROPERTIES COMPILE_OPTIONS "-include;${KZVU_ROOT}/shim/include/kzvu_prefix.h")
target_include_directories(kzvu PUBLIC "${KZVU_ROOT}/include")
if(WIN32)
    target_link_libraries(kzvu INTERFACE pathcch ole32 uuid shell32)
endif()
