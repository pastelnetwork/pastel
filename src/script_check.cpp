// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <utils/util.h>
#include <script_check.h>
#include <checkqueue.h>
#include <script/sigcache.h>

CScriptCheckManager gl_ScriptCheckManager;

using namespace std;

CScriptCheck::CScriptCheck() : 
    amount(0),
    ptxTo(0),
    nIn(0),
    nFlags(0),
    cacheStore(false),
    consensusBranchId(0),
    error(SCRIPT_ERR_UNKNOWN_ERROR),
    txdata(nullptr)
{}

CScriptCheck::CScriptCheck(const CCoins& txFromIn, const CTransaction& txToIn, 
        unsigned int nInIn, unsigned int nFlagsIn, bool cacheIn, 
        uint32_t consensusBranchIdIn, PrecomputedTransactionData* txdataIn) : 
    scriptPubKey(txFromIn.vout[txToIn.vin[nInIn].prevout.n].scriptPubKey),
    amount(txFromIn.vout[txToIn.vin[nInIn].prevout.n].nValue),
    ptxTo(&txToIn),
    nIn(nInIn),
    nFlags(nFlagsIn),
    cacheStore(cacheIn),
    consensusBranchId(consensusBranchIdIn),
    error(SCRIPT_ERR_UNKNOWN_ERROR),
    txdata(txdataIn)
{}

void CScriptCheck::swap(CScriptCheck& check) noexcept
{
    scriptPubKey.swap(check.scriptPubKey);
    std::swap(ptxTo, check.ptxTo);
    std::swap(amount, check.amount);
    std::swap(nIn, check.nIn);
    std::swap(nFlags, check.nFlags);
    std::swap(cacheStore, check.cacheStore);
    std::swap(consensusBranchId, check.consensusBranchId);
    std::swap(error, check.error);
    std::swap(txdata, check.txdata);
}

bool CScriptCheck::operator()()
{
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    if (!VerifyScript(scriptSig, scriptPubKey, nFlags, CachingTransactionSignatureChecker(ptxTo, nIn, amount, cacheStore, *txdata), consensusBranchId, &error))
        return ::error("CScriptCheck(): %s:%d VerifySignature failed: %s", ptxTo->GetHash().ToString(), nIn, ScriptErrorString(error));
    return true;
}

/**
 * Script Check Manager.
 */
CScriptCheckManager::CScriptCheckManager() :
    m_nScriptCheckThreads(DEFAULT_SCRIPTCHECK_THREADS),
    m_ScriptCheckQueue(SCRIPTCHECK_QUEUE_BATCH_SIZE)
{}

void CScriptCheckManager::SetThreadCount(const int64_t nThreadCount)
{
    if (nThreadCount <= 0)
        m_nScriptCheckThreads = GetNumCores();
    else
        m_nScriptCheckThreads = static_cast<size_t>(nThreadCount);
    if (m_nScriptCheckThreads > MAX_SCRIPTCHECK_THREADS)
        m_nScriptCheckThreads = MAX_SCRIPTCHECK_THREADS;
}

/**
 * Create script verification workers.
 * 
 * \param threadGroup - add workers to this thread group
 */
void CScriptCheckManager::create_workers(CServiceThreadGroup &threadGroup)
{
    if (!m_nScriptCheckThreads)
    {
        LogPrintf("Script verification is disabled\n");
        return;
    }
    LogPrintf("Using %zu threads for script verification\n", m_nScriptCheckThreads);
    string sThreadName, error;
    for (size_t i = 0; i < m_nScriptCheckThreads - 1; ++i)
    {
        sThreadName = strprintf("scr-ch%d", i + 1);
        threadGroup.add_thread(error, make_shared<CScriptCheckWorker>(&m_ScriptCheckQueue, false, sThreadName.c_str()), true);
    }
}

unique_ptr<CScriptCheckWorker> CScriptCheckManager::create_master(const bool bEnabled)
{
    return make_unique<CScriptCheckWorker>(bEnabled && m_nScriptCheckThreads ? &m_ScriptCheckQueue : nullptr, 
        true, "scr-chm");
}
