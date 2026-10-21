// p3fes: the one header force-included into every kzvu translation unit (CMake merges repeated -include flags, so
// there is only one). On MinGW, windows.h comes first: PCSX2's Pcsx2Defs.h redefines __forceinline without 'inline',
// and SDK inline helpers parsed after it would become strong definitions in every object.
#pragma once
#if defined(__MINGW32__)
#include <windows.h>
#endif
#include "kzvu_rename.h"
