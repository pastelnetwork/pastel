// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "primitives/transaction.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"

#include "librustzcash.h"

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0,10), n == -1? 0: n);
}

std::string COutPoint::ToStringShort() const
{
    return strprintf("%s-%u", hash.ToString().substr(0,64), n == -1? 0: n);
}


std::string SaplingOutPoint::ToString() const
{
    return strprintf("SaplingOutPoint(%s, %u)", hash.ToString().substr(0, 10), n);
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
    if (nSequence != std::numeric_limits<unsigned int>::max())
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}

CTxOut::CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}

uint256 CTxOut::GetHash() const
{
    return SerializeHash(*this);
}

std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%d.%08d, scriptPubKey=%s)", nValue / COIN, nValue % COIN, HexStr(scriptPubKey).substr(0, 30));
}

CMutableTransaction::CMutableTransaction() : 
    nVersion(CTransaction::SPROUT_MIN_CURRENT_VERSION), 
    fOverwintered(false), 
    nVersionGroupId(0), 
    nExpiryHeight(0), 
    nLockTime(0), 
    valueBalance(0)
{}

CMutableTransaction::CMutableTransaction(const CTransaction& tx) : 
    nVersion(tx.nVersion), 
    fOverwintered(tx.fOverwintered), 
    nVersionGroupId(tx.nVersionGroupId), 
    nExpiryHeight(tx.nExpiryHeight),
    vin(tx.vin), 
    vout(tx.vout), 
    nLockTime(tx.nLockTime),
    valueBalance(tx.valueBalance), 
    vShieldedSpend(tx.vShieldedSpend), 
    vShieldedOutput(tx.vShieldedOutput),
    bindingSig(tx.bindingSig)
{}

uint256 CMutableTransaction::GetHash() const
{
    return SerializeHash(*this);
}

std::string CMutableTransaction::ToString() const
{
    std::string str;
    str += strprintf("CMutableTransaction(hash=%s, ver=%d, vin.size=%zu, vout.size=%zu, nLockTime=%u)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        vin.size(),
        vout.size(),
        nLockTime);
    for (const auto &txIn : vin)
        str += "    " + txIn.ToString() + "\n";
    for (const auto &txOut : vout)
        str += "    " + txOut.ToString() + "\n";
    return str;
}

void CTransaction::UpdateHash() const
{
    *const_cast<uint256*>(&hash) = SerializeHash(*this);
}

CTransaction::CTransaction() : 
    nVersion(CTransaction::SPROUT_MIN_CURRENT_VERSION), 
    fOverwintered(false), 
    nVersionGroupId(0), 
    nExpiryHeight(0), 
    vin(), 
    vout(), 
    nLockTime(0), 
    valueBalance(0), 
    vShieldedSpend(), 
    vShieldedOutput(), 
    bindingSig()
{}

CTransaction::CTransaction(const CMutableTransaction &tx) : 
    nVersion(tx.nVersion), 
    fOverwintered(tx.fOverwintered), 
    nVersionGroupId(tx.nVersionGroupId), 
    nExpiryHeight(tx.nExpiryHeight),
    vin(tx.vin), 
    vout(tx.vout), 
    nLockTime(tx.nLockTime),
    valueBalance(tx.valueBalance), 
    vShieldedSpend(tx.vShieldedSpend), 
    vShieldedOutput(tx.vShieldedOutput),
    bindingSig(tx.bindingSig)
{
    UpdateHash();
}

// Protected constructor which only derived classes can call.
// For developer testing only.
CTransaction::CTransaction(const CMutableTransaction &tx, bool evilDeveloperFlag) : 
    nVersion(tx.nVersion), 
    fOverwintered(tx.fOverwintered), 
    nVersionGroupId(tx.nVersionGroupId), 
    nExpiryHeight(tx.nExpiryHeight),
    vin(tx.vin), 
    vout(tx.vout), 
    nLockTime(tx.nLockTime),
    valueBalance(tx.valueBalance), 
    vShieldedSpend(tx.vShieldedSpend), 
    vShieldedOutput(tx.vShieldedOutput),
    bindingSig(tx.bindingSig)
{
    assert(evilDeveloperFlag);
}

CTransaction::CTransaction(CMutableTransaction &&tx) : 
    nVersion(tx.nVersion), 
    fOverwintered(tx.fOverwintered), 
    nVersionGroupId(tx.nVersionGroupId),
    vin(std::move(tx.vin)), 
    vout(std::move(tx.vout)), 
    nLockTime(tx.nLockTime), 
    nExpiryHeight(tx.nExpiryHeight),
    valueBalance(tx.valueBalance),
    vShieldedSpend(std::move(tx.vShieldedSpend)), 
    vShieldedOutput(std::move(tx.vShieldedOutput))
{
    UpdateHash();
}

CTransaction& CTransaction::operator=(const CTransaction &tx) {
    *const_cast<bool*>(&fOverwintered) = tx.fOverwintered;
    *const_cast<int*>(&nVersion) = tx.nVersion;
    *const_cast<uint32_t*>(&nVersionGroupId) = tx.nVersionGroupId;
    *const_cast<std::vector<CTxIn>*>(&vin) = tx.vin;
    *const_cast<std::vector<CTxOut>*>(&vout) = tx.vout;
    *const_cast<unsigned int*>(&nLockTime) = tx.nLockTime;
    *const_cast<uint32_t*>(&nExpiryHeight) = tx.nExpiryHeight;
    *const_cast<CAmount*>(&valueBalance) = tx.valueBalance;
    *const_cast<std::vector<SpendDescription>*>(&vShieldedSpend) = tx.vShieldedSpend;
    *const_cast<std::vector<OutputDescription>*>(&vShieldedOutput) = tx.vShieldedOutput;
    *const_cast<binding_sig_t*>(&bindingSig) = tx.bindingSig;
    *const_cast<uint256*>(&hash) = tx.hash;
    return *this;
}

CAmount CTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (const auto &txOut : vout)
    {
        nValueOut += txOut.nValue;
        if (!MoneyRange(txOut.nValue) || !MoneyRange(nValueOut))
            throw std::runtime_error("CTransaction::GetValueOut(): value out of range");
    }

    if (valueBalance <= 0) {
        // NB: negative valueBalance "takes" money from the transparent value pool just as outputs do
        nValueOut += -valueBalance;

        if (!MoneyRange(-valueBalance) || !MoneyRange(nValueOut)) {
            throw std::runtime_error("CTransaction::GetValueOut(): value out of range");
        }
    }

    return nValueOut;
}

CAmount CTransaction::GetShieldedValueIn() const
{
    CAmount nValue = 0;

    if (valueBalance >= 0) {
        // NB: positive valueBalance "gives" money to the transparent value pool just as inputs do
        nValue += valueBalance;

        if (!MoneyRange(valueBalance) || !MoneyRange(nValue)) {
            throw std::runtime_error("CTransaction::GetShieldedValueIn(): value out of range");
        }
    }

    return nValue;
}

double CTransaction::ComputePriority(double dPriorityInputs, const size_t nTxSize) const
{
    size_t nTransactionSize = CalculateModifiedSize(nTxSize);
    if (nTransactionSize == 0)
        return 0.0;

    return dPriorityInputs / nTransactionSize;
}

size_t CTransaction::CalculateModifiedSize(const size_t nTxSize) const
{
    // In order to avoid disincentivizing cleaning up the UTXO set we don't count
    // the constant overhead for each txin and up to 110 bytes of scriptSig (which
    // is enough to cover a compressed pubkey p2sh redemption) for priority.
    // Providing any more cleanup incentive than making additional inputs free would
    // risk encouraging people to create junk outputs to redeem later.
    size_t nTransactionSize = nTxSize;
    if (nTransactionSize == 0)
        nTransactionSize = static_cast<unsigned int>(::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION));
    for (const auto &txIn : vin)
    {
        const size_t offset = 41U + std::min(110U, txIn.scriptSig.size());
        if (nTransactionSize > offset)
            nTransactionSize -= offset;
    }
    return nTransactionSize;
}

std::string CTransaction::ToString() const
{
    std::string str;
    if (!fOverwintered) {
        str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%zu, vout.size=%zu, nLockTime=%u)\n",
            GetHash().ToString().substr(0,10),
            nVersion,
            vin.size(),
            vout.size(),
            nLockTime);
    } else if (nVersion >= SAPLING_MIN_TX_VERSION) {
        str += strprintf("CTransaction(hash=%s, ver=%d, fOverwintered=%d, nVersionGroupId=%08x, vin.size=%zu, vout.size=%zu, nLockTime=%u, nExpiryHeight=%u, valueBalance=%u, vShieldedSpend.size=%zu, vShieldedOutput.size=%zu)\n",
            GetHash().ToString().substr(0,10),
            nVersion,
            fOverwintered,
            nVersionGroupId,
            vin.size(),
            vout.size(),
            nLockTime,
            nExpiryHeight,
            valueBalance,
            vShieldedSpend.size(),
            vShieldedOutput.size());
    } else if (nVersion >= 3) {
        str += strprintf("CTransaction(hash=%s, ver=%d, fOverwintered=%d, nVersionGroupId=%08x, vin.size=%zu, vout.size=%zu, nLockTime=%u, nExpiryHeight=%u)\n",
            GetHash().ToString().substr(0,10),
            nVersion,
            fOverwintered,
            nVersionGroupId,
            vin.size(),
            vout.size(),
            nLockTime,
            nExpiryHeight);
    }
    for (const auto &txIn : vin)
        str += "    " + txIn.ToString() + "\n";
    for (const auto &txOut : vout)
        str += "    " + txOut.ToString() + "\n";
    return str;
}
