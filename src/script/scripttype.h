#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

// insightexplorer, there may be more script types in the future
typedef enum class _ScriptType : uint8_t
{
    UNKNOWN = 0,
    P2PKH = 1, // pay to pubkey hash (OP_DUP OP_HASH160 <PubKeyHash> OP_EQUALVERIFY OP_CHECKSIG)
    P2SH = 2,  // pay to script hash (OP_HASH160 <ScriptHash> OP_EQUAL)
} ScriptType;
