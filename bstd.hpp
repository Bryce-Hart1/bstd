#pragma once
/**
* bstd @version 1 @author Bryce Hart
* @details includes all of bstd. Each part is only included when the compiler's C++ version supports it,
* so this compiles on C++11 and up and gives you everything that version allows (C++23 for all of it).
* Nothing here needs a specific OS: Windows, Linux and macOS all get the same headers.
*/

// MSVC leaves __cplusplus at 199711L unless /Zc:__cplusplus is passed, _MSVC_LANG always has the real version
#if defined(_MSVC_LANG) && _MSVC_LANG > __cplusplus
    #define BSTD_CPP _MSVC_LANG
#else
    #define BSTD_CPP __cplusplus
#endif

#if BSTD_CPP < 201103L
    #error "bstd needs C++11 or newer."
#endif

// <windows.h> defines min() and max() macros (unless NOMINMAX is set), which break std::numeric_limits<T>::max().
// They are hidden while bstd is included and put back after.
#if defined(_WIN32)
    #pragma push_macro("min")
    #pragma push_macro("max")
    #undef min
    #undef max
#endif

// C++11
#include "bit/varyingInt.hpp"
#include "cypher/cypher.hpp"
#include "data_structure/binTree.hpp"
#include "data_structure/bTree.hpp"
#include "error/exceptions.hpp"
#include "system/timeKeeper.hpp"     //gmtime_s / localtime_s on windows, the _r versions everywhere else

#if BSTD_CPP >= 201703L
    #include "bit/convertBit.hpp"
    #include "data_structure/threadsafe/tsTrie.hpp"
    #include "data_structure/threadsafe/tsVector.hpp"
    #include "encode/encode.hpp"
    #include "error/errorType.hpp"
    #include "store/convert.hpp"
    #include "system/hardware.hpp"   //reads linux /proc and /sys, compiles everywhere but returns nullopt off linux
    #include "system/periodic.hpp"
#endif //C++17

#if BSTD_CPP >= 202002L
    #include <version>
    #include "bit/bitBuffer.hpp"
    #include "bit/writeBin.hpp"
    #include "data_structure/threadsafe/tsBinaryTree.hpp"
    #include "data_structure/threadsafe/tsQueue.hpp"
    #include "data_structure/threadsafe/tsStack.hpp"
    #include "data_structure/tree.hpp"
    #include "form/numFormat.hpp"
    #include "store/log.hpp"
    #include "store/serialize.hpp"

    // std::string_view::contains is C++23, checked by feature so older compilers' C++2b modes still get it
    #if defined(__cpp_lib_string_contains)
        #include "form/autoCorrect.hpp"
    #endif
#endif //C++20

// data_structure/threadsafe/tsHashmap.hpp is left out until it compiles

#if defined(_WIN32)
    #pragma pop_macro("max")
    #pragma pop_macro("min")
#endif
