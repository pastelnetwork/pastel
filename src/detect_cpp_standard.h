#pragma once

#ifdef _MSC_VER
#  if _MSC_VER >= 1700 // Visual Studio 2012
#    ifdef __cplusplus
#      ifndef _HAS_CPP11_FEATURES
#        define _HAS_CPP11_FEATURES
#      endif
#      if _MSC_VER >= 1900 // Visual Studio 2015
#        ifndef _HAS_CPP14_FEATURES
#          define _HAS_CPP14_FEATURES
#        endif
#      endif
#        if _MSC_VER >= 1910 // Visual Studio 2017
#          ifndef _HAS_CPP17_FEATURES
#          define _HAS_CPP17_FEATURES
#        endif
#      endif
#      if _MSC_VER >= 1920 // Visual Studio 2019
#        ifndef _HAS_CPP20_FEATURES
#          define _HAS_CPP20_FEATURES
#        endif
#      endif
#    endif // __cplusplus
#  endif // _MSC_VER >= 1700
#endif // _WIN32_

// _MSVC_LANG predefined macro specifies the C++ language standard targeted by the compiler
#ifdef _MSVC_LANG
#  if _MSVC_LANG >= 201402L
#     ifndef _FORCED_CPP14_FEATURES
#       define _FORCED_CPP14_FEATURES
#     endif
#     if _MSVC_LANG > 201402L
#       ifndef _FORCED_CPP17_FEATURES
#         define _FORCED_CPP17_FEATURES
#       endif
#       if _MSVC_LANG > 201703L
#          ifndef _FORCED_CPP20_FEATURES
#            define _FORCED_CPP20_FEATURES
#          endif
#       endif 
#     endif
#  endif
#endif // _MSVC_LANG

// __GXX_EXPERIMENTAL_CXX0X__ is defined only with c++0x option
// gcc version >= 4.7 will have __cpluscplus defined as 201103L
#if defined(__GNUG__) || defined(__CLANG__) // g++ compiler
#if defined(__GXX_EXPERIMENTAL_CXX0X__) || (defined(__cplusplus) && (__cplusplus >= 201103L))
// check C++xx features
# if (__cplusplus >= 201703L)
#   if (__cplusplus > 201703L)
#      ifndef _HAS_CPP20_FEATURES
#         define _HAS_CPP20_FEATURES
#      endif
#      ifndef _FORCED_CPP20_FEATURES
#         define _FORCED_CPP20_FEATURES
#      endif
#   endif
#   ifndef _HAS_CPP17_FEATURES
#     define _HAS_CPP17_FEATURES
#   endif
#   ifndef _FORCED_CPP17_FEATURES
#     define _FORCED_CPP17_FEATURES
#   endif
# endif
# if (__cplusplus >= 201402L)
#   ifndef _HAS_CPP14_FEATURES
#     define _HAS_CPP14_FEATURES
#   endif
#   ifndef _FORCED_CPP14_FEATURES
#     define _FORCED_CPP14_FEATURES
#   endif
# endif
# ifndef _HAS_CPP11_FEATURES
#   define _HAS_CPP11_FEATURES
# endif
#endif // __GXX_EXPERIMENTAL_CXX0X__ || _cplusplus >= 201103L
#endif // __GNUG__

