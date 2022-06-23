#pragma once
/*
uint128.h
An unsigned 128 bit integer type for C++

Copyright (c) 2013 - 2017 Jason Lee @ calccrypto at gmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

With much help from Auston Sterling

Thanks to Stefan Deigmüller for finding
a bug in operator*.

Thanks to François Dessenne for convincing me
to do a general rewrite of this class.
*/
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN || \
    defined(__BIG_ENDIAN__) ||                               \
    defined(__ARMEB__) ||                                    \
    defined(__THUMBEB__) ||                                  \
    defined(__AARCH64EB__) ||                                \
    defined(_MIBSEB) || defined(__MIBSEB) || defined(__MIBSEB__)
#ifndef __BIG_ENDIAN__
#define __BIG_ENDIAN__
#endif
#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN || \
    defined(__LITTLE_ENDIAN__) ||                                 \
    defined(__ARMEL__) ||                                         \
    defined(__THUMBEL__) ||                                       \
    defined(__AARCH64EL__) ||                                     \
    defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__) || \
    defined(_WIN32) || defined(__i386__) || defined(__x86_64__) || \
    defined(_X86_) || defined(_IA64_)
#ifndef __LITTLE_ENDIAN__
#define __LITTLE_ENDIAN__
#endif
#else
#error "I don't know what architecture this is!"
#endif

class uint128_t;

// Give uint128_t type traits
namespace std {  // This is probably not a good idea
    template <> struct is_arithmetic <uint128_t> : std::true_type {};
    template <> struct is_integral   <uint128_t> : std::true_type {};
    template <> struct is_unsigned   <uint128_t> : std::true_type {};
}


class uint128_t
{
private:
#ifdef __BIG_ENDIAN__
    uint64_t UPPER, LOWER;
#endif
#ifdef __LITTLE_ENDIAN__
    uint64_t LOWER, UPPER;
#endif

public:
    // Constructors
    uint128_t() = default;
    uint128_t(const uint128_t & rhs) = default;
    uint128_t(uint128_t && rhs) = default;

    // do not use prefixes (0x, 0b, etc.)
    // if the input string is too long, only right most characters are read
    uint128_t(const std::string & s, uint8_t base);
    uint128_t(const char *s, std::size_t len, uint8_t base);

    uint128_t(const bool & b);

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t(const T& rhs)
#ifdef __BIG_ENDIAN__
        : UPPER(0), LOWER(rhs)
#endif
#ifdef __LITTLE_ENDIAN__
        : LOWER(rhs), UPPER(0)
#endif
    {
        if (std::is_signed<T>::value)
        {
            if (rhs < 0)
                UPPER = -1;
        }
    }

    template <typename S, typename T, typename = typename std::enable_if <std::is_integral<S>::value&& std::is_integral<T>::value, void>::type>
    constexpr uint128_t(const S& upper_rhs, const T& lower_rhs) noexcept
#ifdef __BIG_ENDIAN__
        : UPPER(upper_rhs), LOWER(lower_rhs)
#endif
#ifdef __LITTLE_ENDIAN__
        : LOWER(lower_rhs), UPPER(upper_rhs)
#endif
    {}

    //  RHS input args only

    // Assignment Operator
    uint128_t& operator=(const uint128_t& rhs) = default;
    uint128_t& operator=(uint128_t&& rhs) = default;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    uint128_t& operator=(const T& rhs)
    {
        UPPER = 0;
        if (std::is_signed<T>::value)
        {
            if (rhs < 0)
                UPPER = -1;
        }

        LOWER = rhs;
        return *this;
    }

    uint128_t& operator=(const bool& rhs);

    // Typecast Operators
    operator bool() const;
    operator uint8_t() const;
    operator uint16_t() const;
    operator uint32_t() const;
    operator uint64_t() const;

    // Bitwise Operators
    uint128_t operator&(const uint128_t& rhs) const;

    void export_bits(std::vector<uint8_t>& ret) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    uint128_t operator&(const T& rhs) const
    {
        return uint128_t(0, LOWER & (uint64_t)rhs);
    }

    uint128_t& operator&=(const uint128_t& rhs);

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t& operator&=(const T& rhs) noexcept
    {
        UPPER = 0;
        LOWER &= rhs;
        return *this;
    }

    uint128_t operator|(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t operator|(const T& rhs) const noexcept
    {
        return uint128_t(UPPER, LOWER | (uint64_t)rhs);
    }

    uint128_t& operator|=(const uint128_t& rhs);

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t& operator|=(const T& rhs) noexcept
    {
        LOWER |= (uint64_t)rhs;
        return *this;
    }

    uint128_t operator^(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t operator^(const T& rhs) const noexcept
    {
        return uint128_t(UPPER, LOWER ^ (uint64_t)rhs);
    }

    uint128_t& operator^=(const uint128_t& rhs);

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t& operator^=(const T& rhs) noexcept
    {
        LOWER ^= (uint64_t)rhs;
        return *this;
    }

    uint128_t operator~() const;

    // Bit Shift Operators
    constexpr uint128_t operator<<(const uint128_t& rhs) const noexcept
    {
        const uint64_t shift = rhs.LOWER;
        if (((bool)rhs.UPPER) || (shift >= 128)) {
            return 0;
        }
        else if (shift == 64) {
            return uint128_t(LOWER, 0);
        }
        else if (shift == 0) {
            return *this;
        }
        else if (shift < 64) {
            return uint128_t((UPPER << shift) + (LOWER >> (64 - shift)), LOWER << shift);
        }
        else if ((128 > shift) && (shift > 64)) {
            return uint128_t(LOWER << (shift - 64), 0);
        }
        else {
            return 0;
        }
    }
    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t operator<<(const T& rhs) const noexcept
    {
        return *this << uint128_t(rhs);
    }

    constexpr uint128_t& operator<<=(const uint128_t& rhs) noexcept;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t& operator<<=(const T& rhs) noexcept
    {
        *this = *this << uint128_t(rhs);
        return *this;
    }

    constexpr uint128_t operator>>(const uint128_t & rhs) const noexcept
    {
	const uint64_t shift = rhs.LOWER;
	if (((bool) rhs.UPPER) || (shift >= 128))
		return 0;
    	if (shift == 64)
        	return uint128_t(0, UPPER);
	if (shift == 0)
        	return *this;
    	if (shift < 64)
	        return uint128_t(UPPER >> shift, (UPPER << (64 - shift)) + (LOWER >> shift));
    	if ((128 > shift) && (shift > 64))
        	return uint128_t(0, (UPPER >> (shift - 64)));
        return 0;
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t operator>>(const T& rhs) const noexcept
    {
        return *this >> uint128_t(rhs);
    }

    constexpr uint128_t& operator>>=(const uint128_t& rhs) noexcept;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t& operator>>=(const T& rhs) noexcept
    {
        *this = *this >> uint128_t(rhs);
        return *this;
    }

    // Logical Operators
    bool operator!() const;
    bool operator&&(const uint128_t& rhs) const;
    bool operator||(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr bool operator&&(const T& rhs) const noexcept
    {
        return static_cast <bool> (*this && rhs);
    }

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr bool operator||(const T& rhs) const noexcept
    {
        return static_cast <bool> (*this || rhs);
    }

    // Comparison Operators
    bool operator==(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr bool operator==(const T& rhs) const noexcept
    {
        return (!UPPER && (LOWER == (uint64_t)rhs));
    }

    bool operator!=(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr bool operator!=(const T& rhs) const noexcept
    {
        return (UPPER || (LOWER != (uint64_t)rhs));
    }

    bool operator>(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr bool operator>(const T& rhs) const noexcept
    {
        return (UPPER || (LOWER > (uint64_t)rhs));
    }

    bool operator<(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr bool operator<(const T& rhs) const noexcept
    {
        return (!UPPER) ? (LOWER < (uint64_t)rhs) : false;
    }

    bool operator>=(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr bool operator>=(const T& rhs) const noexcept
    {
        return ((*this > rhs) || (*this == rhs));
    }

    bool operator<=(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr bool operator<=(const T& rhs) const noexcept
    {
        return ((*this < rhs) || (*this == rhs));
    }

    // Arithmetic Operators
    uint128_t operator+(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t operator+(const T& rhs) const noexcept
    {
        return uint128_t(UPPER + ((LOWER + (uint64_t)rhs) < LOWER), LOWER + (uint64_t)rhs);
    }

    uint128_t& operator+=(const uint128_t& rhs);

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t& operator+=(const T& rhs) noexcept
    {
        return *this += uint128_t(rhs);
    }

    uint128_t operator-(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t operator-(const T& rhs) const noexcept
    {
        return uint128_t((uint64_t)(UPPER - ((LOWER - rhs) > LOWER)), (uint64_t)(LOWER - rhs));
    }

    uint128_t& operator-=(const uint128_t& rhs);

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t& operator-=(const T& rhs) noexcept
    {
        return *this = *this - uint128_t(rhs);
    }

    uint128_t operator*(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    uint128_t operator*(const T& rhs) const
    {
        return *this * uint128_t(rhs);
    }

    uint128_t& operator*=(const uint128_t& rhs);

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    uint128_t& operator*=(const T& rhs)
    {
        return *this = *this * uint128_t(rhs);
    }

private:
    std::pair <uint128_t, uint128_t> divmod(const uint128_t& lhs, const uint128_t& rhs) const;
    void ConvertToVector(std::vector<uint8_t> & current, const uint64_t & val) const;
        // do not use prefixes (0x, 0b, etc.)
        // if the input string is too long, only right most characters are read
        void init(const char * s, std::size_t len, uint8_t base);
        void _init_hex(const char *s, std::size_t len);
        void _init_dec(const char *s, std::size_t len);
        void _init_oct(const char *s, std::size_t len);
        void _init_bin(const char *s, std::size_t len);

public:
    uint128_t operator/(const uint128_t& rhs) const;

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    uint128_t operator/(const T & rhs) const
    {
        return *this / uint128_t(rhs);
    }
    
    uint128_t & operator/=(const uint128_t & rhs);
    
    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    uint128_t & operator/=(const T & rhs)
    {
        return *this = *this / uint128_t(rhs);
    }
    
    uint128_t operator%(const uint128_t & rhs) const;
    
    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t operator%(const T & rhs) const noexcept
    {
        return *this % uint128_t(rhs);
    }
    
    uint128_t & operator%=(const uint128_t & rhs);

    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
    constexpr uint128_t & operator%=(const T & rhs) noexcept
    {
        return *this = *this % uint128_t(rhs);
    }
    
    // Increment Operator
    uint128_t & operator++();
    uint128_t operator++(int);
    
    // Decrement Operator
    uint128_t & operator--();
    uint128_t operator--(int);
    
    // Nothing done since promotion doesn't work here
    uint128_t operator+() const;
    
    // two's complement
    uint128_t operator-() const;
    
    // Get private values
    constexpr const uint64_t & upper() const noexcept
    {
        return UPPER;
    }
    constexpr const uint64_t & lower() const noexcept
    {
        return LOWER;
    }

    // Get bitsize of value
    uint8_t bits() const;
    
    // Get string representation of value
    std::string str(uint8_t base = 10, const unsigned int & len = 0) const;
};

// lhs type T as first arguemnt
// If the output is not a bool, casts to type T

// Bitwise Operators
template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
uint128_t operator&(const T & lhs, const uint128_t & rhs)
{
    return rhs & lhs;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
T & operator&=(T & lhs, const uint128_t & rhs)
{
    return lhs = static_cast <T> (rhs & lhs);
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
uint128_t operator|(const T & lhs, const uint128_t & rhs)
{
    return rhs | lhs;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
T & operator|=(T & lhs, const uint128_t & rhs)
{
    return lhs = static_cast <T> (rhs | lhs);
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
uint128_t operator^(const T & lhs, const uint128_t & rhs)
{
    return rhs ^ lhs;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
T & operator^=(T & lhs, const uint128_t & rhs)
{
    return lhs = static_cast <T> (rhs ^ lhs);
}

// Bitshift operators
uint128_t operator<<(const bool     & lhs, const uint128_t & rhs);
uint128_t operator<<(const uint8_t  & lhs, const uint128_t & rhs);
uint128_t operator<<(const uint16_t & lhs, const uint128_t & rhs);
uint128_t operator<<(const uint32_t & lhs, const uint128_t & rhs);
uint128_t operator<<(const uint64_t & lhs, const uint128_t & rhs);
uint128_t operator<<(const int8_t   & lhs, const uint128_t & rhs);
uint128_t operator<<(const int16_t  & lhs, const uint128_t & rhs);
uint128_t operator<<(const int32_t  & lhs, const uint128_t & rhs);
uint128_t operator<<(const int64_t  & lhs, const uint128_t & rhs);

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
T & operator<<=(T & lhs, const uint128_t & rhs)
{
    return lhs = static_cast <T> (uint128_t(lhs) << rhs);
}

uint128_t operator>>(const bool     & lhs, const uint128_t & rhs);
uint128_t operator>>(const uint8_t  & lhs, const uint128_t & rhs);
uint128_t operator>>(const uint16_t & lhs, const uint128_t & rhs);
uint128_t operator>>(const uint32_t & lhs, const uint128_t & rhs);
uint128_t operator>>(const uint64_t & lhs, const uint128_t & rhs);
uint128_t operator>>(const int8_t   & lhs, const uint128_t & rhs);
uint128_t operator>>(const int16_t  & lhs, const uint128_t & rhs);
uint128_t operator>>(const int32_t  & lhs, const uint128_t & rhs);
uint128_t operator>>(const int64_t  & lhs, const uint128_t & rhs);

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
T & operator>>=(T & lhs, const uint128_t & rhs)
{
    return lhs = static_cast <T> (uint128_t(lhs) >> rhs);
}

// Comparison Operators
template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
bool operator==(const T & lhs, const uint128_t & rhs)
{
    return (!rhs.upper() && ((uint64_t) lhs == rhs.lower()));
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
bool operator!=(const T & lhs, const uint128_t & rhs)
{
    return (rhs.upper() || ((uint64_t) lhs != rhs.lower()));
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
bool operator>(const T & lhs, const uint128_t & rhs)
{
    return (!rhs.upper()) && ((uint64_t) lhs > rhs.lower());
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
bool operator<(const T & lhs, const uint128_t & rhs)
{
    if (rhs.upper()){
        return true;
    }
    return ((uint64_t) lhs < rhs.lower());
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
bool operator>=(const T & lhs, const uint128_t & rhs)
{
    if (rhs.upper()){
        return false;
    }
    return ((uint64_t) lhs >= rhs.lower());
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
bool operator<=(const T & lhs, const uint128_t & rhs)
{
    if (rhs.upper()){
        return true;
    }
    return ((uint64_t) lhs <= rhs.lower());
}

// Arithmetic Operators
template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
uint128_t operator+(const T & lhs, const uint128_t & rhs)
{
    return rhs + lhs;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
T & operator+=(T & lhs, const uint128_t & rhs)
{
    return lhs = static_cast <T> (rhs + lhs);
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
uint128_t operator-(const T & lhs, const uint128_t & rhs)
{
    return -(rhs - lhs);
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
T & operator-=(T & lhs, const uint128_t & rhs)
{
    return lhs = static_cast <T> (-(rhs - lhs));
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
uint128_t operator*(const T & lhs, const uint128_t & rhs)
{
    return rhs * lhs;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
T & operator*=(T & lhs, const uint128_t & rhs)
{
    return lhs = static_cast <T> (rhs * lhs);
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
uint128_t operator/(const T & lhs, const uint128_t & rhs)
{
    return uint128_t(lhs) / rhs;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
T & operator/=(T & lhs, const uint128_t & rhs)
{
    return lhs = static_cast <T> (uint128_t(lhs) / rhs);
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
uint128_t operator%(const T & lhs, const uint128_t & rhs)
{
    return uint128_t(lhs) % rhs;
}

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value, T>::type >
T & operator%=(T & lhs, const uint128_t & rhs)
{
    return lhs = static_cast <T> (uint128_t(lhs) % rhs);
}

// useful values
static constexpr uint128_t uint128_0{ 0 };
static constexpr uint128_t uint128_1{ 1 };

// IO Operator
std::ostream & operator<<(std::ostream & stream, const uint128_t & rhs);
