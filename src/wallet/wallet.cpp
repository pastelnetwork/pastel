// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>

#include <asyncrpcqueue.h>
#include <checkpoints.h>
#include <coincontrol.h>
#include <core_io.h>
#include <consensus/upgrades.h>
#include <consensus/validation.h>
#include <consensus/consensus.h>
#include <fs.h>
#include <init.h>
#include <key_io.h>
#include <main.h>
#include <net.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <script/script.h>
#include <script/sign.h>
#include <timedata.h>
#include <utilmoneystr.h>
#include <zcash/Note.hpp>
#include <wallet/crypter.h>
#include <zcash/Address.hpp>

#include <algorithm>
#include <assert.h>
#include <random>
#include <variant>
#include <thread>

using namespace std;
using namespace libzcash;

/**
 * Settings
 */
CFeeRate payTxFee(DEFAULT_TRANSACTION_FEE);
CAmount maxTxFee = DEFAULT_TRANSACTION_MAXFEE;
unsigned int nTxConfirmTarget = DEFAULT_TX_CONFIRM_TARGET;
bool bSpendZeroConfChange = true;
bool fSendFreeTransactions = false;
bool fPayAtLeastCustomFee = true;

/**
 * Fees smaller than this (in patoshi) are considered zero fee (for transaction creation)
 * Override with -mintxfee
 */
CFeeRate CWallet::minTxFee = CFeeRate(1000);

/** @defgroup mapWallet
 *
 * @{
 */

struct CompareValueOnly
{
    bool operator()(const pair<CAmount, pair<const CWalletTx*, unsigned int> >& t1,
                    const pair<CAmount, pair<const CWalletTx*, unsigned int> >& t2) const
    {
        return t1.first < t2.first;
    }
};

string COutput::ToString() const
{
    return strprintf("COutput(%s, %d, %d) [%s]", tx->GetHash().ToString(), i, nDepth, FormatMoney(tx->vout[i].nValue));
}

const CWalletTx* CWallet::GetWalletTx(const uint256& hash) const
{
    LOCK(cs_wallet);
    const auto it = mapWallet.find(hash);
    if (it == mapWallet.cend())
        return nullptr;
    return &(it->second);
}

// Generate a new Sapling spending key and return its public payment address
SaplingPaymentAddress CWallet::GenerateNewSaplingZKey()
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata

    // Create new metadata
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    // Try to get the seed
    HDSeed seed;
    if (!GetHDSeed(seed))
        throw runtime_error("CWallet::GenerateNewSaplingZKey(): HD seed not found");

    auto m = libzcash::SaplingExtendedSpendingKey::Master(seed);
    uint32_t bip44CoinType = Params().BIP44CoinType();

    // We use a fixed keypath scheme of m/32'/coin_type'/account'
    // Derive m/32'
    auto m_32h = m.Derive(32 | ZIP32_HARDENED_KEY_LIMIT);
    // Derive m/32'/coin_type'
    auto m_32h_cth = m_32h.Derive(bip44CoinType | ZIP32_HARDENED_KEY_LIMIT);

    // Derive account key at next index, skip keys already known to the wallet
    libzcash::SaplingExtendedSpendingKey xsk;
    do
    {
        xsk = m_32h_cth.Derive(hdChain.saplingAccountCounter | ZIP32_HARDENED_KEY_LIMIT);
        metadata.hdKeypath = "m/32'/" + to_string(bip44CoinType) + "'/" + to_string(hdChain.saplingAccountCounter) + "'";
        metadata.seedFp = hdChain.seedFp;
        // Increment childkey index
        hdChain.saplingAccountCounter++;
    } while (HaveSaplingSpendingKey(xsk.ToXFVK()));

    // Update the chain model in the database
    if (fFileBacked && !CWalletDB(strWalletFile).WriteHDChain(hdChain))
        throw runtime_error("CWallet::GenerateNewSaplingZKey(): Writing HD chain model failed");

    auto ivk = xsk.expsk.full_viewing_key().in_viewing_key();
    mapSaplingZKeyMetadata[ivk] = metadata;

    if (!AddSaplingZKey(xsk)) {
        throw runtime_error("CWallet::GenerateNewSaplingZKey(): AddSaplingZKey failed");
    }
    // return default sapling payment address.
    return xsk.DefaultAddress();
}

// Add spending key to keystore 
bool CWallet::AddSaplingZKey(const libzcash::SaplingExtendedSpendingKey &sk)
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata

    if (!CCryptoKeyStore::AddSaplingSpendingKey(sk)) {
        return false;
    }
    
    if (!fFileBacked) {
        return true;
    }

    if (!IsCrypted()) {
        auto ivk = sk.expsk.full_viewing_key().in_viewing_key();
        return CWalletDB(strWalletFile).WriteSaplingZKey(ivk, sk, mapSaplingZKeyMetadata[ivk]);
    }
    
    return true;
}

bool CWallet::AddSaplingFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    AssertLockHeld(cs_wallet);

    if (!CCryptoKeyStore::AddSaplingFullViewingKey(extfvk)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    return CWalletDB(strWalletFile).WriteSaplingExtendedFullViewingKey(extfvk);
}

// Add payment address -> incoming viewing key map entry
bool CWallet::AddSaplingIncomingViewingKey(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const libzcash::SaplingPaymentAddress &addr)
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata

    if (!CCryptoKeyStore::AddSaplingIncomingViewingKey(ivk, addr)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteSaplingPaymentAddress(addr, ivk);
    }

    return true;
}

CPubKey CWallet::GenerateNewKey()
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    CKey secret;
    secret.MakeNewKey(fCompressed);

    // Compressed public keys were introduced in version 0.6.0
    if (fCompressed)
        SetMinVersion(FEATURE_COMPRPUBKEY);

    CPubKey pubkey = secret.GetPubKey();
    assert(secret.VerifyPubKey(pubkey));

    // Create new metadata
    int64_t nCreationTime = GetTime();
    mapKeyMetadata[pubkey.GetID()] = CKeyMetadata(nCreationTime);
    if (!nTimeFirstKey || nCreationTime < nTimeFirstKey)
        nTimeFirstKey = nCreationTime;

    if (!AddKeyPubKey(secret, pubkey))
        throw runtime_error("CWallet::GenerateNewKey(): AddKey failed");
    return pubkey;
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey &pubkey)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey))
        return false;

    // check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(pubkey.GetID());
    if (HaveWatchOnly(script))
        RemoveWatchOnly(script);

    if (!fFileBacked)
        return true;
    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteKey(pubkey,
                                                 secret.GetPrivKey(),
                                                 mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}

bool CWallet::AddCryptedKey(const CPubKey &vchPubKey,
                            const v_uint8& vchCryptedSecret)
{

    if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret))
        return false;
    if (!fFileBacked)
        return true;
    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption)
            return pwalletdbEncryption->WriteCryptedKey(vchPubKey,
                                                        vchCryptedSecret,
                                                        mapKeyMetadata[vchPubKey.GetID()]);
        else
            return CWalletDB(strWalletFile).WriteCryptedKey(vchPubKey,
                                                            vchCryptedSecret,
                                                            mapKeyMetadata[vchPubKey.GetID()]);
    }
    return false;
}

bool CWallet::AddCryptedSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk,
                                           const v_uint8 &vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedSaplingSpendingKey(extfvk, vchCryptedSecret))
        return false;
    if (!fFileBacked)
        return true;
    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption) {
            return pwalletdbEncryption->WriteCryptedSaplingZKey(extfvk,
                                                         vchCryptedSecret,
                                                         mapSaplingZKeyMetadata[extfvk.fvk.in_viewing_key()]);
        } else {
            return CWalletDB(strWalletFile).WriteCryptedSaplingZKey(extfvk,
                                                         vchCryptedSecret,
                                                         mapSaplingZKeyMetadata[extfvk.fvk.in_viewing_key()]);
        }
    }
    return false;
}

bool CWallet::LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    return true;
}

bool CWallet::LoadCryptedKey(const CPubKey &vchPubKey, const v_uint8 &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::LoadCryptedSaplingZKey(
    const libzcash::SaplingExtendedFullViewingKey &extfvk,
    const v_uint8 &vchCryptedSecret)
{
     return CCryptoKeyStore::AddCryptedSaplingSpendingKey(extfvk, vchCryptedSecret);
}

bool CWallet::LoadSaplingZKeyMetadata(const libzcash::SaplingIncomingViewingKey &ivk, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata
    mapSaplingZKeyMetadata[ivk] = meta;
    return true;
}

bool CWallet::LoadSaplingZKey(const libzcash::SaplingExtendedSpendingKey &key)
{
    return CCryptoKeyStore::AddSaplingSpendingKey(key);
}

bool CWallet::LoadSaplingFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    return CCryptoKeyStore::AddSaplingFullViewingKey(extfvk);
}

bool CWallet::LoadSaplingPaymentAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const libzcash::SaplingIncomingViewingKey &ivk)
{
    return CCryptoKeyStore::AddSaplingIncomingViewingKey(ivk, addr);
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
}

bool CWallet::LoadCScript(const CScript& redeemScript)
{
    /* A sanity check was added in pull #3843 to avoid adding redeemScripts
     * that never can be redeemed. However, old wallets may still contain
     * these. Do not add them to the wallet and warn. */
    KeyIO keyIO(Params());
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
    {
        string strAddr = keyIO.EncodeDestination(CScriptID(redeemScript));
        LogPrintf("%s: Warning: This wallet contains a redeemScript of size %i which exceeds maximum size %i thus can never be redeemed. Do not use address %s.\n",
            __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr);
        return true;
    }

    return CCryptoKeyStore::AddCScript(redeemScript);
}

bool CWallet::AddWatchOnly(const CScript &dest)
{
    if (!CCryptoKeyStore::AddWatchOnly(dest))
        return false;
    nTimeFirstKey = 1; // No birthday information for watch-only keys.
    NotifyWatchonlyChanged(true);
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript &dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
        return false;
    if (!HaveWatchOnly())
        NotifyWatchonlyChanged(false);
    if (fFileBacked)
        if (!CWalletDB(strWalletFile).EraseWatchOnly(dest))
            return false;

    return true;
}

bool CWallet::LoadWatchOnly(const CScript &dest)
{
    return CCryptoKeyStore::AddWatchOnly(dest);
}

bool CWallet::Unlock(const SecureString& strWalletPassphrase)
{
    CCrypter crypter;
    CKeyingMaterial vMasterKey;

    {
        LOCK(cs_wallet);
        for (const auto& pMasterKey : mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                continue; // try another master key
            if (CCryptoKeyStore::Unlock(vMasterKey))
            { // Now that the wallet is decrypted, ensure we have an HD seed.
                if (!this->HaveHDSeed())
                    this->GenerateNewSeed();
                return true;
            }
        }
    }
    return false;
}

unsigned int GetMsecTimeDelta(const int64_t nStartTime) noexcept
{
    const int64_t nEndTime = GetTimeMillis();
    if (nEndTime == nStartTime)
        return 100;
    const unsigned int nDelta = static_cast<unsigned int>(100 / ((double)(nEndTime - nStartTime)));
    if (!nDelta)
        return 100;
    return nDelta;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked();

    {
        LOCK(cs_wallet);
        Lock();

        CCrypter crypter;
        CKeyingMaterial vMasterKey;
        for (auto& [nID, mKey] : mapMasterKeys)
        {
            if (!crypter.SetKeyFromPassphrase(strOldWalletPassphrase, mKey.vchSalt, mKey.nDeriveIterations, mKey.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(mKey.vchCryptedKey, vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(vMasterKey))
            {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, mKey.vchSalt, mKey.nDeriveIterations, mKey.nDerivationMethod);
                mKey.nDeriveIterations *= GetMsecTimeDelta(nStartTime);

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, mKey.vchSalt, mKey.nDeriveIterations, mKey.nDerivationMethod);
                mKey.nDeriveIterations = (mKey.nDeriveIterations + mKey.nDeriveIterations * GetMsecTimeDelta(nStartTime)) / 2;

                if (mKey.nDeriveIterations < 25000)
                    mKey.nDeriveIterations = 25000;

                LogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", mKey.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, mKey.vchSalt, mKey.nDeriveIterations, mKey.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(vMasterKey, mKey.vchCryptedKey))
                    return false;
                CWalletDB(strWalletFile).WriteMasterKey(nID, mKey);
                if (fWasLocked)
                    Lock();
                return true;
            }
        }
    }

    return false;
}

void CWallet::ChainTip(const CBlockIndex *pindex, 
                       const CBlock *pblock,
                       SaplingMerkleTree saplingTree, 
                       bool added)
{
    if (added) {
        IncrementNoteWitnesses(pindex, pblock, saplingTree);
    } else {
        DecrementNoteWitnesses(pindex);
    }
    UpdateSaplingNullifierNoteMapForBlock(pblock);
}

void CWallet::SetBestChain(const CBlockLocator& loc)
{
    CWalletDB walletdb(strWalletFile);
    SetBestChainINTERNAL(walletdb, loc);
}

set<pair<libzcash::PaymentAddress, uint256>> CWallet::GetNullifiersForAddresses(
        const set<libzcash::PaymentAddress> & addresses)
{
    set<pair<libzcash::PaymentAddress, uint256>> nullifierSet;
    // Sapling ivk -> list of addrs map
    // (There may be more than one diversified address for a given ivk.)
    map<libzcash::SaplingIncomingViewingKey, vector<libzcash::SaplingPaymentAddress>> ivkMap;
    for (const auto & addr : addresses) {
        auto saplingAddr = get_if<libzcash::SaplingPaymentAddress>(&addr);
        if (saplingAddr)
        {
            libzcash::SaplingIncomingViewingKey ivk;
            this->GetSaplingIncomingViewingKey(*saplingAddr, ivk);
            ivkMap[ivk].push_back(*saplingAddr);
        }
    }
    for (const auto & txPair : mapWallet)
    {
        // Sapling
        for (const auto & noteDataPair : txPair.second.mapSaplingNoteData) {
            auto & noteData = noteDataPair.second;
            auto & nullifier = noteData.nullifier;
            auto & ivk = noteData.ivk;
            if (nullifier && ivkMap.count(ivk)) {
                for (const auto & addr : ivkMap[ivk]) {
                    nullifierSet.insert(make_pair(addr, nullifier.value()));
                }
            }
        }
    }
    return nullifierSet;
}

bool CWallet::IsNoteSaplingChange(const set<pair<libzcash::PaymentAddress, uint256>> & nullifierSet,
        const libzcash::PaymentAddress & address,
        const SaplingOutPoint & op)
{
    // A Note is marked as "change" if the address that received it
    // also spent Notes in the same transaction. This will catch,
    // for instance:
    // - Change created by spending fractions of Notes (because
    //   z_sendmany sends change to the originating z-address).
    // - Notes created by consolidation transactions (e.g. using
    //   z_mergetoaddress).
    // - Notes sent from one address to itself.
    for (const SpendDescription &spend : mapWallet[op.hash].vShieldedSpend) {
        if (nullifierSet.count(make_pair(address, spend.nullifier))) {
            return true;
        }
    }
    return false;
}

bool CWallet::SetMinVersion(enum WalletFeature nVersion, CWalletDB* pwalletdbIn, bool fExplicit)
{
    LOCK(cs_wallet); // nWalletVersion
    if (nWalletVersion >= nVersion)
        return true;

    // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
    if (fExplicit && nVersion > nWalletMaxVersion)
            nVersion = FEATURE_LATEST;

    nWalletVersion = nVersion;

    if (nVersion > nWalletMaxVersion)
        nWalletMaxVersion = nVersion;

    if (fFileBacked)
    {
        if (nWalletVersion > 40000) {
            if (pwalletdbIn) {
                pwalletdbIn->WriteMinVersion(nWalletVersion);
            } else {
                CWalletDB pwalletdb(strWalletFile);
                pwalletdb.WriteMinVersion(nWalletVersion);
            }
        }
    }

    return true;
}

bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

set<uint256> CWallet::GetConflicts(const uint256& txid) const
{
    set<uint256> result;
    AssertLockHeld(cs_wallet);

    const auto it = mapWallet.find(txid);
    if (it == mapWallet.cend())
        return result;
    const auto& wtx = it->second;

    for (const auto &txin : wtx.vin)
    {
        if (mapTxSpends.count(txin.prevout) <= 1)
            continue;  // No conflict if zero or one spends
        const auto range = mapTxSpends.equal_range(txin.prevout);
        for (auto it = range.first; it != range.second; ++it)
            result.insert(it->second);
    }

    for (const auto &spend : wtx.vShieldedSpend)
    {
        uint256 nullifier = spend.nullifier;
        if (mapTxSaplingNullifiers.count(nullifier) <= 1)
            continue;  // No conflict if zero or one spends
        const auto range_o = mapTxSaplingNullifiers.equal_range(nullifier);
        for (auto it = range_o.first; it != range_o.second; ++it)
            result.insert(it->second);
    }
    return result;
}

void CWallet::Flush(bool shutdown)
{
    bitdb.Flush(shutdown);
}

bool CWallet::Verify(const string& walletFile, string& warningString, string& errorString)
{
    if (!bitdb.Open(GetDataDir()))
    {
        // try moving the database env out of the way
        fs::path pathDatabase = GetDataDir() / "database";
        fs::path pathDatabaseBak = GetDataDir() / strprintf("database.%d.bak", GetTime());
        try {
            fs::rename(pathDatabase, pathDatabaseBak);
            LogPrintf("Moved old %s to %s. Retrying.\n", pathDatabase.string(), pathDatabaseBak.string());
        } catch (const fs::filesystem_error&) {
            // failure is ok (well, not really, but it's not worse than what we started with)
        }

        // try again
        if (!bitdb.Open(GetDataDir())) {
            // if it still fails, it probably means we can't even create the database env
            string msg = strprintf(_("Error initializing wallet database environment %s!"), GetDataDir());
            errorString += msg;
            return true;
        }
    }

    if (GetBoolArg("-salvagewallet", false))
    {
        // Recover readable keypairs:
        if (!CWalletDB::Recover(bitdb, walletFile, true))
            return false;
    }

    if (fs::exists(GetDataDir() / walletFile))
    {
        CDBEnv::VerifyResult r = bitdb.Verify(walletFile, CWalletDB::Recover);
        if (r == CDBEnv::RECOVER_OK)
        {
            warningString += strprintf(_("Warning: wallet.dat corrupt, data salvaged!"
                                     " Original wallet.dat saved as wallet.{timestamp}.bak in %s; if"
                                     " your balance or transactions are incorrect you should"
                                     " restore from a backup."), GetDataDir());
        }
        if (r == CDBEnv::RECOVER_FAIL)
            errorString += _("wallet.dat corrupt, salvage failed");
    }

    return true;
}

template <class T>
void CWallet::SyncMetaData(pair<typename TxSpendMap<T>::iterator, typename TxSpendMap<T>::iterator> range)
{
    // We want all the wallet transactions in range to have the same metadata as
    // the oldest (smallest nOrderPos).
    // So: find smallest nOrderPos:

    int64_t nMinOrderPos = numeric_limits<int64_t>::max();
    const CWalletTx* copyFrom = nullptr;
    for (auto it = range.first; it != range.second; ++it)
    {
        const uint256& hash = it->second;
        int64_t n = mapWallet[hash].nOrderPos;
        if (n < nMinOrderPos)
        {
            nMinOrderPos = n;
            copyFrom = &mapWallet[hash];
        }
    }
    // Now copy data from copyFrom to rest:
    for (auto it = range.first; it != range.second; ++it)
    {
        const uint256& hash = it->second;
        CWalletTx* copyTo = &mapWallet[hash];
        if (copyFrom == copyTo)
            continue;
        copyTo->mapValue = copyFrom->mapValue;
        // mapSaplingNoteData not copied on purpose
        // (it is always set correctly for each CWalletTx)
        copyTo->vOrderForm = copyFrom->vOrderForm;
        // fTimeReceivedIsTxTime not copied on purpose
        // nTimeReceived not copied on purpose
        copyTo->nTimeSmart = copyFrom->nTimeSmart;
        copyTo->fFromMe = copyFrom->fFromMe;
        copyTo->strFromAccount = copyFrom->strFromAccount;
        // nOrderPos not copied on purpose
        // cached members not copied on purpose
    }
}

/**
 * Outpoint is spent if any non-conflicted transaction spends it.
 */
bool CWallet::IsSpent(const uint256& hash, const unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    auto range = mapTxSpends.equal_range(outpoint);
    for (auto it = range.first; it != range.second; ++it)
    {
        const uint256& wtxid = it->second;
        auto mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0)
            return true; // Spent
    }
    return false;
}

bool CWallet::IsSaplingSpent(const uint256& nullifier) const
{
    LOCK(cs_main);

    const auto range = mapTxSaplingNullifiers.equal_range(nullifier);
    for (auto it = range.first; it != range.second; ++it)
    {
        const uint256& wtxid = it->second;
        const auto mit = mapWallet.find(wtxid);
        if (mit != mapWallet.cend() && mit->second.GetDepthInMainChain() >= 0)
            return true; // Spent
    }
    return false;
}

void CWallet::AddToTransparentSpends(const COutPoint& outpoint, const uint256& wtxid)
{
    mapTxSpends.insert(make_pair(outpoint, wtxid));

    auto range = mapTxSpends.equal_range(outpoint);
    SyncMetaData<COutPoint>(range);
}

void CWallet::AddToSaplingSpends(const uint256& nullifier, const uint256& wtxid)
{
    mapTxSaplingNullifiers.insert(make_pair(nullifier, wtxid));

    auto range = mapTxSaplingNullifiers.equal_range(nullifier);
    SyncMetaData<uint256>(range);
}

void CWallet::AddToSpends(const uint256& wtxid)
{
    assert(mapWallet.count(wtxid));
    CWalletTx& thisTx = mapWallet[wtxid];
    if (thisTx.IsCoinBase()) // Coinbases don't spend anything!
        return;

    for (const auto& txin : thisTx.vin)
        AddToTransparentSpends(txin.prevout, wtxid);
    for (const auto &spend : thisTx.vShieldedSpend)
        AddToSaplingSpends(spend.nullifier, wtxid);
}

void CWallet::ClearNoteWitnessCache()
{
    LOCK(cs_wallet);
    for (auto& wtxItem : mapWallet)
    {
        for (auto& item : wtxItem.second.mapSaplingNoteData)
        {
            item.second.witnesses.clear();
            item.second.witnessHeight = -1;
        }
    }
    nWitnessCacheSize = 0;
}

template<typename NoteDataMap>
void CopyPreviousWitnesses(NoteDataMap& noteDataMap, int indexHeight, const uint64_t nWitnessCacheSize)
{
    for (auto& item : noteDataMap) {
        auto* nd = &(item.second);
        // Only increment witnesses that are behind the current height
        if (nd->witnessHeight < indexHeight) {
            // Check the validity of the cache
            // The only time a note witnessed above the current height
            // would be invalid here is during a reindex when blocks
            // have been decremented, and we are incrementing the blocks
            // immediately after.
            assert(nWitnessCacheSize >= nd->witnesses.size());
            // Witnesses being incremented should always be either -1
            // (never incremented or decremented) or one below indexHeight
            assert((nd->witnessHeight == -1) || (nd->witnessHeight == indexHeight - 1));
            // Copy the witness for the previous block if we have one
            if (!nd->witnesses.empty())
                nd->witnesses.push_front(nd->witnesses.front());
            if (nd->witnesses.size() > WITNESS_CACHE_SIZE)
                nd->witnesses.pop_back();
        }
    }
}

template<typename NoteDataMap>
void AppendNoteCommitment(NoteDataMap& noteDataMap, int indexHeight, const uint64_t nWitnessCacheSize, const uint256& note_commitment)
{
    for (auto& item : noteDataMap)
    {
        auto* nd = &(item.second);
        if (nd->witnessHeight < indexHeight && !nd->witnesses.empty())
        {
            // Check the validity of the cache
            // See comment in CopyPreviousWitnesses about validity.
            assert(nWitnessCacheSize >= nd->witnesses.size());
            nd->witnesses.front().append(note_commitment);
        }
    }
}

template<typename OutPoint, typename NoteData, typename Witness>
void WitnessNoteIfMine(map<OutPoint, NoteData>& noteDataMap, int indexHeight, const uint64_t nWitnessCacheSize, const OutPoint& key, const Witness& witness)
{
    if (noteDataMap.count(key) && noteDataMap[key].witnessHeight < indexHeight)
    {
        auto* nd = &(noteDataMap[key]);
        if (!nd->witnesses.empty())
        {
            // We think this can happen because we write out the
            // witness cache state after every block increment or
            // decrement, but the block index itself is written in
            // batches. So if the node crashes in between these two
            // operations, it is possible for IncrementNoteWitnesses
            // to be called again on previously-cached blocks. This
            // doesn't affect existing cached notes because of the
            // NoteData::witnessHeight checks. See #1378 for details.
            LogPrintf("Inconsistent witness cache state found for %s\n- Cache size: %zu\n- Top (height %d): %s\n- New (height %d): %s\n",
                        key.ToString(), nd->witnesses.size(),
                        nd->witnessHeight,
                        nd->witnesses.front().root().GetHex(),
                        indexHeight,
                        witness.root().GetHex());
            nd->witnesses.clear();
        }
        nd->witnesses.push_front(witness);
        // Set height to one less than pindex so it gets incremented
        nd->witnessHeight = indexHeight - 1;
        // Check the validity of the cache
        assert(nWitnessCacheSize >= nd->witnesses.size());
    }
}


template<typename NoteDataMap>
void UpdateWitnessHeights(NoteDataMap& noteDataMap, int indexHeight, const uint64_t nWitnessCacheSize)
{
    for (auto& item : noteDataMap)
    {
        auto* nd = &(item.second);
        if (nd->witnessHeight < indexHeight)
        {
            nd->witnessHeight = indexHeight;
            // Check the validity of the cache
            // See comment in CopyPreviousWitnesses about validity.
            assert(nWitnessCacheSize >= nd->witnesses.size());
        }
    }
}

void CWallet::IncrementNoteWitnesses(const CBlockIndex* pindex,
                                     const CBlock* pblockIn,
                                      SaplingMerkleTree& saplingTree)
{
    LOCK(cs_wallet);
    for (auto& wtxItem : mapWallet)
       ::CopyPreviousWitnesses(wtxItem.second.mapSaplingNoteData, pindex->nHeight, nWitnessCacheSize);

    if (nWitnessCacheSize < WITNESS_CACHE_SIZE)
        ++nWitnessCacheSize;

    const CBlock* pblock {pblockIn};
    CBlock block;
    if (!pblock) {
        ReadBlockFromDisk(block, pindex, Params().GetConsensus());
        pblock = &block;
    }

    for (const auto& tx : pblock->vtx)
    {
        const auto &hash = tx.GetHash();
        bool txIsOurs = mapWallet.count(hash);
        // Sapling
        for (uint32_t i = 0; i < tx.vShieldedOutput.size(); i++) {
            const auto& note_commitment = tx.vShieldedOutput[i].cm;
            saplingTree.append(note_commitment);

            // Increment existing witnesses
            for (auto& wtxItem : mapWallet)
                ::AppendNoteCommitment(wtxItem.second.mapSaplingNoteData, pindex->nHeight, nWitnessCacheSize, note_commitment);

            // If this is our note, witness it
            if (txIsOurs) {
                SaplingOutPoint outPoint {hash, i};
                ::WitnessNoteIfMine(mapWallet[hash].mapSaplingNoteData, pindex->nHeight, nWitnessCacheSize, outPoint, saplingTree.witness());
            }
        }
    }

    // Update witness heights
    for (auto& wtxItem : mapWallet)
        ::UpdateWitnessHeights(wtxItem.second.mapSaplingNoteData, pindex->nHeight, nWitnessCacheSize);

    // For performance reasons, we write out the witness cache in
    // CWallet::SetBestChain() (which also ensures that overall consistency
    // of the wallet.dat is maintained).
}

template<typename NoteDataMap>
void DecrementNoteWitnesses(NoteDataMap& noteDataMap, int indexHeight, const uint64_t nWitnessCacheSize)
{
    for (auto& item : noteDataMap)
    {
        auto* nd = &(item.second);
        // Only decrement witnesses that are not above the current height
        if (nd->witnessHeight <= indexHeight)
        {
            // Check the validity of the cache
            // See comment below (this would be invalid if there were a
            // prior decrement).
            assert(nWitnessCacheSize >= nd->witnesses.size());
            // Witnesses being decremented should always be either -1
            // (never incremented or decremented) or equal to the height
            // of the block being removed (indexHeight)
            assert((nd->witnessHeight == -1) || (nd->witnessHeight == indexHeight));
            if (nd->witnesses.size() > 0) {
                nd->witnesses.pop_front();
            }
            // indexHeight is the height of the block being removed, so 
            // the new witness cache height is one below it.
            nd->witnessHeight = indexHeight - 1;
        }
        // Check the validity of the cache
        // Technically if there are notes witnessed above the current
        // height, their cache will now be invalid (relative to the new
        // value of nWitnessCacheSize). However, this would only occur
        // during a reindex, and by the time the reindex reaches the tip
        // of the chain again, the existing witness caches will be valid
        // again.
        // We don't set nWitnessCacheSize to zero at the start of the
        // reindex because the on-disk blocks had already resulted in a
        // chain that didn't trigger the assertion below.
        if (nd->witnessHeight < indexHeight) {
            // Subtract 1 to compare to what nWitnessCacheSize will be after
            // decrementing.
            assert((nWitnessCacheSize - 1) >= nd->witnesses.size());
        }
    }
}

void CWallet::DecrementNoteWitnesses(const CBlockIndex* pindex)
{
    LOCK(cs_wallet);
    for (auto& wtxItem : mapWallet)
        ::DecrementNoteWitnesses(wtxItem.second.mapSaplingNoteData, pindex->nHeight, nWitnessCacheSize);
    --nWitnessCacheSize;
    // TODO: If nWitnessCache is zero, we need to regenerate the caches (#1302)
    assert(nWitnessCacheSize > 0);

    // For performance reasons, we write out the witness cache in
    // CWallet::SetBestChain() (which also ensures that overall consistency
    // of the wallet.dat is maintained).
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
    if (IsCrypted())
        return false;

    CKeyingMaterial vMasterKey;

    vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    GetRandBytes(&vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey;

    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    GetRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypter;
    int64_t nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25'000, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = 2'500'000 / GetMsecTimeDelta(nStartTime);

    nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = (kMasterKey.nDeriveIterations + kMasterKey.nDeriveIterations * GetMsecTimeDelta(nStartTime)) / 2;

    if (kMasterKey.nDeriveIterations < 25'000)
        kMasterKey.nDeriveIterations = 25'000;

    LogPrintf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

    if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
        return false;
    if (!crypter.Encrypt(vMasterKey, kMasterKey.vchCryptedKey))
        return false;

    {
        LOCK(cs_wallet);
        mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
        if (fFileBacked)
        {
            assert(!pwalletdbEncryption);
            pwalletdbEncryption = new CWalletDB(strWalletFile);
            if (!pwalletdbEncryption->TxnBegin()) {
                delete pwalletdbEncryption;
                pwalletdbEncryption = nullptr;
                return false;
            }
            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
        }

        if (!EncryptKeys(vMasterKey))
        {
            if (fFileBacked) {
                pwalletdbEncryption->TxnAbort();
                delete pwalletdbEncryption;
            }
            // We now probably have half of our keys encrypted in memory, and half not...
            // die and let the user reload the unencrypted wallet.
            assert(false);
        }

        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, pwalletdbEncryption, true);

        if (fFileBacked)
        {
            if (!pwalletdbEncryption->TxnCommit()) {
                delete pwalletdbEncryption;
                // We now have keys encrypted in memory, but not on disk...
                // die to avoid confusion and let the user reload the unencrypted wallet.
                assert(false);
            }

            delete pwalletdbEncryption;
            pwalletdbEncryption = nullptr;
        }

        Lock();
        Unlock(strWalletPassphrase);
        NewKeyPool();
        Lock();

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        CDB::Rewrite(strWalletFile);

    }
    NotifyStatusChanged(this);

    return true;
}

int64_t CWallet::IncOrderPosNext(CWalletDB *pwalletdb)
{
    AssertLockHeld(cs_wallet); // nOrderPosNext
    int64_t nRet = nOrderPosNext++;
    if (pwalletdb) {
        pwalletdb->WriteOrderPosNext(nOrderPosNext);
    } else {
        CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
    }
    return nRet;
}

CWallet::TxItems CWallet::OrderedTxItems(list<CAccountingEntry>& acentries, string strAccount)
{
    AssertLockHeld(cs_wallet); // mapWallet
    CWalletDB walletdb(strWalletFile);

    // First: get all CWalletTx and CAccountingEntry into a sorted-by-order multimap.
    TxItems txOrdered;

    // Note: maintaining indices in the database of (account,time) --> txid and (account, time) --> acentry
    // would make this much faster for applications that do this a lot.
    for (auto&[txid, wtx] : mapWallet)
        txOrdered.insert(make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry*)0)));
    acentries.clear();
    walletdb.ListAccountCreditDebit(strAccount, acentries);
    for (auto &entry : acentries)
        txOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTx*)0, &entry)));
    return txOrdered;
}

void CWallet::MarkDirty()
{
    LOCK(cs_wallet);
    for (auto &item : mapWallet)
        item.second.MarkDirty();
}

/**
 * Ensure that every note in the wallet (for which we possess a spending key)
 * has a cached nullifier.
 */
bool CWallet::UpdateNullifierNoteMap()
{
    LOCK(cs_wallet);

    if (IsLocked())
        return false;

    ZCNoteDecryption dec;
    for (const auto &[txid, wtx] : mapWallet)
    {
            // TODO: Sapling.  This method is only called from RPC walletpassphrase, which is currently unsupported
        // as RPC encryptwallet is hidden behind two flags: -developerencryptwallet -experimentalfeatures

        UpdateNullifierNoteMapWithTx(wtx);
    }
    return true;
}

/**
 * Update mapSaplingNullifiersToNotes
 * with the cached nullifiers in this tx.
 */
void CWallet::UpdateNullifierNoteMapWithTx(const CWalletTx& wtx)
{
    LOCK(cs_wallet);
    for (const auto& item : wtx.mapSaplingNoteData)
    {
        if (item.second.nullifier)
            mapSaplingNullifiersToNotes[*item.second.nullifier] = item.first;
    }
}

/**
 * Update mapSaplingNullifiersToNotes, computing the nullifier from a cached witness if necessary.
 */
void CWallet::UpdateSaplingNullifierNoteMapWithTx(CWalletTx& wtx)
{
    LOCK(cs_wallet);

    for (mapSaplingNoteData_t::value_type &item : wtx.mapSaplingNoteData) {
        SaplingOutPoint op = item.first;
        SaplingNoteData nd = item.second;

        if (nd.witnesses.empty()) {
            // If there are no witnesses, erase the nullifier and associated mapping.
            if (item.second.nullifier) {
                mapSaplingNullifiersToNotes.erase(item.second.nullifier.value());
            }
            item.second.nullifier = nullopt;
        }
        else {
            uint64_t position = nd.witnesses.front().position();
            auto extfvk = mapSaplingFullViewingKeys.at(nd.ivk);
            OutputDescription output = wtx.vShieldedOutput[op.n];
            auto optPlaintext = SaplingNotePlaintext::decrypt(output.encCiphertext, nd.ivk, output.ephemeralKey, output.cm);
            if (!optPlaintext) {
                // An item in mapSaplingNoteData must have already been successfully decrypted,
                // otherwise the item would not exist in the first place.
                assert(false);
            }
            auto optNote = optPlaintext.value().note(nd.ivk); //-V1007
            if (!optNote) {
                assert(false);
            }
            auto optNullifier = optNote.value().nullifier(extfvk.fvk, position); //-V1007
            if (!optNullifier) {
                // This should not happen.  If it does, maybe the position has been corrupted or miscalculated?
                assert(false);
            }
            uint256 nullifier = optNullifier.value(); //-V1007
            mapSaplingNullifiersToNotes[nullifier] = op;
            item.second.nullifier = nullifier;
        }
    }
}

/**
 * Iterate over transactions in a block and update the cached Sapling nullifiers
 * for transactions which belong to the wallet.
 */
void CWallet::UpdateSaplingNullifierNoteMapForBlock(const CBlock *pblock)
{
    LOCK(cs_wallet);

    for (const CTransaction& tx : pblock->vtx)
    {
        auto hash = tx.GetHash();
        bool txIsOurs = mapWallet.count(hash);
        if (txIsOurs)
            UpdateSaplingNullifierNoteMapWithTx(mapWallet[hash]);
    }
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn, bool fFromLoadWallet, CWalletDB* pwalletdb)
{
    uint256 hash = wtxIn.GetHash();

    if (fFromLoadWallet)
    {
        mapWallet[hash] = wtxIn;
        mapWallet[hash].BindWallet(this);
        UpdateNullifierNoteMapWithTx(mapWallet[hash]);
        AddToSpends(hash);
    }
    else
    {
        LOCK(cs_wallet);
        // Inserts only if not already there, returns tx inserted or tx found
        auto ret = mapWallet.emplace(hash, wtxIn);
        CWalletTx& wtx = (*ret.first).second;
        wtx.BindWallet(this);
        UpdateNullifierNoteMapWithTx(wtx);
        bool fInsertedNew = ret.second;
        if (fInsertedNew)
        {
            wtx.nTimeReceived = GetAdjustedTime();
            wtx.nOrderPos = IncOrderPosNext(pwalletdb);

            wtx.nTimeSmart = wtx.nTimeReceived;
            if (!wtxIn.hashBlock.IsNull())
            {
                if (mapBlockIndex.count(wtxIn.hashBlock))
                {
                    int64_t latestNow = wtx.nTimeReceived;
                    int64_t latestEntry = 0;
                    {
                        // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
                        int64_t latestTolerated = latestNow + 300;
                        list<CAccountingEntry> acentries;
                        TxItems txOrdered = OrderedTxItems(acentries);
                        for (TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
                        {
                            CWalletTx *const pwtx = (*it).second.first;
                            if (pwtx == &wtx)
                                continue;
                            CAccountingEntry *const pacentry = (*it).second.second;
                            int64_t nSmartTime;
                            if (pwtx)
                            {
                                nSmartTime = pwtx->nTimeSmart;
                                if (!nSmartTime)
                                    nSmartTime = pwtx->nTimeReceived;
                            }
                            else
                                nSmartTime = pacentry->nTime;
                            if (nSmartTime <= latestTolerated)
                            {
                                latestEntry = nSmartTime;
                                if (nSmartTime > latestNow)
                                    latestNow = nSmartTime;
                                break;
                            }
                        }
                    }

                    int64_t blocktime = mapBlockIndex[wtxIn.hashBlock]->GetBlockTime();
                    wtx.nTimeSmart = max(latestEntry, min(blocktime, latestNow));
                }
                else
                    LogPrintf("AddToWallet(): found %s in block %s not in index\n",
                             wtxIn.GetHash().ToString(),
                             wtxIn.hashBlock.ToString());
            }
            AddToSpends(hash);
        }

        bool fUpdated = false;
        if (!fInsertedNew)
        {
            // Merge
            if (!wtxIn.hashBlock.IsNull() && wtxIn.hashBlock != wtx.hashBlock)
            {
                wtx.hashBlock = wtxIn.hashBlock;
                fUpdated = true;
            }
            if (wtxIn.nIndex != -1 && (wtxIn.vMerkleBranch != wtx.vMerkleBranch || wtxIn.nIndex != wtx.nIndex))
            {
                wtx.vMerkleBranch = wtxIn.vMerkleBranch;
                wtx.nIndex = wtxIn.nIndex;
                fUpdated = true;
            }
            if (UpdatedNoteData(wtxIn, wtx)) {
                fUpdated = true;
            }
            if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe)
            {
                wtx.fFromMe = wtxIn.fFromMe;
                fUpdated = true;
            }
        }

        //// debug print
        LogPrintf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString(), (fInsertedNew ? "new" : ""), (fUpdated ? "update" : ""));

        // Write to disk
        if (fInsertedNew || fUpdated)
            if (!wtx.WriteToDisk(pwalletdb))
                return false;

        // Break debit/credit balance caches:
        wtx.MarkDirty();

        // Notify UI of new or updated transaction
        NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

        // notify an external script when a wallet transaction comes in or is updated
        string strCmd = GetArg("-walletnotify", "");

        if ( !strCmd.empty())
        {
            replaceAll(strCmd, "%s", wtxIn.GetHash().GetHex());
            thread t(runCommand, strCmd); // thread runs free
            if (t.joinable())
                t.join();
        }

    }
    return true;
}

bool CWallet::UpdatedNoteData(const CWalletTx& wtxIn, CWalletTx& wtx)
{
    const bool unchangedSaplingFlag = (wtxIn.mapSaplingNoteData.empty() || wtxIn.mapSaplingNoteData == wtx.mapSaplingNoteData);
    if (!unchangedSaplingFlag)
    {
        auto tmp = wtxIn.mapSaplingNoteData;
        // Ensure we keep any cached witnesses we may already have

        for (const auto &nd : wtx.mapSaplingNoteData) {
            if (tmp.count(nd.first) && nd.second.witnesses.size() > 0) {
                tmp.at(nd.first).witnesses.assign(
                        nd.second.witnesses.cbegin(), nd.second.witnesses.cend());
            }
            tmp.at(nd.first).witnessHeight = nd.second.witnessHeight;
        }

        // Now copy over the updated note data
        wtx.mapSaplingNoteData = tmp;
    }

    return !unchangedSaplingFlag;
}

/**
 * Add a transaction to the wallet, or update it.
 * pblock is optional, but should be provided if the transaction is known to be in a block.
 * If fUpdate is true, existing transactions will be updated.
 *
 * If pblock is null, this transaction has either recently entered the mempool from the
 * network, is re-entering the mempool after a block was disconnected, or is exiting the
 * mempool because it conflicts with another transaction. In all these cases, if there is
 * an existing wallet transaction, the wallet transaction's Merkle branch data is _not_
 * updated; instead, the transaction being in the mempool or conflicted is determined on
 * the fly in CMerkleTx::GetDepthInMainChain().
 */
bool CWallet::AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate)
{
    {
        AssertLockHeld(cs_wallet);
        const bool fExisted = mapWallet.count(tx.GetHash()) != 0;
        if (fExisted && !fUpdate)
            return false;
        auto saplingNoteDataAndAddressesToAdd = FindMySaplingNotes(tx);
        auto saplingNoteData = saplingNoteDataAndAddressesToAdd.first;
        auto addressesToAdd = saplingNoteDataAndAddressesToAdd.second;
        for (const auto &addressToAdd : addressesToAdd) {
            if (!AddSaplingIncomingViewingKey(addressToAdd.second, addressToAdd.first)) {
                return false;
            }
        }
        if (fExisted || IsMine(tx) || IsFromMe(tx) || saplingNoteData.size() > 0)
        {
            CWalletTx wtx(this,tx);

            if (saplingNoteData.size() > 0) {
                wtx.SetSaplingNoteData(saplingNoteData);
            }

            // Get merkle branch if transaction was found in a block
            if (pblock)
                wtx.SetMerkleBranch(*pblock);

            // Do not flush the wallet here for performance reasons
            // this is safe, as in case of a crash, we rescan the necessary blocks on startup through our SetBestChain-mechanism
            CWalletDB walletdb(strWalletFile, "r+", false);

            return AddToWallet(wtx, false, &walletdb);
        }
    }
    return false;
}

void CWallet::SyncTransaction(const CTransaction& tx, const CBlock* pblock)
{
    LOCK2(cs_main, cs_wallet);
    if (!AddToWalletIfInvolvingMe(tx, pblock, true))
        return; // Not one of ours

    MarkAffectedTransactionsDirty(tx);
}

void CWallet::MarkAffectedTransactionsDirty(const CTransaction& tx)
{
    // If a transaction changes 'conflicted' state, that changes the balance
    // available of the outputs it spends. So force those to be
    // recomputed, also:
    for (const auto& txin : tx.vin)
    {
        if (mapWallet.count(txin.prevout.hash))
            mapWallet[txin.prevout.hash].MarkDirty();
    }

    for (const SpendDescription &spend : tx.vShieldedSpend) {
        uint256 nullifier = spend.nullifier;
        if (mapSaplingNullifiersToNotes.count(nullifier) &&
            mapWallet.count(mapSaplingNullifiersToNotes[nullifier].hash)) {
            mapWallet[mapSaplingNullifiersToNotes[nullifier].hash].MarkDirty();
        }
    }
}

void CWallet::EraseFromWallet(const uint256 &hash)
{
    if (!fFileBacked)
        return;
    {
        LOCK(cs_wallet);
        if (mapWallet.erase(hash))
            CWalletDB(strWalletFile).EraseTx(hash);
    }
    return;
}

/**
 * Finds all output notes in the given transaction that have been sent to
 * SaplingPaymentAddresses in this wallet.
 *
 * It should never be necessary to call this method with a CWalletTx, because
 * the result of FindMySaplingNotes (for the addresses available at the time) will
 * already have been cached in CWalletTx.mapSaplingNoteData.
 */
pair<mapSaplingNoteData_t, SaplingIncomingViewingKeyMap> CWallet::FindMySaplingNotes(const CTransaction &tx) const
{
    LOCK(cs_KeyStore);
    uint256 hash = tx.GetHash();

    mapSaplingNoteData_t noteData;
    SaplingIncomingViewingKeyMap viewingKeysToAdd;

    // Protocol Spec: 4.19 Block Chain Scanning (Sapling)
    for (uint32_t i = 0; i < tx.vShieldedOutput.size(); ++i) {
        const OutputDescription output = tx.vShieldedOutput[i];
        for (auto it = mapSaplingFullViewingKeys.begin(); it != mapSaplingFullViewingKeys.end(); ++it) {
            SaplingIncomingViewingKey ivk = it->first;
            auto result = SaplingNotePlaintext::decrypt(output.encCiphertext, ivk, output.ephemeralKey, output.cm);
            if (!result) {
                continue;
            }
            auto address = ivk.address(result.value().d);
            if (address && mapSaplingIncomingViewingKeys.count(address.value()) == 0) {
                viewingKeysToAdd[address.value()] = ivk;
            }
            // We don't cache the nullifier here as computing it requires knowledge of the note position
            // in the commitment tree, which can only be determined when the transaction has been mined.
            SaplingOutPoint op {hash, i};
            SaplingNoteData nd;
            nd.ivk = ivk;
            noteData.insert(make_pair(op, nd));
            break;
        }
    }

    return make_pair(noteData, viewingKeysToAdd);
}

bool CWallet::IsSaplingNullifierFromMe(const uint256& nullifier) const
{
    {
        LOCK(cs_wallet);
        if (mapSaplingNullifiersToNotes.count(nullifier) &&
                mapWallet.count(mapSaplingNullifiersToNotes.at(nullifier).hash)) {
            return true;
        }
    }
    return false;
}

void CWallet::GetSaplingNoteWitnesses(vector<SaplingOutPoint> notes,
                                      vector<optional<SaplingWitness>>& witnesses,
                                      uint256 &final_anchor)
{
    LOCK(cs_wallet);
    witnesses.resize(notes.size());
    optional<uint256> rt;
    int i = 0;
    for (const auto &note : notes)
    {
        if (mapWallet.count(note.hash) &&
                mapWallet[note.hash].mapSaplingNoteData.count(note) &&
                mapWallet[note.hash].mapSaplingNoteData[note].witnesses.size() > 0)
        {
            witnesses[i] = mapWallet[note.hash].mapSaplingNoteData[note].witnesses.front();
            if (!rt)
                rt = witnesses[i]->root();
            else
            {
                assert(*rt == witnesses[i]->root());
            }
        }
        i++;
    }
    // All returned witnesses have the same anchor
    if (rt)
        final_anchor = *rt;
}

isminetype CWallet::GetIsMine(const CTxIn &txin) const
{
    LOCK(cs_wallet);

    const auto mi = mapWallet.find(txin.prevout.hash);
    if (mi != mapWallet.cend())
    {
        const auto& prev = (*mi).second;
        if (txin.prevout.n < prev.vout.size())
            return GetIsMine(prev.vout[txin.prevout.n]);
    }
    return isminetype::NO;
}

CAmount CWallet::GetDebit(const CTxIn &txin, const isminetype& filter) const
{
    {
        LOCK(cs_wallet);
        const auto mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const auto& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
                if (IsMineType(GetIsMine(prev.vout[txin.prevout.n]), filter))
                    return prev.vout[txin.prevout.n].nValue;
        }
    }
    return 0;
}

isminetype CWallet::GetIsMine(const CTxOut& txout) const
{
    return ::GetIsMine(*this, txout.scriptPubKey);
}

CAmount CWallet::GetCredit(const CTxOut& txout, const isminetype& filter) const
{
    if (!MoneyRange(txout.nValue))
        throw runtime_error("CWallet::GetCredit(): value out of range");
    return (IsMineType(GetIsMine(txout), filter) ? txout.nValue : 0);
}

bool CWallet::IsChange(const CTxOut& txout) const
{
    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a script that is ours, but is not in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    if (::IsMine(*this, txout.scriptPubKey))
    {
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
            return true;

        LOCK(cs_wallet);
        if (!mapAddressBook.count(address))
            return true;
    }
    return false;
}

CAmount CWallet::GetChange(const CTxOut& txout) const
{
    if (!MoneyRange(txout.nValue))
        throw runtime_error("CWallet::GetChange(): value out of range");
    return (IsChange(txout) ? txout.nValue : 0);
}

bool CWallet::IsMine(const CTransaction& tx) const
{
    for (const auto& txout : tx.vout)
    {
        if (GetIsMine(txout) != isminetype::NO)
            return true;
    }
    return false;
}

bool CWallet::IsFromMe(const CTransaction& tx) const
{
    if (GetDebit(tx, isminetype::ALL) > 0)
        return true;
    for (const auto &spend : tx.vShieldedSpend)
    {
        if (IsSaplingNullifierFromMe(spend.nullifier))
            return true;
    }
    return false;
}

CAmount CWallet::GetDebit(const CTransaction& tx, const isminetype& filter) const
{
    CAmount nDebit = 0;
    for (const auto& txin : tx.vin)
    {
        nDebit += GetDebit(txin, filter);
        if (!MoneyRange(nDebit))
            throw runtime_error("CWallet::GetDebit(): value out of range");
    }
    return nDebit;
}

CAmount CWallet::GetCredit(const CTransaction& tx, const isminetype& filter) const
{
    CAmount nCredit = 0;
    for (const auto& txout: tx.vout)
    {
        nCredit += GetCredit(txout, filter);
        if (!MoneyRange(nCredit))
            throw runtime_error("CWallet::GetCredit(): value out of range");
    }
    return nCredit;
}

CAmount CWallet::GetChange(const CTransaction& tx) const
{
    CAmount nChange = 0;
    for (const auto& txout : tx.vout)
    {
        nChange += GetChange(txout);
        if (!MoneyRange(nChange))
            throw runtime_error("CWallet::GetChange(): value out of range");
    }
    return nChange;
}

bool CWallet::IsHDFullyEnabled() const
{
    // Only Sapling addresses are HD for now
    return false;
}

void CWallet::GenerateNewSeed()
{
    LOCK(cs_wallet);

    auto seed = HDSeed::Random(HD_WALLET_SEED_LENGTH);

    int64_t nCreationTime = GetTime();

    // If the wallet is encrypted and locked, this will fail.
    if (!SetHDSeed(seed))
        throw runtime_error(string(__func__) + ": SetHDSeed failed");

    // store the key creation time together with
    // the child index counter in the database
    // as a hdchain object
    CHDChain newHdChain;
    newHdChain.nVersion = CHDChain::VERSION_HD_BASE;
    newHdChain.seedFp = seed.Fingerprint();
    newHdChain.nCreateTime = nCreationTime;
    SetHDChain(newHdChain, false);
}

bool CWallet::SetHDSeed(const HDSeed& seed)
{
    if (!CCryptoKeyStore::SetHDSeed(seed)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    {
        LOCK(cs_wallet);
        if (!IsCrypted()) {
            return CWalletDB(strWalletFile).WriteHDSeed(seed);
        }
    }
    return true;
}

bool CWallet::SetCryptedHDSeed(const uint256& seedFp, const v_uint8 &vchCryptedSecret)
{
    if (!CCryptoKeyStore::SetCryptedHDSeed(seedFp, vchCryptedSecret)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption)
            return pwalletdbEncryption->WriteCryptedHDSeed(seedFp, vchCryptedSecret);
        else
            return CWalletDB(strWalletFile).WriteCryptedHDSeed(seedFp, vchCryptedSecret);
    }
    return false;
}

HDSeed CWallet::GetHDSeedForRPC() const {
    HDSeed seed;
    if (!pwalletMain->GetHDSeed(seed)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "HD seed not found");
    }
    return seed;
}

void CWallet::SetHDChain(const CHDChain& chain, bool memonly)
{
    LOCK(cs_wallet);
    if (!memonly && fFileBacked && !CWalletDB(strWalletFile).WriteHDChain(chain))
        throw runtime_error(string(__func__) + ": writing chain failed");

    hdChain = chain;
}

bool CWallet::LoadHDSeed(const HDSeed& seed)
{
    return CBasicKeyStore::SetHDSeed(seed);
}

bool CWallet::LoadCryptedHDSeed(const uint256& seedFp, const v_uint8& seed)
{
    return CCryptoKeyStore::SetCryptedHDSeed(seedFp, seed);
}

void CWalletTx::SetSaplingNoteData(mapSaplingNoteData_t &noteData)
{
    mapSaplingNoteData.clear();
    for (const auto & [saplingOutPoint, saplingNoteData] : noteData)
    {
        if (saplingOutPoint.n < vShieldedOutput.size())
            mapSaplingNoteData[saplingOutPoint] = saplingNoteData;
        else
            throw logic_error("CWalletTx::SetSaplingNoteData(): Invalid note");
    }
}

optional<pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::DecryptSaplingNote(const Consensus::Params& params, int height, SaplingOutPoint op) const
{
    // Check whether we can decrypt this SaplingOutPoint
    if (this->mapSaplingNoteData.count(op) == 0) {
        return nullopt;
    }

    const auto &output = this->vShieldedOutput[op.n];
    const auto &nd = this->mapSaplingNoteData.at(op);

    auto maybe_pt = SaplingNotePlaintext::decrypt(
//        params,
//        height,
        output.encCiphertext,
        nd.ivk,
        output.ephemeralKey,
        output.cm);
    assert(maybe_pt != nullopt);
    auto notePt = maybe_pt.value();  //-V1007

    auto maybe_pa = nd.ivk.address(notePt.d);
    assert(maybe_pa != nullopt);
    auto pa = maybe_pa.value(); //-V1007

    return make_pair(notePt, pa);
}

optional<pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::DecryptSaplingNoteWithoutLeadByteCheck(SaplingOutPoint op) const
{
    // Check whether we can decrypt this SaplingOutPoint
    if (this->mapSaplingNoteData.count(op) == 0)
        return nullopt;

    auto output = this->vShieldedOutput[op.n];
    auto nd = this->mapSaplingNoteData.at(op);

    auto optDeserialized = SaplingNotePlaintext::attempt_sapling_enc_decryption_deserialization(output.encCiphertext, nd.ivk, output.ephemeralKey);

    // The transaction would not have entered the wallet unless
    // its plaintext had been successfully decrypted previously.
    assert(optDeserialized != nullopt);

    auto maybe_pt = SaplingNotePlaintext::plaintext_checks_without_height(
        *optDeserialized, //-V1007
        nd.ivk,
        output.ephemeralKey,
        output.cm);
    assert(maybe_pt != nullopt);
    auto notePt = maybe_pt.value(); //-V1007

    auto maybe_pa = nd.ivk.address(notePt.d);
    assert(static_cast<bool>(maybe_pa));
    auto pa = maybe_pa.value(); //-V1007

    return make_pair(notePt, pa);
}

optional<pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::RecoverSaplingNote(const Consensus::Params& params, int height, SaplingOutPoint op, set<uint256>& ovks) const
{
    auto output = this->vShieldedOutput[op.n];

    for (auto ovk : ovks) {
        auto outPt = SaplingOutgoingPlaintext::decrypt(
            output.outCiphertext,
            ovk,
            output.cv,
            output.cm,
            output.ephemeralKey);
        if (!outPt) {
            // Try decrypting with the next ovk
            continue;
        }

        auto maybe_pt = SaplingNotePlaintext::decrypt(
//            params,
//            height,
            output.encCiphertext,
            output.ephemeralKey,
            outPt->esk,
            outPt->pk_d,
            output.cm);
        assert(static_cast<bool>(maybe_pt));
        auto notePt = maybe_pt.value(); //-V1007

        return make_pair(notePt, SaplingPaymentAddress(notePt.d, outPt->pk_d));
    }

    // Couldn't recover with any of the provided OutgoingViewingKeys
    return nullopt;
}

optional<pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::RecoverSaplingNoteWithoutLeadByteCheck(SaplingOutPoint op, set<uint256>& ovks) const
{
    auto output = this->vShieldedOutput[op.n];

    for (auto ovk : ovks) {
        auto outPt = SaplingOutgoingPlaintext::decrypt(
            output.outCiphertext,
            ovk,
            output.cv,
            output.cm,
            output.ephemeralKey);
        if (!outPt) {
            // Try decrypting with the next ovk
            continue;
        }

        auto optDeserialized = SaplingNotePlaintext::attempt_sapling_enc_decryption_deserialization(output.encCiphertext, output.ephemeralKey, outPt->esk, outPt->pk_d);

        // The transaction would not have entered the wallet unless
        // its plaintext had been successfully decrypted previously.
        assert(optDeserialized != nullopt);

        auto maybe_pt = SaplingNotePlaintext::plaintext_checks_without_height(
            *optDeserialized, //-V1007
            output.ephemeralKey,
            outPt->esk,
            outPt->pk_d,
            output.cm);
        assert(static_cast<bool>(maybe_pt));
        auto notePt = maybe_pt.value(); //-V1007

        return make_pair(notePt, SaplingPaymentAddress(notePt.d, outPt->pk_d));
    }

    // Couldn't recover with any of the provided OutgoingViewingKeys
    return nullopt;
}

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

int CWalletTx::GetRequestCount() const
{
    // Returns -1 if it wasn't being tracked
    int nRequests = -1;
    {
        LOCK(pwallet->cs_wallet);
        if (IsCoinBase())
        {
            // Generated block
            if (!hashBlock.IsNull())
            {
                auto mi = pwallet->mapRequestCount.find(hashBlock);
                if (mi != pwallet->mapRequestCount.end())
                    nRequests = (*mi).second;
            }
        }
        else
        {
            // Did anyone request this transaction?
            auto mi = pwallet->mapRequestCount.find(GetHash());
            if (mi != pwallet->mapRequestCount.end())
            {
                nRequests = (*mi).second;

                // How about the block it's in?
                if (nRequests == 0 && !hashBlock.IsNull())
                {
                    auto _mi = pwallet->mapRequestCount.find(hashBlock);
                    if (_mi != pwallet->mapRequestCount.end())
                        nRequests = (*_mi).second;
                    else
                        nRequests = 1; // If it's in someone else's block it must have got out
                }
            }
        }
    }
    return nRequests;
}

// GetAmounts will determine the transparent debits and credits for a given wallet tx.
void CWalletTx::GetAmounts(list<COutputEntry>& listReceived,
                           list<COutputEntry>& listSent, CAmount& nFee, string& strSentAccount, const isminetype& filter) const
{
    nFee = 0;
    listReceived.clear();
    listSent.clear();
    strSentAccount = strFromAccount;

    // Is this tx sent/signed by me?
    CAmount nDebit = GetDebit(filter);
    bool isFromMyTaddr = nDebit > 0; // debit>0 means we signed/sent this transaction

    // Compute fee if we sent this transaction.
    if (isFromMyTaddr) {
        CAmount nValueOut = GetValueOut();  // transparent outputs plus all Sprout vpub_old and negative Sapling valueBalance
        CAmount nValueIn = GetShieldedValueIn();
        nFee = nDebit - nValueOut + nValueIn;
    }

    // If we sent utxos from this transaction, create output for value taken from (negative valueBalance)
    // or added (positive valueBalance) to the transparent value pool by Sapling shielding and unshielding.
    if (isFromMyTaddr) {
        if (valueBalance < 0) {
            COutputEntry output = {CNoDestination(), -valueBalance, (int) vout.size()};
            listSent.push_back(output);
        } else if (valueBalance > 0) {
            COutputEntry output = {CNoDestination(), valueBalance, (int) vout.size()};
            listReceived.push_back(output);
        }
    }

    // Sent/received.
    for (unsigned int i = 0; i < vout.size(); ++i)
    {
        const CTxOut& txout = vout[i];
        isminetype fIsMine = pwallet->GetIsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
                continue;
        }
        else if (!IsMineType(fIsMine, filter))
            continue;

        // In either case, we need to get the destination address
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
        {
            LogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",
                     this->GetHash().ToString());
            address = CNoDestination();
        }

        COutputEntry output = {address, txout.nValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (IsMineType(fIsMine, filter))
            listReceived.push_back(output);
    }

}

void CWalletTx::GetAccountAmounts(const string& strAccount, CAmount& nReceived,
                                  CAmount& nSent, CAmount& nFee, const isminetype& filter) const
{
    nReceived = nSent = nFee = 0;

    CAmount allFee;
    string strSentAccount;
    list<COutputEntry> listReceived;
    list<COutputEntry> listSent;
    GetAmounts(listReceived, listSent, allFee, strSentAccount, filter);

    if (strAccount == strSentAccount)
    {
        for (const auto& s : listSent)
            nSent += s.amount;
        nFee = allFee;
    }
    {
        LOCK(pwallet->cs_wallet);
        for (const auto &r : listReceived)
        {
            if (pwallet->mapAddressBook.count(r.destination))
            {
                const auto mi = pwallet->mapAddressBook.find(r.destination);
                if (mi != pwallet->mapAddressBook.cend() && mi->second.name == strAccount)
                    nReceived += r.amount;
            }
            else if (strAccount.empty())
            {
                nReceived += r.amount;
            }
        }
    }
}


bool CWalletTx::WriteToDisk(CWalletDB *pwalletdb)
{
    return pwalletdb->WriteTx(GetHash(), *this);
}

/**
 * Scan the block chain (starting in pindexStart) for transactions
 * from or to us. If fUpdate is true, found transactions that already
 * exist in the wallet will be updated.
 */
int CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate)
{
    int ret = 0;
    int64_t nNow = GetTime();
    const CChainParams& chainParams = Params();

    CBlockIndex* pindex = pindexStart;

    v_uint256 myTxHashes;

    {
        LOCK2(cs_main, cs_wallet);

        // no need to read and scan block, if block was created before
        // our wallet birthday (as adjusted for block time variability)
        while (pindex && nTimeFirstKey && (pindex->GetBlockTime() < (nTimeFirstKey - 7200)))
            pindex = chainActive.Next(pindex);

        ShowProgress(_("Rescanning..."), 0); // show rescan progress in GUI as dialog or on splashscreen, if -rescan on startup
        double dProgressStart = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex, false);
        double dProgressTip = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip(), false);
        while (pindex)
        {
            if (pindex->nHeight % 100 == 0 && dProgressTip - dProgressStart > 0.0)
                ShowProgress(_("Rescanning..."), max(1, min(99, (int)((Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex, false) - dProgressStart) / (dProgressTip - dProgressStart) * 100))));

            CBlock block;
            ReadBlockFromDisk(block, pindex, Params().GetConsensus());
            for (auto& tx : block.vtx)
            {
                if (AddToWalletIfInvolvingMe(tx, &block, fUpdate)) {
                    myTxHashes.push_back(tx.GetHash());
                    ret++;
                }
            }

            SproutMerkleTree sproutTree;
            SaplingMerkleTree saplingTree;
            // This should never fail: we should always be able to get the tree
            // state on the path to the tip of our chain
            assert(pcoinsTip->GetSproutAnchorAt(pindex->hashSproutAnchor, sproutTree));
            if (pindex->pprev)
            {
                if (NetworkUpgradeActive(pindex->pprev->nHeight, Params().GetConsensus(), Consensus::UpgradeIndex::UPGRADE_SAPLING)) {
                    assert(pcoinsTip->GetSaplingAnchorAt(pindex->pprev->hashFinalSaplingRoot, saplingTree));
                }
            }
            // Increment note witness caches
            ChainTip(pindex, &block, saplingTree, true);

            pindex = chainActive.Next(pindex);
            if (GetTime() >= nNow + 60) {
                nNow = GetTime();
                LogPrintf("Still rescanning. At block %d. Progress=%f\n", pindex->nHeight, Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex));
            }
        }

        // After rescanning, persist Sapling note data that might have changed, e.g. nullifiers.
        // Do not flush the wallet here for performance reasons.
        CWalletDB walletdb(strWalletFile, "r+", false);
        for (auto hash : myTxHashes) {
            CWalletTx wtx = mapWallet[hash];
            if (!wtx.mapSaplingNoteData.empty()) {
                if (!wtx.WriteToDisk(&walletdb)) {
                    LogPrintf("Rescanning... WriteToDisk failed to update Sapling note data for: %s\n", hash.ToString());
                }
            }
        }

        ShowProgress(_("Rescanning..."), 100); // hide progress dialog in GUI
    }
    return ret;
}

void CWallet::ReacceptWalletTransactions()
{
    // If transactions aren't being broadcasted, don't let them into local mempool either
    if (!fBroadcastTransactions)
        return;
    LOCK2(cs_main, cs_wallet);
    unordered_map<int64_t, CWalletTx*> mapSorted;

    // Sort pending wallet transactions based on their initial wallet insertion order
    for (auto &[txId, wtx] : mapWallet)
    {
        assert(wtx.GetHash() == txId);

        const int nDepth = wtx.GetDepthInMainChain();
        if (!wtx.IsCoinBase() && nDepth < 0)
            mapSorted.emplace(wtx.nOrderPos, &wtx);
    }

    // Try to add wallet transactions to memory pool
    for (auto& [txId, pTx] : mapSorted)
    {
        LOCK(mempool.cs);
        pTx->AcceptToMemoryPool(false);
    }
}

bool CWalletTx::RelayWalletTransaction()
{
    assert(pwallet->GetBroadcastTransactions());
    if (!IsCoinBase())
    {
        if (GetDepthInMainChain() == 0) {
            LogPrintf("Relaying wtx %s\n", GetHash().ToString());
            RelayTransaction((CTransaction)*this);
            return true;
        }
    }
    return false;
}

set<uint256> CWalletTx::GetConflicts() const
{
    set<uint256> result;
    if (pwallet)
    {
        uint256 myHash = GetHash();
        result = pwallet->GetConflicts(myHash);
        result.erase(myHash);
    }
    return result;
}

CAmount CWalletTx::GetDebit(const isminetype& filter) const
{
    if (vin.empty())
        return 0;

    CAmount debit = 0;
    if(IsMineSpendable(filter))
    {
        if (fDebitCached)
            debit += nDebitCached;
        else
        {
            nDebitCached = pwallet->GetDebit(*this, isminetype::SPENDABLE);
            fDebitCached = true;
            debit += nDebitCached;
        }
    }
    if (IsMineWatchOnly(filter))
    {
        if(fWatchDebitCached)
            debit += nWatchDebitCached;
        else
        {
            nWatchDebitCached = pwallet->GetDebit(*this, isminetype::WATCH_ONLY);
            fWatchDebitCached = true;
            debit += nWatchDebitCached;
        }
    }
    return debit;
}

CAmount CWalletTx::GetCredit(const isminetype& filter) const
{
    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    int64_t credit = 0;
    if (IsMineSpendable(filter))
    {
        // GetBalance can assume transactions in mapWallet won't change
        if (fCreditCached)
            credit += nCreditCached;
        else
        {
            nCreditCached = pwallet->GetCredit(*this, isminetype::SPENDABLE);
            fCreditCached = true;
            credit += nCreditCached;
        }
    }
    if (IsMineWatchOnly(filter))
    {
        if (fWatchCreditCached)
            credit += nWatchCreditCached;
        else
        {
            nWatchCreditCached = pwallet->GetCredit(*this, isminetype::WATCH_ONLY);
            fWatchCreditCached = true;
            credit += nWatchCreditCached;
        }
    }
    return credit;
}

CAmount CWalletTx::GetImmatureCredit(const bool fUseCache) const
{
    if (IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
    {
        if (fUseCache && fImmatureCreditCached)
            return nImmatureCreditCached;
        nImmatureCreditCached = pwallet->GetCredit(*this, isminetype::SPENDABLE);
        fImmatureCreditCached = true;
        return nImmatureCreditCached;
    }

    return 0;
}

CAmount CWalletTx::GetAvailableCredit(const bool fUseCache) const
{
    if (pwallet == 0)
        return 0;

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    if (fUseCache && fAvailableCreditCached)
        return nAvailableCreditCached;

    CAmount nCredit = 0;
    uint256 hashTx = GetHash();
    for (unsigned int i = 0; i < vout.size(); i++)
    {
        if (!pwallet->IsSpent(hashTx, i))
        {
            const CTxOut &txOut = vout[i];
            nCredit += pwallet->GetCredit(txOut, isminetype::SPENDABLE);
            if (!MoneyRange(nCredit))
                throw runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
        }
    }

    nAvailableCreditCached = nCredit;
    fAvailableCreditCached = true;
    return nCredit;
}

CAmount CWalletTx::GetImmatureWatchOnlyCredit(const bool& fUseCache) const
{
    if (IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
    {
        if (fUseCache && fImmatureWatchCreditCached)
            return nImmatureWatchCreditCached;
        nImmatureWatchCreditCached = pwallet->GetCredit(*this, isminetype::WATCH_ONLY);
        fImmatureWatchCreditCached = true;
        return nImmatureWatchCreditCached;
    }

    return 0;
}

CAmount CWalletTx::GetAvailableWatchOnlyCredit(const bool& fUseCache) const
{
    if (pwallet == 0)
        return 0;

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    if (fUseCache && fAvailableWatchCreditCached)
        return nAvailableWatchCreditCached;

    CAmount nCredit = 0;
    for (unsigned int i = 0; i < vout.size(); i++)
    {
        if (!pwallet->IsSpent(GetHash(), i))
        {
            const CTxOut &txout = vout[i];
            nCredit += pwallet->GetCredit(txout, isminetype::WATCH_ONLY);
            if (!MoneyRange(nCredit))
                throw runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
        }
    }

    nAvailableWatchCreditCached = nCredit;
    fAvailableWatchCreditCached = true;
    return nCredit;
}

CAmount CWalletTx::GetChange() const
{
    if (fChangeCached)
        return nChangeCached;
    nChangeCached = pwallet->GetChange(*this);
    fChangeCached = true;
    return nChangeCached;
}

bool CWalletTx::IsTrusted() const
{
    // Quick answer in most cases
    if (!CheckFinalTx(*this))
        return false;
    const int nDepth = GetDepthInMainChain();
    if (nDepth >= 1)
        return true;
    if (nDepth < 0)
        return false;
    if (!bSpendZeroConfChange || !IsFromMe(isminetype::ALL)) // using wtx's cached debit
        return false;

    // Trusted if all inputs are from us and are in the mempool:
    for (const auto& txin : vin)
    {
        // Transactions not sent by us: not trusted
        const CWalletTx* parent = pwallet->GetWalletTx(txin.prevout.hash);
        if (!parent)
            return false;
        const CTxOut& parentOut = parent->vout[txin.prevout.n];
        if (pwallet->GetIsMine(parentOut) != isminetype::SPENDABLE)
            return false;
    }
    return true;
}

v_uint256 CWallet::ResendWalletTransactionsBefore(int64_t nTime)
{
    v_uint256 result;

    LOCK(cs_wallet);
    // Sort them in chronological order
    multimap<unsigned int, CWalletTx*> mapSorted;
    for (auto &[hash, wtx] : mapWallet)
    {
        // Don't rebroadcast if newer than nTime:
        if (wtx.nTimeReceived > nTime)
            continue;
        mapSorted.insert(make_pair(wtx.nTimeReceived, &wtx));
    }
    for (auto& [nTimeReceived, pWalletTx] : mapSorted)
    {
        if (pWalletTx->RelayWalletTransaction())
            result.push_back(pWalletTx->GetHash());
    }
    return result;
}

void CWallet::ResendWalletTransactions(int64_t nBestBlockTime)
{
    // Do this infrequently and randomly to avoid giving away
    // that these are our transactions.
    if (GetTime() < nNextResend || !fBroadcastTransactions)
        return;
    bool fFirst = (nNextResend == 0);
    nNextResend = GetTime() + GetRand(30 * 60);
    if (fFirst)
        return;

    // Only do it if there's been a new block since last time
    if (nBestBlockTime < nLastResend)
        return;
    nLastResend = GetTime();

    // Rebroadcast unconfirmed txes older than 5 minutes before the last
    // block was found:
    v_uint256 relayed = ResendWalletTransactionsBefore(nBestBlockTime - 5 * 60);
    if (!relayed.empty())
        LogPrintf("%s: rebroadcast %u unconfirmed transactions\n", __func__, relayed.size());
}

/** @} */ // end of mapWallet




/** @defgroup Actions
 *
 * @{
 */


CAmount CWallet::GetBalance() const
{
    CAmount nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (const auto &[txid, coin] : mapWallet)
    {
        if (coin.IsTrusted())
            nTotal += coin.GetAvailableCredit();
    }
    return nTotal;
}

CAmount CWallet::GetUnconfirmedBalance() const
{
    CAmount nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (const auto& [txid, coin] : mapWallet)
    {
        if (!CheckFinalTx(coin) || (!coin.IsTrusted() && coin.GetDepthInMainChain() == 0))
            nTotal += coin.GetAvailableCredit();
    }
    return nTotal;
}

CAmount CWallet::GetImmatureBalance() const
{
    CAmount nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (const auto& [txid, coin] : mapWallet)
        nTotal += coin.GetImmatureCredit();
    return nTotal;
}

CAmount CWallet::GetWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (const auto& [txid, coin] : mapWallet)
    {
        if (coin.IsTrusted())
            nTotal += coin.GetAvailableWatchOnlyCredit();
    }
    return nTotal;
}

CAmount CWallet::GetUnconfirmedWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (const auto& [txid, coin] : mapWallet)
    {
        if (!CheckFinalTx(coin) || (!coin.IsTrusted() && coin.GetDepthInMainChain() == 0))
            nTotal += coin.GetAvailableWatchOnlyCredit();
    }
    return nTotal;
}

CAmount CWallet::GetImmatureWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (const auto& [txid, coin] : mapWallet)
        nTotal += coin.GetImmatureWatchOnlyCredit();
    return nTotal;
}

/**
 * populate vCoins with vector of available COutputs.
 */
void CWallet::AvailableCoins(vector<COutput>& vCoins, bool fOnlyConfirmed, const CCoinControl *pCoinControl, bool fIncludeZeroValue, bool fIncludeCoinBase, int exactCoins, bool fIncludeLocked) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (const auto& [txid, wtx] : mapWallet)
        {
            if (!CheckFinalTx(wtx))
                continue;

            if (fOnlyConfirmed && !wtx.IsTrusted())
                continue;

            if (wtx.IsCoinBase() && !fIncludeCoinBase)
                continue;

            if (wtx.IsCoinBase() && wtx.GetBlocksToMaturity() > 0)
                continue;

            const int nDepth = wtx.GetDepthInMainChain();
            if (nDepth < 0)
                continue;

            for (unsigned int i = 0; i < wtx.vout.size(); i++)
            {
                const auto& txOut = wtx.vout[i];
                // exact coins check
                if (exactCoins != 0 && txOut.nValue != exactCoins * COIN)
                    continue;

                // check if mine transaction (none, spendable or watch-only)
                const isminetype mine = GetIsMine(txOut);
                if (mine == isminetype::NO)
                    continue;
                // check if output already spent
                if (IsSpent(txid, i))
                    continue;
                // check if coin is locked and skip flag is not specified
                if (IsLockedCoin(txid, i) && !fIncludeLocked)
                    continue;
                // check if tx value is not positive and skip flag is not specified
                if (txOut.nValue <= 0 && !fIncludeZeroValue)
                    continue;
                // coin control object is used and has some tx outputs to use as a filter
                if (pCoinControl && pCoinControl->HasSelected())
                {
                    if (!pCoinControl->fAllowOtherInputs && !pCoinControl->IsSelected(txid, i))
                        continue;
                }
                vCoins.emplace_back(&wtx, i, nDepth, IsMineSpendable(mine));
            }
        }
    }
}

static void ApproximateBestSubset(vector<pair<CAmount, pair<const CWalletTx*,unsigned int> > >vValue, const CAmount& nTotalLower, const CAmount& nTargetValue,
                                  vector<char>& vfBest, CAmount& nBest, int iterations = 1000)
{
    vector<char> vfIncluded;

    vfBest.assign(vValue.size(), true);
    nBest = nTotalLower;

    seed_insecure_rand();

    for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++)
    {
        vfIncluded.assign(vValue.size(), false);
        CAmount nTotal = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < vValue.size(); i++)
            {
                //The solver here uses a randomized algorithm,
                //the randomness serves no real security purpose but is just
                //needed to prevent degenerate behavior and it is important
                //that the rng is fast. We do not use a constant random sequence,
                //because there may be some privacy improvement by making
                //the selection random.
                if (nPass == 0 ? insecure_rand()&1 : !vfIncluded[i])
                {
                    nTotal += vValue[i].first;
                    vfIncluded[i] = true;
                    if (nTotal >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (nTotal < nBest)
                        {
                            nBest = nTotal;
                            vfBest = vfIncluded;
                        }
                        nTotal -= vValue[i].first;
                        vfIncluded[i] = false;
                    }
                }
            }
        }
    }
}

bool CWallet::SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, vector<COutput> vCoins,
                                 set<pair<const CWalletTx*,unsigned int> >& setCoinsRet, CAmount& nValueRet) const
{
    setCoinsRet.clear();
    nValueRet = 0;

    // List of values less than target
    pair<CAmount, pair<const CWalletTx*,unsigned int> > coinLowestLarger;
    coinLowestLarger.first = numeric_limits<CAmount>::max();
    coinLowestLarger.second.first = nullptr;
    vector<pair<CAmount, pair<const CWalletTx*,unsigned int> > > vValue;
    CAmount nTotalLower = 0;

    random_device rd;
    mt19937 g(rd());
    shuffle(vCoins.begin(), vCoins.end(), g);

    for (const auto &output : vCoins)
    {
        if (!output.fSpendable)
            continue;

        const CWalletTx *pcoin = output.tx;

        if (output.nDepth < (pcoin->IsFromMe(isminetype::ALL) ? nConfMine : nConfTheirs))
            continue;

        int i = output.i;
        CAmount n = pcoin->vout[i].nValue;

        auto coin = make_pair(n,make_pair(pcoin, i));

        if (n == nTargetValue)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            return true;
        }
        else if (n < nTargetValue + CENT)
        {
            vValue.push_back(coin);
            nTotalLower += n;
        }
        else if (n < coinLowestLarger.first)
        {
            coinLowestLarger = coin;
        }
    }

    if (nTotalLower == nTargetValue)
    {
        for (unsigned int i = 0; i < vValue.size(); ++i)
        {
            setCoinsRet.insert(vValue[i].second);
            nValueRet += vValue[i].first;
        }
        return true;
    }

    if (nTotalLower < nTargetValue)
    {
        if (!coinLowestLarger.second.first)
            return false;
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
        return true;
    }

    // Solve subset sum by stochastic approximation
    sort(vValue.rbegin(), vValue.rend(), CompareValueOnly());
    vector<char> vfBest;
    CAmount nBest;

    ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest, 1000);
    if (nBest != nTargetValue && nTotalLower >= nTargetValue + CENT)
        ApproximateBestSubset(vValue, nTotalLower, nTargetValue + CENT, vfBest, nBest, 1000);

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    if (coinLowestLarger.second.first &&
        ((nBest != nTargetValue && nBest < nTargetValue + CENT) || coinLowestLarger.first <= nBest))
    {
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
    }
    else {
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
            {
                setCoinsRet.insert(vValue[i].second);
                nValueRet += vValue[i].first;
            }

        LogPrint("selectcoins", "SelectCoins() best subset: ");
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
                LogPrint("selectcoins", "%s ", FormatMoney(vValue[i].first));
        LogPrint("selectcoins", "total %s\n", FormatMoney(nBest));
    }

    return true;
}

bool CWallet::SelectCoins(const CAmount& nTargetValue, set<pair<const CWalletTx*,unsigned int> >& setCoinsRet, CAmount& nValueRet,  bool& fOnlyCoinbaseCoinsRet, bool& fNeedCoinbaseCoinsRet, const CCoinControl* coinControl) const
{
    // Output parameter fOnlyCoinbaseCoinsRet is set to true when the only available coins are coinbase utxos.
    vector<COutput> vCoinsNoCoinbase, vCoinsWithCoinbase;
    AvailableCoins(vCoinsNoCoinbase, true, coinControl, false, false);
    AvailableCoins(vCoinsWithCoinbase, true, coinControl, false, true);
    fOnlyCoinbaseCoinsRet = vCoinsNoCoinbase.size() == 0 && vCoinsWithCoinbase.size() > 0;

    vector<COutput> vCoins = vCoinsWithCoinbase;

    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs)
    {
        for (const auto& out : vCoins)
        {
            if (!out.fSpendable)
                 continue;
            nValueRet += out.tx->vout[out.i].nValue;
            setCoinsRet.insert(make_pair(out.tx, out.i));
        }
        return (nValueRet >= nTargetValue);
    }

    // calculate value from preset inputs and store them
    set<pair<const CWalletTx*, uint32_t> > setPresetCoins;
    CAmount nValueFromPresetInputs = 0;

    vector<COutPoint> vPresetInputs;
    if (coinControl)
        coinControl->ListSelected(vPresetInputs);
    for (const auto& outpoint : vPresetInputs)
    {
        auto it = mapWallet.find(outpoint.hash);
        if (it != mapWallet.end())
        {
            const CWalletTx* pcoin = &it->second;
            // Clearly invalid input, fail
            if (pcoin->vout.size() <= outpoint.n)
                return false;
            nValueFromPresetInputs += pcoin->vout[outpoint.n].nValue;
            setPresetCoins.insert(make_pair(pcoin, outpoint.n));
        } else
            return false; // TODO: Allow non-wallet inputs
    }

    // remove preset inputs from vCoins
    for (auto it = vCoins.begin(); it != vCoins.end() && coinControl && coinControl->HasSelected();)
    {
        if (setPresetCoins.count(make_pair(it->tx, it->i)))
            it = vCoins.erase(it);
        else
            ++it;
    }

    bool res = nTargetValue <= nValueFromPresetInputs ||
        SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, 1, 6, vCoins, setCoinsRet, nValueRet) ||
        SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, 1, 1, vCoins, setCoinsRet, nValueRet) ||
        (bSpendZeroConfChange && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, 0, 1, vCoins, setCoinsRet, nValueRet));

    // because SelectCoinsMinConf clears the setCoinsRet, we now add the possible inputs to the coinset
    setCoinsRet.insert(setPresetCoins.begin(), setPresetCoins.end());

    // add preset inputs to the total value selected
    nValueRet += nValueFromPresetInputs;

    return res;
}

bool CWallet::FundTransaction(CMutableTransaction& tx, CAmount &nFeeRet, int& nChangePosRet, string& strFailReason)
{
    // Turn the txout set into a CRecipient vector
    vector<CRecipient> vecSend;
    vecSend.reserve(tx.vout.size());
    for (const auto& txOut : tx.vout)
        vecSend.emplace_back(txOut.scriptPubKey, txOut.nValue, false);

    CCoinControl coinControl;
    coinControl.fAllowOtherInputs = true;
    for (const auto& txin : tx.vin)
        coinControl.Select(txin.prevout);

    CReserveKey reservekey(this);
    CWalletTx wtx;

    if (!CreateTransaction(vecSend, wtx, reservekey, nFeeRet, nChangePosRet, strFailReason, &coinControl, false))
        return false;

    if (nChangePosRet != -1)
        tx.vout.insert(tx.vout.begin() + nChangePosRet, wtx.vout[nChangePosRet]);

    // Add new txins (keeping original txin scriptSig/order)
    for (const auto& txin : wtx.vin)
    {
        bool found = false;
        for (const auto& origTxIn : tx.vin)
        {
            if (txin.prevout.hash == origTxIn.prevout.hash && txin.prevout.n == origTxIn.prevout.n)
            {
                found = true;
                break;
            }
        }
        if (!found)
            tx.vin.push_back(txin);
    }

    return true;
}

bool CWallet::CreateTransaction(const vector<CRecipient>& vecSend, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRet,
                                int& nChangePosRet, string& strFailReason, const CCoinControl* coinControl, bool sign)
{
    CAmount nValue = 0;
    unsigned int nSubtractFeeFromAmount = 0;
    for (const auto &recipient : vecSend)
    {
        if (nValue < 0 || recipient.nAmount < 0)
        {
            strFailReason = _("Transaction amounts must be positive");
            return false;
        }
        nValue += recipient.nAmount;

        if (recipient.fSubtractFeeFromAmount)
            nSubtractFeeFromAmount++;
    }
    if (vecSend.empty() || nValue < 0)
    {
        strFailReason = _("Transaction amounts must be positive");
        return false;
    }

    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.BindWallet(this);

    const auto &consensus = Params().GetConsensus();

    LOCK(cs_main);
    const uint32_t nextBlockHeight = chainActive.Height() + 1;
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensus, nextBlockHeight);

    // Activates after Overwinter network upgrade
    if (NetworkUpgradeActive(nextBlockHeight, consensus, Consensus::UpgradeIndex::UPGRADE_OVERWINTER)) {
        if (txNew.nExpiryHeight >= TX_EXPIRY_HEIGHT_THRESHOLD){
            strFailReason = _("nExpiryHeight must be less than TX_EXPIRY_HEIGHT_THRESHOLD.");
            return false;
        }
    }

    unsigned int max_tx_size = MAX_TX_SIZE_AFTER_SAPLING;
    if (!NetworkUpgradeActive(nextBlockHeight, consensus, Consensus::UpgradeIndex::UPGRADE_SAPLING))
        max_tx_size = MAX_TX_SIZE_BEFORE_SAPLING;

    // Discourage fee sniping.
    //
    // However because of a off-by-one-error in previous versions we need to
    // neuter it by setting nLockTime to at least one less than nBestHeight.
    // Secondly currently propagation of transactions created for block heights
    // corresponding to blocks that were just mined may be iffy - transactions
    // aren't re-accepted into the mempool - we additionally neuter the code by
    // going ten blocks back. Doesn't yet do anything for sniping, but does act
    // to shake out wallet bugs like not showing nLockTime'd transactions at
    // all.
    txNew.nLockTime = max(0, chainActive.Height() - 10);

    // Secondly occasionally randomly pick a nLockTime even further back, so
    // that transactions that are delayed after signing for whatever reason,
    // e.g. high-latency mix networks and some CoinJoin implementations, have
    // better privacy.
    if (GetRandInt(10) == 0)
        txNew.nLockTime = max(0, (int)txNew.nLockTime - GetRandInt(100));

    assert(txNew.nLockTime <= (unsigned int)chainActive.Height());
    assert(txNew.nLockTime < LOCKTIME_THRESHOLD);

    {
        // cs_main already taken above
        LOCK(cs_wallet);
        {
            nFeeRet = 0;
            // Start with no fee and loop until there is enough fee
            while (true)
            {
                txNew.vin.clear();
                txNew.vout.clear();
                wtxNew.fFromMe = true;
                nChangePosRet = -1;
                bool fFirst = true;

                CAmount nTotalValue = nValue;
                if (nSubtractFeeFromAmount == 0)
                    nTotalValue += nFeeRet;
                double dPriority = 0;
                // vouts to the payees
                for (const auto& recipient : vecSend)
                {
                    CTxOut txout(recipient.nAmount, recipient.scriptPubKey);

                    if (recipient.fSubtractFeeFromAmount)
                    {
                        txout.nValue -= nFeeRet / nSubtractFeeFromAmount; // Subtract fee equally from each selected recipient

                        if (fFirst) // first receiver pays the remainder not divisible by output count
                        {
                            fFirst = false;
                            txout.nValue -= nFeeRet % nSubtractFeeFromAmount;
                        }
                    }

                    if (txout.IsDust(::minRelayTxFee))
                    {
                        if (recipient.fSubtractFeeFromAmount && nFeeRet > 0)
                        {
                            if (txout.nValue < 0)
                                strFailReason = _("The transaction amount is too small to pay the fee");
                            else
                                strFailReason = _("The transaction amount is too small to send after the fee has been deducted");
                        }
                        else
                            strFailReason = _("Transaction amount too small");
                        return false;
                    }
                    txNew.vout.push_back(txout);
                }

                // Choose coins to use
                set<pair<const CWalletTx*,unsigned int> > setCoins;
                CAmount nValueIn = 0;
                bool fOnlyCoinbaseCoins = false;
                bool fNeedCoinbaseCoins = false;
                if (!SelectCoins(nTotalValue, setCoins, nValueIn, fOnlyCoinbaseCoins, fNeedCoinbaseCoins, coinControl))
                {
                    strFailReason = _("Insufficient funds");
                    return false;
                }
                for (const auto &[pTx, nOut] : setCoins)
                {
                    const CAmount nCredit = pTx->vout[nOut].nValue;
                    //The coin age after the next block (depth+1) is used instead of the current,
                    //reflecting an assumption the user would accept a bit more delay for
                    //a chance at a free transaction.
                    //But mempool inputs might still be in the mempool, so their age stays 0
                    int age = pTx->GetDepthInMainChain();
                    if (age != 0)
                        age += 1;
                    dPriority += (double)nCredit * age;
                }

                CAmount nChange = nValueIn - nValue;
                if (nSubtractFeeFromAmount == 0)
                    nChange -= nFeeRet;

                if (nChange > 0)
                {
                    // Fill a vout to ourself
                    // TODO: pass in scriptChange instead of reservekey so
                    // change transaction isn't always pay-to-bitcoin-address
                    CScript scriptChange;

                    // coin control: send change to custom address
                    if (coinControl && !get_if<CNoDestination>(&coinControl->destChange))
                        scriptChange = GetScriptForDestination(coinControl->destChange);

                    // no coin control: send change to newly generated address
                    else
                    {
                        // Note: We use a new key here to keep it from being obvious which side is the change.
                        //  The drawback is that by not reusing a previous key, the change may be lost if a
                        //  backup is restored, if the backup doesn't have the new private key for the change.
                        //  If we reused the old key, it would be possible to add code to look for and
                        //  rediscover unknown transactions that were written with keys of ours to recover
                        //  post-backup change.

                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        bool ret;
                        ret = reservekey.GetReservedKey(vchPubKey);
                        assert(ret); // should never fail, as we just unlocked

                        scriptChange = GetScriptForDestination(vchPubKey.GetID());
                    }

                    CTxOut newTxOut(nChange, scriptChange);

                    // We do not move dust-change to fees, because the sender would end up paying more than requested.
                    // This would be against the purpose of the all-inclusive feature.
                    // So instead we raise the change and deduct from the recipient.
                    if (nSubtractFeeFromAmount > 0 && newTxOut.IsDust(::minRelayTxFee))
                    {
                        CAmount nDust = newTxOut.GetDustThreshold(::minRelayTxFee) - newTxOut.nValue;
                        newTxOut.nValue += nDust; // raise change until no more dust
                        for (unsigned int i = 0; i < vecSend.size(); i++) // subtract from first recipient
                        {
                            if (vecSend[i].fSubtractFeeFromAmount)
                            {
                                txNew.vout[i].nValue -= nDust;
                                if (txNew.vout[i].IsDust(::minRelayTxFee))
                                {
                                    strFailReason = _("The transaction amount is too small to send after the fee has been deducted");
                                    return false;
                                }
                                break;
                            }
                        }
                    }

                    // Never create dust outputs; if we would, just
                    // add the dust to the fee.
                    if (newTxOut.IsDust(::minRelayTxFee))
                    {
                        nFeeRet += nChange;
                        reservekey.ReturnKey();
                    }
                    else
                    {
                        // Insert change txn at random position:
                        nChangePosRet = GetRandInt(txNew.vout.size()+1);
                        const auto position = txNew.vout.cbegin() + nChangePosRet;
                        txNew.vout.insert(position, newTxOut);
                    }
                }
                else
                    reservekey.ReturnKey();

                // Fill vin
                //
                // Note how the sequence number is set to max()-1 so that the
                // nLockTime set above actually works.
                for (const auto &[pTx, nOut] : setCoins)
                    txNew.vin.emplace_back(pTx->GetHash(), nOut, CScript(), numeric_limits<unsigned int>::max() - 1);

                // Grab the current consensus branch ID
                auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, consensus);

                // Sign
                int nIn = 0;
                CTransaction txNewConst(txNew);
                for (const auto& [pTx, nOut] : setCoins)
                {
                    bool signSuccess;
                    const CScript& scriptPubKey = pTx->vout[nOut].scriptPubKey;
                    SignatureData sigdata;
                    if (sign)
                        signSuccess = ProduceSignature(TransactionSignatureCreator(this, &txNewConst, nIn, pTx->vout[nOut].nValue, to_integral_type(SIGHASH::ALL)), scriptPubKey, sigdata, consensusBranchId);
                    else
                        signSuccess = ProduceSignature(DummySignatureCreator(this), scriptPubKey, sigdata, consensusBranchId);

                    if (!signSuccess)
                    {
                        strFailReason = _("Signing transaction failed");
                        return false;
                    } else {
                        UpdateTransaction(txNew, nIn, sigdata);
                    }

                    nIn++;
                }

                unsigned int nBytes = static_cast<unsigned int>(::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION));

                // Remove scriptSigs if we used dummy signatures for fee calculation
                if (!sign)
                {
                    for (auto& vin : txNew.vin)
                        vin.scriptSig = CScript();
                }

                // Embed the constructed transaction data in wtxNew.
                *static_cast<CTransaction*>(&wtxNew) = CTransaction(txNew);

                // Limit size
                if (nBytes >= max_tx_size)
                {
                    strFailReason = _("Transaction too large");
                    return false;
                }

                dPriority = wtxNew.ComputePriority(dPriority, nBytes);

                // Can we complete this as a free transaction?
                if (fSendFreeTransactions && nBytes <= MAX_FREE_TRANSACTION_CREATE_SIZE)
                {
                    // Not enough fee: enough priority?
                    double dPriorityNeeded = mempool.estimatePriority(nTxConfirmTarget);
                    // Not enough mempool history to estimate: use hard-coded AllowFree.
                    if (dPriorityNeeded <= 0 && AllowFree(dPriority))
                        break;

                    // Small enough, and priority high enough, to send for free
                    if (dPriorityNeeded > 0 && dPriority >= dPriorityNeeded)
                        break;
                }

                CAmount nFeeNeeded = GetMinimumFee(nBytes, nTxConfirmTarget, mempool);

                // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
                // because we must be at the maximum allowed fee.
                if (nFeeNeeded < ::minRelayTxFee.GetFee(nBytes))
                {
                    strFailReason = strprintf("Transaction too large for fee policy: fee needed = %s; minRelayTxFee for %d bytes is set to %s",
                                              FormatMoney(nFeeNeeded), nBytes, FormatMoney(::minRelayTxFee.GetFee(nBytes)));
                    return false;
                }

                if (nFeeRet >= nFeeNeeded)
                    break; // Done, enough fee included.

                // Include more fee and try again.
                nFeeRet = nFeeNeeded;
                continue;
            }
        }
    }

    return true;
}

/**
 * Call after CreateTransaction unless you want to abort
 */
bool CWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey)
{
    {
        LOCK2(cs_main, cs_wallet);
        LogPrintf("CommitTransaction:\n%s", wtxNew.ToString());
        {
            // This is only to keep the database open to defeat the auto-flush for the
            // duration of this scope.  This is the only place where this optimization
            // maybe makes sense; please don't do it anywhere else.
            CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(strWalletFile,"r+") : nullptr;

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew, false, pwalletdb);

            // Notify that old coins are spent
            set<CWalletTx*> setCoins;
            for (const auto& txin : wtxNew.vin)
            {
                CWalletTx &coin = mapWallet[txin.prevout.hash];
                coin.BindWallet(this);
                NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
            }

            if (fFileBacked)
                delete pwalletdb;
        }

        // Track how many getdata requests our transaction gets
        mapRequestCount[wtxNew.GetHash()] = 0;

        if (fBroadcastTransactions)
        {
            // Broadcast
            if (!wtxNew.AcceptToMemoryPool(false))
            {
                // This must not fail. The transaction has already been signed and recorded.
                LogPrintf("CommitTransaction(): Error: Transaction not valid\n");
                return false;
            }
            wtxNew.RelayWalletTransaction();
        }
    }
    return true;
}

CAmount CWallet::GetMinimumFee(unsigned int nTxBytes, unsigned int nConfirmTarget, const CTxMemPool& pool)
{
    // payTxFee is user-set "I want to pay this much"
    CAmount nFeeNeeded = payTxFee.GetFee(nTxBytes);
    // user selected total at least (default=true)
    if (fPayAtLeastCustomFee && nFeeNeeded > 0 && nFeeNeeded < payTxFee.GetFeePerK())
        nFeeNeeded = payTxFee.GetFeePerK();
    // User didn't set: use -txconfirmtarget to estimate...
    if (nFeeNeeded == 0)
        nFeeNeeded = pool.estimateFee(nConfirmTarget).GetFee(nTxBytes);
    // ... unless we don't have enough mempool data, in which case fall
    // back to a hard-coded fee
    if (nFeeNeeded == 0)
        nFeeNeeded = minTxFee.GetFee(nTxBytes);
    // prevent user from paying a non-sense fee (like 1 patoshi): 0 < fee < minRelayFee
    if (nFeeNeeded < ::minRelayTxFee.GetFee(nTxBytes))
        nFeeNeeded = ::minRelayTxFee.GetFee(nTxBytes);
    // But always obey the maximum
    if (nFeeNeeded > maxTxFee)
        nFeeNeeded = maxTxFee;
    return nFeeNeeded;
}




DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
    if (!fFileBacked)
        return DB_LOAD_OK;

    LOCK2(cs_main, cs_wallet);

    fFirstRunRet = false;
    DBErrors nLoadWalletRet = CWalletDB(strWalletFile,"cr+").LoadWallet(this);
    if (nLoadWalletRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;
    fFirstRunRet = !vchDefaultKey.IsValid();

    uiInterface.LoadWallet(this);

    return DB_LOAD_OK;
}


DBErrors CWallet::ZapWalletTx(vector<CWalletTx>& vWtx)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    DBErrors nZapWalletTxRet = CWalletDB(strWalletFile,"cr+").ZapWalletTx(this, vWtx);
    if (nZapWalletTxRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nZapWalletTxRet != DB_LOAD_OK)
        return nZapWalletTxRet;

    return DB_LOAD_OK;
}


bool CWallet::SetAddressBook(const CTxDestination& address, const string& strName, const string& strPurpose)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet); // mapAddressBook
        auto mi = mapAddressBook.find(address);
        fUpdated = mi != mapAddressBook.end();
        mapAddressBook[address].name = strName;
        if (!strPurpose.empty()) /* update purpose only if requested */
            mapAddressBook[address].purpose = strPurpose;
    }
    NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address),
                             strPurpose, (fUpdated ? CT_UPDATED : CT_NEW) );
    KeyIO keyIO(Params());
    if (!fFileBacked)
        return false;
    if (!strPurpose.empty() && !CWalletDB(strWalletFile).WritePurpose(keyIO.EncodeDestination(address), strPurpose))
        return false;
    return CWalletDB(strWalletFile).WriteName(keyIO.EncodeDestination(address), strName);
}

bool CWallet::DelAddressBook(const CTxDestination& address)
{
    KeyIO keyIO(Params());
    {
        LOCK(cs_wallet); // mapAddressBook

        if(fFileBacked)
        {
            // Delete destdata tuples associated with address
            string strAddress = keyIO.EncodeDestination(address);
            for (const auto &[key, value] : mapAddressBook[address].destdata)
                CWalletDB(strWalletFile).EraseDestData(strAddress, key);
        }
        mapAddressBook.erase(address);
    }

    NotifyAddressBookChanged(this, address, "", ::IsMine(*this, address), "", CT_DELETED);

    if (!fFileBacked)
        return false;
    CWalletDB(strWalletFile).ErasePurpose(keyIO.EncodeDestination(address));
    return CWalletDB(strWalletFile).EraseName(keyIO.EncodeDestination(address));
}

bool CWallet::SetDefaultKey(const CPubKey &vchPubKey)
{
    if (fFileBacked)
    {
        if (!CWalletDB(strWalletFile).WriteDefaultKey(vchPubKey))
            return false;
    }
    vchDefaultKey = vchPubKey;
    return true;
}

/**
 * Mark old keypool keys as used,
 * and generate all new keys
 */
bool CWallet::NewKeyPool()
{
    {
        LOCK(cs_wallet);
        CWalletDB walletdb(strWalletFile);
        for (const auto & nIndex : setKeyPool)
            walletdb.ErasePool(nIndex);
        setKeyPool.clear();

        if (IsLocked())
            return false;

        int64_t nKeys = max(GetArg("-keypool", 100), (int64_t)0);
        for (int i = 0; i < nKeys; i++)
        {
            int64_t nIndex = i+1;
            walletdb.WritePool(nIndex, CKeyPool(GenerateNewKey()));
            setKeyPool.insert(nIndex);
        }
        LogPrintf("CWallet::NewKeyPool wrote %d new keys\n", nKeys);
    }
    return true;
}

bool CWallet::TopUpKeyPool(unsigned int kpSize)
{
    {
        LOCK(cs_wallet);

        if (IsLocked())
            return false;

        CWalletDB walletdb(strWalletFile);

        // Top up key pool
        uint64_t nTargetSize;
        if (kpSize > 0)
            nTargetSize = kpSize;
        else
            nTargetSize = static_cast<uint64_t>(max(GetArg("-keypool", 100), (int64_t) 0));

        while (setKeyPool.size() < (nTargetSize + 1))
        {
            int64_t nEnd = 1;
            if (!setKeyPool.empty())
                nEnd = *(--setKeyPool.end()) + 1;
            if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey())))
                throw runtime_error("TopUpKeyPool(): writing generated key failed");
            setKeyPool.insert(nEnd);
            LogPrintf("keypool added key %d, size=%u\n", nEnd, setKeyPool.size());
        }
    }
    return true;
}

void CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        if (!IsLocked())
            TopUpKeyPool();

        // Get the oldest key
        if(setKeyPool.empty())
            return;

        CWalletDB walletdb(strWalletFile);

        nIndex = *(setKeyPool.begin());
        setKeyPool.erase(setKeyPool.begin());
        if (!walletdb.ReadPool(nIndex, keypool))
            throw runtime_error("ReserveKeyFromKeyPool(): read failed");
        if (!HaveKey(keypool.vchPubKey.GetID()))
            throw runtime_error("ReserveKeyFromKeyPool(): unknown key in key pool");
        assert(keypool.vchPubKey.IsValid());
        LogPrintf("keypool reserve %d\n", nIndex);
    }
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    if (fFileBacked)
    {
        CWalletDB walletdb(strWalletFile);
        walletdb.ErasePool(nIndex);
    }
    LogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        setKeyPool.insert(nIndex);
    }
    LogPrintf("keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result)
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex == -1)
        {
            if (IsLocked())
                return false;
            result = GenerateNewKey();
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    ReserveKeyFromKeyPool(nIndex, keypool);
    if (nIndex == -1)
        return GetTime();
    ReturnKey(nIndex);
    return keypool.nTime;
}

map<CTxDestination, CAmount> CWallet::GetAddressBalances(const isminetype& isMineFilter)
{
    map<CTxDestination, CAmount> balances;
    {
        LOCK(cs_wallet);
        for (const auto &[hash, coin] : mapWallet)
        {
            if (!CheckFinalTx(coin) || !coin.IsTrusted())
                continue;
            if (coin.IsCoinBase() && coin.GetBlocksToMaturity() > 0)
                continue;
            const int nDepth = coin.GetDepthInMainChain();
            if (nDepth < (coin.IsFromMe(isminetype::ALL) ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < coin.vout.size(); ++i)
            {
                const auto& txOut = coin.vout[i];
                CTxDestination addr;
                const isminetype isMineType = GetIsMine(txOut);
                // filter by ismine (spendableOnly, watchOnly or both)
                if ((isMineType == isminetype::NO) || !IsMineType(isMineType, isMineFilter))
                    continue;
                if (!ExtractDestination(txOut.scriptPubKey, addr))
                    continue;

                const CAmount n = IsSpent(hash, i) ? 0 : txOut.nValue;
                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }
    return balances;
}

set< set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet); // mapWallet
    set< set<CTxDestination> > groupings;
    set<CTxDestination> grouping;

    for (const auto& [hash, coin] : mapWallet)
    {
        if (coin.vin.size() > 0)
        {
            bool any_mine = false;
            // group all input addresses with each other
            for (const auto &txin : coin.vin)
            {
                CTxDestination address;
                if (!IsMine(txin)) /* If this input isn't mine, ignore it */
                    continue;
                if(!ExtractDestination(mapWallet[txin.prevout.hash].vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                any_mine = true;
            }

            // group change with input addresses
            if (any_mine)
            {
               for (const auto &txout : coin.vout)
                   if (IsChange(txout))
                   {
                       CTxDestination txoutAddr;
                       if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
                           continue;
                       grouping.insert(txoutAddr);
                   }
            }
            if (grouping.size() > 0)
            {
                groupings.insert(grouping);
                grouping.clear();
            }
        }

        // group lone addrs by themselves
        for (unsigned int i = 0; i < coin.vout.size(); i++)
        {
            if (IsMine(coin.vout[i]))
            {
                CTxDestination address;
                if(!ExtractDestination(coin.vout[i].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
        }
    }

    set< set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
    map< CTxDestination, set<CTxDestination>* > setmap;  // map addresses to the unique group containing it
    for(const auto &grouping : groupings)
    {
        // make a set of all the groups hit by this new group
        set< set<CTxDestination>* > hits;
        map< CTxDestination, set<CTxDestination>* >::iterator it;
        for (const auto &address : grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        set<CTxDestination>* merged = new set<CTxDestination>(grouping);
        for (auto hit : hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        for (const auto &element : *merged)
            setmap[element] = merged;
    }

    set< set<CTxDestination> > ret;
    for (auto uniqueGrouping : uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

set<CTxDestination> CWallet::GetAccountAddresses(const string& strAccount) const
{
    LOCK(cs_wallet);
    set<CTxDestination> result;
    for (const auto &[address, addressBookData] : mapAddressBook)
    {
        const string& strName = addressBookData.name;
        if (strName == strAccount)
            result.insert(address);
    }
    return result;
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey)
{
    if (nIndex == -1)
    {
        CKeyPool keypool;
        pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex != -1)
            vchPubKey = keypool.vchPubKey;
        else {
            return false;
        }
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1)
        pwallet->KeepKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1)
        pwallet->ReturnKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::GetAllReserveKeys(set<CKeyID>& setAddress) const
{
    setAddress.clear();

    CWalletDB walletdb(strWalletFile);

    LOCK2(cs_main, cs_wallet);
    for (const auto& id : setKeyPool)
    {
        CKeyPool keypool;
        if (!walletdb.ReadPool(id, keypool))
            throw runtime_error("GetAllReserveKeyHashes(): read failed");
        assert(keypool.vchPubKey.IsValid());
        CKeyID keyID = keypool.vchPubKey.GetID();
        if (!HaveKey(keyID))
            throw runtime_error("GetAllReserveKeyHashes(): unknown key in key pool");
        setAddress.insert(keyID);
    }
}

void CWallet::UpdatedTransaction(const uint256 &hashTx)
{
    {
        LOCK(cs_wallet);
        // Only notify UI if this transaction is in this wallet
        auto mi = mapWallet.find(hashTx);
        if (mi != mapWallet.end())
            NotifyTransactionChanged(this, hashTx, CT_UPDATED);
    }
}

void CWallet::LockCoin(COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.insert(output);
}

void CWallet::UnlockCoin(COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.erase(output);
}

void CWallet::UnlockAllCoins()
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.clear();
}

bool CWallet::IsLockedCoin(uint256 hash, unsigned int n) const
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    COutPoint outpt(hash, n);

    return (setLockedCoins.count(outpt) > 0);
}

void CWallet::ListLockedCoins(vector<COutPoint>& vOutpts)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    for (set<COutPoint>::iterator it = setLockedCoins.begin();
         it != setLockedCoins.end(); it++) {
        COutPoint outpt = (*it);
        vOutpts.push_back(outpt);
    }
}


// Note Locking Operations
void CWallet::LockNote(const SaplingOutPoint& output)
{
    AssertLockHeld(cs_wallet);
    setLockedSaplingNotes.insert(output);
}

void CWallet::UnlockNote(const SaplingOutPoint& output)
{
    AssertLockHeld(cs_wallet);
    setLockedSaplingNotes.erase(output);
}

void CWallet::UnlockAllSaplingNotes()
{
    AssertLockHeld(cs_wallet);
    setLockedSaplingNotes.clear();
}

bool CWallet::IsLockedNote(const SaplingOutPoint& output) const
{
    AssertLockHeld(cs_wallet);
    return (setLockedSaplingNotes.count(output) > 0);
}

vector<SaplingOutPoint> CWallet::ListLockedSaplingNotes()
{
    AssertLockHeld(cs_wallet);
    vector<SaplingOutPoint> vOutputs(setLockedSaplingNotes.begin(), setLockedSaplingNotes.end());
    return vOutputs;
}

/** @} */ // end of Actions

class CAffectedKeysVisitor {
private:
    const CKeyStore &keystore;
    vector<CKeyID> &vKeys;

public:
    CAffectedKeysVisitor(const CKeyStore &keystoreIn, vector<CKeyID> &vKeysIn) : keystore(keystoreIn), vKeys(vKeysIn) {}

    void Process(const CScript &script) {
        txnouttype type;
        vector<CTxDestination> vDest;
        int nRequired;
        if (ExtractDestinations(script, type, vDest, nRequired))
        {
            for (const auto &dest : vDest)
                visit(*this, dest);
        }
    }

    void operator()(const CKeyID &keyId) {
        if (keystore.HaveKey(keyId))
            vKeys.push_back(keyId);
    }

    void operator()(const CScriptID &scriptId) {
        CScript script;
        if (keystore.GetCScript(scriptId, script))
            Process(script);
    }

    void operator()(const CNoDestination &none) {}
};

void CWallet::GetKeyBirthTimes(map<CKeyID, int64_t> &mapKeyBirth) const {
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (map<CKeyID, CKeyMetadata>::const_iterator it = mapKeyMetadata.begin(); it != mapKeyMetadata.end(); it++)
        if (it->second.nCreateTime)
            mapKeyBirth[it->first] = it->second.nCreateTime;

    // map in which we'll infer heights of other keys
    CBlockIndex *pindexMax = chainActive[max(0, chainActive.Height() - 144)]; // the tip can be reorganised; use a 144-block safety margin
    map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
    for (const auto &keyid : GetKeys())
    {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = pindexMax;
    }

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    vector<CKeyID> vAffected;
    for (const auto &[txid, wtx] : mapWallet)
    {
        // iterate over all wallet transactions...
        auto blit = mapBlockIndex.find(wtx.hashBlock);
        if (blit != mapBlockIndex.end() && chainActive.Contains(blit->second))
        {
            // ... which are already in a block
            int nHeight = blit->second->nHeight;
            for (const auto &txout : wtx.vout)
            {
                // iterate over all their outputs
                CAffectedKeysVisitor(*this, vAffected).Process(txout.scriptPubKey);
                for (const auto &keyid : vAffected)
                {
                    // ... and all their affected keys
                    auto rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
                        rit->second = blit->second;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (const auto &[keyID, pBlockIndex] : mapKeyFirstBlock)
        mapKeyBirth[keyID] = pBlockIndex->GetBlockTime() - 7200; // block times can be 2h off
}

bool CWallet::AddDestData(const CTxDestination &dest, const string &key, const string &value)
{
    if (get_if<CNoDestination>(&dest))
        return false;

    mapAddressBook[dest].destdata.insert(make_pair(key, value));
    if (!fFileBacked)
        return true;
    KeyIO keyIO(Params());
    return CWalletDB(strWalletFile).WriteDestData(keyIO.EncodeDestination(dest), key, value);
}

bool CWallet::EraseDestData(const CTxDestination &dest, const string &key)
{
    if (!mapAddressBook[dest].destdata.erase(key))
        return false;
    if (!fFileBacked)
        return true;
    KeyIO keyIO(Params());
    return CWalletDB(strWalletFile).EraseDestData(keyIO.EncodeDestination(dest), key);
}

bool CWallet::LoadDestData(const CTxDestination &dest, const string &key, const string &value)
{
    mapAddressBook[dest].destdata.insert(make_pair(key, value));
    return true;
}

bool CWallet::GetDestData(const CTxDestination &dest, const string &key, string *value) const
{
    const auto i = mapAddressBook.find(dest);
    if(i != mapAddressBook.cend())
    {
        const auto j = i->second.destdata.find(key);
        if(j != i->second.destdata.cend())
        {
            if(value)
                *value = j->second;
            return true;
        }
    }
    return false;
}

CKeyPool::CKeyPool()
{
    nTime = GetTime();
}

CKeyPool::CKeyPool(const CPubKey& vchPubKeyIn)
{
    nTime = GetTime();
    vchPubKey = vchPubKeyIn;
}

CWalletKey::CWalletKey(int64_t nExpires)
{
    nTimeCreated = (nExpires ? GetTime() : 0);
    nTimeExpires = nExpires;
}

int CMerkleTx::SetMerkleBranch(const CBlock& block)
{
    AssertLockHeld(cs_main);
    CBlock blockTmp;

    // Update the tx's hashBlock
    hashBlock = block.GetHash();

    // Locate the transaction
    for (nIndex = 0; nIndex < (int)block.vtx.size(); nIndex++)
        if (block.vtx[nIndex] == *(CTransaction*)this)
            break;
    if (nIndex == (int)block.vtx.size())
    {
        vMerkleBranch.clear();
        nIndex = -1;
        LogPrintf("ERROR: SetMerkleBranch(): couldn't find tx in block\n");
        return 0;
    }

    // Fill in merkle branch
    vMerkleBranch = block.GetMerkleBranch(nIndex);

    // Is the tx in a block that's in the main chain
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    const CBlockIndex* pindex = (*mi).second;
    if (!pindex || !chainActive.Contains(pindex))
        return 0;

    return chainActive.Height() - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChainINTERNAL(const CBlockIndex* &pindexRet) const
{
    if (hashBlock.IsNull() || nIndex == -1)
        return 0;
    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    auto mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = mi->second;
    if (!pindex || !chainActive.Contains(pindex))
        return 0;

    // Make sure the merkle branch connects to this block
    if (!fMerkleVerified)
    {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
            return 0;
        fMerkleVerified = true;
    }

    pindexRet = pindex;
    return chainActive.Height() - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChain(const CBlockIndex* &pindexRet) const
{
    AssertLockHeld(cs_main);
    int nResult = GetDepthInMainChainINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(GetHash()))
        return -1; // Not in chain, not in mempool

    return nResult;
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!IsCoinBase())
        return 0;
    return max(0, (COINBASE_MATURITY+1) - GetDepthInMainChain());
}


bool CMerkleTx::AcceptToMemoryPool(bool fLimitFree, bool fRejectAbsurdFee)
{
    CValidationState state;
    return ::AcceptToMemoryPool(Params(), mempool, state, *this, fLimitFree, nullptr, fRejectAbsurdFee);
}

/**
 * Find notes in the wallet filtered by payment address, min depth and ability to spend.
 * These notes are decrypted and added to the output parameter vector, outEntries.
 */
void CWallet::GetFilteredNotes(
    vector<SaplingNoteEntry>& saplingEntries,
    string address,
    int minDepth,
    bool ignoreSpent,
    bool requireSpendingKey)
{
    set<PaymentAddress> filterAddresses;

    KeyIO keyIO(Params());
    if (!address.empty())
        filterAddresses.insert(keyIO.DecodePaymentAddress(address));

    GetFilteredNotes(saplingEntries, filterAddresses, minDepth, INT_MAX, ignoreSpent, requireSpendingKey);
}

/**
 * Find notes in the wallet filtered by payment addresses, min depth, max depth, 
 * if the note is spent, if a spending key is required, and if the notes are locked.
 * These notes are decrypted and added to the output parameter vector, outEntries.
 */
void CWallet::GetFilteredNotes(
    vector<SaplingNoteEntry>& saplingEntries,
    set<PaymentAddress>& filterAddresses,
    int minDepth,
    int maxDepth,
    bool ignoreSpent,
    bool requireSpendingKey,
    bool ignoreLocked)
{
    LOCK2(cs_main, cs_wallet);

    KeyIO keyIO(Params());
    for (const auto & p : mapWallet)
    {
        const auto &wtx = p.second;

        // Filter the transactions before checking for notes
        if (!CheckFinalTx(wtx) ||
            wtx.GetBlocksToMaturity() > 0 ||
            wtx.GetDepthInMainChain() < minDepth ||
            wtx.GetDepthInMainChain() > maxDepth) {
            continue;
        }

        // [SaplingOutPoint, SaplingNoteData]
        for (const auto & [op, nd] : wtx.mapSaplingNoteData)
        {
            auto maybe_pt = SaplingNotePlaintext::decrypt(
                wtx.vShieldedOutput[op.n].encCiphertext,
                nd.ivk,
                wtx.vShieldedOutput[op.n].ephemeralKey,
                wtx.vShieldedOutput[op.n].cm);
            assert(static_cast<bool>(maybe_pt));
            auto notePt = maybe_pt.value(); //-V1007

            auto maybe_pa = nd.ivk.address(notePt.d);
            assert(static_cast<bool>(maybe_pa));
            auto pa = maybe_pa.value();

            // skip notes which belong to a different payment address in the wallet
            if (!(filterAddresses.empty() || filterAddresses.count(pa))) {
                continue;
            }

            if (ignoreSpent && nd.nullifier && IsSaplingSpent(*nd.nullifier)) {
                continue;
            }

            // skip notes which cannot be spent
            if (requireSpendingKey && !HaveSpendingKeyForPaymentAddress(this)(pa)) {
                continue;
            }

            // skip locked notes
            if (ignoreLocked && IsLockedNote(op)) {
                continue;
            }

            auto note = notePt.note(nd.ivk).value();
            saplingEntries.push_back(SaplingNoteEntry {
                op, pa, note, notePt.memo(), wtx.GetDepthInMainChain() });
        }
    }
}


//
// Shielded key and address generalizations
//

bool PaymentAddressBelongsToWallet::operator()(const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;

    // If we have a SaplingExtendedSpendingKey in the wallet, then we will
    // also have the corresponding SaplingExtendedFullViewingKey.
    return m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk) &&
        m_wallet->HaveSaplingFullViewingKey(ivk);
}

bool PaymentAddressBelongsToWallet::operator()(const libzcash::InvalidEncoding& no) const
{
    return false;
}

optional<libzcash::ViewingKey> GetViewingKeyForPaymentAddress::operator()(
    const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;

    if (m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk) &&
        m_wallet->GetSaplingFullViewingKey(ivk, extfvk))
    {
        return libzcash::ViewingKey(extfvk);
    } else {
        return nullopt;
    }
}

optional<libzcash::ViewingKey> GetViewingKeyForPaymentAddress::operator()(
    const libzcash::InvalidEncoding& no) const
{
    // Defaults to InvalidEncoding
    return libzcash::ViewingKey();
}

bool HaveSpendingKeyForPaymentAddress::operator()(const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;

    return m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk) &&
        m_wallet->GetSaplingFullViewingKey(ivk, extfvk) &&
        m_wallet->HaveSaplingSpendingKey(extfvk);
}

bool HaveSpendingKeyForPaymentAddress::operator()(const libzcash::InvalidEncoding& no) const
{
    return false;
}

optional<libzcash::SpendingKey> GetSpendingKeyForPaymentAddress::operator()(
    const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingExtendedSpendingKey extsk;
    if (m_wallet->GetSaplingExtendedSpendingKey(zaddr, extsk)) {
        return libzcash::SpendingKey(extsk);
    } else {
        return nullopt;
    }
}

optional<libzcash::SpendingKey> GetSpendingKeyForPaymentAddress::operator()(
    const libzcash::InvalidEncoding& no) const
{
    // Defaults to InvalidEncoding
    return libzcash::SpendingKey();
}

KeyAddResult AddViewingKeyToWallet::operator()(const libzcash::SaplingExtendedFullViewingKey &extfvk) const {
    if (m_wallet->HaveSaplingSpendingKey(extfvk)) {
        return SpendingKeyExists;
    } else if (m_wallet->HaveSaplingFullViewingKey(extfvk.fvk.in_viewing_key())) {
        return KeyAlreadyExists;
    } else if (m_wallet->AddSaplingFullViewingKey(extfvk)) {
        return KeyAdded;
    } else {
        return KeyNotAdded;
    }
}

KeyAddResult AddViewingKeyToWallet::operator()(const libzcash::InvalidEncoding& no) const {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid viewing key");
}

KeyAddResult AddSpendingKeyToWallet::operator()(const libzcash::SaplingExtendedSpendingKey &sk) const {
    auto extfvk = sk.ToXFVK();
    auto ivk = extfvk.fvk.in_viewing_key();
    KeyIO keyIO(Params());
    {
        if (log){
            LogPrint("zrpc", "Importing zaddr %s...\n", keyIO.EncodePaymentAddress(sk.DefaultAddress()));
        }
        // Don't throw error in case a key is already there
        if (m_wallet->HaveSaplingSpendingKey(extfvk)) {
            return KeyAlreadyExists;
        } else {
            if (!m_wallet-> AddSaplingZKey(sk)) {
                return KeyNotAdded;
            }

            // Sapling addresses can't have been used in transactions prior to activation.
            if (params.vUpgrades[to_integral_type(Consensus::UpgradeIndex::UPGRADE_SAPLING)].nActivationHeight == Consensus::NetworkUpgrade::ALWAYS_ACTIVE)
            {
                m_wallet->mapSaplingZKeyMetadata[ivk].nCreateTime = nTime;
            } else {
                // 154051200 seconds from epoch is Friday, 26 October 2018 00:00:00 GMT - definitely before Sapling activates
                m_wallet->mapSaplingZKeyMetadata[ivk].nCreateTime = max((int64_t) 154051200, nTime);
            }
            if (hdKeypath) {
                m_wallet->mapSaplingZKeyMetadata[ivk].hdKeypath = hdKeypath.value();
            }
            if (seedFpStr) {
                uint256 seedFp;
                seedFp.SetHex(seedFpStr.value());
                m_wallet->mapSaplingZKeyMetadata[ivk].seedFp = seedFp;
            }
            return KeyAdded;
        }    
    }
}

KeyAddResult AddSpendingKeyToWallet::operator()(const libzcash::InvalidEncoding& no) const {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid spending key");
}
