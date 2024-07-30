// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2021-2024 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include <utils/random.h>
#include <utils/hash.h>
#include <addrman.h>

using namespace std;
using namespace testing;

class CAddrManTest : 
    public CAddrMan,
    public Test
{
    uint64_t state;

public:
    CAddrManTest()
    {
        state = 1;
    }

    //! Ensure that bucket placement is always the same for testing purposes.
    void MakeDeterministic()
    {
        nKey.SetNull();
        seed_insecure_rand(true);
    }

    int RandomInt(int nMax)
    {
        state = (CHashWriter(SER_GETHASH, 0) << state).GetHash().GetCheapHash();
        return (unsigned int)(state % nMax);
    }

    CAddrInfo* Find(const CNetAddr& addr, int* pnId = nullptr)
    {
        return CAddrMan::Find(addr, pnId);
    }

    CAddrInfo* Create(const CAddress& addr, const CNetAddr& addrSource, int* pnId = nullptr)
    {
        return CAddrMan::Create(addr, addrSource, pnId);
    }

    void Delete(int nId)
    {
        CAddrMan::Delete(nId);
    }
};

TEST_F(CAddrManTest, simple)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    CNetAddr source("252.2.2.2");

    // Test 1: Does Addrman respond correctly when empty.
    EXPECT_TRUE(empty());
    CAddrInfo addr_null = Select();
    EXPECT_EQ(addr_null.ToString(), "[::]:0");

    // Test 2: Does Addrman::Add work as expected.
    CService addr1("250.1.1.1", 8333);
    Add(CAddress(addr1), source);
    EXPECT_EQ(size(), 1u);

    CAddrInfo addr_ret1 = Select();
    EXPECT_EQ(addr_ret1.ToString(), "250.1.1.1:8333");

    // Test 3: Does IP address deduplication work correctly.
    //  Expected dup IP should not be added.
    CService addr1_dup("250.1.1.1", 8333);
    Add(CAddress(addr1_dup), source);
    EXPECT_EQ(size(), 1u);

    // Test 5: New table has one addr and we add a diff addr we should
    //  have two addrs.
    CService addr2("250.1.1.2", 8333);
    Add(CAddress(addr2), source);
    EXPECT_EQ(size(), 2u);

    // Test 6: AddrMan::Clear() should empty the new table.
    Clear();
    EXPECT_TRUE(empty());
    CAddrInfo addr_null2 = Select();
    EXPECT_EQ(addr_null2.ToString(), "[::]:0");
}

TEST_F(CAddrManTest, ports)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    CNetAddr source("252.2.2.2");

    EXPECT_TRUE(empty());

    // Test 7; Addr with same IP but diff port does not replace existing addr.
    CService addr1("250.1.1.1", 8333);
    Add(CAddress(addr1), source);
    EXPECT_EQ(size(), 1u);

    CService addr1_port("250.1.1.1", 8334);
    Add(CAddress(addr1_port), source);
    EXPECT_EQ(size(), 1u);
    CAddrInfo addr_ret2 = Select();
    EXPECT_EQ(addr_ret2.ToString(), "250.1.1.1:8333");

    // Test 8: Add same IP but diff port to tried table, it doesn't get added.
    //  Perhaps this is not ideal behavior but it is the current behavior.
    Good(CAddress(addr1_port));
    EXPECT_EQ(size(), 1u);
    const bool newOnly = true;
    CAddrInfo addr_ret3 = Select(newOnly);
    EXPECT_EQ(addr_ret3.ToString(), "250.1.1.1:8333");
}


TEST_F(CAddrManTest, select)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    CNetAddr source("252.2.2.2");

    // Test 9: Select from new with 1 addr in new.
    CService addr1("250.1.1.1", 8333);
    Add(CAddress(addr1), source);
    EXPECT_EQ(size(), 1u);

    const bool newOnly = true;
    CAddrInfo addr_ret1 = Select(newOnly);
    EXPECT_EQ(addr_ret1.ToString(),"250.1.1.1:8333");

    // Test 10: move addr to tried, select from new expected nothing returned.
    Good(CAddress(addr1));
    EXPECT_EQ(size(), 1u);
    CAddrInfo addr_ret2 = Select(newOnly);
    EXPECT_EQ(addr_ret2.ToString(), "[::]:0");

    CAddrInfo addr_ret3 = Select();
    EXPECT_EQ(addr_ret3.ToString(), "250.1.1.1:8333");

    EXPECT_EQ(size(), 1u);


    // Add three addresses to new table.
    CService addr2("250.3.1.1", 8333);
    CService addr3("250.3.2.2", 9999);
    CService addr4("250.3.3.3", 9999);

    Add(CAddress(addr2), CService("250.3.1.1", 8333));
    Add(CAddress(addr3), CService("250.3.1.1", 8333));
    Add(CAddress(addr4), CService("250.4.1.1", 8333));

    // Add three addresses to tried table.
    CService addr5("250.4.4.4", 8333);
    CService addr6("250.4.5.5", 7777);
    CService addr7("250.4.6.6", 8333);

    Add(CAddress(addr5), CService("250.3.1.1", 8333));
    Good(CAddress(addr5));
    Add(CAddress(addr6), CService("250.3.1.1", 8333));
    Good(CAddress(addr6));
    Add(CAddress(addr7), CService("250.1.1.3", 8333));
    Good(CAddress(addr7));

    // Test 11: 6 addrs + 1 addr from last test = 7.
    EXPECT_EQ(size(), 7u);

    // Test 12: Select pulls from new and tried regardless of port number.
    EXPECT_EQ(Select().ToString(), "250.4.6.6:8333");
    EXPECT_EQ(Select().ToString(), "250.3.2.2:9999");
    EXPECT_EQ(Select().ToString(), "250.3.3.3:9999");
    EXPECT_EQ(Select().ToString(), "250.4.4.4:8333");
}

TEST_F(CAddrManTest, new_collisions)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    CNetAddr source("252.2.2.2");

    EXPECT_TRUE(empty());

    for (size_t i = 1; i < 18; i++)
    {
        CService addr("250.1.1." + to_string(i));
        Add(CAddress(addr), source);

        //Test 13: No collision in new table yet.
        EXPECT_EQ(size(), i);
    }

    //Test 14: new table collision!
    CService addr1("250.1.1.18");
    Add(CAddress(addr1), source);
    EXPECT_EQ(size(), 17);

    CService addr2("250.1.1.19");
    Add(CAddress(addr2), source);
    EXPECT_EQ(size(), 18);
}

TEST_F(CAddrManTest, tried_collisions)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    CNetAddr source("252.2.2.2");

    EXPECT_TRUE(empty());

    for (size_t i = 1; i < 80; i++)
    {
        CService addr("250.1.1." + to_string(i));
        Add(CAddress(addr), source);
        Good(CAddress(addr));

        //Test 15: No collision in tried table yet.
        EXPECT_EQ(size(), i) << size();
    }

    //Test 16: tried table collision!
    CService addr1("250.1.1.80");
    Add(CAddress(addr1), source);
    EXPECT_EQ(size(), 79u);

    CService addr2("250.1.1.81");
    Add(CAddress(addr2), source);
    EXPECT_EQ(size(), 80u);
}

TEST_F(CAddrManTest, find)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    EXPECT_TRUE(empty());

    CAddress addr1(CService("250.1.2.1", 8333));
    CAddress addr2(CService("250.1.2.1", 9999));
    CAddress addr3(CService("251.255.2.1", 8333));

    CNetAddr source1("250.1.2.1");
    CNetAddr source2("250.1.2.2");

    Add(addr1, source1);
    Add(addr2, source2);
    Add(addr3, source1);

    // Test 17: ensure Find returns an IP matching what we searched on.
    CAddrInfo* info1 = Find(addr1);
    EXPECT_NE(info1, nullptr);
    if (info1)
        EXPECT_EQ(info1->ToString(), "250.1.2.1:8333");

    // Test 18; Find does not discriminate by port number.
    CAddrInfo* info2 = Find(addr2);
    EXPECT_NE(info2, nullptr);
    if (info2)
        EXPECT_EQ(info2->ToString(), info1->ToString());

    // Test 19: Find returns another IP matching what we searched on.
    CAddrInfo* info3 = Find(addr3);
    EXPECT_NE(info3, nullptr);
    if (info3)
        EXPECT_EQ(info3->ToString(), "251.255.2.1:8333");
}

TEST_F(CAddrManTest, create)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    EXPECT_TRUE(empty());

    CAddress addr1(CService("250.1.2.1", 8333));
    CNetAddr source1("250.1.2.1");

    int nId;
    CAddrInfo* pinfo = Create(addr1, source1, &nId);

    // Test 20: The result should be the same as the input addr.
    EXPECT_EQ(pinfo->ToString(), "250.1.2.1:8333");

    CAddrInfo* info2 = Find(addr1);
    EXPECT_EQ(info2->ToString(), "250.1.2.1:8333");
}


TEST_F(CAddrManTest, delete)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    EXPECT_TRUE(empty());

    CAddress addr1(CService("250.1.2.1", 8333));
    CNetAddr source1("250.1.2.1");

    int nId;
    Create(addr1, source1, &nId);

    // Test 21: Delete should actually delete the addr.
    EXPECT_EQ(size(), 1u);
    Delete(nId);
    EXPECT_TRUE(empty());

    CAddrInfo* info2 = Find(addr1);
    EXPECT_EQ(info2, nullptr);
}

TEST_F(CAddrManTest, getaddr)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    // Test 22: Sanity check, GetAddr should never return anything if addrman
    //  is empty.
    EXPECT_TRUE(empty());
    vector<CAddress> vAddr1 = GetAddr();
    EXPECT_TRUE(vAddr1.empty());

    CAddress addr1(CService("250.250.2.1", 8333));
    addr1.nTime = static_cast<unsigned int>(GetAdjustedTime()); // Set time so isTerrible = false
    CAddress addr2(CService("250.251.2.2", 9999));
    addr2.nTime = static_cast<unsigned int>(GetAdjustedTime());
    CAddress addr3(CService("251.252.2.3", 8333));
    addr3.nTime = static_cast<unsigned int>(GetAdjustedTime());
    CAddress addr4(CService("252.253.3.4", 8333));
    addr4.nTime = static_cast<unsigned int>(GetAdjustedTime());
    CAddress addr5(CService("252.254.4.5", 8333));
    addr5.nTime = static_cast<unsigned int>(GetAdjustedTime());
    CNetAddr source1("250.1.2.1");
    CNetAddr source2("250.2.3.3");

    // Test 23: Ensure GetAddr works with new addresses.
    Add(addr1, source1);
    Add(addr2, source2);
    Add(addr3, source1);
    Add(addr4, source2);
    Add(addr5, source1);

    // GetAddr returns 23% of addresses, 23% of 5 is 1 rounded down.
    EXPECT_EQ(GetAddr().size(), 1u);

    // Test 24: Ensure GetAddr works with new and tried addresses.
    Good(CAddress(addr1));
    Good(CAddress(addr2));
    EXPECT_EQ(GetAddr().size(), 1u);

    // Test 25: Ensure GetAddr still returns 23% when addrman has many addrs.
    for (size_t i = 1; i < (8 * 256); i++)
    {
        const int octet1 = i % 256;
        const int octet2 = (i / 256) % 256;
        const int octet3 = (i / (256 * 2)) % 256;
        string strAddr = to_string(octet1) + "." + to_string(octet2) + "." + to_string(octet3) + ".23";

        CAddress addr(CService(strAddr), NODE_NETWORK);

        // Ensure that for all addrs in addrman, isTerrible == false.
        addr.nTime = static_cast<unsigned int>(GetAdjustedTime());
        Add(addr, CNetAddr(strAddr));
        if (i % 8 == 0)
            Good(addr);
    }
    vector<CAddress> vAddr = GetAddr();

    const size_t percent23 = (size() * 23) / 100;
    EXPECT_EQ(vAddr.size(), percent23);
    EXPECT_EQ(vAddr.size(), 461u);
    // (Addrman.size() < number of addresses added) due to address collisons.
    EXPECT_EQ(size(), 2007u);
}

TEST_F(CAddrManTest, addrinfo_get_tried_bucket)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    CAddress addr1(CService("250.1.1.1", 8333));
    CAddress addr2(CService("250.1.1.1", 9999));

    CNetAddr source1 = CNetAddr("250.1.1.1");


    CAddrInfo info1(addr1, source1);

    uint256 nKey1 = (uint256)(CHashWriter(SER_GETHASH, 0) << 1).GetHash();
    uint256 nKey2 = (uint256)(CHashWriter(SER_GETHASH, 0) << 2).GetHash();


    EXPECT_EQ(info1.GetTriedBucket(nKey1), 40);

    // Test 26: Make sure key actually randomizes bucket placement. A fail on
    //  this test could be a security issue.
    EXPECT_NE(info1.GetTriedBucket(nKey1), info1.GetTriedBucket(nKey2));

    // Test 27: Two addresses with same IP but different ports can map to
    //  different buckets because they have different keys.
    CAddrInfo info2(addr2, source1);

    EXPECT_NE(info1.GetKey(), info2.GetKey());
    EXPECT_NE(info1.GetTriedBucket(nKey1), info2.GetTriedBucket(nKey1));

    set<int> buckets;
    for (int i = 0; i < 255; i++)
    {
        CAddrInfo infoi(
            CAddress(CService("250.1.1." + to_string(i))),
            CNetAddr("250.1.1." + to_string(i)));
        int bucket = infoi.GetTriedBucket(nKey1);
        buckets.insert(bucket);
    }
    // Test 28: IP addresses in the same group (\16 prefix for IPv4) should
    //  never get more than 8 buckets
    EXPECT_EQ(buckets.size(), 8u);

    buckets.clear();
    for (int j = 0; j < 255; j++)
    {
        CAddrInfo infoj(
            CAddress(CService("250." + to_string(j) + ".1.1")),
            CNetAddr("250." + to_string(j) + ".1.1"));
        const int bucket = infoj.GetTriedBucket(nKey1);
        buckets.insert(bucket);
    }
    // Test 29: IP addresses in the different groups should map to more than
    //  8 buckets.
    EXPECT_EQ(buckets.size(), 160u);
}

TEST_F(CAddrManTest, addrinfo_get_new_bucket)
{
    // Set addrman addr placement to be deterministic.
    MakeDeterministic();

    CAddress addr1(CService("250.1.2.1", 8333));
    CAddress addr2(CService("250.1.2.1", 9999));

    CNetAddr source1("250.1.2.1");

    CAddrInfo info1(addr1, source1);

    uint256 nKey1 = (uint256)(CHashWriter(SER_GETHASH, 0) << 1).GetHash();
    uint256 nKey2 = (uint256)(CHashWriter(SER_GETHASH, 0) << 2).GetHash();

    EXPECT_EQ(info1.GetNewBucket(nKey1), 786u);

    // Test 30: Make sure key actually randomizes bucket placement. A fail on
    //  this test could be a security issue.
    EXPECT_NE(info1.GetNewBucket(nKey1), info1.GetNewBucket(nKey2));

    // Test 31: Ports should not affect bucket placement in the addr
    CAddrInfo info2(addr2, source1);
    EXPECT_NE(info1.GetKey(), info2.GetKey());
    EXPECT_EQ(info1.GetNewBucket(nKey1), info2.GetNewBucket(nKey1));

    set<int> buckets;
    for (int i = 0; i < 255; i++)
    {
        CAddrInfo infoi(
            CAddress(CService("250.1.1." + to_string(i))),
            CNetAddr("250.1.1." + to_string(i)));
        const int bucket = infoi.GetNewBucket(nKey1);
        buckets.insert(bucket);
    }
    // Test 32: IP addresses in the same group (\16 prefix for IPv4) should
    //  always map to the same bucket.
    EXPECT_EQ(buckets.size(), 1u);

    buckets.clear();
    for (int j = 0; j < 4 * 255; j++)
    {
        CAddrInfo infoj(CAddress(CService(to_string(250 + (j / 255)) + "." + to_string(j % 256) + ".1.1")),
                                 CNetAddr("251.4.1.1"));
        const int bucket = infoj.GetNewBucket(nKey1);
        buckets.insert(bucket);
    }
    // Test 33: IP addresses in the same source groups should map to no more
    //  than 64 buckets.
    EXPECT_LE(buckets.size(), 64u);

    buckets.clear();
    for (int p = 0; p < 255; p++)
    {
        CAddrInfo infoj(
            CAddress(CService("250.1.1.1")),
            CNetAddr("250." + to_string(p) + ".1.1"));
        const int bucket = infoj.GetNewBucket(nKey1);
        buckets.insert(bucket);
    }
    // Test 34: IP addresses in the different source groups should map to more
    //  than 64 buckets.
    EXPECT_GT(buckets.size(), 64);
}
