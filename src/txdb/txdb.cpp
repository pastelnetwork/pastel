// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>

#include <utils/uint256.h>
#include <utils/hash.h>
#include <txdb/txdb.h>
#include <chainparams.h>
#include <chain_options.h>
#include <main.h>
#include <init.h>
#include <mining/pow.h>
#include <script/scripttype.h>

using namespace std;

unique_ptr<CBlockTreeDB> gl_pBlockTreeDB;

// NOTE: Per issue #3277, do not use the prefix 'X' or 'x' as they were
// previously used by DB_SAPLING_ANCHOR and DB_BEST_SAPLING_ANCHOR.
constexpr char DB_SPROUT_ANCHOR = 'A';
constexpr char DB_SAPLING_ANCHOR = 'Z';
constexpr char DB_NULLIFIER = 's';
constexpr char DB_SAPLING_NULLIFIER = 'S';
constexpr char DB_COINS = 'c';
constexpr char DB_BLOCK_FILES = 'f';
constexpr char DB_TXINDEX = 't';
constexpr char DB_BLOCK_INDEX = 'b';

constexpr char DB_BEST_BLOCK = 'B';
constexpr char DB_BEST_SPROUT_ANCHOR = 'a';
constexpr char DB_BEST_SAPLING_ANCHOR = 'z';
constexpr char DB_FLAG = 'F';
constexpr char DB_REINDEX_FLAG = 'R';
constexpr char DB_LAST_BLOCK = 'l';

// insightexplorer
constexpr char DB_ADDRESSINDEX = 'd';
constexpr char DB_ADDRESSUNSPENTINDEX = 'u';
constexpr char DB_SPENTINDEX = 'p';
constexpr char DB_TIMESTAMPINDEX = 'T';
constexpr char DB_BLOCKHASHINDEX = 'h';
constexpr char DB_FUNDSTRANSFERINDEX = 'D';

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

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) : 
    CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe)
{}   

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) const
{
    return Read(make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing)
{
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) const
{
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) const
{
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
    try
    {
        for (const auto pBlockIndex : blockinfo)
            batch.Write(make_pair(DB_BLOCK_INDEX, pBlockIndex->GetBlockHash()), CDiskBlockIndex(pBlockIndex));
    } catch (const runtime_error&) {
		return false;
	}
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

bool CBlockTreeDB::ReadFlag(const string &name, bool &fValue) const
{
    char ch;
    if (!Read(make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::ReadFlag(const string& name, atomic_bool& fValue) const
{
    bool fTempValue;
    if (!ReadFlag(name, fTempValue))
		return false;
    fValue.store(fTempValue);
	return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts(const CChainParams& chainparams, string &strLoadError)
{
    auto pcursor = NewIterator();
    pcursor->Seek(make_pair(DB_BLOCK_INDEX, uint256()));

    size_t nCount = 0;
    // Load mapBlockIndex
    while (pcursor->Valid())
    {
        func_thread_interrupt_point();
        pair<char, uint256> key;
        if (!pcursor->GetKey(key) || key.first != DB_BLOCK_INDEX)
            break;

        CDiskBlockIndex diskBlockIndex;
        if (!pcursor->GetValue(diskBlockIndex))
        {
            strLoadError = strprintf("Failed to read block index value with key '%s'", key.second.ToString());
            return false;
        }

        // Construct block index object
        CBlockIndex* pindexNew = InsertBlockIndex(diskBlockIndex.GetBlockHash());
        pindexNew->pprev       = InsertBlockIndex(diskBlockIndex.hashPrev);
        pindexNew->assign(diskBlockIndex);

        if (nCount++ % 10'000 == 0)
        {
            if (IsShutdownRequested())
			{
				strLoadError = "Shutdown requested";
				return false;
			}
        }
        // Consistency checks
        auto header = pindexNew->GetBlockHeader();
        const auto& hashBlock = header.GetHash();
        if (hashBlock != pindexNew->GetBlockHash())
        {
            strLoadError = strprintf("Block hash inconsistency detected: on-disk = %s, in-memory = %s",
				diskBlockIndex.ToString(), pindexNew->ToString());
            return false;
        }
    
        //INGEST->!!!
        if (chainparams.IsRegTest() || pindexNew->nHeight > TOP_INGEST_BLOCK)
        {
            if (!CheckProofOfWork(hashBlock, pindexNew->nBits, chainparams.GetConsensus()))
            {
                strLoadError = strprintf("CheckProofOfWork failed: %s", pindexNew->ToString());
                return false;
            }
        }
        //<-INGEST!!!

        pcursor->Next();
    }

    return true;
}

// START insightexplorer
bool CBlockTreeDB::WriteAddressIndex(const address_index_vector_t &vAddressIndex)
{
    if (vAddressIndex.empty())
        return true;
    LogFnPrint("txdb", "AddressIndex - writing %zu entries", vAddressIndex.size());

    CDBBatch batch(*this);
    for (const auto &[key, value] : vAddressIndex)
        batch.Write(make_pair(DB_ADDRESSINDEX, key), value);
    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseAddressIndex(const address_index_vector_t &vAddressIndex)
{
    if (vAddressIndex.empty())
        return true;
    LogFnPrint("txdb", "AddressIndex - erasing %zu entries", vAddressIndex.size());

    CDBBatch batch(*this);
    for (const auto &[key, value] : vAddressIndex)
		batch.Erase(make_pair(DB_ADDRESSINDEX, key));
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressIndex(
    const uint160 &addressHash, const ScriptType addressType,
    address_index_vector_t &vAddressIndex,
    const height_range_opt_t &height_range) const
{
    uint32_t nStartHeight = 0;
    uint32_t nEndHeight = 0;
    if (height_range)
    {
        tie(nStartHeight, nEndHeight) = height_range.value();
        LogFnPrint("txdb", "AddressIndex - reading address %s, type %hhu, height range [%u..%u]",
            addressHash.GetHex(), to_integral_type(addressType), nStartHeight, nEndHeight);
    }
    else
		LogFnPrint("txdb", "AddressIndex - reading address %s, type %hhu", addressHash.GetHex(), to_integral_type(addressType));

    unique_ptr<CDBIterator> pcursor(NewIterator());

    if (height_range && nStartHeight > 0)
        pcursor->Seek(make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorHeightKey(addressType, addressHash, nStartHeight)));
    else
        pcursor->Seek(make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorKey(addressType, addressHash)));

    while (pcursor->Valid())
    {
        func_thread_interrupt_point();
        pair<char,CAddressIndexKey> key;
        if (!(pcursor->GetKey(key) && (key.first == DB_ADDRESSINDEX) && (key.second.addressHash == addressHash)))
            break;

        if (nEndHeight > 0 && key.second.blockHeight > nEndHeight)
            break;

        CAmount nValue;
        if (!pcursor->GetValue(nValue))
            return error("failed to get address index value");
        vAddressIndex.emplace_back(key.second, nValue);
        pcursor->Next();
    }
    return true;
}

bool CBlockTreeDB::ReadAddressIndexAll(address_index_vector_t& addressIndex, const height_range_opt_t& height_range) const
{
    // use NewIteratorFromChar to get an iterator that starts at the first key
    unique_ptr<CDBIterator> pcursor(NewIteratorFromChar(DB_ADDRESSINDEX));
    if (!pcursor->Valid())
        return true;

    uint32_t nStartHeight = 0;
    uint32_t nEndHeight = 0;
    if (height_range) {
        tie(nStartHeight, nEndHeight) = height_range.value();
        LogFnPrint("txdb", "AddressIndex - reading all addresses, height range [%u..%u]", nStartHeight, nEndHeight);
    } else
        LogFnPrint("txdb", "AddressIndex - reading all addresses");

    size_t nEstimatedCount = EstimateSliceItemCount(DB_ADDRESSINDEX);
    addressIndex.reserve(nEstimatedCount);

    while (pcursor->Valid())
    {
        func_thread_interrupt_point();
        pair<char, CAddressIndexKey> key;
        if (!(pcursor->GetKey(key) && (key.first == DB_ADDRESSINDEX)))
            break;

        if (height_range && nStartHeight > 0 && key.second.blockHeight < nStartHeight)
            continue;
        if (height_range && nEndHeight > 0 && key.second.blockHeight > nEndHeight)
            break;

        CAmount nValue;
        if (!pcursor->GetValue(nValue))
            return error("failed to get address index value");
        addressIndex.emplace_back(key.second, nValue);
        pcursor->Next();
    }
    return true;
}

bool CBlockTreeDB::UpdateAddressUnspentIndex(const address_unspent_vector_t &v)
{
    if (v.empty())
		return true;
    LogFnPrint("txdb", "AddressUnspentIndex - updating %zu entries", v.size());
    CDBBatch batch(*this);
    for (const auto &[key, value] : v)
    {
        if (value.IsNull())
            batch.Erase(make_pair(DB_ADDRESSUNSPENTINDEX, key));
        else
            batch.Write(make_pair(DB_ADDRESSUNSPENTINDEX, key), value);
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressUnspentIndex(const uint160 &addressHash, const ScriptType addressType, 
    address_unspent_vector_t &vUnspentOutputs) const
{
    LogFnPrint("txdb", "AddressUnspentIndex - reading address %s, type %hdd",
        addressHash.GetHex(), to_integral_type(addressType));

    unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ADDRESSUNSPENTINDEX, CAddressIndexIteratorKey(addressType, addressHash)));
    while (pcursor->Valid())
    {
        func_thread_interrupt_point();
        pair<char,CAddressUnspentKey> key;
        if (!(pcursor->GetKey(key) && (key.first == DB_ADDRESSUNSPENTINDEX)))
            break;

        const auto &unspentKey = key.second;
        if ((unspentKey.addressHash != addressHash) || (unspentKey.type != addressType))
			break;

        CAddressUnspentValue nValue;
        if (!pcursor->GetValue(nValue))
            return error("failed to get address unspent value");
        vUnspentOutputs.push_back(make_pair(key.second, nValue));
        pcursor->Next();
    }
    return true;
}

optional<CAddressUnspentValue> CBlockTreeDB::GetAddressUnspentIndexValue(const uint160& addressHash, const ScriptType addressType,
    const uint256& txid, const uint32_t nTxOut) const
{
    CAddressUnspentKey key(addressType, addressHash, txid, nTxOut);
	CAddressUnspentValue value;
	if (!Read(make_pair(DB_ADDRESSUNSPENTINDEX, key), value))
		return nullopt;
	return value;
}

bool CBlockTreeDB::ReadSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value) const
{
    return Read(make_pair(DB_SPENTINDEX, key), value);
}

bool CBlockTreeDB::UpdateSpentIndex(const spent_index_vector_t &v)
{
    if (v.empty())
        return true;

    LogFnPrint("txdb", "SpentIndex - updating %zu entries", v.size());
    CDBBatch batch(*this);
    for (const auto &[key, value] : v)
    {
        if (value.IsNull())
			batch.Erase(make_pair(DB_SPENTINDEX, key));
		else
			batch.Write(make_pair(DB_SPENTINDEX, key), value);
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::WriteTimestampIndex(const CTimestampIndexKey &timestampIndex)
{
    CDBBatch batch(*this);
    batch.Write(make_pair(DB_TIMESTAMPINDEX, timestampIndex), 0);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadTimestampIndex(unsigned int high, unsigned int low,
    const bool fActiveOnly, vector<pair<uint256, unsigned int> > &vHashes)
{
    AssertLockHeld(cs_main);
    unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_TIMESTAMPINDEX, CTimestampIndexIteratorKey(low)));

    while (pcursor->Valid())
    {
        func_thread_interrupt_point();
        pair<char, CTimestampIndexKey> key;
        if (!(pcursor->GetKey(key) && (key.first == DB_TIMESTAMPINDEX) && key.second.timestamp < high))
            break;
        if (fActiveOnly)
        {
            CBlockIndex* pblockindex = mapBlockIndex[key.second.blockHash];
            if (chainActive.Contains(pblockindex))
                vHashes.emplace_back(key.second.blockHash, key.second.timestamp);
        } else
            vHashes.emplace_back(key.second.blockHash, key.second.timestamp);
        pcursor->Next();
    }
    return true;
}

bool CBlockTreeDB::WriteTimestampBlockIndex(const CTimestampBlockIndexKey &blockhashIndex,
    const CTimestampBlockIndexValue &logicalts)
{
    CDBBatch batch(*this);
    batch.Write(make_pair(DB_BLOCKHASHINDEX, blockhashIndex), logicalts);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadTimestampBlockIndex(const uint256 &hash, unsigned int &ltimestamp) const
{
    CTimestampBlockIndexValue(lts);
    if (!Read(make_pair(DB_BLOCKHASHINDEX, hash), lts))
        return false;

    ltimestamp = lts.ltimestamp;
    return true;
}

bool CBlockTreeDB::WriteFundsTransferIndex(const funds_transfer_vector_t &vFundsTransferIndex)
{
	if (vFundsTransferIndex.empty())
		return true;

	LogFnPrint("txdb", "FundsTransferIndex - writing %zu entries", vFundsTransferIndex.size());

	CDBBatch batch(*this);
	for (const auto &[key, value] : vFundsTransferIndex)
		batch.Write(make_pair(DB_FUNDSTRANSFERINDEX, key), value);
	return WriteBatch(batch);
}

bool CBlockTreeDB::ReadFundsTransferIndex(
    const uint160& addressHashFrom, const ScriptType addressTypeFrom,
    const uint160& addressHashTo,   const ScriptType addressTypeTo,
    funds_transfer_vector_t& vFundsTransferIndex,
    const height_range_opt_t &height_range) const
{
    uint32_t nStartHeight = 0;
    uint32_t nEndHeight = 0;
    if (height_range)
    {
        tie(nStartHeight, nEndHeight) = height_range.value();
        LogFnPrint("txdb", "FundsTransferIndex - reading address %s, type %hhu, height range [%u..%u]",
            addressHashFrom.GetHex(), to_integral_type(addressTypeFrom), nStartHeight, nEndHeight);
    }
	else
		LogFnPrint("txdb", "FundsTransferIndex - reading address %s, type %hhu", addressHashFrom.GetHex(), to_integral_type(addressTypeFrom));

	unique_ptr<CDBIterator> pcursor(NewIterator());

    if (height_range && nStartHeight > 0)
    {
        if (nEndHeight < nStartHeight)
            return error("invalid height range");

        pcursor->Seek(make_pair(DB_FUNDSTRANSFERINDEX, 
            CFundsTransferIndexIteratorHeightKey(addressTypeFrom, addressHashFrom, addressTypeTo, addressHashTo, nStartHeight)));
    }
    else
        pcursor->Seek(make_pair(DB_FUNDSTRANSFERINDEX,
            CFundsTransferIndexIteratorKey(addressTypeFrom, addressHashFrom, addressTypeTo, addressHashTo)));

	while (pcursor->Valid())
	{
		func_thread_interrupt_point();
		pair<char, CFundsTransferIndexKey> key;
        if (!pcursor->GetKey(key))
            break;

        auto &idxKey = key.second;
		if (!((key.first == DB_FUNDSTRANSFERINDEX) && 
              (idxKey.addressTypeFrom == addressTypeFrom) &&
              (idxKey.addressHashFrom == addressHashFrom) &&
              (idxKey.addressTypeTo == addressTypeTo) &&
              (idxKey.addressHashTo == addressHashTo)))
			break;

		if (nEndHeight > 0 && idxKey.blockHeight > nEndHeight)
			break;

		CFundsTransferIndexValue value;
		if (!pcursor->GetValue(value))
			return error("failed to get funds transfer index value");

		vFundsTransferIndex.emplace_back(idxKey, value);
		pcursor->Next();
	}
	return true;
}

bool CBlockTreeDB::EraseFundsTransferIndex(const funds_transfer_vector_t& vFundsTransferIndex)
{
    if (vFundsTransferIndex.empty())
		return true;

	LogFnPrint("txdb", "FundsTransferIndex - erasing %zu entries", vFundsTransferIndex.size());

	CDBBatch batch(*this);
	for (const auto &[key, value] : vFundsTransferIndex)
		batch.Erase(make_pair(DB_FUNDSTRANSFERINDEX, key));
	return WriteBatch(batch);
}

bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value)
{
    AssertLockHeld(cs_main);
    if (!fSpentIndex)
    {
        LogPrint("rpc", "Spent index not enabled\n");
        return false;
    }
    if (mempool.getSpentIndex(key, value))
        return true;

    if (!gl_pBlockTreeDB->ReadSpentIndex(key, value))
    {
        LogPrint("rpc", "Unable to get spent index information\n");
        return false;
    }
    return true;
}

bool GetAddressIndex(const uint160& addressHash, const ScriptType addressType,
                     address_index_vector_t& vAddressIndex,
                     const height_range_opt_t &height_range)
{
    if (!fAddressIndex)
    {
        LogPrint("rpc", "Address index not enabled\n");
        return false;
    }

    if (!gl_pBlockTreeDB->ReadAddressIndex(addressHash, addressType, vAddressIndex, height_range))
    {
        LogPrint("rpc", "Unable to get txids for address\n");
        return false;
    }
    return true;
}

bool GetAddressIndexAll(address_index_vector_t& vAddressIndex,
    const height_range_opt_t& height_range)
{
    if (!fAddressIndex)
    {
        LogPrint("rpc", "Address index not enabled\n");
        return false;
    }

    if (!gl_pBlockTreeDB->ReadAddressIndexAll(vAddressIndex, height_range))
    {
        LogPrint("rpc", "Unable to get all address index information\n");
        return false;
    }
    return true;
}

bool GetFundsTransferIndex(const uint160& addressHashFrom, const ScriptType addressTypeFrom,
    const uint160& addressHashTo, const ScriptType addressTypeTo,
    funds_transfer_vector_t& vFundsTransferIndex,
    const height_range_opt_t& height_range)
{
    if (!fFundsTransferIndex)
    {
        LogPrint("rpc", "Funds transfer index not enabled\n");
        return false;
    }
	
    if (!gl_pBlockTreeDB->ReadFundsTransferIndex(addressHashFrom, addressTypeFrom, 
        addressHashTo, addressTypeTo, vFundsTransferIndex, height_range))
	{
		LogPrint("rpc", "Unable to get funds transfer index information\n");
		return false;
	}
    return true;
}

bool GetAddressUnspent(const uint160& addressHash, const ScriptType addressType,
                       address_unspent_vector_t& vUnspentOutputs)
{
    if (!fAddressIndex)
    {
        LogPrint("rpc", "Address index not enabled\n");
        return false;
    }
    if (!gl_pBlockTreeDB->ReadAddressUnspentIndex(addressHash, addressType, vUnspentOutputs))
    {
        LogPrint("rpc", "Unable to get txids for address\n");
        return false;
    }
    return true;
}

optional<CAddressUnspentValue> GetAddressUnspent(const uint160& addressHash, const ScriptType addressType,
    const uint256& txid, const uint32_t nTxOut)
{
    if (!fAddressIndex)
    {
        LogPrint("rpc", "Address index not enabled\n");
        return nullopt;
    }
    return gl_pBlockTreeDB->GetAddressUnspentIndexValue(addressHash, addressType, txid, nTxOut);
}

bool GetTimestampIndex(unsigned int high, unsigned int low, bool fActiveOnly,
    vector<pair<uint256, unsigned int> > &vHashes)
{
    if (!fTimestampIndex)
    {
        LogPrint("rpc", "Timestamp index not enabled\n");
        return false;
    }
    if (!gl_pBlockTreeDB->ReadTimestampIndex(high, low, fActiveOnly, vHashes))
    {
        LogPrint("rpc", "Unable to get vHashes for timestamps\n");
        return false;
    }
    return true;
}

// END insightexplorer
