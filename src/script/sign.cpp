// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "primitives/transaction.h"
#include "key.h"
#include "keystore.h"
#include "script/sign.h"
#include "script/standard.h"
#include "uint256.h"

using namespace std;

TransactionSignatureCreator::TransactionSignatureCreator(const CKeyStore* keystoreIn, const CTransaction* txToIn, unsigned int nInIn, const CAmount& amountIn, const uint8_t nHashTypeIn) : 
    BaseSignatureCreator(keystoreIn), txTo(txToIn), nIn(nInIn), 
    nHashType(nHashTypeIn), 
    amount(amountIn), 
    checker(txTo, nIn, amountIn)
{}

bool TransactionSignatureCreator::CreateSig(v_uint8& vchSig, const CKeyID& address, const CScript& scriptCode, uint32_t consensusBranchId) const
{
    CKey key;
    if (!keystore->GetKey(address, key))
        return false;

    uint256 hash;
    try {
        hash = SignatureHash(scriptCode, *txTo, nIn, nHashType, amount, consensusBranchId);
    } catch (logic_error ex) {
        return false;
    }

    if (!key.Sign(hash, vchSig))
        return false;
    vchSig.push_back((unsigned char)nHashType);
    return true;
}

static bool Sign1(const CKeyID& address, const BaseSignatureCreator& creator, const CScript& scriptCode, std::vector<v_uint8>& ret, uint32_t consensusBranchId)
{
    v_uint8 vchSig;
    if (!creator.CreateSig(vchSig, address, scriptCode, consensusBranchId))
        return false;
    ret.push_back(vchSig);
    return true;
}

static bool SignN(const vector<v_uint8>& multisigdata, const BaseSignatureCreator& creator, const CScript& scriptCode, std::vector<v_uint8>& ret, uint32_t consensusBranchId)
{
    int nSigned = 0;
    int nRequired = multisigdata.front()[0];
    for (unsigned int i = 1; i < multisigdata.size()-1 && nSigned < nRequired; i++)
    {
        const v_uint8& pubkey = multisigdata[i];
        CKeyID keyID = CPubKey(pubkey).GetID();
        if (Sign1(keyID, creator, scriptCode, ret, consensusBranchId))
            ++nSigned;
    }
    return nSigned==nRequired;
}

/**
 * Sign scriptPubKey using signature made with creator.
 * Signatures are returned in scriptSigRet (or returns false if scriptPubKey can't be signed),
 * unless whichTypeRet is TX_SCRIPTHASH, in which case scriptSigRet is the redemption script.
 * Returns false if scriptPubKey could not be completely satisfied.
 */
static bool SignStep(const BaseSignatureCreator& creator, const CScript& scriptPubKey,
                     std::vector<v_uint8>& ret, txnouttype& whichTypeRet, uint32_t consensusBranchId)
{
    CScript scriptRet;
    uint160 h160;
    ret.clear();

    vector<v_uint8> vSolutions;
    if (!Solver(scriptPubKey, whichTypeRet, vSolutions))
        return false;

    CKeyID keyID;
    switch (whichTypeRet)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
        return false;
    case TX_PUBKEY:
        keyID = CPubKey(vSolutions[0]).GetID();
        return Sign1(keyID, creator, scriptPubKey, ret, consensusBranchId);
    case TX_PUBKEYHASH:
        keyID = CKeyID(uint160(vSolutions[0]));
        if (!Sign1(keyID, creator, scriptPubKey, ret, consensusBranchId))
            return false;
        else
        {
            CPubKey vch;
            creator.KeyStore().GetPubKey(keyID, vch);
            ret.push_back(ToByteVector(vch));
        }
        return true;
    case TX_SCRIPTHASH:
        if (creator.KeyStore().GetCScript(uint160(vSolutions[0]), scriptRet)) {
            ret.push_back(v_uint8(scriptRet.begin(), scriptRet.end()));
            return true;
        }
        return false;

    case TX_MULTISIG:
        ret.push_back(v_uint8()); // workaround CHECKMULTISIG bug
        return (SignN(vSolutions, creator, scriptPubKey, ret, consensusBranchId));

    default:
        return false;
    }
}

static CScript PushAll(const vector<v_uint8>& values)
{
    CScript result;
    for (const auto &v : values)
    {
        if (v.size() == 0) {
            result << OP_0;
        } else if (v.size() == 1 && v[0] >= 1 && v[0] <= 16) {
            result << CScript::EncodeOP_N(v[0]);
        } else {
            result << v;
        }
    }
    return result;
}

bool ProduceSignature(const BaseSignatureCreator& creator, const CScript& fromPubKey, SignatureData& sigdata, uint32_t consensusBranchId)
{
    CScript script = fromPubKey;
    bool solved = true;
    std::vector<v_uint8> result;
    txnouttype whichType;
    solved = SignStep(creator, script, result, whichType, consensusBranchId);
    CScript subscript;

    if (solved && whichType == TX_SCRIPTHASH)
    {
        // Solver returns the subscript that needs to be evaluated;
        // the final scriptSig is the signatures from that
        // and then the serialized subscript:
        script = subscript = CScript(result[0].begin(), result[0].end());
        solved = solved && SignStep(creator, script, result, whichType, consensusBranchId) && whichType != TX_SCRIPTHASH;
        result.push_back(v_uint8(subscript.begin(), subscript.end()));
    }

    sigdata.scriptSig = PushAll(result);

    // Test solution
    return solved && VerifyScript(sigdata.scriptSig, fromPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, creator.Checker(), consensusBranchId);
}

SignatureData DataFromTransaction(const CMutableTransaction& tx, unsigned int nIn)
{
    SignatureData data;
    assert(tx.vin.size() > nIn);
    data.scriptSig = tx.vin[nIn].scriptSig;
    return data;
}

void UpdateTransaction(CMutableTransaction& tx, unsigned int nIn, const SignatureData& data)
{
    assert(tx.vin.size() > nIn);
    tx.vin[nIn].scriptSig = data.scriptSig;
}

bool SignSignature(
    const CKeyStore &keystore,
    const CScript& fromPubKey,
    CMutableTransaction& txTo,
    unsigned int nIn,
    const CAmount& amount,
    const uint8_t nHashType,
    uint32_t consensusBranchId)
{
    assert(nIn < txTo.vin.size());

    CTransaction txToConst(txTo);
    TransactionSignatureCreator creator(&keystore, &txToConst, nIn, amount, nHashType);

    SignatureData sigdata;
    bool ret = ProduceSignature(creator, fromPubKey, sigdata, consensusBranchId);
    UpdateTransaction(txTo, nIn, sigdata);
    return ret;
}

bool SignSignature(
    const CKeyStore &keystore,
    const CTransaction& txFrom,
    CMutableTransaction& txTo,
    unsigned int nIn,
    const uint8_t nHashType,
    uint32_t consensusBranchId)
{
    assert(nIn < txTo.vin.size());
    CTxIn& txin = txTo.vin[nIn];
    assert(txin.prevout.n < txFrom.vout.size());
    const CTxOut& txout = txFrom.vout[txin.prevout.n];

    return SignSignature(keystore, txout.scriptPubKey, txTo, nIn, txout.nValue, nHashType, consensusBranchId);
}

static vector<v_uint8> CombineMultisig(const CScript& scriptPubKey, const BaseSignatureChecker& checker,
                               const vector<v_uint8>& vSolutions,
                               const vector<v_uint8>& sigs1, const vector<v_uint8>& sigs2, uint32_t consensusBranchId)
{
    // Combine all the signatures we've got:
    set<v_uint8> allsigs;
    for (const auto &v : sigs1)
    {
        if (!v.empty())
            allsigs.insert(v);
    }
    for (const auto &v : sigs2)
    {
        if (!v.empty())
            allsigs.insert(v);
    }

    // Build a map of pubkey -> signature by matching sigs to pubkeys:
    assert(vSolutions.size() > 1);
    unsigned int nSigsRequired = vSolutions.front()[0];
    const size_t nPubKeys = vSolutions.size()-2;
    map<v_uint8, v_uint8> sigs;
    for(const auto& sig : allsigs)
    {
        for (size_t i = 0; i < nPubKeys; i++)
        {
            const v_uint8& pubkey = vSolutions[i + 1];
            if (sigs.count(pubkey))
                continue; // Already got a sig for this pubkey

            if (checker.CheckSig(sig, pubkey, scriptPubKey, consensusBranchId))
            {
                sigs[pubkey] = sig;
                break;
            }
        }
    }
    // Now build a merged CScript:
    unsigned int nSigsHave = 0;
    std::vector<v_uint8> result;
    result.push_back(v_uint8()); // pop-one-too-many workaround
    for (size_t i = 0; (i < nPubKeys) && (nSigsHave < nSigsRequired); i++)
    {
        if (sigs.count(vSolutions[i + 1]))
        {
            result.push_back(sigs[vSolutions[i + 1]]);
            ++nSigsHave;
        }
    }
    // Fill any missing with OP_0:
    for (unsigned int i = nSigsHave; i < nSigsRequired; i++)
        result.push_back(v_uint8());

    return result;
}

namespace
{
struct Stacks
{
    std::vector<v_uint8> script;

    Stacks() {}
    explicit Stacks(const std::vector<v_uint8>& scriptSigStack_) : script(scriptSigStack_) {}
    explicit Stacks(const SignatureData& data, uint32_t consensusBranchId) {
        EvalScript(script, data.scriptSig, SCRIPT_VERIFY_STRICTENC, BaseSignatureChecker(), consensusBranchId);
    }

    SignatureData Output() const {
        SignatureData result;
        result.scriptSig = PushAll(script);
        return result;
    }
};
}

static Stacks CombineSignatures(const CScript& scriptPubKey, const BaseSignatureChecker& checker,
                                 const txnouttype txType, const vector<v_uint8>& vSolutions,
                                 Stacks sigs1, Stacks sigs2, uint32_t consensusBranchId)
{
    switch (txType)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
        // Don't know anything about this, assume bigger one is correct:
        if (sigs1.script.size() >= sigs2.script.size())
            return sigs1;
        return sigs2;
    case TX_PUBKEY:
    case TX_PUBKEYHASH:
        // Signatures are bigger than placeholders or empty scripts:
        if (sigs1.script.empty() || sigs1.script[0].empty())
            return sigs2;
        return sigs1;
    case TX_SCRIPTHASH:
        if (sigs1.script.empty() || sigs1.script.back().empty())
            return sigs2;
        else if (sigs2.script.empty() || sigs2.script.back().empty())
            return sigs1;
        else
        {
            // Recur to combine:
            v_uint8 spk = sigs1.script.back();
            CScript pubKey2(spk.begin(), spk.end());

            txnouttype txType2;
            vector<v_uint8> vSolutions2;
            Solver(pubKey2, txType2, vSolutions2);
            sigs1.script.pop_back();
            sigs2.script.pop_back();
            Stacks result = CombineSignatures(pubKey2, checker, txType2, vSolutions2, sigs1, sigs2, consensusBranchId);
            result.script.push_back(spk);
            return result;
        }
    case TX_MULTISIG:
        return Stacks(CombineMultisig(scriptPubKey, checker, vSolutions, sigs1.script, sigs2.script, consensusBranchId));
    default:
        return Stacks();
    }
}

SignatureData CombineSignatures(const CScript& scriptPubKey, const BaseSignatureChecker& checker,
                          const SignatureData& scriptSig1, const SignatureData& scriptSig2,
                          uint32_t consensusBranchId)
{
    txnouttype txType;
    vector<v_uint8> vSolutions;
    Solver(scriptPubKey, txType, vSolutions);

    return CombineSignatures(
        scriptPubKey, checker, txType, vSolutions,
        Stacks(scriptSig1, consensusBranchId),
        Stacks(scriptSig2, consensusBranchId),
        consensusBranchId).Output();
}

namespace {
/** Dummy signature checker which accepts all signatures. */
class DummySignatureChecker : public BaseSignatureChecker
{
public:
    DummySignatureChecker() {}

    bool CheckSig(
        const v_uint8& scriptSig,
        const v_uint8& vchPubKey,
        const CScript& scriptCode,
        uint32_t consensusBranchId) const
    {
        return true;
    }
};
const DummySignatureChecker dummyChecker;
}

const BaseSignatureChecker& DummySignatureCreator::Checker() const noexcept
{
    return dummyChecker;
}

bool DummySignatureCreator::CreateSig(
    v_uint8& vchSig,
    const CKeyID& keyid,
    const CScript& scriptCode,
    uint32_t consensusBranchId) const
{
    // Create a dummy signature that is a valid DER-encoding
    vchSig.assign(72, '\000');
    vchSig[0] = 0x30;
    vchSig[1] = 69;
    vchSig[2] = 0x02;
    vchSig[3] = 33;
    vchSig[4] = 0x01;
    vchSig[4 + 33] = 0x02;
    vchSig[5 + 33] = 32;
    vchSig[6 + 33] = 0x01;
    vchSig[6 + 33 + 32] = to_integral_type(SIGHASH::ALL);
    return true;
}
