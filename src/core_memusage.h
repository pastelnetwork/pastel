#pragma once
// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <primitives/transaction.h>
#include <primitives/block.h>
#include <memusage.h>

static inline size_t RecursiveDynamicUsage(const CScript& script)
{
    return memusage::DynamicUsage(*static_cast<const CScriptBase*>(&script));
}

static inline size_t RecursiveDynamicUsage(const COutPoint& out)
{
    return 0;
}

static inline size_t RecursiveDynamicUsage(const CTxIn& in) {
    return RecursiveDynamicUsage(in.scriptSig) + RecursiveDynamicUsage(in.prevout);
}

static inline size_t RecursiveDynamicUsage(const CTxOut& out) {
    return RecursiveDynamicUsage(out.scriptPubKey);
}

static inline size_t RecursiveDynamicUsage(const CTransaction& tx)
{
    size_t mem = memusage::DynamicUsage(tx.vin) + memusage::DynamicUsage(tx.vout);
    for (const auto &txIn : tx.vin)
        mem += RecursiveDynamicUsage(txIn);
    for (const auto &txOut : tx.vout)
        mem += RecursiveDynamicUsage(txOut);
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CMutableTransaction& tx)
{
    size_t mem = memusage::DynamicUsage(tx.vin) + memusage::DynamicUsage(tx.vout);
    for (const auto &txIn : tx.vin)
        mem += RecursiveDynamicUsage(txIn);
    for (const auto &txOut : tx.vout)
        mem += RecursiveDynamicUsage(txOut);
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CBlock& block)
{
    size_t mem = memusage::DynamicUsage(block.vtx) + memusage::DynamicUsage(block.vMerkleTree);
    for (const auto &tx : block.vtx)
        mem += RecursiveDynamicUsage(tx);
    return mem;
}

static inline size_t RecursiveDynamicUsage(const CBlockLocator& locator)
{
    return memusage::DynamicUsage(locator.vHave);
}
