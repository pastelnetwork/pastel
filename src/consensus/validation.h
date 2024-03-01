#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
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
static const unsigned char REJECT_SIGNATURE_ERROR = 0x45;

// origin of the transaction
typedef enum class _TxOriginEnum: uint8_t
{
    UNKNOWN = 0,        // unknown origin
    MINED_BLOCK = 1,    // block mined by this node
    GENERATED = 2,      // generated by this node via RPC API (for RegTest only)
    MSG_BLOCK = 3,      // received in a "block" message
    MSG_TX = 4,         // received in a "tx" message
    MSG_HEADERS = 5,	// received in a "headers" message
    LOADED_BLOCK = 6,   // block loaded from disk
    NEW_TX = 7,		    // new transaction created by this node
} TxOrigin;

inline const char* GetTxOriginName(const TxOrigin txOrigin) noexcept
{
    switch (txOrigin)
    {
        case TxOrigin::UNKNOWN:
            return "UNKNOWN";
        case TxOrigin::MINED_BLOCK:
            return "MINED_BLOCK";
        case TxOrigin::GENERATED:
            return "GENERATED";
        case TxOrigin::MSG_BLOCK:
            return "MSG_BLOCK";
        case TxOrigin::MSG_TX:
            return "MSG_TX";
        case TxOrigin::MSG_HEADERS:
            return "MSG_HEADERS";
        case TxOrigin::LOADED_BLOCK:
            return "LOADED_BLOCK";
        case TxOrigin::NEW_TX:
            return "NEW_TX";
        default:    
            return "UNKNOWN";
    }
}

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
    std::string strRejectReasonDetails;
    unsigned char chRejectCode;
    bool m_bCorruptionPossible;
    TxOrigin txOrigin;

public:
    CValidationState(TxOrigin _txOrigin) :
        mode(STATE::VALID), 
        nDoS(0), 
        chRejectCode(0), 
        m_bCorruptionPossible(false),
        txOrigin(_txOrigin)
    {}

    virtual bool DoS(const int level, 
        const bool bRet = false,
        unsigned char chRejectCodeIn = 0,
        const std::string &strRejectReasonIn = "",
        const bool bCorruptionPossible = false,
        const std::string &strRejectReasonDetailsIn = "") noexcept
    {
        chRejectCode = chRejectCodeIn;
        strRejectReason = strRejectReasonIn;
        strRejectReasonDetails = strRejectReasonDetailsIn;
        m_bCorruptionPossible = bCorruptionPossible;
        if (mode == STATE::ERR)
            return bRet;
        nDoS += level;
        mode = STATE::INVALID;
        return bRet;
    }

    virtual bool Invalid(const bool ret = false, const unsigned char _chRejectCode = 0, 
        const std::string &_strRejectReason = "",
        const std::string &_strRejectReasonDetails = "") noexcept
    {
        return DoS(0, ret, _chRejectCode, _strRejectReason, false, _strRejectReasonDetails);
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
    virtual std::string GetRejectReasonDetails() const noexcept { return strRejectReasonDetails; }

    virtual TxOrigin getTxOrigin() const noexcept { return txOrigin; }
};
