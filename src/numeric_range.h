#pragma once
#pragma once
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

/**
 * Simple class to represent numeric range for integer types.
 * Supports range-based interface.
 * 
 * \tparam _IntType - any integer type (int, unsigned int, uint32_t, int64_t, uint64_t, size_t)
 */
template <typename _IntType>
class numeric_range
{
public:
    class iterator
    {
        friend class numeric_range;

    public:
        _IntType operator*() const noexcept { return iter; }
        const iterator &operator++() noexcept
        {
            ++iter;
            return *this;
        }
        iterator operator++(int) noexcept
        {
            iterator copy(*this);
            ++iter;
            return copy;
        }

        bool operator==(const iterator& other) const noexcept { return iter == other.iter; }
        bool operator!=(const iterator& other) const noexcept { return iter != other.iter; }

    protected:
        iterator(const _IntType start) : 
            iter(start)
        {}

    private:
        _IntType iter;
    };

    typedef _IntType range_type;

    numeric_range(const _IntType nMin, const _IntType nMax) : 
        m_nMin(nMin),
        m_nMax(nMax)
    {}

    iterator begin() const noexcept { return iterator(m_nMin); }
    iterator end() const noexcept { return iterator(m_nMax + 1); }

    iterator cbegin() const noexcept { return iterator(m_nMin); }
    iterator cend() const noexcept { return iterator(m_nMax + 1); }

    _IntType min() const noexcept { return m_nMin; }
    _IntType max() const noexcept { return m_nMax; }

    bool contains(const _IntType N) const noexcept
    {
        return (N >= m_nMin) && (N <= m_nMax);
    }

private:
    _IntType m_nMin;
    _IntType m_nMax;
};
