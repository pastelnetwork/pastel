// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>

#include <utils/uint256.h>
#include <txdb.h>
#include <chainparams.h>
#include <hash.h>
#include <main.h>
#include <pow.h>
#include <script/scripttype.h>

using namespace std;

// NOTE: Per issue #3277, do not use the prefix 'X' or 'x' as they were
// previously used by DB_SAPLING_ANCHOR and DB_BEST_SAPLING_ANCHOR.
static constexpr char DB_SPROUT_ANCHOR = 'A';
static constexpr char DB_SAPLING_ANCHOR = 'Z';
static constexpr char DB_NULLIFIER = 's';
static constexpr char DB_SAPLING_NULLIFIER = 'S';
static constexpr char DB_COINS = 'c';
static constexpr char DB_BLOCK_FILES = 'f';
static constexpr char DB_TXINDEX = 't';
static constexpr char DB_BLOCK_INDEX = 'b';

static constexpr char DB_BEST_BLOCK = 'B';
static constexpr char DB_BEST_SPROUT_ANCHOR = 'a';
static constexpr char DB_BEST_SAPLING_ANCHOR = 'z';
static constexpr char DB_FLAG = 'F';
static constexpr char DB_REINDEX_FLAG = 'R';
static constexpr char DB_LAST_BLOCK = 'l';

static constexpr char DB_SPENTINDEX = 'p';


CCoinsViewDB::CCoinsViewDB(string dbName, size_t nCacheSize, bool fMemory, bool fWipe) :
    db(GetDataDir() / dbName, nCacheSize, fMemory, fWipe)
{}

CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe) 
{}

bool CCoinsViewDB::GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const
{
    if (rt == SproutMerkleTree::empty_root())
    {
        SproutMerkleTree new_tree;
        tree = new_tree;
        return true;
    }

    bool read = db.Read(make_pair(DB_SPROUT_ANCHOR, rt), tree);

    return read;
}

bool CCoinsViewDB::GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const {
    if (rt == SaplingMerkleTree::empty_root()) {
        SaplingMerkleTree new_tree;
        tree = new_tree;
        return true;
    }

    bool read = db.Read(make_pair(DB_SAPLING_ANCHOR, rt), tree);

    return read;
}

bool CCoinsViewDB::GetNullifier(const uint256 &nf, ShieldedType type) const {
    bool spent = false;
    char dbChar;
    switch (type) {
        case SPROUT:
            dbChar = DB_NULLIFIER;
            break;
        case SAPLING:
            dbChar = DB_SAPLING_NULLIFIER;
            break;
        default:
            throw runtime_error("Unknown shielded type");
    }
    return db.Read(make_pair(dbChar, nf), spent);
}

bool CCoinsViewDB::GetCoins(const uint256 &txid, CCoins &coins) const {
    return db.Read(make_pair(DB_COINS, txid), coins);
}

bool CCoinsViewDB::HaveCoins(const uint256 &txid) const {
    return db.Exists(make_pair(DB_COINS, txid));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

uint256 CCoinsViewDB::GetBestAnchor(ShieldedType type) const {
    uint256 hashBestAnchor;
    
    switch (type) {
        case SPROUT:
            if (!db.Read(DB_BEST_SPROUT_ANCHOR, hashBestAnchor))
                return SproutMerkleTree::empty_root();
            break;
        case SAPLING:
            if (!db.Read(DB_BEST_SAPLING_ANCHOR, hashBestAnchor))
                return SaplingMerkleTree::empty_root();
            break;
        default:
            throw runtime_error("Unknown shielded type");
    }

    return hashBestAnchor;
}

void BatchWriteNullifiers(CDBBatch& batch, CNullifiersMap& mapToUse, const char& dbChar)
{
    for (CNullifiersMap::iterator it = mapToUse.begin(); it != mapToUse.end();) {
        if (it->second.flags & CNullifiersCacheEntry::DIRTY) {
            if (!it->second.entered)
                batch.Erase(make_pair(dbChar, it->first));
            else
                batch.Write(make_pair(dbChar, it->first), true);
            // TODO: changed++? ... See comment in CCoinsViewDB::BatchWrite. If this is needed we could return an int
        }
        CNullifiersMap::iterator itOld = it++;
        mapToUse.erase(itOld);
    }
}

template<typename Map, typename MapIterator, typename MapEntry, typename Tree>
void BatchWriteAnchors(CDBBatch& batch, Map& mapToUse, const char& dbChar)
{
    for (MapIterator it = mapToUse.begin(); it != mapToUse.end();) {
        if (it->second.flags & MapEntry::DIRTY) {
            if (!it->second.entered)
                batch.Erase(make_pair(dbChar, it->first));
            else {
                if (it->first != Tree::empty_root()) {
                    batch.Write(make_pair(dbChar, it->first), it->second.tree);
                }
            }
            // TODO: changed++?
        }
        MapIterator itOld = it++;
        mapToUse.erase(itOld);
    }
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins,
                              const uint256 &hashBlock,
                              const uint256 &hashSproutAnchor,
                              const uint256 &hashSaplingAnchor,
                              CAnchorsSproutMap &mapSproutAnchors,
                              CAnchorsSaplingMap &mapSaplingAnchors,
                              CNullifiersMap &mapSproutNullifiers,
                              CNullifiersMap &mapSaplingNullifiers) {
    CDBBatch batch(db);
    size_t count = 0;
    size_t changed = 0;
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            if (it->second.coins.IsPruned())
                batch.Erase(make_pair(DB_COINS, it->first));
            else
                batch.Write(make_pair(DB_COINS, it->first), it->second.coins);
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }

    ::BatchWriteAnchors<CAnchorsSproutMap, CAnchorsSproutMap::iterator, CAnchorsSproutCacheEntry, SproutMerkleTree>(batch, mapSproutAnchors, DB_SPROUT_ANCHOR);
    ::BatchWriteAnchors<CAnchorsSaplingMap, CAnchorsSaplingMap::iterator, CAnchorsSaplingCacheEntry, SaplingMerkleTree>(batch, mapSaplingAnchors, DB_SAPLING_ANCHOR);

    ::BatchWriteNullifiers(batch, mapSproutNullifiers, DB_NULLIFIER);
    ::BatchWriteNullifiers(batch, mapSaplingNullifiers, DB_SAPLING_NULLIFIER);

    if (!hashBlock.IsNull())
        batch.Write(DB_BEST_BLOCK, hashBlock);
    if (!hashSproutAnchor.IsNull())
        batch.Write(DB_BEST_SPROUT_ANCHOR, hashSproutAnchor);
    if (!hashSaplingAnchor.IsNull())
        batch.Write(DB_BEST_SAPLING_ANCHOR, hashSaplingAnchor);

    LogPrint("coindb", "Committing %u changed transactions (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return db.WriteBatch(batch);
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe) {
}

bool CBlockTreeDB::ReadSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value) {
    return Read(make_pair(DB_SPENTINDEX, key), value);
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

bool CCoinsViewDB::GetStats(CCoinsStats &stats) const
{
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    auto pcursor = const_cast<CDBWrapper*>(&db)->NewIterator();
    pcursor->Seek(DB_COINS);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = GetBestBlock();
    ss << stats.hashBlock;
    CAmount nTotalAmount = 0;
    while (pcursor->Valid())
    {
        func_thread_interrupt_point();
        pair<char, uint256> key;
        CCoins coins;
        if (pcursor->GetKey(key) && key.first == DB_COINS) {
            if (pcursor->GetValue(coins)) {
                stats.nTransactions++;
                for (unsigned int i=0; i<coins.vout.size(); i++) {
                    const CTxOut &out = coins.vout[i];
                    if (!out.IsNull()) {
                        stats.nTransactionOutputs++;
                        ss << VARINT(i+1);
                        ss << out;
                        nTotalAmount += out.nValue;
                    }
                }
                stats.nSerializedSize += 32 + pcursor->GetValueSize();
                ss << VARINT(0);
            } else {
                return error("CCoinsViewDB::GetStats() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    stats.hashSerialized = ss.GetHash();
    stats.nTotalAmount = nTotalAmount;
    return true;
}

bool CBlockTreeDB::WriteBatchSync(const vector<pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const block_index_cvector_t& blockinfo)
{
    CDBBatch batch(*this);
    for (const auto& [nFile, pBlockFileInfo] : fileInfo)
        batch.Write(make_pair(DB_BLOCK_FILES, nFile), *pBlockFileInfo);
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (const auto pBlockIndex : blockinfo)
        batch.Write(make_pair(DB_BLOCK_INDEX, pBlockIndex->GetBlockHash()), CDiskBlockIndex(pBlockIndex));
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::EraseBatchSync(const block_index_cvector_t& blockinfo)
{
    CDBBatch batch(*this);
    for (const auto pBlockIndex : blockinfo)
		batch.Erase(make_pair(DB_BLOCK_INDEX, pBlockIndex->GetBlockHash()));
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos)
{
    return Read(make_pair(DB_TXINDEX, txid), pos);
}

bool CBlockTreeDB::WriteTxIndex(const vector<pair<uint256, CDiskTxPos> >&vect)
{
    CDBBatch batch(*this);
    for (const auto &[hash, diskTxPos] : vect)
        batch.Write(make_pair(DB_TXINDEX, hash), diskTxPos);
    return WriteBatch(batch);
}

bool CBlockTreeDB::WriteFlag(const string &name, bool fValue)
{
    return Write(make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const string &name, bool &fValue)
{
    char ch;
    if (!Read(make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::ReadFlag(const string& name, atomic_bool& fValue)
{
    bool fTempValue;
    if (!ReadFlag(name, fTempValue))
		return false;
    fValue.store(fTempValue);
	return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts(const CChainParams& chainparams)
{
    auto pcursor = NewIterator();
    pcursor->Seek(make_pair(DB_BLOCK_INDEX, uint256()));

    // Load mapBlockIndex
    while (pcursor->Valid())
    {
        func_thread_interrupt_point();
        pair<char, uint256> key;
        if (!pcursor->GetKey(key) || key.first != DB_BLOCK_INDEX)
            break;

        CDiskBlockIndex diskBlockIndex;
        if (!pcursor->GetValue(diskBlockIndex))
            return error("LoadBlockIndex(): failed to read block index value with key '%s'", key.second.ToString());

        // Construct block index object
        CBlockIndex* pindexNew = InsertBlockIndex(diskBlockIndex.GetBlockHash());
        pindexNew->pprev       = InsertBlockIndex(diskBlockIndex.hashPrev);
        pindexNew->assign(diskBlockIndex);

        // Consistency checks
        auto header = pindexNew->GetBlockHeader();
        const auto& hashBlock = header.GetHash();
        if (hashBlock != pindexNew->GetBlockHash())
            return error("LoadBlockIndex(): block header inconsistency detected: on-disk = %s, in-memory = %s",
                diskBlockIndex.ToString(),  pindexNew->ToString());
    
        //INGEST->!!!
        if (chainparams.IsRegTest() || pindexNew->nHeight > TOP_INGEST_BLOCK)
        {
            if (!CheckProofOfWork(hashBlock, pindexNew->nBits, chainparams.GetConsensus()))
                return error("LoadBlockIndex(): CheckProofOfWork failed: %s", pindexNew->ToString());
        }
        //<-INGEST!!!

        pcursor->Next();
    }

    return true;
}
