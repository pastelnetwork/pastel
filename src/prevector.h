#pragma once
#include <util.h>
#include <assert.h>
#include <stdlib.h>
#include <cstdint>
#include <string.h>
#include <iterator>

#include <util.h>

#pragma pack(push, 1)
/** Implements a drop-in replacement for std::vector<T> which stores up to N
 *  elements directly (without heap allocation). The types Size and Diff are
 *  used to store element counts, and can be any unsigned + signed type.
 *
 *  Storage layout is either:
 *  - Direct allocation:
 *    - Size _size: the number of used elements (between 0 and N)
 *    - T direct[N]: an array of N elements of type T
 *      (only the first _size are initialized).
 *  - Indirect allocation:
 *    - Size _size: the number of used elements plus N + 1
 *    - Size capacity: the number of allocated elements
 *    - T* indirect: a pointer to an array of capacity elements of type T
 *      (only the first _size are initialized).
 *
 *  The data type T must be movable by memmove/realloc(). Once we switch to C++,
 *  move constructors can be used instead.
 */
template<unsigned int N, typename T, typename Size = uint32_t, typename Diff = int32_t>
class prevector {
public:
    typedef Size size_type;
    typedef Diff difference_type;
    typedef T value_type;
    typedef value_type& reference;
    typedef const value_type& const_reference;
    typedef value_type* pointer;
    typedef const value_type* const_pointer;

    class iterator {
        T* ptr;
    public:
        typedef Diff difference_type;
        typedef T value_type;
        typedef T* pointer;
        typedef T& reference;
        typedef std::random_access_iterator_tag iterator_category;
        iterator(T* ptr_) noexcept : ptr(ptr_) {}
        T& operator*() const noexcept { return *ptr; }
        T* operator->() const noexcept { return ptr; }
        T& operator[](const size_type pos) noexcept { return ptr[pos]; }
        const T& operator[](const size_type pos) const noexcept { return ptr[pos]; }
        iterator& operator++() noexcept { ptr++; return *this; }
        iterator& operator--() noexcept { ptr--; return *this; }
        iterator operator++(int) { iterator copy(*this); ++(*this); return copy; }
        iterator operator--(int) { iterator copy(*this); --(*this); return copy; }
        difference_type friend operator-(iterator a, iterator b) noexcept { return static_cast<difference_type>(&(*a) - &(*b)); }
        iterator operator+(size_type n) noexcept { return iterator(ptr + n); }
        iterator& operator+=(size_type n) noexcept { ptr += n; return *this; }
        iterator operator-(size_type n) noexcept { return iterator(ptr - n); }
        iterator& operator-=(size_type n) noexcept { ptr -= n; return *this; }
        bool operator==(iterator x) const noexcept { return ptr == x.ptr; }
        bool operator!=(iterator x) const noexcept { return ptr != x.ptr; }
        bool operator>=(iterator x) const noexcept { return ptr >= x.ptr; }
        bool operator<=(iterator x) const noexcept { return ptr <= x.ptr; }
        bool operator>(iterator x) const noexcept { return ptr > x.ptr; }
        bool operator<(iterator x) const noexcept { return ptr < x.ptr; }
    };

    class reverse_iterator {
        T* ptr;
    public:
        typedef Diff difference_type;
        typedef T value_type;
        typedef T* pointer;
        typedef T& reference;
        typedef std::bidirectional_iterator_tag iterator_category;
        reverse_iterator(T* ptr_) noexcept : ptr(ptr_) {}
        T& operator*() noexcept { return *ptr; }
        const T& operator*() const noexcept { return *ptr; }
        T* operator->() noexcept { return ptr; }
        const T* operator->() const noexcept { return ptr; }
        reverse_iterator& operator--() noexcept { ptr++; return *this; }
        reverse_iterator& operator++() noexcept { ptr--; return *this; }
        reverse_iterator operator++(int) { reverse_iterator copy(*this); ++(*this); return copy; }
        reverse_iterator operator--(int) { reverse_iterator copy(*this); --(*this); return copy; }
        bool operator==(reverse_iterator x) const noexcept { return ptr == x.ptr; }
        bool operator!=(reverse_iterator x) const noexcept { return ptr != x.ptr; }
    };

    class const_iterator {
        const T* ptr;
    public:
        typedef Diff difference_type;
        typedef const T value_type;
        typedef const T* pointer;
        typedef const T& reference;
        typedef std::random_access_iterator_tag iterator_category;
        const_iterator(const T* ptr_) noexcept : ptr(ptr_) {}
        const_iterator(iterator x) noexcept : ptr(&(*x)) {}
        const T& operator*() const noexcept { return *ptr; }
        const T* operator->() const noexcept { return ptr; }
        const T& operator[](size_type pos) const noexcept { return ptr[pos]; }
        const_iterator& operator++() noexcept { ptr++; return *this; }
        const_iterator& operator--() noexcept { ptr--; return *this; }
        const_iterator operator++(int) { const_iterator copy(*this); ++(*this); return copy; }
        const_iterator operator--(int) { const_iterator copy(*this); --(*this); return copy; }
        difference_type friend operator-(const_iterator a, const_iterator b) { return static_cast<difference_type>(&(*a) - &(*b)); }
        const_iterator operator+(size_type n) noexcept { return const_iterator(ptr + n); }
        const_iterator& operator+=(size_type n) noexcept { ptr += n; return *this; }
        const_iterator operator-(size_type n) noexcept { return const_iterator(ptr - n); }
        const_iterator& operator-=(size_type n) noexcept { ptr -= n; return *this; }
        bool operator==(const_iterator x) const noexcept { return ptr == x.ptr; }
        bool operator!=(const_iterator x) const noexcept { return ptr != x.ptr; }
        bool operator>=(const_iterator x) const noexcept { return ptr >= x.ptr; }
        bool operator<=(const_iterator x) const noexcept { return ptr <= x.ptr; }
        bool operator>(const_iterator x) const noexcept { return ptr > x.ptr; }
        bool operator<(const_iterator x) const noexcept { return ptr < x.ptr; }
    };

    class const_reverse_iterator {
        const T* ptr;
    public:
        typedef Diff difference_type;
        typedef const T value_type;
        typedef const T* pointer;
        typedef const T& reference;
        typedef std::bidirectional_iterator_tag iterator_category;
        const_reverse_iterator(const T* ptr_) noexcept : ptr(ptr_) {}
        const_reverse_iterator(reverse_iterator x) noexcept : ptr(&(*x)) {}
        const T& operator*() const noexcept { return *ptr; }
        const T* operator->() const noexcept { return ptr; }
        const_reverse_iterator& operator--() noexcept { ptr++; return *this; }
        const_reverse_iterator& operator++() noexcept { ptr--; return *this; }
        const_reverse_iterator operator++(int) { const_reverse_iterator copy(*this); ++(*this); return copy; }
        const_reverse_iterator operator--(int) { const_reverse_iterator copy(*this); --(*this); return copy; }
        bool operator==(const_reverse_iterator x) const noexcept { return ptr == x.ptr; }
        bool operator!=(const_reverse_iterator x) const noexcept { return ptr != x.ptr; }
    };

private:
    size_type _size;
    union {
        char direct[sizeof(T) * N];
        struct {
            size_type capacity;
            char* indirect;
        };
    } _union;

    T* direct_ptr(const difference_type pos) noexcept { return reinterpret_cast<T*>(_union.direct) + pos; }
    const T* direct_ptr(const difference_type pos) const noexcept { return reinterpret_cast<const T*>(_union.direct) + pos; }
    T* indirect_ptr(const difference_type pos) noexcept { return reinterpret_cast<T*>(_union.indirect) + pos; }
    const T* indirect_ptr(const difference_type pos) const noexcept { return reinterpret_cast<const T*>(_union.indirect) + pos; }
    bool is_direct() const noexcept { return _size <= N; }

    void change_capacity(const size_type new_capacity)
    {
        if (new_capacity <= N)
        {
            if (!is_direct())
            {
                T* indirect = indirect_ptr(0);
                T* src = indirect;
                T* dst = direct_ptr(0);
                memcpy(dst, src, size() * sizeof(T));
                free(indirect);
                _size -= N + 1;
            }
        } else {
            if (!is_direct())
            {
                /* FIXME: Because malloc/realloc here won't call new_handler if allocation fails, assert
                    success. These should instead use an allocator or new/delete so that handlers
                    are called as necessary, but performance would be slightly degraded by doing so. */
                 char* temp = static_cast<char*>(realloc(_union.indirect, ((size_t)sizeof(T)) * new_capacity));
                 if (!temp)
                     new_handler_terminate();
                 _union.indirect = temp;
                _union.capacity = new_capacity;
            } else {
                char* new_indirect = static_cast<char*>(malloc(((size_t)sizeof(T)) * new_capacity));
                if (!new_indirect)
                    new_handler_terminate();
                // copy data from direct to indirect
                const T* src = direct_ptr(0);
                T* dst = reinterpret_cast<T*>(new_indirect);
                memcpy(dst, src, size() * sizeof(T));
                _union.indirect = new_indirect;
                _union.capacity = new_capacity;
                // set _size to the end of direct data
                _size += N + 1;
            }
        }
    }

    T* item_ptr(const difference_type pos) noexcept { return is_direct() ? direct_ptr(pos) : indirect_ptr(pos); }
    const T* item_ptr(const difference_type pos) const noexcept { return is_direct() ? direct_ptr(pos) : indirect_ptr(pos); }

public:
    void assign(size_type n, const T& val)
    {
        clear();
        if (capacity() < n)
            change_capacity(n);
        while (size() < n)
        {
            _size++;
            new(static_cast<void*>(item_ptr(size() - 1))) T(val);
        }
    }

    template<typename InputIterator>
    void assign(InputIterator first, InputIterator last)
    {
        size_type n = last - first;
        clear();
        if (capacity() < n)
            change_capacity(n);
        while (first != last)
        {
            _size++;
            new(static_cast<void*>(item_ptr(size() - 1))) T(*first);
            ++first;
        }
    }

    prevector() noexcept : 
        _size(0),
        _union()
    {
        _union.capacity = 0;
    }

    prevector(prevector&& p) noexcept
    {
        _size = p._size;
        if (p.is_direct())
        {
			memcpy(_union.direct, p._union.direct, _size * sizeof(T));
        }
        else
        {
			_union.indirect = p._union.indirect;
			_union.capacity = p._union.capacity;
			p._union.indirect = nullptr;
			p._union.capacity = 0;
		}
		p._size = 0;
    }
    prevector& operator=(prevector&& p) noexcept
    {
        if (this != &p)
        {
			clear();
			_size = p._size;
            if (p.is_direct())
            {
				memcpy(_union.direct, p._union.direct, _size * sizeof(T));
            }
            else
            {
				_union.indirect = p._union.indirect;
				_union.capacity = p._union.capacity;
				p._union.indirect = nullptr;
				p._union.capacity = 0;
			}
			p._size = 0;
		}
		return *this;
    }

    explicit prevector(const size_type n) : 
        _size(0),
        _union()
    {
        resize(n);
    }

    explicit prevector(const size_type n, const T& val = T()) : 
        _size(0),
        _union()
    {
        change_capacity(n);
        if (is_direct())
        {
            for (size_type i = 0; i < n; ++i)
            {
                new(static_cast<void*>(direct_ptr(i))) T(val);
                ++_size;
            }
        } else
        {
            for (size_type i = 0; i < n; ++i)
            {
                new(static_cast<void*>(indirect_ptr(i))) T(val);
                ++_size;
            }
        }
    }

    template<typename InputIterator>
    prevector(InputIterator first, InputIterator last) :
        _size(0)
    {
        size_type n = static_cast<size_type>(std::distance(first, last));
        change_capacity(n);
        while (first != last)
        {
            _size++;
            new(static_cast<void*>(item_ptr(size() - 1))) T(*first);
            ++first;
        }
    }

    prevector(const prevector& other) :
        _size(0),
        _union()
    {
        change_capacity(other.size());

        size_type size_other = other.size();
        const T* other_data = other.is_direct() ? other.direct_ptr(0) : other.indirect_ptr(0);

        for (size_type i = 0; i < size_other; ++i)
        {
            _size++;
            new(static_cast<void*>(item_ptr(size() - 1))) T(other_data[i]);
        }
    }

    prevector& operator=(const prevector& other)
    {
        if (&other == this)
            return *this;

        resize(0);
        change_capacity(other.size());

        size_type size_other = other.size();
        const T* other_data = other.is_direct() ? other.direct_ptr(0) : other.indirect_ptr(0);

        for (size_type i = 0; i < size_other; ++i)
        {
            _size++;
            new(static_cast<void*>(item_ptr(size() - 1))) T(other_data[i]);
        }
        return *this;
    }

    size_type size() const noexcept
    {
        return is_direct() ? _size : _size - N - 1;
    }

    bool empty() const noexcept
    {
        return size() == 0;
    }

    iterator begin() noexcept { return iterator(item_ptr(0)); }
    const_iterator begin() const noexcept { return const_iterator(item_ptr(0)); }
    iterator end() noexcept { return iterator(item_ptr(size())); }
    const_iterator end() const noexcept { return const_iterator(item_ptr(size())); }

    reverse_iterator rbegin() noexcept { return reverse_iterator(item_ptr(size() - 1)); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(item_ptr(size() - 1)); }
    reverse_iterator rend() noexcept { return reverse_iterator(item_ptr(-1)); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(item_ptr(-1)); }

    size_t capacity() const noexcept
    {
        return is_direct() ? N : _union.capacity;
    }

    T& operator[](size_type pos) noexcept
    {
        return *item_ptr(pos);
    }

    const T& operator[](size_type pos) const noexcept
    {
        return *item_ptr(pos);
    }

    void resize(size_type new_size)
    {
        while (size() > new_size)
        {
            item_ptr(size() - 1)->~T();
            _size--;
        }
        if (new_size > capacity())
            change_capacity(new_size);
        while (size() < new_size)
        {
            _size++;
            new(static_cast<void*>(item_ptr(size() - 1))) T();
        }
    }

    void reserve(size_type new_capacity) 
    {
        if (new_capacity > capacity())
            change_capacity(new_capacity);
    }

    void shrink_to_fit()
    {
        change_capacity(size());
    }

    void clear()
    {
        resize(0);
    }

    iterator insert(iterator pos, const T& value)
    {
        size_type p = pos - begin();
        size_type new_size = size() + 1;
        if (capacity() < new_size)
            change_capacity(new_size + (new_size >> 1));
        memmove(item_ptr(p + 1), item_ptr(p), (size() - p) * sizeof(T));
        _size++;
        new(static_cast<void*>(item_ptr(p))) T(value);
        return iterator(item_ptr(p));
    }

    void insert(iterator pos, size_type count, const T& value)
    {
        size_type p = pos - begin();
        size_type new_size = size() + count;
        if (capacity() < new_size)
            change_capacity(new_size + (new_size >> 1));
        memmove(item_ptr(p + count), item_ptr(p), (size() - p) * sizeof(T));
        _size += count;
        for (size_type i = 0; i < count; i++)
            new(static_cast<void*>(item_ptr(p + i))) T(value);
    }

    template<typename InputIterator>
    void insert(iterator pos, InputIterator first, InputIterator last)
    {
        size_type p = pos - begin();
        difference_type count = static_cast<difference_type>(last - first);
        size_type new_size = size() + count;
        if (capacity() < new_size)
            change_capacity(new_size + (new_size >> 1));
        memmove(item_ptr(p + count), item_ptr(p), (size() - p) * sizeof(T));
        _size += count;
        while (first != last)
        {
            new(static_cast<void*>(item_ptr(p))) T(*first);
            ++p;
            ++first;
        }
    }

    iterator erase(iterator pos)
    {
        (*pos).~T();
        memmove(&(*pos), &(*pos) + 1, ((const char*)&(*end())) - ((const char*)(1 + &(*pos))));
        _size--;
        return pos;
    }

    iterator erase(iterator first, iterator last) noexcept
    {
        iterator p = first;
        const char* endp = reinterpret_cast<const char*>(&(*end()));
        while (p != last)
        {
            (*p).~T();
            _size--;
            ++p;
        }
        memmove(&(*first), &(*last), endp - (reinterpret_cast<const char*>(&(*last))));
        return first;
    }

    void push_back(const T& value)
    {
        size_type new_size = size() + 1;
        if (capacity() < new_size) {
            change_capacity(new_size + (new_size >> 1));
        }
        new(item_ptr(size())) T(value);
        _size++;
    }

    void pop_back() noexcept
    {
        _size--;
    }

    T& front() noexcept
    {
        return *item_ptr(0);
    }

    const T& front() const noexcept
    {
        return *item_ptr(0);
    }

    T& back() noexcept
    {
        return *item_ptr(size() - 1);
    }

    const T& back() const noexcept
    {
        return *item_ptr(size() - 1);
    }

    void swap(prevector<N, T, Size, Diff>& other) noexcept
    {
        if (_size & other._size & 1)
        {
            std::swap(_union.capacity, other._union.capacity);
            std::swap(_union.indirect, other._union.indirect);
        } else
            std::swap(_union, other._union);
        std::swap(_size, other._size);
    }

    virtual ~prevector()
    {
        clear();
        if (!is_direct())
        {
            free(_union.indirect);
            _union.indirect = nullptr;
        }
    }

    bool operator==(const prevector<N, T, Size, Diff>& other) const
    {
        if (other.size() != size())
            return false;
        const_iterator b1 = begin();
        const_iterator b2 = other.begin();
        const_iterator e1 = end();
        while (b1 != e1)
        {
            if ((*b1) != (*b2))
                return false;
            ++b1;
            ++b2;
        }
        return true;
    }

    bool operator!=(const prevector<N, T, Size, Diff>& other) const
    {
        return !(*this == other);
    }

    bool operator<(const prevector<N, T, Size, Diff>& other) const
    {
        if (size() < other.size())
            return true;
        if (size() > other.size())
            return false;
        const_iterator b1 = begin();
        const_iterator b2 = other.begin();
        const_iterator e1 = end();
        while (b1 != e1)
        {
            if ((*b1) < (*b2))
                return true;
            if ((*b2) < (*b1))
                return false;
            ++b1;
            ++b2;
        }
        return false;
    }

    size_t allocated_memory() const
    {
        if (is_direct())
            return 0;
        return ((size_t)(sizeof(T))) * _union.capacity;
    }
};
#pragma pack(pop)
