#pragma once

#include "detect_cpp_standard.h"
#ifdef _HAS_CPP11_FEATURES
#include <type_traits>
#endif

// use to_integral_type to convert enum class to underlying type
#ifdef _FORCED_CPP14_FEATURES
// c++14
template<typename _EnumClass>
constexpr auto to_integral_type(const _EnumClass e)
{
	return static_cast<std::underlying_type_t<_EnumClass>>(e);
}
#elif defined(_HAS_CPP11_FEATURES)
// c++11
template<typename _EnumClass>
#if defined(_MSC_VER) && (_MSC_VER <= 1700)
auto
#else
const auto
#endif
to_integral_type(const _EnumClass e) -> typename std::underlying_type<_EnumClass>::type
{
	return static_cast<typename std::underlying_type<_EnumClass>::type>(e);
}
#endif

template <typename _EnumClass>
constexpr auto enum_or(const _EnumClass e1, const _EnumClass e2)
{
    return to_integral_type<_EnumClass>(e1) | to_integral_type<_EnumClass>(e2);
}

template <typename _EnumClass>
constexpr bool is_enum_valid(const std::underlying_type_t<_EnumClass> e, const _EnumClass eLowValid, const _EnumClass eHighValid)
{
    return (e >= to_integral_type<_EnumClass>(eLowValid) && (e <= to_integral_type<_EnumClass>(eHighValid)));
}
