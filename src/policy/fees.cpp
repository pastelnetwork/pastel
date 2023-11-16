// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <utils/util.h>
#include <policy/fees.h>
#include <amount.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <txmempool.h>

using namespace std;

void TxConfirmStats::Initialize(v_doubles& defaultBuckets,
                                unsigned int maxConfirms, double _decay, string _dataTypeString)
{
    decay = _decay;
    dataTypeString = _dataTypeString;

    buckets.insert(buckets.end(), defaultBuckets.begin(), defaultBuckets.end());
    buckets.push_back(numeric_limits<double>::infinity());

    for (unsigned int i = 0; i < buckets.size(); i++) {
        bucketMap[buckets[i]] = i;
    }

    confAvg.resize(maxConfirms);
    curBlockConf.resize(maxConfirms);
    unconfTxs.resize(maxConfirms);
    for (unsigned int i = 0; i < maxConfirms; i++) {
        confAvg[i].resize(buckets.size());
        curBlockConf[i].resize(buckets.size());
        unconfTxs[i].resize(buckets.size());
    }

    oldUnconfTxs.resize(buckets.size());
    curBlockTxCt.resize(buckets.size());
    txCtAvg.resize(buckets.size());
    curBlockVal.resize(buckets.size());
    avg.resize(buckets.size());
}

// Zero out the data for the current block
void TxConfirmStats::ClearCurrent(unsigned int nBlockHeight)
{
    for (unsigned int j = 0; j < buckets.size(); j++) {
        oldUnconfTxs[j] += unconfTxs[nBlockHeight%unconfTxs.size()][j];
        unconfTxs[nBlockHeight%unconfTxs.size()][j] = 0;
        for (unsigned int i = 0; i < curBlockConf.size(); i++)
            curBlockConf[i][j] = 0;
        curBlockTxCt[j] = 0;
        curBlockVal[j] = 0;
    }
}

unsigned int TxConfirmStats::FindBucketIndex(double val)
{
    auto it = bucketMap.lower_bound(val);
    assert(it != bucketMap.end());
    return it->second;
}

void TxConfirmStats::Record(int blocksToConfirm, double val)
{
    // blocksToConfirm is 1-based
    if (blocksToConfirm < 1)
        return;
    unsigned int bucketindex = FindBucketIndex(val);
    for (size_t i = blocksToConfirm; i <= curBlockConf.size(); i++) {
        curBlockConf[i - 1][bucketindex]++;
    }
    curBlockTxCt[bucketindex]++;
    curBlockVal[bucketindex] += val;
}

void TxConfirmStats::UpdateMovingAverages()
{
    for (unsigned int j = 0; j < buckets.size(); j++) {
        for (unsigned int i = 0; i < confAvg.size(); i++)
            confAvg[i][j] = confAvg[i][j] * decay + curBlockConf[i][j];
        avg[j] = avg[j] * decay + curBlockVal[j];
        txCtAvg[j] = txCtAvg[j] * decay + curBlockTxCt[j];
    }
}

// returns -1 on error conditions
double TxConfirmStats::EstimateMedianVal(const size_t nConfTarget, double sufficientTxVal,
                                         double successBreakPoint, bool requireGreater,
                                         unsigned int nBlockHeight)
{
    if (buckets.empty())
        return -1;
    // Counters for a bucket (or range of buckets)
    double nConf = 0; // Number of tx's confirmed within the confTarget
    double totalNum = 0; // Total number of tx's that were ever confirmed
    int extraNum = 0;  // Number of tx's still in mempool for confTarget or longer

    unsigned int maxbucketindex = static_cast<unsigned int>(buckets.size() - 1);

    // requireGreater means we are looking for the lowest fee/priority such that all higher
    // values pass, so we start at maxbucketindex (highest fee) and look at succesively
    // smaller buckets until we reach failure.  Otherwise, we are looking for the highest
    // fee/priority such that all lower values fail, and we go in the opposite direction.
    unsigned int startbucket = requireGreater ? maxbucketindex : 0;
    int step = requireGreater ? -1 : 1;

    // We'll combine buckets until we have enough samples.
    // The near and far variables will define the range we've combined
    // The best variables are the last range we saw which still had a high
    // enough confirmation rate to count as success.
    // The cur variables are the current range we're counting.
    unsigned int curNearBucket = startbucket;
    unsigned int bestNearBucket = startbucket;
    unsigned int curFarBucket = startbucket;
    unsigned int bestFarBucket = startbucket;

    bool foundAnswer = false;
    unsigned int bins = static_cast<unsigned int>(unconfTxs.size());

    // Start counting from highest(default) or lowest fee/pri transactions
    for (unsigned int bucket = startbucket; bucket <= maxbucketindex; bucket += step)
    {
        curFarBucket = bucket;
        nConf += confAvg[nConfTarget - 1][bucket];
        totalNum += txCtAvg[bucket];
        for (size_t confct = nConfTarget; confct < GetMaxConfirms(); confct++)
            extraNum += unconfTxs[(nBlockHeight - confct)%bins][bucket];
        extraNum += oldUnconfTxs[bucket];
        // If we have enough transaction data points in this range of buckets,
        // we can test for success
        // (Only count the confirmed data points, so that each confirmation count
        // will be looking at the same amount of data and same bucket breaks)
        if (totalNum >= sufficientTxVal / (1 - decay)) {
            double curPct = nConf / (totalNum + extraNum);

            // Check to see if we are no longer getting confirmed at the success rate
            if (requireGreater && curPct < successBreakPoint)
                break;
            if (!requireGreater && curPct > successBreakPoint)
                break;

            // Otherwise update the cumulative stats, and the bucket variables
            // and reset the counters
            else {
                foundAnswer = true;
                nConf = 0;
                totalNum = 0;
                extraNum = 0;
                bestNearBucket = curNearBucket;
                bestFarBucket = curFarBucket;
                curNearBucket = bucket + step;
            }
        }
    }

    double median = -1;
    double txSum = 0;

    // Calculate the "average" fee of the best bucket range that met success conditions
    // Find the bucket with the median transaction and then report the average fee from that bucket
    // This is a compromise between finding the median which we can't since we don't save all tx's
    // and reporting the average which is less accurate
    unsigned int minBucket = bestNearBucket < bestFarBucket ? bestNearBucket : bestFarBucket;
    unsigned int maxBucket = bestNearBucket > bestFarBucket ? bestNearBucket : bestFarBucket;
    for (unsigned int j = minBucket; j <= maxBucket; j++) {
        txSum += txCtAvg[j];
    }
    if (foundAnswer && txSum != 0) {
        txSum = txSum / 2;
        for (unsigned int j = minBucket; j <= maxBucket; j++) {
            if (txCtAvg[j] < txSum)
                txSum -= txCtAvg[j];
            else { // we're in the right bucket
                median = avg[j] / txCtAvg[j];
                break;
            }
        }
    }

    LogPrint("estimatefee", "%3zu: For conf success %s %4.2f need %s %s: %12.5g from buckets %8g - %8g  Cur Bucket stats %6.2f%%  %8.1f/(%.1f+%d mempool)\n",
             nConfTarget, requireGreater ? ">" : "<", successBreakPoint, dataTypeString,
             requireGreater ? ">" : "<", median, buckets[minBucket], buckets[maxBucket],
             100 * nConf / (totalNum + extraNum), nConf, totalNum, extraNum);

    return median;
}

void TxConfirmStats::Write(CAutoFile& fileout)
{
    fileout << decay;
    fileout << buckets;
    fileout << avg;
    fileout << txCtAvg;
    fileout << confAvg;
}

void TxConfirmStats::Read(CAutoFile& filein)
{
    // Read data file into temporary variables and do some very basic sanity checking
    v_doubles fileBuckets;
    v_doubles fileAvg;
    vector<v_doubles> fileConfAvg;
    v_doubles fileTxCtAvg;
    double fileDecay;
    size_t maxConfirms;
    size_t numBuckets;

    filein >> fileDecay;
    if (fileDecay <= 0 || fileDecay >= 1)
        throw runtime_error("Corrupt estimates file. Decay must be between 0 and 1 (non-inclusive)");
    filein >> fileBuckets;
    numBuckets = fileBuckets.size();
    if (numBuckets <= 1 || numBuckets > 1000)
        throw runtime_error("Corrupt estimates file. Must have between 2 and 1000 fee/pri buckets");
    filein >> fileAvg;
    if (fileAvg.size() != numBuckets)
        throw runtime_error("Corrupt estimates file. Mismatch in fee/pri average bucket count");
    filein >> fileTxCtAvg;
    if (fileTxCtAvg.size() != numBuckets)
        throw runtime_error("Corrupt estimates file. Mismatch in tx count bucket count");
    filein >> fileConfAvg;
    maxConfirms = fileConfAvg.size();
    if (maxConfirms <= 0 || maxConfirms > 6 * 24 * 7) // one week
        throw runtime_error("Corrupt estimates file.  Must maintain estimates for between 1 and 1008 (one week) confirms");
    for (unsigned int i = 0; i < maxConfirms; i++) {
        if (fileConfAvg[i].size() != numBuckets)
            throw runtime_error("Corrupt estimates file. Mismatch in fee/pri conf average bucket count");
    }
    // Now that we've processed the entire fee estimate data file and not
    // thrown any errors, we can copy it to our data structures
    decay = fileDecay;
    buckets = fileBuckets;
    avg = fileAvg;
    confAvg = fileConfAvg;
    txCtAvg = fileTxCtAvg;
    bucketMap.clear();

    // Resize the current block variables which aren't stored in the data file
    // to match the number of confirms and buckets
    curBlockConf.resize(maxConfirms);
    for (unsigned int i = 0; i < maxConfirms; i++) {
        curBlockConf[i].resize(buckets.size());
    }
    curBlockTxCt.resize(buckets.size());
    curBlockVal.resize(buckets.size());

    unconfTxs.resize(maxConfirms);
    for (unsigned int i = 0; i < maxConfirms; i++) {
        unconfTxs[i].resize(buckets.size());
    }
    oldUnconfTxs.resize(buckets.size());

    for (unsigned int i = 0; i < buckets.size(); i++)
        bucketMap[buckets[i]] = i;

    LogPrint("estimatefee", "Reading estimates: %u %s buckets counting confirms up to %u blocks\n",
             numBuckets, dataTypeString, maxConfirms);
}

unsigned int TxConfirmStats::NewTx(const uint32_t nBlockHeight, const double val, string &sMsg)
{
    unsigned int bucketindex = FindBucketIndex(val);
    unsigned int blockIndex = nBlockHeight % unconfTxs.size();
    unconfTxs[blockIndex][bucketindex]++;
    sMsg += strprintf("adding to %s", dataTypeString);
    return bucketindex;
}

void TxConfirmStats::removeTx(unsigned int entryHeight, unsigned int nBestSeenHeight, unsigned int bucketindex)
{
    //nBestSeenHeight is not updated yet for the new block
    int blocksAgo = nBestSeenHeight - entryHeight;
    if (nBestSeenHeight == 0)  // the BlockPolicyEstimator hasn't seen any blocks yet
        blocksAgo = 0;
    if (blocksAgo < 0) {
        LogPrint("estimatefee", "Blockpolicy error, blocks ago is negative for mempool tx\n");
        return;  //This can't happen because we call this with our best seen height, no entries can have higher
    }

    if (blocksAgo >= (int)unconfTxs.size()) {
        if (oldUnconfTxs[bucketindex] > 0)
            oldUnconfTxs[bucketindex]--;
        else
            LogPrint("estimatefee", "Blockpolicy error, mempool tx removed from >25 blocks,bucketIndex=%u already\n",
                     bucketindex);
    }
    else {
        unsigned int blockIndex = entryHeight % unconfTxs.size();
        if (unconfTxs[blockIndex][bucketindex] > 0)
            unconfTxs[blockIndex][bucketindex]--;
        else
            LogPrint("estimatefee", "Blockpolicy error, mempool tx removed from blockIndex=%u,bucketIndex=%u already\n",
                     blockIndex, bucketindex);
    }
}

void CBlockPolicyEstimator::removeTx(const uint256 &hash)
{
    auto pos = mapMemPoolTxs.find(hash);
    if (pos == mapMemPoolTxs.end()) {
        LogPrint("estimatefee", "Blockpolicy error mempool tx %s not found for removeTx\n",
                 hash.ToString().c_str());
        return;
    }
    TxConfirmStats *stats = pos->second.stats;
    unsigned int entryHeight = pos->second.blockHeight;
    unsigned int bucketIndex = pos->second.bucketIndex;

    if (stats)
        stats->removeTx(entryHeight, nBestSeenHeight, bucketIndex);
    mapMemPoolTxs.erase(hash);
}

CBlockPolicyEstimator::CBlockPolicyEstimator(const CFeeRate& _minRelayFee)
    : nBestSeenHeight(0)
{
    const auto nMinFeeRate = static_cast<CAmount>(MIN_FEERATE);
    minTrackedFee = _minRelayFee < CFeeRate(nMinFeeRate) ? CFeeRate(nMinFeeRate) : _minRelayFee;

    v_doubles vfeelist;
    for (double bucketBoundary = static_cast<double>(minTrackedFee.GetFeePerK()); bucketBoundary <= MAX_FEERATE; bucketBoundary *= FEE_SPACING)
        vfeelist.push_back(bucketBoundary);
    feeStats.Initialize(vfeelist, MAX_BLOCK_CONFIRMS, DEFAULT_DECAY, "FeeRate");

    minTrackedPriority = ALLOW_FREE_THRESHOLD < MIN_FEE_PRIORITY ? MIN_FEE_PRIORITY : ALLOW_FREE_THRESHOLD;

    v_doubles vprilist;
    for (double bucketBoundary = minTrackedPriority; bucketBoundary <= MAX_FEE_PRIORITY; bucketBoundary *= PRI_SPACING)
        vprilist.push_back(bucketBoundary);
    priStats.Initialize(vprilist, MAX_BLOCK_CONFIRMS, DEFAULT_DECAY, "Priority");

    feeUnlikely = CFeeRate(0);
    feeLikely = CFeeRate(static_cast<CAmount>(INF_FEERATE));
    priUnlikely = 0;
    priLikely = INF_PRIORITY;
}

bool CBlockPolicyEstimator::isFeeDataPoint(const CFeeRate &fee, double pri)
{
    if ((pri < minTrackedPriority && fee >= minTrackedFee) ||
        (pri < priUnlikely && fee > feeLikely)) {
        return true;
    }
    return false;
}

bool CBlockPolicyEstimator::isPriDataPoint(const CFeeRate &fee, double pri)
{
    if ((fee < minTrackedFee && pri >= minTrackedPriority) ||
        (fee < feeUnlikely && pri > priLikely)) {
        return true;
    }
    return false;
}

void CBlockPolicyEstimator::processTransaction(const CTxMemPoolEntry& entry, const bool fCurrentEstimate)
{
    const unsigned int txHeight = entry.GetHeight();
    const auto &hash = entry.GetTx().GetHash();
    if (mapMemPoolTxs[hash].stats)
    {
        LogPrint("estimatefee", "Blockpolicy error mempool tx %s already being tracked\n",
                 hash.ToString().c_str());
	    return;
    }

    if (txHeight < nBestSeenHeight) {
        // Ignore side chains and re-orgs; assuming they are random they don't
        // affect the estimate.  We'll potentially double count transactions in 1-block reorgs.
        return;
    }

    // Only want to be updating estimates when our blockchain is synced,
    // otherwise we'll miscalculate how many blocks its taking to get included.
    if (!fCurrentEstimate)
        return;

    if (!entry.WasClearAtEntry()) {
        // This transaction depends on other transactions in the mempool to
        // be included in a block before it will be able to be included, so
        // we shouldn't include it in our calculations
        return;
    }

    // Fees are stored and reported as BTC-per-kb:
    CFeeRate feeRate(entry.GetFee(), entry.GetTxSize());

    // Want the priority of the tx at confirmation. However we don't know
    // what that will be and its too hard to continue updating it
    // so use starting priority as a proxy
    double curPri = entry.GetPriority(txHeight);
    mapMemPoolTxs[hash].blockHeight = txHeight;

    string sMsg = strprintf("Blockpolicy mempool tx %s", hash.ToString().substr(0,10));
    // Record this as a priority estimate
    if (entry.GetFee() == 0 || isPriDataPoint(feeRate, curPri))
    {
        mapMemPoolTxs[hash].stats = &priStats;
        mapMemPoolTxs[hash].bucketIndex =  priStats.NewTx(txHeight, curPri, sMsg);
    }
    // Record this as a fee estimate
    else if (isFeeDataPoint(feeRate, curPri)) {
        mapMemPoolTxs[hash].stats = &feeStats;
        mapMemPoolTxs[hash].bucketIndex = feeStats.NewTx(txHeight, (double)feeRate.GetFeePerK(), sMsg);
    }
    else
        sMsg += " not adding";
    LogFnPrint("estimatefee", sMsg);
}

void CBlockPolicyEstimator::processBlockTx(unsigned int nBlockHeight, const CTxMemPoolEntry& entry)
{
    if (!entry.WasClearAtEntry()) {
        // This transaction depended on other transactions in the mempool to
        // be included in a block before it was able to be included, so
        // we shouldn't include it in our calculations
        return;
    }

    // How many blocks did it take for miners to include this transaction?
    // blocksToConfirm is 1-based, so a transaction included in the earliest
    // possible block has confirmation count of 1
    int blocksToConfirm = nBlockHeight - entry.GetHeight();
    if (blocksToConfirm <= 0) {
        // This can't happen because we don't process transactions from a block with a height
        // lower than our greatest seen height
        LogPrint("estimatefee", "Blockpolicy error Transaction had negative blocksToConfirm\n");
        return;
    }

    // Fees are stored and reported as BTC-per-kb:
    CFeeRate feeRate(entry.GetFee(), entry.GetTxSize());

    // Want the priority of the tx at confirmation.  The priority when it
    // entered the mempool could easily be very small and change quickly
    double curPri = entry.GetPriority(nBlockHeight);

    // Record this as a priority estimate
    if (entry.GetFee() == 0 || isPriDataPoint(feeRate, curPri)) {
        priStats.Record(blocksToConfirm, curPri);
    }
    // Record this as a fee estimate
    else if (isFeeDataPoint(feeRate, curPri)) {
        feeStats.Record(blocksToConfirm, (double)feeRate.GetFeePerK());
    }
}

void CBlockPolicyEstimator::processBlock(unsigned int nBlockHeight,
                                         vector<CTxMemPoolEntry>& entries, bool fCurrentEstimate)
{
    if (nBlockHeight <= nBestSeenHeight) {
        // Ignore side chains and re-orgs; assuming they are random
        // they don't affect the estimate.
        // And if an attacker can re-org the chain at will, then
        // you've got much bigger problems than "attacker can influence
        // transaction fees."
        return;
    }
    nBestSeenHeight = nBlockHeight;

    // Only want to be updating estimates when our blockchain is synced,
    // otherwise we'll miscalculate how many blocks its taking to get included.
    if (!fCurrentEstimate)
        return;

    // Update the dynamic cutoffs
    // a fee/priority is "likely" the reason your tx was included in a block if >85% of such tx's
    // were confirmed in 2 blocks and is "unlikely" if <50% were confirmed in 10 blocks
    LogPrint("estimatefee", "Blockpolicy recalculating dynamic cutoffs:\n");
    priLikely = priStats.EstimateMedianVal(2, SUFFICIENT_PRITXS, MIN_SUCCESS_PCT, true, nBlockHeight);
    if (priLikely == -1)
        priLikely = INF_PRIORITY;

    double feeLikelyEst = feeStats.EstimateMedianVal(2, SUFFICIENT_FEETXS, MIN_SUCCESS_PCT, true, nBlockHeight);
    if (feeLikelyEst == -1)
        feeLikely = CFeeRate(static_cast<CAmount>(INF_FEERATE));
    else
        feeLikely = CFeeRate(static_cast<CAmount>(feeLikelyEst));

    priUnlikely = priStats.EstimateMedianVal(10, SUFFICIENT_PRITXS, UNLIKELY_PCT, false, nBlockHeight);
    if (priUnlikely == -1)
        priUnlikely = 0;

    double feeUnlikelyEst = feeStats.EstimateMedianVal(10, SUFFICIENT_FEETXS, UNLIKELY_PCT, false, nBlockHeight);
    if (feeUnlikelyEst == -1)
        feeUnlikely = CFeeRate(0);
    else
        feeUnlikely = CFeeRate(static_cast<CAmount>(feeUnlikelyEst));

    // Clear the current block states
    feeStats.ClearCurrent(nBlockHeight);
    priStats.ClearCurrent(nBlockHeight);

    // Repopulate the current block states
    for (unsigned int i = 0; i < entries.size(); i++)
        processBlockTx(nBlockHeight, entries[i]);

    // Update all exponential averages with the current block states
    feeStats.UpdateMovingAverages();
    priStats.UpdateMovingAverages();

    LogPrint("estimatefee", "Blockpolicy after updating estimates for %u confirmed entries, new mempool map size %u\n",
             entries.size(), mapMemPoolTxs.size());
}

CFeeRate CBlockPolicyEstimator::estimateFee(const int confTarget)
{
    // Return failure if trying to analyze a target we're not tracking
    if (confTarget <= 0)
        return CFeeRate(0);
    const size_t nConfTarget = static_cast<size_t>(confTarget);
    if (nConfTarget > feeStats.GetMaxConfirms())
        return CFeeRate(0);

    const double median = feeStats.EstimateMedianVal(confTarget, SUFFICIENT_FEETXS, MIN_SUCCESS_PCT, true, nBestSeenHeight);
    if (median < 0)
        return CFeeRate(0);

    return CFeeRate(static_cast<CAmount>(median));
}

double CBlockPolicyEstimator::estimatePriority(int confTarget)
{
    // Return failure if trying to analyze a target we're not tracking
    if (confTarget <= 0 || (unsigned int)confTarget > priStats.GetMaxConfirms())
        return -1;

    return priStats.EstimateMedianVal(confTarget, SUFFICIENT_PRITXS, MIN_SUCCESS_PCT, true, nBestSeenHeight);
}

void CBlockPolicyEstimator::Write(CAutoFile& fileout)
{
    fileout << nBestSeenHeight;
    feeStats.Write(fileout);
    priStats.Write(fileout);
}

void CBlockPolicyEstimator::Read(CAutoFile& filein)
{
    int nFileBestSeenHeight;
    filein >> nFileBestSeenHeight;
    feeStats.Read(filein);
    priStats.Read(filein);
    nBestSeenHeight = nFileBestSeenHeight;
}
