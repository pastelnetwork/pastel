// Copyright (c) 2012-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>

#include <utils/util.h>

#include <leveldb/cache.h>
#include <leveldb/env.h>
#include <leveldb/filter_policy.h>

#include <dbwrapper.h>
#include <memenv.h>

using namespace std;

static leveldb::Options GetOptions(size_t nCacheSize)
{
    leveldb::Options options;
    options.block_cache = leveldb::NewLRUCache(nCacheSize / 2);
    options.write_buffer_size = nCacheSize / 4; // up to two write buffers may be held in memory simultaneously
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    options.compression = leveldb::kNoCompression;
    options.max_open_files = 1000;
    if (leveldb::kMajorVersion > 1 || (leveldb::kMajorVersion == 1 && leveldb::kMinorVersion >= 16)) {
        // LevelDB versions before 1.16 consider short writes to be corruption. Only trigger error
        // on corruption in later versions.
        options.paranoid_checks = true;
    }
    return options;
}

CDBWrapper::CDBWrapper(const fs::path& path, size_t nCacheSize, bool fMemory, bool fWipe) :
    pdb(nullptr),
    penv(nullptr),
    m_bCreated(false)
{
    readoptions.verify_checksums = true;
    iteroptions.verify_checksums = true;
    iteroptions.fill_cache = false;
    syncoptions.sync = true;
    options = GetOptions(nCacheSize);
    options.create_if_missing = true;
    if (fMemory)
    {
        penv = leveldb::NewMemEnv(leveldb::Env::Default());
        options.env = penv;
        LogPrintf("Creating in-memory LevelDB\n");
        leveldb::Status status = leveldb::DB::Open(options, "", &pdb);
        dbwrapper_private::HandleError(status);
        LogPrintf("Created in-memory LevelDB successfully\n");
        m_bCreated = true;
    } else {
        // Initial attempt should not create DB
        options.create_if_missing = false;
        if (fWipe)
        {
            LogPrintf("Wiping LevelDB in '%s'\n", path.string());
            leveldb::Status result = leveldb::DestroyDB(path.string(), options);
            dbwrapper_private::HandleError(result);
        }
        TryCreateDirectory(path);
        LogPrintf("Opening LevelDB in '%s'\n", path.string());
        leveldb::Status status = leveldb::DB::Open(options, path.string(), &pdb);

        if (!status.ok() && (status.IsNotFound() || status.IsInvalidArgument()))
        {
            // If the database does not exist, try to create it
            LogPrintf("LevelDB not found in '%s', creating new LevelDB\n", path.string());
            options.create_if_missing = true;
            status = leveldb::DB::Open(options, path.string(), &pdb);
            if (status.ok())
            {
                LogPrintf("Created new LevelDB in '%s' successfully\n", path.string());
                m_bCreated = true;
            }
        }
        else if (status.ok())
        {
            LogPrintf("Opened existing LevelDB in '%s' successfully\n", path.string());
        }

        dbwrapper_private::HandleError(status);
    }
    LogPrintf("Opened LevelDB successfully\n");
}

CDBWrapper::~CDBWrapper()
{
    safe_delete_obj(pdb);
    safe_delete_obj(options.filter_policy);
    safe_delete_obj(options.block_cache);
    safe_delete_obj(penv);
    options.env = nullptr;
}

bool CDBWrapper::WriteBatch(CDBBatch& batch, bool fSync)
{
    const auto status = pdb->Write(fSync ? syncoptions : writeoptions, &batch.batch);
    dbwrapper_private::HandleError(status);
    return true;
}

bool CDBWrapper::IsEmpty()
{
    auto it = NewIterator();
    it->SeekToFirst();
    return !(it->Valid());
}

CDBIterator::~CDBIterator() { delete piter; }
bool CDBIterator::Valid() const { return piter->Valid(); }
void CDBIterator::SeekToFirst() { piter->SeekToFirst(); }
void CDBIterator::Next() { piter->Next(); }

namespace dbwrapper_private {

void HandleError(const leveldb::Status& status)
{
    if (status.ok())
        return;
    LogPrintf("%s\n", status.ToString());
    if (status.IsCorruption())
        throw dbwrapper_error("Database corrupted");
    if (status.IsIOError())
        throw dbwrapper_error("Database I/O error");
    if (status.IsNotFound())
        throw dbwrapper_error("Database entry missing");
    throw dbwrapper_error("Unknown database error");
}

};
