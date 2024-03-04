#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <list>
#include <cstdint>
#include <string>
#include <utility>
#include <mutex>

#include <utils/vector_types.h>
#include <utils/svc_thread.h>
#include <amount.h>
#include <wallet/db.h>
#include <key.h>
#include <keystore.h>
#include <zcash/Address.hpp>

class CAccount;
class CAccountingEntry;
struct CBlockLocator;
class CKeyPool;
class CMasterKey;
class CScript;
class CWallet;
class CWalletTx;
class uint160;
class uint256;

/** Error statuses for the wallet database */
enum DBErrors
{
    DB_LOAD_OK,
    DB_CORRUPT,
    DB_NONCRITICAL_ERROR,
    DB_TOO_NEW,
    DB_LOAD_FAIL,
    DB_NEED_REWRITE
};

/* simple hd chain data model */
class CHDChain
{
public:
    static const int VERSION_HD_BASE = 1;
    static const int CURRENT_VERSION = VERSION_HD_BASE;
    int nVersion;
    uint256 seedFp;
    int64_t nCreateTime; // 0 means unknown
    uint32_t saplingAccountCounter;

    CHDChain() { SetNull(); }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(nVersion);
        READWRITE(seedFp);
        READWRITE(nCreateTime);
        READWRITE(saplingAccountCounter);
    }

    void SetNull()
    {
        nVersion = CHDChain::CURRENT_VERSION;
        seedFp.SetNull();
        nCreateTime = 0;
        saplingAccountCounter = 0;
    }
};

class CKeyMetadata
{
public:
    static const int VERSION_BASIC=1;
    static const int VERSION_WITH_HDDATA=10;
    static const int CURRENT_VERSION=VERSION_WITH_HDDATA;
    int nVersion;
    int64_t nCreateTime; // 0 means unknown
    std::string hdKeypath; //optional HD/zip32 keypath
    uint256 seedFp;

    CKeyMetadata()
    {
        SetNull();
    }
    CKeyMetadata(int64_t nCreateTime_)
    {
        SetNull();
        nCreateTime = nCreateTime_;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(this->nVersion);
        READWRITE(nCreateTime);
        if (this->nVersion >= VERSION_WITH_HDDATA)
        {
            READWRITE(hdKeypath);
            READWRITE(seedFp);
        }
    }

    void SetNull()
    {
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = 0;
        hdKeypath.clear();
        seedFp.SetNull();
    }
};

/** Access to the wallet database (wallet.dat) */
class CWalletDB : public CDB
{
public:
    CWalletDB(const std::string& strFilename, const char* pszMode = "r+", bool fFlushOnClose = true) : 
        CDB(strFilename, pszMode, fFlushOnClose)
    {}

    CWalletDB(const CWalletDB&) = delete;
    void operator=(const CWalletDB&) = delete;

    bool WriteName(const std::string& strAddress, const std::string& strName);
    bool EraseName(const std::string& strAddress);

    bool WritePurpose(const std::string& strAddress, const std::string& purpose);
    bool ErasePurpose(const std::string& strAddress);

    bool WriteTx(const uint256& hash, const CWalletTx& wtx);
    bool EraseTx(const uint256& hash);

    bool WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata &keyMeta);
    bool WriteCryptedKey(const CPubKey& vchPubKey, const v_uint8& vchCryptedSecret, const CKeyMetadata& keyMeta);
    bool WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey);

    bool WriteCScript(const uint160& hash, const CScript& redeemScript);

    bool WriteWatchOnly(const CScript &script);
    bool EraseWatchOnly(const CScript &script);

    bool WriteBestBlock(const CBlockLocator& locator);
    bool ReadBestBlock(CBlockLocator& locator);

    bool WriteOrderPosNext(int64_t nOrderPosNext);

    bool WriteDefaultKey(const CPubKey& vchPubKey);

    bool WriteWitnessCacheSize(const uint64_t nWitnessCacheSize);

    bool ReadPool(int64_t nPool, CKeyPool& keypool);
    bool WritePool(int64_t nPool, const CKeyPool& keypool);
    bool ErasePool(int64_t nPool);

    bool WriteMinVersion(int nVersion);

    bool ReadAccount(const std::string& strAccount, CAccount& account);
    bool WriteAccount(const std::string& strAccount, const CAccount& account);

    /// Write destination data key,value tuple to database
    bool WriteDestData(const std::string &address, const std::string &key, const std::string &value);
    /// Erase destination data tuple from wallet database
    bool EraseDestData(const std::string &address, const std::string &key);

    bool WriteAccountingEntry(const CAccountingEntry& acentry);
    CAmount GetAccountCreditDebit(const std::string& strAccount);
    void ListAccountCreditDebit(const std::string& strAccount, std::list<CAccountingEntry>& acentries);

    DBErrors ReorderTransactions(CWallet* pwallet);
    DBErrors LoadWallet(CWallet* pwallet);
    DBErrors FindWalletTx(CWallet* pwallet, v_uint256& vTxHash, std::vector<CWalletTx>& vWtx);
    DBErrors ZapWalletTx(CWallet* pwallet, std::vector<CWalletTx>& vWtx);
    static bool Recover(CDBEnv& dbenv, const std::string& filename, bool fOnlyKeys);
    static bool Recover(CDBEnv& dbenv, const std::string& filename);

    bool WriteHDSeed(const HDSeed& seed);
    bool WriteCryptedHDSeed(const uint256& seedFp, const v_uint8& vchCryptedSecret);
    //! write the hdchain model (external chain child index counter)
    bool WriteHDChain(const CHDChain& chain);

    /// Write spending key to wallet database, where key is payment address and value is spending key.
    bool WriteSaplingZKey(const libzcash::SaplingIncomingViewingKey &ivk,
                          const libzcash::SaplingExtendedSpendingKey &key,
                          const CKeyMetadata  &keyMeta);
    bool WriteSaplingPaymentAddress(const libzcash::SaplingPaymentAddress &addr,
                                    const libzcash::SaplingIncomingViewingKey &ivk);
    bool WriteCryptedSaplingZKey(const libzcash::SaplingExtendedFullViewingKey &extfvk,
                                 const v_uint8& vchCryptedSecret,
                          const CKeyMetadata &keyMeta);

    bool WriteSaplingExtendedFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk);
    bool EraseSaplingExtendedFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk);

    static void IncrementUpdateCounter();
    static unsigned int GetUpdateCounter();

private:
    bool WriteAccountingEntry(const uint64_t nAccEntryNum, const CAccountingEntry& acentry);
};

bool BackupWallet(const CWallet& wallet, const std::string& strDest);

class CFlushWalletDBThread : public CStoppableServiceThread
{
public:
    CFlushWalletDBThread(const std::string& sWalletFile) : 
        CStoppableServiceThread("wallet"),
        m_sWalletFile(sWalletFile)
    {}

    void execute() override;

private:
    std::string m_sWalletFile;
    static std::once_flag m_onceFlag;

    void execute_internal();
};