#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <variant>
#include <vector>
#include <set>
#include <memory>

#include <utils/uint256.h>
#include <script/interpreter.h>
#include <script/scripttype.h>
#include <pubkey.h>
#include <chainparams.h>

class CKeyID;
class CScript;

/** A reference to a CScript: the Hash160 of its serialization (see script.h) */
class CScriptID : public uint160
{
public:
    CScriptID() : uint160() {}
    explicit CScriptID(const CScript& in);
    CScriptID(const uint160& in) : uint160(in) {}
};

constexpr unsigned int MAX_OP_RETURN_RELAY = 80;      //! bytes
extern unsigned nMaxDatacarrierBytes;

/**
 * Mandatory script verification flags that all new blocks must comply with for
 * them to be valid. (but old blocks may not comply with) Currently just P2SH,
 * but in the future other flags may be added.
 *
 * Failing one of these tests may trigger a DoS ban - see CheckInputs() for
 * details.
 */
constexpr unsigned int MANDATORY_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_P2SH;

/**
 * Standard script verification flags that standard transactions will comply
 * with. However scripts violating these flags may still be present in valid
 * blocks and we must accept those blocks.
 */
constexpr unsigned int STANDARD_SCRIPT_VERIFY_FLAGS = MANDATORY_SCRIPT_VERIFY_FLAGS |
                                                         // SCRIPT_VERIFY_DERSIG is always enforced
                                                         SCRIPT_VERIFY_STRICTENC |
                                                         SCRIPT_VERIFY_MINIMALDATA |
                                                         SCRIPT_VERIFY_NULLDUMMY |
                                                         SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS |
                                                         SCRIPT_VERIFY_CLEANSTACK |
                                                         SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY |
                                                         SCRIPT_VERIFY_LOW_S;

/** For convenience, standard but not mandatory verify flags. */
constexpr unsigned int STANDARD_NOT_MANDATORY_VERIFY_FLAGS = STANDARD_SCRIPT_VERIFY_FLAGS & ~MANDATORY_SCRIPT_VERIFY_FLAGS;

enum txnouttype
{
    TX_NONSTANDARD = 0,
    // 'standard' transaction types:
    TX_PUBKEY,
    TX_PUBKEYHASH,
    TX_SCRIPTHASH,
    TX_MULTISIG,
    TX_NULL_DATA
};

class CNoDestination
{
public:
    friend bool operator==(const CNoDestination &a, const CNoDestination &b) { return true; }
    friend bool operator!=(const CNoDestination &a, const CNoDestination &b) { return false; }
    friend bool operator<(const CNoDestination &a, const CNoDestination &b) { return true; }
};

/**
 * A txout script template with a specific destination. It is either:
 *  * CNoDestination: no destination set
 *  * CKeyID: TX_PUBKEYHASH destination
 *  * CScriptID: TX_SCRIPTHASH destination
 *  A CTxDestination is the internal data type encoded in a Pastel address
 */
typedef std::variant<CNoDestination, CKeyID, CScriptID> CTxDestination;

/** Check whether a CTxDestination is a CNoDestination. */
bool IsValidDestination(const CTxDestination& dest) noexcept;

/** Check whether a CTxDestination is a CKeyID. */
bool IsKeyDestination(const CTxDestination& dest) noexcept;

/** Check whether a CTxDestination is a CScriptID. */
bool IsScriptDestination(const CTxDestination& dest) noexcept;

/** Get the name of a txnouttype as a C string, or nullptr if unknown. */
const char* GetTxnOutputType(const txnouttype t);

// This function accepts an address and returns in the output parameters
// the version and raw bytes for the RIPEMD-160 hash.
bool GetTxDestinationHash(const CTxDestination& dest, uint160& hashBytes, ScriptType& type);

using txdest_vector_t = std::vector<CTxDestination>;
using txdest_set_t = std::set<CTxDestination>;
using txdest_unique_set_t = std::unique_ptr<txdest_set_t>;
using txdest_group_set_t = std::set<txdest_set_t>;

// Custom comparator for unique_ptr<txdest_set_t>
struct CompareTxDestSet
{
    bool operator()(const txdest_unique_set_t& lhs, const txdest_unique_set_t& rhs) const
    {
        if (!lhs || !rhs)
            return false;
        return *lhs < *rhs;  // Compare the underlying sets
    }
};
using txdest_unique_group_set_t = std::set<txdest_unique_set_t, CompareTxDestSet>;

struct CompareTxDestConstIterator
{
    bool operator()(const txdest_unique_group_set_t::const_iterator& lhs, const txdest_unique_group_set_t::const_iterator& rhs) const
    {
        return *lhs < *rhs;
    }
};

/**
 * Parse a scriptPubKey and identify script type for standard scripts. If
 * successful, returns script type and parsed pubkeys or hashes, depending on 
 * the type. For example, for a P2SH script, vSolutionsRet will contain the 
 * script hash, for P2PKH it will contain the key hash, etc.
 *
 * @param[in]   scriptPubKey   Script to parse
 * @param[out]  typeRet        The script type
 * @param[out]  vSolutionsRet  Vector of parsed pubkeys and hashes
 * @return                     True if script matches standard template
 */
bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<v_uint8>& vSolutionsRet);
int ScriptSigArgsExpected(txnouttype t, const std::vector<v_uint8>& vSolutions);
bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType);
/**
 * Parse a standard scriptPubKey for the destination address. Assigns result to
 * the addressRet parameter and returns true if successful. For multisig
 * scripts (that can have multiple destination addresses), instead use ExtractDestinations. 
 * Currently only works for P2PK, P2PKH, and P2SH scripts.
 */
bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet, txnouttype* pScriptType = nullptr);

/**
 * Parse a standard scriptPubKey with one or more destination addresses. For
 * multisig scripts, this populates the addressRet vector with the pubkey IDs
 * and nRequiredRet with the n required to spend. For other destinations,
 * addressRet is populated with a single value and nRequiredRet is set to 1.
 * Returns true if successful.
 */
bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, txdest_vector_t& addressRet, int& nRequiredRet);

/**
 * Generate a Bitcoin scriptPubKey for the given CTxDestination. Returns a P2PKH
 * script for a CKeyID destination, a P2SH script for a CScriptID, and an empty
 * script for CNoDestination.
 */
CScript GetScriptForDestination(const CTxDestination& dest);
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);

// insightexplorer
CTxDestination DestFromAddressHash(const ScriptType scriptType, const uint160& addressHash);
