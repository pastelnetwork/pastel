// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2021-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <cstdint>
#include <sstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include <string>

#include <gmock/gmock.h>

#include <utils/vector_types.h>
#include <utils/uint256.h>
#include <arith_uint256.h>
#include <version.h>

using namespace testing;
using namespace std;

/// Convert vector to arith_uint256, via uint256 blob
inline arith_uint256 arith_uint256V(const v_uint8& vch)
{
    return UintToArith256(uint256(vch));
}

constexpr unsigned char R1Array[] =
    "\x9c\x52\x4a\xdb\xcf\x56\x11\x12\x2b\x29\x12\x5e\x5d\x35\xd2\xd2"
    "\x22\x81\xaa\xb5\x33\xf0\x08\x32\xd5\x56\xb1\xf9\xea\xe5\x1d\x7d";
constexpr char R1ArrayHex[] = "7D1DE5EAF9B156D53208F033B5AA8122D2d2355d5e12292b121156cfdb4a529c";
constexpr double R1Ldouble = 0.4887374590559308955; // R1L equals roughly R1Ldouble * 2^256
const arith_uint256 R1L = arith_uint256V(v_uint8(R1Array,R1Array+32));
constexpr uint64_t R1LLow64 = 0x121156cfdb4a529cULL;

constexpr unsigned char R2Array[] =
    "\x70\x32\x1d\x7c\x47\xa5\x6b\x40\x26\x7e\x0a\xc3\xa6\x9c\xb6\xbf"
    "\x13\x30\x47\xa3\x19\x2d\xda\x71\x49\x13\x72\xf0\xb4\xca\x81\xd7";
const arith_uint256 R2L = arith_uint256V(v_uint8(R2Array,R2Array+32));

constexpr char R1LplusR2L[] = "549FB09FEA236A1EA3E31D4D58F1B1369288D204211CA751527CFC175767850C";

constexpr unsigned char ZeroArray[] =
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
const arith_uint256 ZeroL = arith_uint256V(v_uint8(ZeroArray,ZeroArray+32));

constexpr unsigned char OneArray[] =
    "\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
const arith_uint256 OneL = arith_uint256V(v_uint8(OneArray,OneArray+32));

constexpr unsigned char MaxArray[] =
    "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
    "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
const arith_uint256 MaxL = arith_uint256V(v_uint8(MaxArray,MaxArray+32));

const arith_uint256 HalfL = (OneL << 255);
string ArrToStr(const unsigned char A[], unsigned int width)
{
    stringstream Stream;
    Stream << hex;
    for (unsigned int i = 0; i < width; ++i)
    {
        Stream<<setw(2)<<setfill('0')<<(unsigned int)A[width-i-1];
    }
    return Stream.str();
}

TEST(arith_uint256_tests, basic)
{
    EXPECT_EQ(1 , 0+1);
    // constructor arith_uint256(vector<char>):
    EXPECT_EQ(R1L.ToString() , ArrToStr(R1Array,32));
    EXPECT_EQ(R2L.ToString() , ArrToStr(R2Array,32));
    EXPECT_EQ(ZeroL.ToString() , ArrToStr(ZeroArray,32));
    EXPECT_EQ(OneL.ToString() , ArrToStr(OneArray,32));
    EXPECT_EQ(MaxL.ToString() , ArrToStr(MaxArray,32));
    EXPECT_NE(OneL.ToString() , ArrToStr(ZeroArray,32));

    // == and !=
    EXPECT_NE(R1L , R2L);
    EXPECT_NE(ZeroL , OneL);
    EXPECT_NE(OneL , ZeroL);
    EXPECT_NE(MaxL , ZeroL);
    EXPECT_EQ(~MaxL , ZeroL);
    EXPECT_EQ( ((R1L ^ R2L) ^ R1L) , R2L);

    uint64_t Tmp64 = 0xc4dab720d9c7acaaULL;
    for (unsigned int i = 0; i < 256; ++i)
    {
        EXPECT_NE(ZeroL , (OneL << i));
        EXPECT_NE((OneL << i) , ZeroL);
        EXPECT_NE(R1L , (R1L ^ (OneL << i)));
        EXPECT_NE((arith_uint256(Tmp64) ^ (OneL << i) ) , Tmp64);
    }
    EXPECT_EQ(ZeroL , (OneL << 256));

    // String Constructor and Copy Constructor
    EXPECT_EQ(arith_uint256("0x"+R1L.ToString()) , R1L);
    EXPECT_EQ(arith_uint256("0x"+R2L.ToString()) , R2L);
    EXPECT_EQ(arith_uint256("0x"+ZeroL.ToString()) , ZeroL);
    EXPECT_EQ(arith_uint256("0x"+OneL.ToString()) , OneL);
    EXPECT_EQ(arith_uint256("0x"+MaxL.ToString()) , MaxL);
    EXPECT_EQ(arith_uint256(R1L.ToString()) , R1L);
    EXPECT_EQ(arith_uint256("   0x"+R1L.ToString()+"   ") , R1L);
    EXPECT_EQ(arith_uint256("") , ZeroL);
    EXPECT_EQ(R1L , arith_uint256(R1ArrayHex));
    EXPECT_EQ(arith_uint256(R1L) , R1L);
    EXPECT_EQ((arith_uint256(R1L^R2L)^R2L) , R1L);
    EXPECT_EQ(arith_uint256(ZeroL) , ZeroL);
    EXPECT_EQ(arith_uint256(OneL) , OneL);

    // uint64_t constructor
    EXPECT_EQ( (R1L & arith_uint256("0xffffffffffffffff")) , arith_uint256(R1LLow64));
    EXPECT_EQ(ZeroL , arith_uint256(0));
    EXPECT_EQ(OneL , arith_uint256(1));
    EXPECT_EQ((arith_uint256("0xffffffffffffffff") = arith_uint256(0xffffffffffffffffULL)) , arith_uint256(0xffffffffffffffffULL) );

    // Assignment (from base_uint)
    arith_uint256 tmpL = ~ZeroL; EXPECT_EQ(tmpL , ~ZeroL);
    tmpL = ~OneL; EXPECT_EQ(tmpL , ~OneL);
    tmpL = ~R1L; EXPECT_EQ(tmpL , ~R1L);
    tmpL = ~R2L; EXPECT_EQ(tmpL , ~R2L);
    tmpL = ~MaxL; EXPECT_EQ(tmpL , ~MaxL);

}

void shiftArrayRight(unsigned char* to, const unsigned char* from, unsigned int arrayLength, unsigned int bitsToShift)
{
    for (unsigned int T=0; T < arrayLength; ++T)
    {
        unsigned int F = (T+bitsToShift/8);
        if (F < arrayLength)
            to[T]  = from[F] >> (bitsToShift%8);
        else
            to[T] = 0;
        if (F + 1 < arrayLength)
            to[T] |= from[(F+1)] << (8-bitsToShift%8);
    }
}

void shiftArrayLeft(unsigned char* to, const unsigned char* from, unsigned int arrayLength, unsigned int bitsToShift)
{
    for (unsigned int T=0; T < arrayLength; ++T)
    {
        if (T >= bitsToShift/8)
        {
            unsigned int F = T-bitsToShift/8;
            to[T]  = from[F] << (bitsToShift%8);
            if (T >= bitsToShift/8+1)
                to[T] |= from[F-1] >> (8-bitsToShift%8);
        }
        else {
            to[T] = 0;
        }
    }
}

TEST(arith_uint256_tests, shifts) // "<<"  ">>"  "<<="  ">>="
{
    unsigned char TmpArray[32];
    arith_uint256 TmpL;
    for (unsigned int i = 0; i < 256; ++i)
    {
        shiftArrayLeft(TmpArray, OneArray, 32, i);
        EXPECT_EQ(arith_uint256V(v_uint8(TmpArray,TmpArray+32)) , (OneL << i));
        TmpL = OneL; TmpL <<= i;
        EXPECT_EQ(TmpL , (OneL << i));
        EXPECT_EQ((HalfL >> (255-i)) , (OneL << i));
        TmpL = HalfL; TmpL >>= (255-i);
        EXPECT_EQ(TmpL , (OneL << i));

        shiftArrayLeft(TmpArray, R1Array, 32, i);
        EXPECT_EQ(arith_uint256V(v_uint8(TmpArray,TmpArray+32)) , (R1L << i));
        TmpL = R1L; TmpL <<= i;
        EXPECT_EQ(TmpL , (R1L << i));

        shiftArrayRight(TmpArray, R1Array, 32, i);
        EXPECT_EQ(arith_uint256V(v_uint8(TmpArray,TmpArray+32)) , (R1L >> i));
        TmpL = R1L; TmpL >>= i;
        EXPECT_EQ(TmpL , (R1L >> i));

        shiftArrayLeft(TmpArray, MaxArray, 32, i);
        EXPECT_EQ(arith_uint256V(v_uint8(TmpArray,TmpArray+32)) , (MaxL << i));
        TmpL = MaxL; TmpL <<= i;
        EXPECT_EQ(TmpL , (MaxL << i));

        shiftArrayRight(TmpArray, MaxArray, 32, i);
        EXPECT_EQ(arith_uint256V(v_uint8(TmpArray,TmpArray+32)) , (MaxL >> i));
        TmpL = MaxL; TmpL >>= i;
        EXPECT_EQ(TmpL , (MaxL >> i));
    }
    arith_uint256 c1L = arith_uint256(0x0123456789abcdefULL);
    arith_uint256 c2L = c1L << 128;
    for (unsigned int i = 0; i < 128; ++i) {
        EXPECT_EQ((c1L << i) , (c2L >> (128-i)));
    }
    for (unsigned int i = 128; i < 256; ++i) {
        EXPECT_EQ((c1L << i) , (c2L << (i-128)));
    }
}

TEST(arith_uint256_tests, unaryOperators) // !    ~    -
{
    EXPECT_TRUE(!ZeroL);
    EXPECT_TRUE(!(!OneL));
    for (unsigned int i = 0; i < 256; ++i)
        EXPECT_TRUE(!(!(OneL<<i)));
    EXPECT_TRUE(!(!R1L));
    EXPECT_TRUE(!(!MaxL));

    EXPECT_EQ(~ZeroL , MaxL);

    unsigned char TmpArray[32];
    for (unsigned int i = 0; i < 32; ++i) { TmpArray[i] = ~R1Array[i]; }
    EXPECT_EQ(arith_uint256V(v_uint8(TmpArray,TmpArray+32)) , (~R1L));

    EXPECT_EQ(-ZeroL , ZeroL);
    EXPECT_EQ(-R1L , (~R1L)+1);
    for (unsigned int i = 0; i < 256; ++i)
        EXPECT_EQ(-(OneL<<i) , (MaxL << i));
}

// Check if doing _A_ _OP_ _B_ results in the same as applying _OP_ onto each
// element of Aarray and Barray, and then converting the result into a arith_uint256.
#define CHECKBITWISEOPERATOR(_A_,_B_,_OP_)                              \
    for (unsigned int i = 0; i < 32; ++i) { TmpArray[i] = _A_##Array[i] _OP_ _B_##Array[i]; } \
    EXPECT_EQ(arith_uint256V(v_uint8(TmpArray,TmpArray+32)) , (_A_##L _OP_ _B_##L));

#define CHECKASSIGNMENTOPERATOR(_A_,_B_,_OP_)                           \
    TmpL = _A_##L; TmpL _OP_##= _B_##L; EXPECT_EQ(TmpL , (_A_##L _OP_ _B_##L));

TEST(arith_uint256_tests, bitwiseOperators)
{
    unsigned char TmpArray[32];

    CHECKBITWISEOPERATOR(R1,R2,|)
    CHECKBITWISEOPERATOR(R1,R2,^)
    CHECKBITWISEOPERATOR(R1,R2,&)
    CHECKBITWISEOPERATOR(R1,Zero,|)
    CHECKBITWISEOPERATOR(R1,Zero,^)
    CHECKBITWISEOPERATOR(R1,Zero,&)
    CHECKBITWISEOPERATOR(R1,Max,|)
    CHECKBITWISEOPERATOR(R1,Max,^)
    CHECKBITWISEOPERATOR(R1,Max,&)
    CHECKBITWISEOPERATOR(Zero,R1,|)
    CHECKBITWISEOPERATOR(Zero,R1,^)
    CHECKBITWISEOPERATOR(Zero,R1,&)
    CHECKBITWISEOPERATOR(Max,R1,|)
    CHECKBITWISEOPERATOR(Max,R1,^)
    CHECKBITWISEOPERATOR(Max,R1,&)

    arith_uint256 TmpL;
    CHECKASSIGNMENTOPERATOR(R1,R2,|)
    CHECKASSIGNMENTOPERATOR(R1,R2,^)
    CHECKASSIGNMENTOPERATOR(R1,R2,&)
    CHECKASSIGNMENTOPERATOR(R1,Zero,|)
    CHECKASSIGNMENTOPERATOR(R1,Zero,^)
    CHECKASSIGNMENTOPERATOR(R1,Zero,&)
    CHECKASSIGNMENTOPERATOR(R1,Max,|)
    CHECKASSIGNMENTOPERATOR(R1,Max,^)
    CHECKASSIGNMENTOPERATOR(R1,Max,&)
    CHECKASSIGNMENTOPERATOR(Zero,R1,|)
    CHECKASSIGNMENTOPERATOR(Zero,R1,^)
    CHECKASSIGNMENTOPERATOR(Zero,R1,&)
    CHECKASSIGNMENTOPERATOR(Max,R1,|)
    CHECKASSIGNMENTOPERATOR(Max,R1,^)
    CHECKASSIGNMENTOPERATOR(Max,R1,&)

    uint64_t Tmp64 = 0xe1db685c9a0b47a2ULL;
    TmpL = R1L; TmpL |= Tmp64;  EXPECT_EQ(TmpL , (R1L | arith_uint256(Tmp64)));
    TmpL = R1L; TmpL |= 0; EXPECT_EQ(TmpL , R1L);
    TmpL ^= 0; EXPECT_EQ(TmpL , R1L);
    TmpL ^= Tmp64;  EXPECT_EQ(TmpL , (R1L ^ arith_uint256(Tmp64)));
}

TEST(arith_uint256_tests, comparison) // <= >= < >
{
    arith_uint256 TmpL;
    for (unsigned int i = 0; i < 256; ++i) {
        TmpL= OneL<< i;
        EXPECT_TRUE( TmpL >= ZeroL && TmpL > ZeroL && ZeroL < TmpL && ZeroL <= TmpL); //-V501
        EXPECT_TRUE( TmpL >= 0 && TmpL > 0 && 0 < TmpL && 0 <= TmpL); //-V501
        TmpL |= R1L;
        EXPECT_TRUE( TmpL >= R1L ); EXPECT_NE( (TmpL == R1L) , (TmpL > R1L)); EXPECT_TRUE( (TmpL == R1L) || !( TmpL <= R1L));
        EXPECT_TRUE( R1L <= TmpL ); EXPECT_NE( (R1L == TmpL) , (R1L < TmpL)); EXPECT_TRUE( (TmpL == R1L) || !( R1L >= TmpL));
        EXPECT_TRUE(! (TmpL < R1L)); EXPECT_TRUE(! (R1L > TmpL));
    }
}

TEST(arith_uint256_tests, plusMinus)
{
    arith_uint256 TmpL = 0;
    EXPECT_EQ(R1L+R2L , arith_uint256(R1LplusR2L));
    TmpL += R1L;
    EXPECT_EQ(TmpL , R1L);
    TmpL += R2L;
    EXPECT_EQ(TmpL , R1L + R2L);
    EXPECT_EQ(OneL+MaxL , ZeroL);
    EXPECT_EQ(MaxL+OneL , ZeroL);
    for (unsigned int i = 1; i < 256; ++i) {
        EXPECT_EQ( (MaxL >> i) + OneL , (HalfL >> (i-1)) );
        EXPECT_EQ( OneL + (MaxL >> i) , (HalfL >> (i-1)) );
        TmpL = (MaxL>>i); TmpL += OneL;
        EXPECT_EQ( TmpL , (HalfL >> (i-1)) );
        TmpL = (MaxL>>i); TmpL += 1;
        EXPECT_EQ( TmpL , (HalfL >> (i-1)) );
        TmpL = (MaxL>>i);
        EXPECT_EQ( TmpL++ , (MaxL>>i) );
        EXPECT_EQ( TmpL , (HalfL >> (i-1)));
    }
    EXPECT_EQ(arith_uint256(0xbedc77e27940a7ULL) + 0xee8d836fce66fbULL , arith_uint256(0xbedc77e27940a7ULL + 0xee8d836fce66fbULL));
    TmpL = arith_uint256(0xbedc77e27940a7ULL); TmpL += 0xee8d836fce66fbULL;
    EXPECT_EQ(TmpL , arith_uint256(0xbedc77e27940a7ULL+0xee8d836fce66fbULL));
    TmpL -= 0xee8d836fce66fbULL;  EXPECT_EQ(TmpL , 0xbedc77e27940a7ULL);
    TmpL = R1L;
    EXPECT_EQ(++TmpL , R1L+1);

    EXPECT_EQ(R1L -(-R2L) , R1L+R2L);
    EXPECT_EQ(R1L -(-OneL) , R1L+OneL);
    EXPECT_EQ(R1L - OneL , R1L+(-OneL));
    for (unsigned int i = 1; i < 256; ++i) {
        EXPECT_EQ((MaxL>>i) - (-OneL)  , (HalfL >> (i-1)));
        EXPECT_EQ((HalfL >> (i-1)) - OneL , (MaxL>>i));
        TmpL = (HalfL >> (i-1));
        EXPECT_EQ(TmpL-- , (HalfL >> (i-1)));
        EXPECT_EQ(TmpL , (MaxL >> i));
        TmpL = (HalfL >> (i-1));
        EXPECT_EQ(--TmpL , (MaxL >> i));
    }
    TmpL = R1L;
    EXPECT_EQ(--TmpL , R1L-1);
}

TEST(arith_uint256_tests, multiply)
{
    EXPECT_EQ((R1L * R1L).ToString() , "62a38c0486f01e45879d7910a7761bf30d5237e9873f9bff3642a732c4d84f10");
    EXPECT_EQ((R1L * R2L).ToString() , "de37805e9986996cfba76ff6ba51c008df851987d9dd323f0e5de07760529c40");
    EXPECT_EQ((R1L * ZeroL) , ZeroL);
    EXPECT_EQ((R1L * OneL) , R1L);
    EXPECT_EQ((R1L * MaxL) , -R1L);
    EXPECT_EQ((R2L * R1L) , (R1L * R2L));
    EXPECT_EQ((R2L * R2L).ToString() , "ac8c010096767d3cae5005dec28bb2b45a1d85ab7996ccd3e102a650f74ff100");
    EXPECT_EQ((R2L * ZeroL) , ZeroL);
    EXPECT_EQ((R2L * OneL) , R2L);
    EXPECT_EQ((R2L * MaxL) , -R2L);

    EXPECT_EQ(MaxL * MaxL , OneL);

    EXPECT_EQ((R1L * 0) , 0);
    EXPECT_EQ((R1L * 1) , R1L);
    EXPECT_EQ((R1L * 3).ToString() , "7759b1c0ed14047f961ad09b20ff83687876a0181a367b813634046f91def7d4");
    EXPECT_EQ((R2L * 0x87654321UL).ToString() , "23f7816e30c4ae2017257b7a0fa64d60402f5234d46e746b61c960d09a26d070");
}

TEST(arith_uint256_tests, divide)
{
    arith_uint256 D1L("AD7133AC1977FA2B7");
    arith_uint256 D2L("ECD751716");
    EXPECT_EQ((R1L / D1L).ToString() , "00000000000000000b8ac01106981635d9ed112290f8895545a7654dde28fb3a");
    EXPECT_EQ((R1L / D2L).ToString() , "000000000873ce8efec5b67150bad3aa8c5fcb70e947586153bf2cec7c37c57a");
    EXPECT_EQ(R1L / OneL , R1L);
    EXPECT_EQ(R1L / MaxL , ZeroL);
    EXPECT_EQ(MaxL / R1L , 2);
    EXPECT_THROW(R1L / ZeroL, uint_error);
    EXPECT_EQ((R2L / D1L).ToString() , "000000000000000013e1665895a1cc981de6d93670105a6b3ec3b73141b3a3c5");
    EXPECT_EQ((R2L / D2L).ToString() , "000000000e8f0abe753bb0afe2e9437ee85d280be60882cf0bd1aaf7fa3cc2c4");
    EXPECT_EQ(R2L / OneL , R2L);
    EXPECT_EQ(R2L / MaxL , ZeroL);
    EXPECT_EQ(MaxL / R2L , 1);
    EXPECT_THROW(R2L / ZeroL, uint_error);
}


bool almostEqual(double d1, double d2)
{
    return fabs(d1-d2) <= 4*fabs(d1)*numeric_limits<double>::epsilon();
}

TEST(arith_uint256_tests, methods) // GetHex SetHex size() GetLow64 GetSerializeSize, Serialize, Unserialize
{
    EXPECT_EQ(R1L.GetHex() , R1L.ToString());
    EXPECT_EQ(R2L.GetHex() , R2L.ToString());
    EXPECT_EQ(OneL.GetHex() , OneL.ToString());
    EXPECT_EQ(MaxL.GetHex() , MaxL.ToString());
    arith_uint256 TmpL(R1L);
    EXPECT_EQ(TmpL , R1L);
    TmpL.SetHex(R2L.ToString());   EXPECT_EQ(TmpL , R2L);
    TmpL.SetHex(ZeroL.ToString()); EXPECT_EQ(TmpL , 0);
    TmpL.SetHex(HalfL.ToString()); EXPECT_EQ(TmpL , HalfL);

    TmpL.SetHex(R1L.ToString());
    EXPECT_EQ(R1L.size() , 32);
    EXPECT_EQ(R2L.size() , 32);
    EXPECT_EQ(ZeroL.size() , 32);
    EXPECT_EQ(MaxL.size() , 32);
    EXPECT_EQ(R1L.GetLow64()  , R1LLow64);
    EXPECT_EQ(HalfL.GetLow64() ,0x0000000000000000ULL);
    EXPECT_EQ(OneL.GetLow64() ,0x0000000000000001ULL);

    for (unsigned int i = 0; i < 255; ++i)
    {
        EXPECT_EQ((OneL << i).getdouble() , ldexp(1.0,i));
    }
    EXPECT_EQ(ZeroL.getdouble() , 0.0);
    for (int i = 256; i > 53; --i)
        EXPECT_TRUE(almostEqual((R1L>>(256-i)).getdouble(), ldexp(R1Ldouble,i)));
    uint64_t R1L64part = (R1L>>192).GetLow64();
    for (int i = 53; i > 0; --i) // doubles can store all integers in {0,...,2^54-1} exactly
    {
        EXPECT_EQ((R1L>>(256-i)).getdouble() , (double)(R1L64part >> (64-i)));
    }
}

class PTest_arith_uint256: public TestWithParam<tuple<uint32_t, string, uint32_t, bool, bool>>
{};

TEST_P(PTest_arith_uint256, bignum_SetCompact) 
{
    const auto &setCompact = get<0>(GetParam());
    const auto &expectedHex = get<1>(GetParam());
    const auto &getCompact = get<2>(GetParam());
    const auto &expectedNeg = get<3>(GetParam());
    const auto &expectedOverflow = get<4>(GetParam());

    arith_uint256 num;
    bool fNegative;
    bool fOverflow;
    num.SetCompact(setCompact, &fNegative, &fOverflow);
    EXPECT_EQ(num.GetHex(), expectedHex);
    EXPECT_EQ(num.GetCompact(), getCompact);
    EXPECT_EQ(fNegative, expectedNeg);
    EXPECT_EQ(fOverflow, expectedOverflow);
}

INSTANTIATE_TEST_SUITE_P(bignum_SetCompact, PTest_arith_uint256, Values(
    make_tuple(0U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x00123456U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x01003456U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x02000056U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x03000000U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x04000000U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x00923456U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x01803456U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x02800056U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x03800000U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x04800000U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, false),
    make_tuple(0x01123456U, "0000000000000000000000000000000000000000000000000000000000000012", 0x01120000U, false, false)
));

class PTest_arith_uint256_80: public TestWithParam<tuple<uint32_t, string, uint32_t, bool, bool>>
{};

TEST_P(PTest_arith_uint256_80, bignum_SetCompact) 
{
    const auto &setCompact = get<0>(GetParam());
    const auto &expectedHex = get<1>(GetParam());
    const auto &getCompact = get<2>(GetParam());
    const auto &expectedNeg = get<3>(GetParam());
    const auto &expectedOverflow = get<4>(GetParam());

    arith_uint256 num;
    bool fNegative;
    bool fOverflow;
    // Make sure that we don't generate compacts with the 0x00800000 bit set
    num = 0x80;
    EXPECT_EQ(num.GetCompact(), 0x02008000U);
    num.SetCompact(setCompact, &fNegative, &fOverflow);
    EXPECT_EQ(num.GetHex(), expectedHex);
    EXPECT_EQ(num.GetCompact(expectedNeg), getCompact);
    EXPECT_EQ(fNegative, expectedNeg);
    EXPECT_EQ(fOverflow, expectedOverflow);
}
INSTANTIATE_TEST_SUITE_P(bignum_SetCompact_80, PTest_arith_uint256_80, Values(
    make_tuple(0x01fedcbaU, "000000000000000000000000000000000000000000000000000000000000007e", 0x01fe0000U, true, false),
    make_tuple(0x02123456U, "0000000000000000000000000000000000000000000000000000000000001234", 0x02123400U, false, false),
    make_tuple(0x03123456U, "0000000000000000000000000000000000000000000000000000000000123456", 0x03123456U, false, false),
    make_tuple(0x04123456U, "0000000000000000000000000000000000000000000000000000000012345600", 0x04123456U, false, false),
    make_tuple(0x04923456U, "0000000000000000000000000000000000000000000000000000000012345600", 0x04923456U, true, false),
    make_tuple(0x05009234U, "0000000000000000000000000000000000000000000000000000000092340000", 0x05009234U, false, false),
    make_tuple(0x20123456U, "1234560000000000000000000000000000000000000000000000000000000000", 0x20123456U, false, false),
    make_tuple(0xff123456U, "0000000000000000000000000000000000000000000000000000000000000000", 0U, false, true)
));

TEST(arith_uint256_tests, getmaxcoverage) // some more tests just to get 100% coverage
{
    // ~R1L give a base_uint<256>
    EXPECT_EQ((~~R1L >> 10) , (R1L >> 10));
    EXPECT_EQ((~~R1L << 10) , (R1L << 10));
    EXPECT_TRUE(!(~~R1L < R1L));
    EXPECT_TRUE(~~R1L <= R1L);
    EXPECT_TRUE(!(~~R1L > R1L));
    EXPECT_TRUE(~~R1L >= R1L);
    EXPECT_TRUE(!(R1L < ~~R1L));
    EXPECT_TRUE(R1L <= ~~R1L);
    EXPECT_TRUE(!(R1L > ~~R1L));
    EXPECT_TRUE(R1L >= ~~R1L);

    EXPECT_EQ(~~R1L + R2L , R1L + ~~R2L);
    EXPECT_EQ(~~R1L - R2L , R1L - ~~R2L);
    EXPECT_NE(~R1L , R1L); EXPECT_NE(R1L , ~R1L);
    unsigned char TmpArray[32];
    CHECKBITWISEOPERATOR(~R1,R2,|)
    CHECKBITWISEOPERATOR(~R1,R2,^)
    CHECKBITWISEOPERATOR(~R1,R2,&)
    CHECKBITWISEOPERATOR(R1,~R2,|)
    CHECKBITWISEOPERATOR(R1,~R2,^)
    CHECKBITWISEOPERATOR(R1,~R2,&)
}
