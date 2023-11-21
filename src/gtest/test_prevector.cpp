// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2021-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <vector>

#include <gtest/gtest.h>

#include <utils/prevector.h>
#include <utils/serialize.h>
#include <utils/streams.h>
#include <random.h>

using namespace std;
using namespace testing;

template<unsigned int N, typename T>
class prevector_tester
{
    typedef vector<T> realtype;
    realtype real_vector;

    typedef prevector<N, T> pretype;
    pretype pre_vector;

    typedef typename pretype::size_type Size;

    void test()
    {
        const pretype& const_pre_vector = pre_vector;
        EXPECT_EQ(real_vector.size(), pre_vector.size());
        EXPECT_EQ(real_vector.empty(), pre_vector.empty());
        for (Size s = 0; s < static_cast<Size>(real_vector.size()); s++)
        {
             EXPECT_EQ(real_vector[s] , pre_vector[s]);
             EXPECT_EQ(&(pre_vector[s]) , &(pre_vector.begin()[s]));
             EXPECT_EQ(&(pre_vector[s]) , &*(pre_vector.begin() + s));
             EXPECT_EQ(&(pre_vector[s]) , &*((pre_vector.end() + s) - static_cast<Size>(real_vector.size())));
        }

        EXPECT_EQ(pretype(real_vector.begin(), real_vector.end()) , pre_vector);
        EXPECT_EQ(pretype(pre_vector.begin(), pre_vector.end()) , pre_vector);
        size_t pos = 0;
        for (const T& v : pre_vector)
	    {
             EXPECT_EQ(v , real_vector[pos++]);
        }
        for (auto it = pre_vector.rbegin(); it != pre_vector.rend(); ++it)
	    {
             EXPECT_EQ(*it , real_vector[--pos]);
        }
        for (const T& v : const_pre_vector)
	    {
             EXPECT_EQ(v , real_vector[pos++]);
        }
        for (auto it = const_pre_vector.rbegin(); it != const_pre_vector.rend(); ++it)
	    {
             EXPECT_EQ(*it , real_vector[--pos]);
        }
        CDataStream ss1(SER_DISK, 0);
        CDataStream ss2(SER_DISK, 0);
        ss1 << real_vector;
        ss2 << pre_vector;
        EXPECT_EQ(ss1.size(), ss2.size());
        for (Size s = 0; s < ss1.size(); s++) {
            EXPECT_EQ(ss1[s], ss2[s]);
        }
    }

public:
    void resize(Size s)
    {
        real_vector.resize(s);
        EXPECT_EQ(real_vector.size(), s);
        pre_vector.resize(s);
        EXPECT_EQ(pre_vector.size(), s);
        test();
    }

    void reserve(Size s)
    {
        real_vector.reserve(s);
        EXPECT_TRUE(real_vector.capacity() >= s);
        pre_vector.reserve(s);
        EXPECT_TRUE(pre_vector.capacity() >= s);
        test();
    }

    void insert(Size position, const T& value)
    {
        real_vector.insert(real_vector.begin() + position, value);
        pre_vector.insert(pre_vector.begin() + position, value);
        test();
    }

    void insert(Size position, Size count, const T& value)
    {
        real_vector.insert(real_vector.begin() + position, count, value);
        pre_vector.insert(pre_vector.begin() + position, count, value);
        test();
    }

    template<typename I>
    void insert_range(Size position, I first, I last)
    {
        real_vector.insert(real_vector.begin() + position, first, last);
        pre_vector.insert(pre_vector.begin() + position, first, last);
        test();
    }

    void erase(Size position)
    {
        real_vector.erase(real_vector.begin() + position);
        pre_vector.erase(pre_vector.begin() + position);
        test();
    }

    void erase(Size first, Size last)
    {
        real_vector.erase(real_vector.begin() + first, real_vector.begin() + last);
        pre_vector.erase(pre_vector.begin() + first, pre_vector.begin() + last);
        test();
    }

    void update(Size pos, const T& value)
    {
        real_vector[pos] = value;
        pre_vector[pos] = value;
        test();
    }

    void push_back(const T& value)
    {
        real_vector.push_back(value);
        pre_vector.push_back(value);
        test();
    }

    void pop_back()
    {
        real_vector.pop_back();
        pre_vector.pop_back();
        test();
    }

    void clear()
    {
        real_vector.clear();
        pre_vector.clear();
    }

    void assign(Size n, const T& value)
    {
        real_vector.assign(n, value);
        pre_vector.assign(n, value);
    }

    Size size() const noexcept
    {
        return static_cast<Size>(real_vector.size());
    }

    Size capacity() const noexcept
    {
        return static_cast<Size>(pre_vector.capacity());
    }

    void shrink_to_fit()
    {
        pre_vector.shrink_to_fit();
        test();
    }

    void test_constructors_and_assignments()
    {
        // Test copy constructor
        pretype copy_constructed_vector(pre_vector);
        EXPECT_EQ(pre_vector, copy_constructed_vector);

        // Test move constructor
        pretype move_constructed_vector(std::move(copy_constructed_vector));
        EXPECT_EQ(pre_vector, move_constructed_vector);

        // Create new vectors for assignment tests
        pretype copy_assigned_vector;
        pretype move_assigned_vector;

        // Test copy assignment operator
        copy_assigned_vector = pre_vector;
        EXPECT_EQ(pre_vector, copy_assigned_vector);

        // Test move assignment operator
        move_assigned_vector = std::move(copy_assigned_vector);
        EXPECT_EQ(pre_vector, move_assigned_vector);
    }

    void test_size_value_constructor(const unsigned int nSize, const T value)
    {
        // Create pre_vector and real_vector with the same size and value.
        pretype pre_vector1(nSize, value);
        realtype real_vector1(nSize, value);

        // Test if the size of pre_vector and real_vector is the same.
        EXPECT_EQ(pre_vector1.size(), real_vector1.size());

        // Test if all elements in the pre_vector and real_vector are the same.
        for (Size i = 0; i < nSize; ++i)
        {
            EXPECT_EQ(pre_vector1[i], real_vector1[i]);
        }

        // Test if the memory type of pre_vector is correct.
        EXPECT_EQ(pre_vector1.capacity() <= N, nSize <= N);
    }
};

TEST(test_prevector, PrevectorTestInt)
{
    for (int j = 0; j < 64; j++)
    {
        prevector_tester<8, int> test;
        for (int i = 0; i < 2048; i++)
        {
            int r = insecure_rand();
            if ((r % 4) == 0)
                test.insert(insecure_rand() % (test.size() + 1), insecure_rand());

            if (test.size() > 0 && ((r >> 2) % 4) == 1)
                test.erase(insecure_rand() % test.size());

            if (((r >> 4) % 8) == 2)
            {
                const int new_size = max<int>(0, min<int>(30, test.size() + (insecure_rand() % 5) - 2));
                test.resize(new_size);
            }

            if (((r >> 7) % 8) == 3)
                test.insert(insecure_rand() % (test.size() + 1), 1 + (insecure_rand() % 2), insecure_rand());

            if (((r >> 10) % 8) == 4)
            {
                const int del = min<int>(test.size(), 1 + (insecure_rand() % 2));
                const int beg = insecure_rand() % (test.size() + 1 - del);
                test.erase(beg, beg + del);
            }

            if (((r >> 13) % 16) == 5)
                test.push_back(insecure_rand());

            if (test.size() > 0 && ((r >> 17) % 16) == 6)
                test.pop_back();

            if (((r >> 21) % 32) == 7)
            {
                int values[4];
                int num = 1 + (insecure_rand() % 4);
                for (int i = 0; i < num; i++)
                    values[i] = insecure_rand();
                test.insert_range(insecure_rand() % (test.size() + 1), values, values + num);
            }
            if (((r >> 26) % 32) == 8)
            {
                const int del = min<int>(test.size(), 1 + (insecure_rand() % 4));
                const int beg = insecure_rand() % (test.size() + 1 - del);
                test.erase(beg, beg + del);
            }

            r = insecure_rand();
            if (r % 32 == 9)
                test.reserve(insecure_rand() % 32);

            if ((r >> 5) % 64 == 10)
                test.shrink_to_fit();

            if (test.size() > 0)
                test.update(insecure_rand() % test.size(), insecure_rand());

            if (((r >> 11) & 1024) == 11)
                test.clear();

            if (((r >> 21) & 512) == 12)
                test.assign(insecure_rand() % 32, insecure_rand());

            test.test_constructors_and_assignments();
        }
    }
}

TEST(test_prevector, PrevectorTestSizeValueConstructor)
{
    for (int j = 0; j < 64; j++) 
    {
        prevector_tester<8, int> test;

        // Call explicit constructor with size and val parameters
        const int size = insecure_rand() % 32;
        const int val = insecure_rand();
        test.test_size_value_constructor(size, val);
    }
}