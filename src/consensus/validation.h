#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <string>

/** "reject" message codes */
static const unsigned char REJECT_MALFORMED = 0x01;
static const unsigned char REJECT_INVALID = 0x10;
static const unsigned char REJECT_OBSOLETE = 0x11;
static const unsigned char REJECT_DUPLICATE = 0x12;
static const unsigned char REJECT_NONSTANDARD = 0x40;
static const unsigned char REJECT_DUST = 0x41;
static const unsigned char REJECT_INSUFFICIENTFEE = 0x42;
static const unsigned char REJECT_CHECKPOINT = 0x43;
static const unsigned char REJECT_MISSING_INPUTS = 0x44;

/** Capture information about block/transaction validation */
class CValidationState
{
private:
    enum class STATE
    {
        VALID,   //! everything ok
        INVALID, //! network rule violation (DoS value may be set)
        ERR      //! run-time error
    } mode;
    int nDoS;
    std::string strRejectReason;
    unsigned char chRejectCode;
    bool m_bCorruptionPossible;

public:
    CValidationState() : 
        mode(STATE::VALID), 
        nDoS(0), 
        chRejectCode(0), 
        m_bCorruptionPossible(false)
    {}
    virtual bool DoS(const int level, const bool bRet = false,
             unsigned char chRejectCodeIn=0, const std::string strRejectReasonIn="",
             const bool bCorruptionPossible = false) noexcept
    {
        chRejectCode = chRejectCodeIn;
        strRejectReason = strRejectReasonIn;
        m_bCorruptionPossible = bCorruptionPossible;
        if (mode == STATE::ERR)
            return bRet;
        nDoS += level;
        mode = STATE::INVALID;
        return bRet;
    }

    virtual bool Invalid(const bool ret = false, const unsigned char _chRejectCode = 0, const std::string _strRejectReason = "") noexcept
    {
        return DoS(0, ret, _chRejectCode, _strRejectReason);
    }

    virtual bool Error(const std::string& strRejectReasonIn) noexcept
    {
        if (mode == STATE::VALID)
            strRejectReason = strRejectReasonIn;
        mode = STATE::ERR;
        return false;
    }

    virtual bool IsValid() const noexcept
    {
        return mode == STATE::VALID;
    }

    virtual bool IsInvalid() const noexcept
    {
        return mode == STATE::INVALID;
    }

    virtual bool IsError() const noexcept
    {
        return mode == STATE::ERR;
    }

    virtual bool IsInvalid(int &nDoSOut) const noexcept 
    {
        if (IsInvalid())
        {
            nDoSOut = nDoS;
            return true;
        }
        return false;
    }

    virtual bool CorruptionPossible() const noexcept
    {
        return m_bCorruptionPossible;
    }

    virtual unsigned char GetRejectCode() const noexcept { return chRejectCode; }
    virtual bool IsRejectCode(const unsigned char chRejCode) const noexcept { return chRejCode == chRejectCode; }
    virtual std::string GetRejectReason() const noexcept { return strRejectReason; }
};
