#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <amount.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/interpreter.h>
#include <coins.h>
#include <checkqueue.h>

/** -par default (number of script-checking threads, 0 = auto) */
static constexpr size_t DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Maximum number of script-checking threads allowed */
static constexpr size_t MAX_SCRIPTCHECK_THREADS = 16;
/**  Script Check Queue Batch Size */
static constexpr size_t SCRIPTCHECK_QUEUE_BATCH_SIZE = 128;

/** 
 * Closure representing one script verification
 * Note that this stores references to the spending transaction 
 */
class CScriptCheck
{
private:
    CScript scriptPubKey;
    CAmount amount;
    const CTransaction* ptxTo;
    unsigned int nIn;
    unsigned int nFlags;
    bool cacheStore;
    uint32_t consensusBranchId;
    ScriptError error;
    PrecomputedTransactionData* txdata;

public:
    CScriptCheck();

    CScriptCheck(const CCoins& txFromIn, const CTransaction& txToIn, 
            unsigned int nInIn, unsigned int nFlagsIn, bool cacheIn, 
            uint32_t consensusBranchIdIn, PrecomputedTransactionData* txdataIn);

    bool operator()();

    void swap(CScriptCheck& check) noexcept;
    ScriptError GetScriptError() const noexcept { return error; }
};

using CScriptCheckWorker = CCheckQueueWorkerThread<CScriptCheck>;

class CScriptCheckManager
{
public:
    CScriptCheckManager();

    void SetThreadCount(const int64_t nThreadCount);
    size_t GetThreadCount() const noexcept { return m_nScriptCheckThreads; }

    // create script verification workers
    void create_workers(CServiceThreadGroup &threadGroup);

    std::unique_ptr<CScriptCheckWorker> create_master(const bool bEnabled);

private:
    size_t m_nScriptCheckThreads; // number of script check worker threads

    // script check queue
    CCheckQueue<CScriptCheck> m_ScriptCheckQueue;
};

extern CScriptCheckManager gl_ScriptCheckManager;

