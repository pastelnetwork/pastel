// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2021-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or httpd://www.opensource.org/licenses/mit-license.php.
#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include <utils/vector_types.h>
#include <utils/enum_util.h>
#include <utils/utilstrencodings.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <primitives/transaction.h>

using namespace std;
using namespace testing;

template<typename T>
void check_ser_rep(T thing, v_uint8 expected)
{
    CDataStream ss(SER_DISK, 0);
    ss << thing;

    EXPECT_EQ(GetSerializeSize(thing, 0, 0) , ss.size());

    v_uint8 serialized_representation(ss.begin(), ss.end());

    EXPECT_EQ(serialized_representation , expected);

    T thing_deserialized;
    ss >> thing_deserialized;

    EXPECT_EQ(thing_deserialized , thing);
}

class CSerializeMethodsTestSingle
{
protected:
    int intval = 0;
    bool boolval = 0;
    string stringval;
    const char* charstrval = nullptr;
    CTransaction txval;
public:
    CSerializeMethodsTestSingle() = default;
    CSerializeMethodsTestSingle(int intvalin, bool boolvalin, string stringvalin, 
        const char* charstrvalin, CTransaction txvalin) : 
        intval(intvalin), boolval(boolvalin), stringval(move(stringvalin)), 
        charstrval(charstrvalin), txval(txvalin){}
    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(intval);
        READWRITE(boolval);
        READWRITE(stringval);
        READWRITE(FLATDATA(charstrval));
        READWRITE(txval);
    }

    bool operator==(const CSerializeMethodsTestSingle& rhs) const
    {
        return  intval == rhs.intval && \
                boolval == rhs.boolval && \
                stringval == rhs.stringval && \
                strcmp(charstrval, rhs.charstrval) == 0 && \
                txval == rhs.txval;
    }
};

class CSerializeMethodsTestMany : public CSerializeMethodsTestSingle
{
public:
    using CSerializeMethodsTestSingle::CSerializeMethodsTestSingle;
    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITEMANY(intval, boolval, stringval, FLATDATA(charstrval), txval);
    }
};

TEST(test_serialize, optional)
{
    check_ser_rep<optional<unsigned char>>(0xff, {0x01, 0xff});
    check_ser_rep<optional<unsigned char>>(nullopt, {0x00});
    check_ser_rep<optional<string>>(string("Test"), {0x01, 0x04, 'T', 'e', 's', 't'});

    {
        // Ensure that canonical optional discriminant is used
        CDataStream ss(SER_DISK, 0);
        ss.write("\x02\x04Test", 6);
        optional<string> into;

        EXPECT_THROW(ss >> into, ios_base::failure);
    }
}

TEST(test_serialize, arrays)
{
    array<string, 2> test_case = {string("zub"), string("baz")};
    CDataStream ss(SER_DISK, 0);
    ss << test_case;

    auto hash = Hash(ss.begin(), ss.end());

    EXPECT_EQ("037a75620362617a" , HexStr(ss.begin(), ss.end()))<< HexStr(ss.begin(), ss.end());
    EXPECT_EQ(hash , uint256S("13cb12b2dd098dced0064fe4897c97f907ba3ed36ae470c2e7fc2b1111eba35a"))<< "actually got: " << hash.ToString();

    {
        // note: array of size 2 should serialize to be the same as a tuple
        pair<string, string> test_case_2 = {string("zub"), string("baz")};

        CDataStream ss2(SER_DISK, 0);
        ss2 << test_case_2;

        auto hash2 = Hash(ss2.begin(), ss2.end());

        EXPECT_EQ(hash , hash2);
    }

    array<string, 2> decoded_test_case;
    ss >> decoded_test_case;

    EXPECT_EQ(decoded_test_case , test_case);

    array<int32_t, 2> test = {100, 200};

    EXPECT_EQ(GetSerializeSize(test, 0, 0), 8u);
}

TEST(test_serialize, sizes)
{
    EXPECT_EQ(sizeof(char), GetSerializeSize(char(0), 0));
    EXPECT_EQ(sizeof(int8_t), GetSerializeSize(int8_t(0), 0));
    EXPECT_EQ(sizeof(uint8_t), GetSerializeSize(uint8_t(0), 0));
    EXPECT_EQ(sizeof(int16_t), GetSerializeSize(int16_t(0), 0));
    EXPECT_EQ(sizeof(uint16_t), GetSerializeSize(uint16_t(0), 0));
    EXPECT_EQ(sizeof(int32_t), GetSerializeSize(int32_t(0), 0));
    EXPECT_EQ(sizeof(uint32_t), GetSerializeSize(uint32_t(0), 0));
    EXPECT_EQ(sizeof(int64_t), GetSerializeSize(int64_t(0), 0));
    EXPECT_EQ(sizeof(uint64_t), GetSerializeSize(uint64_t(0), 0));
    EXPECT_EQ(sizeof(float), GetSerializeSize(float(0), 0));
    EXPECT_EQ(sizeof(double), GetSerializeSize(double(0), 0));
    // Bool is serialized as char
    EXPECT_EQ(sizeof(char), GetSerializeSize(bool(0), 0));

    // Sanity-check GetSerializeSize and c++ type matching
    EXPECT_EQ(GetSerializeSize(char(0), 0), 1U);
    EXPECT_EQ(GetSerializeSize(int8_t(0), 0), 1U);
    EXPECT_EQ(GetSerializeSize(uint8_t(0), 0), 1U);
    EXPECT_EQ(GetSerializeSize(int16_t(0), 0), 2U);
    EXPECT_EQ(GetSerializeSize(uint16_t(0), 0), 2U);
    EXPECT_EQ(GetSerializeSize(int32_t(0), 0), 4U);
    EXPECT_EQ(GetSerializeSize(uint32_t(0), 0), 4U);
    EXPECT_EQ(GetSerializeSize(int64_t(0), 0), 8U);
    EXPECT_EQ(GetSerializeSize(uint64_t(0), 0), 8U);
    EXPECT_EQ(GetSerializeSize(float(0), 0), 4U);
    EXPECT_EQ(GetSerializeSize(double(0), 0), 8U);
    EXPECT_EQ(GetSerializeSize(bool(0), 0), 1U);
}

TEST(test_serialize, floats_conversion)
{
    // Choose values that map unambigiously to binary floating point to avoid
    // rounding issues at the compiler side.
    EXPECT_EQ(ser_uint32_to_float(0x00000000), 0.0F);
    EXPECT_EQ(ser_uint32_to_float(0x3f000000), 0.5F);
    EXPECT_EQ(ser_uint32_to_float(0x3f800000), 1.0F);
    EXPECT_EQ(ser_uint32_to_float(0x40000000), 2.0F);
    EXPECT_EQ(ser_uint32_to_float(0x40800000), 4.0F);
    EXPECT_EQ(ser_uint32_to_float(0x44444444), 785.066650390625F);

    EXPECT_EQ(ser_float_to_uint32(0.0F), 0x00000000U);
    EXPECT_EQ(ser_float_to_uint32(0.5F), 0x3f000000U);
    EXPECT_EQ(ser_float_to_uint32(1.0F), 0x3f800000U);
    EXPECT_EQ(ser_float_to_uint32(2.0F), 0x40000000U);
    EXPECT_EQ(ser_float_to_uint32(4.0F), 0x40800000U);
    EXPECT_EQ(ser_float_to_uint32(785.066650390625F), 0x44444444U);
}

TEST(test_serialize, doubles_conversion)
{
    // Choose values that map unambigiously to binary floating point to avoid
    // rounding issues at the compiler side.
    EXPECT_EQ(ser_uint64_to_double(0x0000000000000000ULL), 0.0F);
    EXPECT_EQ(ser_uint64_to_double(0x3fe0000000000000ULL), 0.5F);
    EXPECT_EQ(ser_uint64_to_double(0x3ff0000000000000ULL), 1.0F);
    EXPECT_EQ(ser_uint64_to_double(0x4000000000000000ULL), 2.0F);
    EXPECT_EQ(ser_uint64_to_double(0x4010000000000000ULL), 4.0F);
    EXPECT_EQ(ser_uint64_to_double(0x4088888880000000ULL), 785.066650390625F);

    EXPECT_EQ(ser_double_to_uint64(0.0), 0x0000000000000000ULL);
    EXPECT_EQ(ser_double_to_uint64(0.5), 0x3fe0000000000000ULL);
    EXPECT_EQ(ser_double_to_uint64(1.0), 0x3ff0000000000000ULL);
    EXPECT_EQ(ser_double_to_uint64(2.0), 0x4000000000000000ULL);
    EXPECT_EQ(ser_double_to_uint64(4.0), 0x4010000000000000ULL);
    EXPECT_EQ(ser_double_to_uint64(785.066650390625), 0x4088888880000000ULL);
}
/*
Python code to generate the below hashes:

    def reversed_hex(x):
        return binascii.hexlify(''.join(reversed(x)))
    def dsha256(x):
        return hashlib.sha256(hashlib.sha256(x).digest()).digest()

    reversed_hex(dsha256(''.join(struct.pack('<f', x) for x in range(0,1000)))) == '8e8b4cf3e4df8b332057e3e23af42ebc663b61e0495d5e7e32d85099d7f3fe0c'
    reversed_hex(dsha256(''.join(struct.pack('<d', x) for x in range(0,1000)))) == '43d0c82591953c4eafe114590d392676a01585d25b25d433557f0d7878b23f96'
*/
TEST(test_serialize, floats)
{
    CDataStream ss(SER_DISK, 0);
    // encode
    for (int i = 0; i < 1000; i++) {
        ss << float(i);
    }
    EXPECT_EQ(Hash(ss.begin(), ss.end()) , uint256S("8e8b4cf3e4df8b332057e3e23af42ebc663b61e0495d5e7e32d85099d7f3fe0c"));

    // decode
    for (int i = 0; i < 1000; i++) {
        float j;
        ss >> j;
        EXPECT_EQ(i , j)<< "decoded:" << j << " expected:" << i;
    }
}

TEST(test_serialize, doubles)
{
    CDataStream ss(SER_DISK, 0);
    // encode
    for (int i = 0; i < 1000; i++) {
        ss << double(i);
    }
    EXPECT_EQ(Hash(ss.begin(), ss.end()) , uint256S("43d0c82591953c4eafe114590d392676a01585d25b25d433557f0d7878b23f96"));

    // decode
    for (int i = 0; i < 1000; i++) {
        double j;
        ss >> j;
        EXPECT_EQ(i , j)<< "decoded:" << j << " expected:" << i;
    }
}

TEST(test_serialize, varints)
{
    // encode

    CDataStream ss(SER_DISK, 0);
    CDataStream::size_type size = 0;
    for (int i = 0; i < 100000; i++) {
        ss << VARINT(i);
        size += ::GetSerializeSize(VARINT(i), 0, 0);
        EXPECT_EQ(size , ss.size());
    }

    for (uint64_t i = 0;  i < 100000000000ULL; i += 999999937) {
        ss << VARINT(i);
        size += ::GetSerializeSize(VARINT(i), 0, 0);
        EXPECT_EQ(size , ss.size());
    }

    // decode
    for (int i = 0; i < 100000; i++) {
        int j = -1;
        ss >> VARINT(j);
        EXPECT_EQ(i , j)<< "decoded:" << j << " expected:" << i;
    }

    for (uint64_t i = 0;  i < 100000000000ULL; i += 999999937) {
        uint64_t j = -1;
        ss >> VARINT(j);
        EXPECT_EQ(i , j)<< "decoded:" << j << " expected:" << i;
    }
}

TEST(test_serialize, compactsize)
{
    CDataStream ss(SER_DISK, 0);
    vector<char>::size_type i, j;

    for (i = 1; i <= MAX_DATA_SIZE; i *= 2)
    {
        WriteCompactSize(ss, i-1);
        WriteCompactSize(ss, i);
    }
    for (i = 1; i <= MAX_DATA_SIZE; i *= 2)
    {
        j = ReadCompactSize(ss);
        EXPECT_EQ((i-1) , j)<< "decoded:" << j << " expected:" << (i-1);
        j = ReadCompactSize(ss);
        EXPECT_EQ(i , j)<< "decoded:" << j << " expected:" << i;
    }
}

static bool isCanonicalException(const ios_base::failure& ex)
{
    ios_base::failure expectedException("non-canonical ReadCompactSize()");

    // The string returned by what() can be different for different platforms.
    // Instead of directly comparing the ex.what() with an expected string,
    // create an instance of exception to see if ex.what() matches 
    // the expected explanatory string returned by the exception instance. 
    return strcmp(expectedException.what(), ex.what()) == 0;
}


TEST(test_serialize, noncanonical)
{
    // Write some non-canonical CompactSize encodings, and
    // make sure an exception is thrown when read back.
    CDataStream ss(SER_DISK, 0);
    vector<char>::size_type n;

    // zero encoded with three bytes:
    ss.write("\xfd\x00\x00", 3);
    try {
        ReadCompactSize(ss);
    } catch (ios_base::failure& ex) {
      EXPECT_TRUE(isCanonicalException(ex));
    }

    // 0xfc encoded with three bytes:
    ss.write("\xfd\xfc\x00", 3);
    try {
        ReadCompactSize(ss);
    } catch (ios_base::failure& ex) {
      EXPECT_TRUE(isCanonicalException(ex));
    }

    // 0xfd encoded with three bytes is OK:
    ss.write("\xfd\xfd\x00", 3);
    n = ReadCompactSize(ss);
    EXPECT_EQ(n, 0xfdU);

    // zero encoded with five bytes:
    ss.write("\xfe\x00\x00\x00\x00", 5);
    try {
        ReadCompactSize(ss);
    } catch (ios_base::failure& ex) {
      EXPECT_TRUE(isCanonicalException(ex));
    }

    // 0xffff encoded with five bytes:
    ss.write("\xfe\xff\xff\x00\x00", 5);
    try {
        ReadCompactSize(ss);
    } catch (ios_base::failure& ex) {
      EXPECT_TRUE(isCanonicalException(ex));
    }

    // zero encoded with nine bytes:
    ss.write("\xff\x00\x00\x00\x00\x00\x00\x00\x00", 9);
    try {
        ReadCompactSize(ss);
    } catch (ios_base::failure& ex) {
      EXPECT_TRUE(isCanonicalException(ex));
    }

    // 0x01ffffff encoded with nine bytes:
    ss.write("\xff\xff\xff\xff\x01\x00\x00\x00\x00", 9);
    try {
        ReadCompactSize(ss);
    } catch (ios_base::failure& ex) {
      EXPECT_TRUE(isCanonicalException(ex));
    }
}

TEST(test_serialize, insert_delete)
{
    // Test inserting/deleting bytes.
    CDataStream ss(SER_DISK, 0);
    EXPECT_EQ(ss.size(), 0U);

    ss.write("\x00\x01\x02\xff", 4);
    EXPECT_EQ(ss.size(), 4U);

    char c = (char)11;

    // Inserting at beginning/end/middle:
    ss.insert(ss.begin(), c);
    EXPECT_EQ(ss.size(), 5U);
    EXPECT_EQ(ss[0], c);
    EXPECT_EQ(ss[1], 0);

    ss.insert(ss.end(), c);
    EXPECT_EQ(ss.size(), 6U);
    EXPECT_EQ(ss[4], (char)0xff);
    EXPECT_EQ(ss[5], c);

    ss.insert(ss.begin()+2, c);
    EXPECT_EQ(ss.size(), 7U);
    EXPECT_EQ(ss[2], c);

    // Delete at beginning/end/middle
    ss.erase(ss.begin());
    EXPECT_EQ(ss.size(), 6U);
    EXPECT_EQ(ss[0], 0);

    ss.erase(ss.begin()+ss.size()-1);
    EXPECT_EQ(ss.size(), 5U);
    EXPECT_EQ(ss[4], (char)0xff);

    ss.erase(ss.begin()+1);
    EXPECT_EQ(ss.size(), 4U);
    EXPECT_EQ(ss[0], 0);
    EXPECT_EQ(ss[1], 1);
    EXPECT_EQ(ss[2], 2);
    EXPECT_EQ(ss[3], (char)0xff);

    // Make sure GetAndClear does the right thing:
    CSerializeData d;
    ss.GetAndClear(d);
    EXPECT_EQ(ss.size(), 0U);
}

TEST(test_serialize, class_methods)
{
    int intval(100);
    bool boolval(true);
    string stringval("testing");
    const char* charstrval("testing charstr");
    CMutableTransaction txval;
    CSerializeMethodsTestSingle methodtest1(intval, boolval, stringval, charstrval, txval);
    CSerializeMethodsTestMany methodtest2(intval, boolval, stringval, charstrval, txval);
    CSerializeMethodsTestSingle methodtest3;
    CSerializeMethodsTestMany methodtest4;
    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    // EXPECT_EQ needs to rewrite operator==
    EXPECT_EQ(methodtest1, methodtest2);
    ss << methodtest1;
    ss >> methodtest4;
    ss << methodtest2;
    ss >> methodtest3;
    EXPECT_EQ(methodtest1, methodtest2);
    EXPECT_EQ(methodtest2, methodtest3);
    EXPECT_EQ(methodtest3, methodtest4);

    CDataStream ss2(SER_DISK, PROTOCOL_VERSION, intval, boolval, stringval, FLATDATA(charstrval), txval);
    ss2 >> methodtest3;
    EXPECT_EQ(methodtest3, methodtest4);
}

// protected serialization
TEST(test_serialize, protected_data_type)
{
    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    ss << PROTECTED_DATA_TYPE::MAP;
    EXPECT_EQ(2U, ss.size());

    EXPECT_NO_THROW(ReadProtectedSerializeMarker(ss, PROTECTED_DATA_TYPE::MAP));

    ss << PROTECTED_DATA_TYPE::MAP;
    EXPECT_THROW(ReadProtectedSerializeMarker(ss, PROTECTED_DATA_TYPE::LIST), ios_base::failure);

    ss.clear();
    EXPECT_THROW(ReadProtectedSerializeMarker(ss, PROTECTED_DATA_TYPE::LIST), ios_base::failure);

    ss.clear();
    ss << uint8_t(1) << to_integral_type(PROTECTED_DATA_TYPE::LIST);
    EXPECT_THROW(ReadProtectedSerializeMarker(ss, PROTECTED_DATA_TYPE::LIST), ios_base::failure);

    ss.clear();
    ss << PROTECTED_SERIALIZE_MARKER;
    EXPECT_THROW(ReadProtectedSerializeMarker(ss, PROTECTED_DATA_TYPE::LIST), ios_base::failure);

    ss.clear();
    ss << PROTECTED_DATA_TYPE::LIST;
    WriteCompactSize(ss, 12345);
    EXPECT_NO_THROW(ReadProtectedSerializeMarker(ss, PROTECTED_DATA_TYPE::LIST));
    const size_t nSize = ReadCompactSize(ss);
    EXPECT_EQ(12345U, nSize);
}

class CPSerObjV1
{
public:
    string str1;
    uint64_t num1;

    CPSerObjV1() : num1(0) {}
    CPSerObjV1(const string& s, uint64_t n) :
        str1(s), num1(n)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(str1);
        READWRITE(num1);
    }
};

class CPSerObjV2 : public CPSerObjV1
{
public:
    string str2;
    uint64_t num2;

    CPSerObjV2() : num2(0) {}
    CPSerObjV2(const string& s1, uint64_t n1, const string& s2, uint64_t n2) :
        CPSerObjV1(s1, n1),
        str2(s2), num2(n2)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(str1);
        READWRITE(num1);
        READWRITE(str2);
        READWRITE(num2);
    }
};

class CProtectedSerializationTest
{
public:
    string str1;
    uint64_t num1;
    map<string, CPSerObjV1> map1;
    string str2;
    list<CPSerObjV1> list1;
    uint64_t num2;
    unordered_map<string, CPSerObjV1> map2;
    string str3;
    set<string> set1;
    uint64_t num3;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(str1);
		READWRITE(num1);
		READWRITE_PROTECTED(map1);
		READWRITE(str2);
        READWRITE_PROTECTED(list1);
		READWRITE(num2);
        READWRITE_PROTECTED(map2);
        READWRITE(str3);
        READWRITE_PROTECTED(set1);
        READWRITE(num3);
    }
};

TEST(test_serialize, protected_serialization)
{
    CProtectedSerializationTest objWrite, objRead;
    objWrite.str1 = "str1";
    objWrite.num1 = 100123;
    objWrite.map1["key1"] = CPSerObjV1("value1_1", 11);
    objWrite.map1["key2"] = CPSerObjV2("value2_1", 21, "value_2_2", 22);
    objWrite.map1["key3"] = CPSerObjV1("value3_1", 31);
    objWrite.str2 = "str2";
    objWrite.list1.push_back(CPSerObjV1("value4_1", 41));
    objWrite.list1.push_back(CPSerObjV2("value5_1", 51, "value5_2", 52));
    objWrite.list1.push_back(CPSerObjV1("value6_1", 61));
    objWrite.num2 = 200123;
    objWrite.map2["key4"] = CPSerObjV1("value7_1", 71);
    objWrite.map2["key5"] = CPSerObjV2("value8_1", 81, "value8_2", 82);
    objWrite.map2["key6"] = CPSerObjV1("value9_1", 91);
    objWrite.str3 = "str3";
    objWrite.set1.insert("value10_1");
    objWrite.set1.insert("value10_2");
    objWrite.set1.insert("value10_3");
    objWrite.num3 = 300123;

    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    ss << objWrite;
    ss >> objRead;

    EXPECT_EQ(objWrite.str1, objRead.str1);
    EXPECT_EQ(objWrite.num1, objRead.num1);
    EXPECT_EQ(objWrite.map1.size(), objRead.map1.size());

    auto s1 = objWrite.map1["key1"];
    auto s2 = objRead.map1["key1"];
    EXPECT_EQ(s1.str1, s2.str1);
    EXPECT_EQ(s1.num1, s2.num1);

    s1 = objWrite.map1["key2"];
    s2 = objRead.map1["key2"];
    EXPECT_EQ(s1.str1, s2.str1);
    EXPECT_EQ(s1.num1, s2.num1);

    s1 = objWrite.map1["key3"];
    s2 = objRead.map1["key3"];
    EXPECT_EQ(s1.str1, s2.str1);
    EXPECT_EQ(s1.num1, s2.num1);

    EXPECT_EQ(objWrite.str2, objRead.str2);
    EXPECT_EQ(objWrite.list1.size(), objRead.list1.size());
    s1 = objWrite.list1.front();
    s2 = objRead.list1.front();
    EXPECT_EQ(s1.str1, s2.str1);
    EXPECT_EQ(s1.num1, s2.num1);
    objWrite.list1.pop_front();
    objRead.list1.pop_front();
    s1 = objWrite.list1.front();
    s2 = objRead.list1.front();
    EXPECT_EQ(s1.str1, s2.str1);
    EXPECT_EQ(s1.num1, s2.num1);
    objWrite.list1.pop_front();
    objRead.list1.pop_front();
    s1 = objWrite.list1.front();
    s2 = objRead.list1.front();
    EXPECT_EQ(s1.str1, s2.str1);
    EXPECT_EQ(s1.num1, s2.num1);
    EXPECT_EQ(objWrite.num2, objRead.num2);

    EXPECT_EQ(objWrite.map2.size(), objRead.map2.size());
    s1 = objWrite.map2["key4"];
    s2 = objRead.map2["key4"];
    EXPECT_EQ(s1.str1, s2.str1);
    EXPECT_EQ(s1.num1, s2.num1);

    s1 = objWrite.map2["key5"];
    s2 = objRead.map2["key5"];
    EXPECT_EQ(s1.str1, s2.str1);
    EXPECT_EQ(s1.num1, s2.num1);

    s1 = objWrite.map2["key6"];
    s2 = objRead.map2["key6"];
    EXPECT_EQ(s1.str1, s2.str1);
    EXPECT_EQ(s1.num1, s2.num1);
        
    EXPECT_EQ(objWrite.str3, objRead.str3);
    EXPECT_EQ(objWrite.set1.size(), objRead.set1.size());
    auto it1 = objWrite.set1.begin();
    auto it2 = objRead.set1.begin();
    for (; it1 != objWrite.set1.end(); ++it1, ++it2)
    {
		EXPECT_EQ(*it1, *it2);
	}

    EXPECT_EQ(objWrite.num3, objRead.num3);
}
