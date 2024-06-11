#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <atomic>

#include <coins.h>
#include <dbwrapper.h>
#include <txdb/addressindex.h>
#include <txdb/timestampindex.h>
#include <txdb/spentindex.h>
#include <chainparams.h>
#include <chain.h>

class CBlockFileInfo;
class CBlockIndex;
struct CDiskTxPos;
class uint256;

//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 450;
//! max. -dbcache (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;
//! min. -dbcache in (MiB)
static const int64_t nMinDbCache = 4;

constexpr auto TXDB_FLAG_INSIGHT_EXPLORER = "insightexplorer";
constexpr auto TXDB_FLAG_TXINDEX = "txindex";
constexpr auto TXDB_FLAG_PRUNEDBLOCKFILES = "prunedblockfiles";

// START insightexplorer
struct CAddressUnspentKey;
struct CAddressUnspentValue;
struct CAddressIndexKey;
struct CAddressIndexIteratorKey;
struct CAddressIndexIteratorHeightKey;
struct CSpentIndexKey;
struct CSpentIndexValue;
struct CTimestampIndexKey;
struct CTimestampIndexIteratorKey;
struct CTimestampBlockIndexKey;
struct CTimestampBlockIndexValue;

typedef std::pair<CAddressUnspentKey, CAddressUnspentValue> CAddressUnspentDbEntry;
typedef std::pair<CAddressIndexKey, CAmount> CAddressIndexDbEntry;
typedef std::pair<CSpentIndexKey, CSpentIndexValue> CSpentIndexDbEntry;
// END insightexplorer

/** CCoinsView backed by the coin database (chainstate/) */
class CCoinsViewDB : public CCoinsView
{
protected:
    CDBWrapper db;
    CCoinsViewDB(std::string dbName, size_t nCacheSize, bool fMemory = false, bool fWipe = false);
public:
    CCoinsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const;
    bool GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const;
    bool GetNullifier(const uint256 &nf, ShieldedType type) const;
    bool GetCoins(const uint256 &txid, CCoins &coins) const;
    bool HaveCoins(const uint256 &txid) const;
    uint256 GetBestBlock() const;
    uint256 GetBestAnchor(ShieldedType type) const;
    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashSproutAnchor,
                    const uint256 &hashSaplingAnchor,
                    CAnchorsSproutMap &mapSproutAnchors,
                    CAnchorsSaplingMap &mapSaplingAnchors,
                    CNullifiersMap &mapSproutNullifiers,
                    CNullifiersMap &mapSaplingNullifiers);
    bool GetStats(CCoinsStats &stats) const;
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CDBWrapper
{
public:
    CBlockTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const block_index_cvector_t& blockinfo);
    bool EraseBatchSync(const block_index_cvector_t& blockinfo);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo) const;
    bool ReadLastBlockFile(int &nFile) const;
    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex) const;
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &list);
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue) const;
    bool ReadFlag(const std::string &name, std::atomic_bool &fValue) const;
    bool LoadBlockIndexGuts(const CChainParams& chainparams, std::string &strLoadError);

    // START insightexplorer
    bool UpdateAddressUnspentIndex(const std::vector<CAddressUnspentDbEntry> &vect);
    bool ReadAddressUnspentIndex(const uint160 &addressHash, const uint8_t type, std::vector<CAddressUnspentDbEntry> &vect) const;
    bool WriteAddressIndex(const std::vector<CAddressIndexDbEntry> &vect);
    bool EraseAddressIndex(const std::vector<CAddressIndexDbEntry> &vect);
    bool ReadAddressIndex(const uint160 &addressHash, const uint8_t type, std::vector<CAddressIndexDbEntry> &addressIndex, 
        const uint32_t nStartHeight = 0, const uint32_t nEndHeight = 0) const;
    bool ReadSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value) const;
    bool UpdateSpentIndex(const std::vector<CSpentIndexDbEntry> &vect);
    bool WriteTimestampIndex(const CTimestampIndexKey &timestampIndex);
    bool ReadTimestampIndex(unsigned int high, unsigned int low,
            const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &vect);
    bool WriteTimestampBlockIndex(const CTimestampBlockIndexKey &blockhashIndex,
            const CTimestampBlockIndexValue &logicalts);
    bool ReadTimestampBlockIndex(const uint256 &hash, unsigned int &logicalTS) const;
    // END insightexplorer

private:
    CBlockTreeDB(const CBlockTreeDB&);
    void operator=(const CBlockTreeDB&);
};

