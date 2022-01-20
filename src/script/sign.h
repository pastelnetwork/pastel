#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <type_traits>

#include "script/interpreter.h"

class CKeyID;
class CKeyStore;
class CScript;
class CTransaction;

struct CMutableTransaction;

/** Virtual base class for signature creators. */
class BaseSignatureCreator {
protected:
    const CKeyStore* keystore;

public:
    BaseSignatureCreator(const CKeyStore* keystoreIn) : 
        keystore(keystoreIn)
    {}
    const CKeyStore& KeyStore() const noexcept { return *keystore; };
    virtual ~BaseSignatureCreator() = default;
    virtual const BaseSignatureChecker& Checker() const noexcept = 0;

    /** Create a singular (non-script) signature. */
    virtual bool CreateSig(v_uint8& vchSig, const CKeyID& keyid, const CScript& scriptCode, uint32_t consensusBranchId) const = 0;
};

/** A signature creator for transactions. */
class TransactionSignatureCreator : public BaseSignatureCreator
{
public:
    TransactionSignatureCreator(const CKeyStore* keystoreIn, const CTransaction* txToIn, unsigned int nInIn, 
        const CAmount& amountIn, const uint8_t nHashTypeIn = to_integral_type(SIGHASH::ALL));
    const BaseSignatureChecker& Checker() const noexcept override { return checker; }
    bool CreateSig(v_uint8& vchSig, const CKeyID& keyid, const CScript& scriptCode, uint32_t consensusBranchId) const override;

protected:
    const CTransaction* txTo;
    unsigned int nIn;
    uint8_t nHashType;
    CAmount amount;
    const TransactionSignatureChecker checker;
};

class MutableTransactionSignatureCreator : public TransactionSignatureCreator
{
public:
    MutableTransactionSignatureCreator(const CKeyStore* keystoreIn, const CMutableTransaction* txToIn, unsigned int nInIn, const CAmount& amount, const uint8_t nHashTypeIn) : 
        TransactionSignatureCreator(keystoreIn, &tx, nInIn, amount, nHashTypeIn),
        tx(*txToIn)
    {}

protected:
    CTransaction tx;
};

/** A signature creator that just produces 72-byte empty signatures. */
class DummySignatureCreator : public BaseSignatureCreator
{
public:
    DummySignatureCreator(const CKeyStore* keystoreIn) : 
        BaseSignatureCreator(keystoreIn)
    {}
    const BaseSignatureChecker& Checker() const noexcept override;
    bool CreateSig(v_uint8& vchSig, const CKeyID& keyid, const CScript& scriptCode, uint32_t consensusBranchId) const override;
};

struct SignatureData
{
    CScript scriptSig;

    SignatureData() {}
    explicit SignatureData(const CScript& script) : scriptSig(script) {}
};

/** Produce a script signature using a generic signature creator. */
bool ProduceSignature(const BaseSignatureCreator& creator, const CScript& scriptPubKey, SignatureData& sigdata, uint32_t consensusBranchId);

/** Produce a script signature for a transaction. */
bool SignSignature(
    const CKeyStore &keystore,
    const CScript& fromPubKey,
    CMutableTransaction& txTo,
    unsigned int nIn,
    const CAmount& amount,
    const uint8_t nHashType,
    uint32_t consensusBranchId);
bool SignSignature(
    const CKeyStore& keystore,
    const CTransaction& txFrom,
    CMutableTransaction& txTo,
    unsigned int nIn,
    const uint8_t nHashType,
    uint32_t consensusBranchId);

/** Combine two script signatures using a generic signature checker, intelligently, possibly with OP_0 placeholders. */
SignatureData CombineSignatures(
    const CScript& scriptPubKey,
    const BaseSignatureChecker& checker,
    const SignatureData& scriptSig1,
    const SignatureData& scriptSig2,
    uint32_t consensusBranchId);

/** Extract signature data from a transaction, and insert it. */
SignatureData DataFromTransaction(const CMutableTransaction& tx, unsigned int nIn);
void UpdateTransaction(CMutableTransaction& tx, unsigned int nIn, const SignatureData& data);

