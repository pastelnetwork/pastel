#pragma once

#include "consensus/upgrades.h"
#include "txmempool.h"

struct TestMemPoolEntryHelper {
    // Default values
    CAmount nFee;
    int64_t nTime;
    double dPriority;
    unsigned int nHeight;
    bool hadNoDependencies;
    bool spendsCoinbase;
    uint32_t nBranchId;

    TestMemPoolEntryHelper() : nFee(0),
                               nTime(0),
                               dPriority(0.0),
                               nHeight(1),
                               hadNoDependencies(false),
                               spendsCoinbase(false),
                               nBranchId(SPROUT_BRANCH_ID)
    {}

    CTxMemPoolEntry FromTx(const CMutableTransaction& tx, CTxMemPool* pool = nullptr)
    {
        return CTxMemPoolEntry(tx, nFee, nTime, dPriority, nHeight,
                               pool ? pool->HasNoInputsOf(tx) : hadNoDependencies,
                               spendsCoinbase, nBranchId);
    }

    // Change the default value
    TestMemPoolEntryHelper& Fee(const CAmount _fee) noexcept
    {
        nFee = _fee;
        return *this;
    }
    TestMemPoolEntryHelper& Time(const int64_t _time) noexcept
    {
        nTime = _time;
        return *this;
    }
    TestMemPoolEntryHelper& Priority(const double _priority) noexcept
    {
        dPriority = _priority;
        return *this;
    }
    TestMemPoolEntryHelper& Height(const unsigned int _height) noexcept
    {
        nHeight = _height;
        return *this;
    }
    TestMemPoolEntryHelper& HadNoDependencies(const bool _hnd) noexcept
    {
        hadNoDependencies = _hnd;
        return *this;
    }
    TestMemPoolEntryHelper& SpendsCoinbase(const bool _flag) noexcept
    {
        spendsCoinbase = _flag;
        return *this;
    }
    TestMemPoolEntryHelper& BranchId(const uint32_t _branchId) noexcept
    {
        nBranchId = _branchId;
        return *this;
    }
};
