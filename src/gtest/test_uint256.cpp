// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "arith_uint256.h"
#include "uint256.h"
#include "version.h"
#include "test/test_bitcoin.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdint.h>
#include <sstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include <string>
#include <stdio.h>

constexpr unsigned char R1Array[] =
    "\x9c\x52\x4a\xdb\xcf\x56\x11\x12\x2b\x29\x12\x5e\x5d\x35\xd2\xd2"
    "\x22\x81\xaa\xb5\x33\xf0\x08\x32\xd5\x56\xb1\xf9\xea\xe5\x1d\x7d";
constexpr char R1ArrayHex[] = "7D1DE5EAF9B156D53208F033B5AA8122D2d2355d5e12292b121156cfdb4a529c";
const uint256 R1L = uint256(std::vector<unsigned char>(R1Array,R1Array+32));
const uint160 R1S = uint160(std::vector<unsigned char>(R1Array,R1Array+20));

constexpr unsigned char R2Array[] =
    "\x70\x32\x1d\x7c\x47\xa5\x6b\x40\x26\x7e\x0a\xc3\xa6\x9c\xb6\xbf"
    "\x13\x30\x47\xa3\x19\x2d\xda\x71\x49\x13\x72\xf0\xb4\xca\x81\xd7";
const uint256 R2L = uint256(std::vector<unsigned char>(R2Array,R2Array+32));
const uint160 R2S = uint160(std::vector<unsigned char>(R2Array,R2Array+20));

constexpr unsigned char ZeroArray[] =
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
const uint256 ZeroL = uint256(std::vector<unsigned char>(ZeroArray,ZeroArray+32));
const uint160 ZeroS = uint160(std::vector<unsigned char>(ZeroArray,ZeroArray+20));

constexpr unsigned char OneArray[] =
    "\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
const uint256 OneL = uint256(std::vector<unsigned char>(OneArray,OneArray+32));
const uint160 OneS = uint160(std::vector<unsigned char>(OneArray,OneArray+20));

constexpr unsigned char MaxArray[] =
    "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
    "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
const uint256 MaxL = uint256(std::vector<unsigned char>(MaxArray,MaxArray+32));
const uint160 MaxS = uint160(std::vector<unsigned char>(MaxArray,MaxArray+20));

std::string ArrayToString(const unsigned char A[], unsigned int width)
{
    std::stringstream Stream;
    Stream << std::hex;
    for (unsigned int i = 0; i < width; ++i)
    {
        Stream<<std::setw(2)<<std::setfill('0')<<(unsigned int)A[width-i-1];
    }
    return Stream.str();
}

inline uint160 uint160S(const char *str)
{
    uint160 rv;
    rv.SetHex(str);
    return rv;
}
inline uint160 uint160S(const std::string& str)
{
    uint160 rv;
    rv.SetHex(str);
    return rv;
}

TEST(uint256_tests, basics) // constructors, equality, inequality
{
    EXPECT_EQ(1 , 0+1);
    // constructor uint256(vector<char>):
    EXPECT_EQ(R1L.ToString() , ArrayToString(R1Array,32));
    EXPECT_EQ(R1S.ToString() , ArrayToString(R1Array,20));
    EXPECT_EQ(R2L.ToString() , ArrayToString(R2Array,32));
    EXPECT_EQ(R2S.ToString() , ArrayToString(R2Array,20));
    EXPECT_EQ(ZeroL.ToString() , ArrayToString(ZeroArray,32));
    EXPECT_EQ(ZeroS.ToString() , ArrayToString(ZeroArray,20));
    EXPECT_EQ(OneL.ToString() , ArrayToString(OneArray,32));
    EXPECT_EQ(OneS.ToString() , ArrayToString(OneArray,20));
    EXPECT_EQ(MaxL.ToString() , ArrayToString(MaxArray,32));
    EXPECT_EQ(MaxS.ToString() , ArrayToString(MaxArray,20));
    EXPECT_NE(OneL.ToString() , ArrayToString(ZeroArray,32));
    EXPECT_NE(OneS.ToString() , ArrayToString(ZeroArray,20));

    // == and !=
    EXPECT_TRUE(R1L != R2L && R1S != R2S);
    EXPECT_TRUE(ZeroL != OneL && ZeroS != OneS);
    EXPECT_TRUE(OneL != ZeroL && OneS != ZeroS);
    EXPECT_TRUE(MaxL != ZeroL && MaxS != ZeroS);

    // String Constructor and Copy Constructor
    EXPECT_EQ(uint256S("0x"+R1L.ToString()) , R1L);
    EXPECT_EQ(uint256S("0x"+R2L.ToString()) , R2L);
    EXPECT_EQ(uint256S("0x"+ZeroL.ToString()) , ZeroL);
    EXPECT_EQ(uint256S("0x"+OneL.ToString()) , OneL);
    EXPECT_EQ(uint256S("0x"+MaxL.ToString()) , MaxL);
    EXPECT_EQ(uint256S(R1L.ToString()) , R1L);
    EXPECT_EQ(uint256S("   0x"+R1L.ToString()+"   ") , R1L);
    EXPECT_EQ(uint256S("") , ZeroL);
    EXPECT_EQ(R1L , uint256S(R1ArrayHex));
    EXPECT_EQ(uint256(R1L) , R1L);
    EXPECT_EQ(uint256(ZeroL) , ZeroL);
    EXPECT_EQ(uint256(OneL) , OneL);

    EXPECT_EQ(uint160S("0x"+R1S.ToString()) , R1S);
    EXPECT_EQ(uint160S("0x"+R2S.ToString()) , R2S);
    EXPECT_EQ(uint160S("0x"+ZeroS.ToString()) , ZeroS);
    EXPECT_EQ(uint160S("0x"+OneS.ToString()) , OneS);
    EXPECT_EQ(uint160S("0x"+MaxS.ToString()) , MaxS);
    EXPECT_EQ(uint160S(R1S.ToString()) , R1S);
    EXPECT_EQ(uint160S("   0x"+R1S.ToString()+"   ") , R1S);
    EXPECT_EQ(uint160S("") , ZeroS);
    EXPECT_EQ(R1S , uint160S(R1ArrayHex));

    EXPECT_EQ(uint160(R1S) , R1S);
    EXPECT_EQ(uint160(ZeroS) , ZeroS);
    EXPECT_EQ(uint160(OneS) , OneS);
}

TEST(uint256_tests, comparison ) // <= >= < >
{
    uint256 LastL;
    for (int i = 255; i >= 0; --i) {
        uint256 TmpL;
        *(TmpL.begin() + (i>>3)) |= 1<<(7-(i&7));
        EXPECT_TRUE( LastL < TmpL );
        LastL = TmpL;
    }

    EXPECT_TRUE( ZeroL < R1L );
    EXPECT_TRUE( R2L < R1L );
    EXPECT_TRUE( ZeroL < OneL );
    EXPECT_TRUE( OneL < MaxL );
    EXPECT_TRUE( R1L < MaxL );
    EXPECT_TRUE( R2L < MaxL );

    uint160 LastS;
    for (int i = 159; i >= 0; --i) {
        uint160 TmpS;
        *(TmpS.begin() + (i>>3)) |= 1<<(7-(i&7));
        EXPECT_TRUE( LastS < TmpS );
        LastS = TmpS;
    }
    EXPECT_TRUE( ZeroS < R1S );
    EXPECT_TRUE( R2S < R1S );
    EXPECT_TRUE( ZeroS < OneS );
    EXPECT_TRUE( OneS < MaxS );
    EXPECT_TRUE( R1S < MaxS );
    EXPECT_TRUE( R2S < MaxS );
}

TEST(uint256_tests, methods ) // GetHex SetHex begin() end() size() GetLow64 GetSerializeSize, Serialize, Unserialize
{
    EXPECT_EQ(R1L.GetHex() , R1L.ToString());
    EXPECT_EQ(R2L.GetHex() , R2L.ToString());
    EXPECT_EQ(OneL.GetHex() , OneL.ToString());
    EXPECT_EQ(MaxL.GetHex() , MaxL.ToString());
    uint256 TmpL(R1L);
    EXPECT_EQ(TmpL , R1L);
    TmpL.SetHex(R2L.ToString());   EXPECT_EQ(TmpL , R2L);
    TmpL.SetHex(ZeroL.ToString()); EXPECT_EQ(TmpL , uint256());

    TmpL.SetHex(R1L.ToString());
    EXPECT_EQ(memcmp(R1L.begin(), R1Array, 32),0);
    EXPECT_EQ(memcmp(TmpL.begin(), R1Array, 32),0);
    EXPECT_EQ(memcmp(R2L.begin(), R2Array, 32),0);
    EXPECT_EQ(memcmp(ZeroL.begin(), ZeroArray, 32),0);
    EXPECT_EQ(memcmp(OneL.begin(), OneArray, 32),0);
    EXPECT_EQ(R1L.size() , sizeof(R1L));
    EXPECT_EQ(sizeof(R1L) , 32);
    EXPECT_EQ(R1L.size() , 32);
    EXPECT_EQ(R2L.size() , 32);
    EXPECT_EQ(ZeroL.size() , 32);
    EXPECT_EQ(MaxL.size() , 32);
    EXPECT_EQ(R1L.begin() + 32 , R1L.end());
    EXPECT_EQ(R2L.begin() + 32 , R2L.end());
    EXPECT_EQ(OneL.begin() + 32 , OneL.end());
    EXPECT_EQ(MaxL.begin() + 32 , MaxL.end());
    EXPECT_EQ(TmpL.begin() + 32 , TmpL.end());
    EXPECT_EQ(GetSerializeSize(R1L, 0, PROTOCOL_VERSION) , 32);
    EXPECT_EQ(GetSerializeSize(ZeroL, 0, PROTOCOL_VERSION) , 32);

    CDataStream ss(0, PROTOCOL_VERSION);
    ss << R1L;
    EXPECT_EQ(ss.str() , std::string(R1Array,R1Array+32));
    ss >> TmpL;
    EXPECT_EQ(R1L , TmpL);
    ss.clear();
    ss << ZeroL;
    EXPECT_EQ(ss.str() , std::string(ZeroArray,ZeroArray+32));
    ss >> TmpL;
    EXPECT_EQ(ZeroL , TmpL);
    ss.clear();
    ss << MaxL;
    EXPECT_EQ(ss.str() , std::string(MaxArray,MaxArray+32));
    ss >> TmpL;
    EXPECT_EQ(MaxL , TmpL);
    ss.clear();

    EXPECT_EQ(R1S.GetHex() , R1S.ToString());
    EXPECT_EQ(R2S.GetHex() , R2S.ToString());
    EXPECT_EQ(OneS.GetHex() , OneS.ToString());
    EXPECT_EQ(MaxS.GetHex() , MaxS.ToString());
    uint160 TmpS(R1S);
    EXPECT_EQ(TmpS , R1S);
    TmpS.SetHex(R2S.ToString());   EXPECT_EQ(TmpS , R2S);
    TmpS.SetHex(ZeroS.ToString()); EXPECT_EQ(TmpS , uint160());

    TmpS.SetHex(R1S.ToString());
    EXPECT_EQ(memcmp(R1S.begin(), R1Array, 20),0);
    EXPECT_EQ(memcmp(TmpS.begin(), R1Array, 20),0);
    EXPECT_EQ(memcmp(R2S.begin(), R2Array, 20),0);
    EXPECT_EQ(memcmp(ZeroS.begin(), ZeroArray, 20),0);
    EXPECT_EQ(memcmp(OneS.begin(), OneArray, 20),0);
    EXPECT_EQ(R1S.size() , sizeof(R1S));
    EXPECT_EQ(sizeof(R1S) , 20);
    EXPECT_EQ(R1S.size() , 20);
    EXPECT_EQ(R2S.size() , 20);
    EXPECT_EQ(ZeroS.size() , 20);
    EXPECT_EQ(MaxS.size() , 20);
    EXPECT_EQ(R1S.begin() + 20 , R1S.end());
    EXPECT_EQ(R2S.begin() + 20 , R2S.end());
    EXPECT_EQ(OneS.begin() + 20 , OneS.end());
    EXPECT_EQ(MaxS.begin() + 20 , MaxS.end());
    EXPECT_EQ(TmpS.begin() + 20 , TmpS.end());
    EXPECT_EQ(GetSerializeSize(R1S, 0, PROTOCOL_VERSION) , 20);
    EXPECT_EQ(GetSerializeSize(ZeroS, 0, PROTOCOL_VERSION) , 20);

    ss << R1S;
    EXPECT_EQ(ss.str() , std::string(R1Array,R1Array+20));
    ss >> TmpS;
    EXPECT_EQ(R1S , TmpS);
    ss.clear();
    ss << ZeroS;
    EXPECT_EQ(ss.str() , std::string(ZeroArray,ZeroArray+20));
    ss >> TmpS;
    EXPECT_EQ(ZeroS , TmpS);
    ss.clear();
    ss << MaxS;
    EXPECT_EQ(ss.str() , std::string(MaxArray,MaxArray+20));
    ss >> TmpS;
    EXPECT_EQ(MaxS , TmpS);
    ss.clear();
}

TEST(uint256_tests, conversion )
{
    EXPECT_EQ(ArithToUint256(UintToArith256(ZeroL)) , ZeroL);
    EXPECT_EQ(ArithToUint256(UintToArith256(OneL)) , OneL);
    EXPECT_EQ(ArithToUint256(UintToArith256(R1L)) , R1L);
    EXPECT_EQ(ArithToUint256(UintToArith256(R2L)) , R2L);
    EXPECT_EQ(UintToArith256(ZeroL) , 0);
    EXPECT_EQ(UintToArith256(OneL) , 1);
    EXPECT_EQ(ArithToUint256(0) , ZeroL);
    EXPECT_EQ(ArithToUint256(1) , OneL);
    EXPECT_EQ(arith_uint256(R1L.GetHex()) , UintToArith256(R1L));
    EXPECT_EQ(arith_uint256(R2L.GetHex()) , UintToArith256(R2L));
    EXPECT_EQ(R1L.GetHex() , UintToArith256(R1L).GetHex());
    EXPECT_EQ(R2L.GetHex() , UintToArith256(R2L).GetHex());
}

