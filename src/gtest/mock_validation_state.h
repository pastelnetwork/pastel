#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <gmock/gmock.h>

#include <consensus/validation.h>

class MockCValidationState : public CValidationState
{
public:
    MockCValidationState(TxOrigin txOrigin) noexcept :
        CValidationState(txOrigin)
    {}
    MOCK_METHOD(bool, DoS, (const int level, const bool bRet, const unsigned char chRejectCodeIn, const std::string strRejectReasonIn, const bool bCorruptionPossible), (override, noexcept));
    MOCK_METHOD(bool, Invalid, (const bool bRet, const unsigned char chRejectCode, const std::string strRejectReason), (override, noexcept));
    MOCK_METHOD(bool, Error, (const std::string& strRejectReasonIn), (override, noexcept));
    MOCK_METHOD(bool, IsValid, (), (const, override, noexcept));
    MOCK_METHOD(bool, IsInvalid, (), (const, override, noexcept));
    MOCK_METHOD(bool, IsError, (), (const, override, noexcept));
    MOCK_METHOD(bool, IsInvalid, (int& nDoSOut), (const, override, noexcept));
    MOCK_METHOD(bool, CorruptionPossible, (), (const, override, noexcept));
    MOCK_METHOD(unsigned char, GetRejectCode, (), (const, override, noexcept));
    MOCK_METHOD(std::string, GetRejectReason, (), (const, override, noexcept));
};
