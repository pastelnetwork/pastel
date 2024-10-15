#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <array>
#include <variant>
#include <vector>

#include <utils/uint256.h>
#include <utils/serialize.h>
#include <utils/streams.h>
#include <utils/random.h>
#include <amount.h>
#include <script/script.h>
#include <consensus/consensus.h>

#include <zcash/NoteEncryption.hpp>
#include <zcash/Zcash.h>
#include <zcash/Proof.hpp>
#include <zcash/Note.hpp>

// Overwinter transaction version
constexpr int32_t OVERWINTER_TX_VERSION = 3;
static_assert(OVERWINTER_TX_VERSION >= OVERWINTER_MIN_TX_VERSION,
    "Overwinter tx version must not be lower than minimum");
static_assert(OVERWINTER_TX_VERSION <= OVERWINTER_MAX_TX_VERSION,
    "Overwinter tx version must not be higher than maximum");

// Sapling transaction version
constexpr int32_t SAPLING_TX_VERSION = 4;
static_assert(SAPLING_TX_VERSION >= SAPLING_MIN_TX_VERSION,
    "Sapling tx version must not be lower than minimum");
static_assert(SAPLING_TX_VERSION <= SAPLING_MAX_TX_VERSION,
    "Sapling tx version must not be higher than maximum");

// Transaction change destination
enum class TxChangeDestination
{
	ORIGINAL,  // Send change to the original address
	NEW,       // Send change to a new address
	SPECIFIED  // Send change to a specified address
};

/**
 * A shielded input to a transaction. It contains data that describes a Spend transfer.
 */
class SpendDescription
{
public:
    typedef std::array<unsigned char, 64> spend_auth_sig_t;

    uint256 cv;                    //!< A value commitment to the value of the input note.
    uint256 anchor;                //!< A Merkle root of the Sapling note commitment tree at some block height in the past.
    uint256 nullifier;             //!< The nullifier of the input note.
    uint256 rk;                    //!< The randomized public key for spendAuthSig.
    libzcash::GrothProof zkproof;  //!< A zero-knowledge proof using the spend circuit.
    spend_auth_sig_t spendAuthSig; //!< A signature authorizing this spend.

    SpendDescription() : cv{}, anchor{}, nullifier{}, rk{}, zkproof{}, spendAuthSig{} {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(cv);
        READWRITE(anchor);
        READWRITE(nullifier);
        READWRITE(rk);
        READWRITE(zkproof);
        READWRITE(spendAuthSig);
    }

    friend bool operator==(const SpendDescription& a, const SpendDescription& b)
    {
        return (
            a.cv == b.cv &&
            a.anchor == b.anchor &&
            a.nullifier == b.nullifier &&
            a.rk == b.rk &&
            a.zkproof == b.zkproof &&
            a.spendAuthSig == b.spendAuthSig
            );
    }

    friend bool operator!=(const SpendDescription& a, const SpendDescription& b)
    {
        return !(a == b);
    }
};

/**
 * A shielded output to a transaction. It contains data that describes an Output transfer.
 */
class OutputDescription
{
public:
    uint256 cv;                     //!< A value commitment to the value of the output note.
    uint256 cm;                     //!< The note commitment for the output note.
    uint256 ephemeralKey;           //!< A Jubjub public key.
    libzcash::SaplingEncCiphertext encCiphertext; //!< A ciphertext component for the encrypted output note.
    libzcash::SaplingOutCiphertext outCiphertext; //!< A ciphertext component for the encrypted output note.
    libzcash::GrothProof zkproof;   //!< A zero-knowledge proof using the output circuit.

    OutputDescription() : cv{}, cm{}, ephemeralKey{}, encCiphertext{}, outCiphertext{}, zkproof{} {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(cv);
        READWRITE(cm);
        READWRITE(ephemeralKey);
        READWRITE(encCiphertext);
        READWRITE(outCiphertext);
        READWRITE(zkproof);
    }

    friend bool operator==(const OutputDescription& a, const OutputDescription& b)
    {
        return (
            a.cv == b.cv &&
            a.cm == b.cm &&
            a.ephemeralKey == b.ephemeralKey &&
            a.encCiphertext == b.encCiphertext &&
            a.outCiphertext == b.outCiphertext &&
            a.zkproof == b.zkproof
            );
    }

    friend bool operator!=(const OutputDescription& a, const OutputDescription& b)
    {
        return !(a == b);
    }
};

template <typename Stream>
class SproutProofSerializer
{
    Stream& s;
    bool useGroth;

public:
    SproutProofSerializer(Stream& s, bool useGroth) : s(s), useGroth(useGroth) {}

    void operator()(const libzcash::PHGRProof& proof) const
    {
        if (useGroth) {
            throw std::ios_base::failure("Invalid Sprout proof for transaction format (expected GrothProof, found PHGRProof)");
        }
        ::Serialize(s, proof);
    }

    void operator()(const libzcash::GrothProof& proof) const
    {
        if (!useGroth) {
            throw std::ios_base::failure("Invalid Sprout proof for transaction format (expected PHGRProof, found GrothProof)");
        }
        ::Serialize(s, proof);
    }
};

template<typename Stream, typename T>
inline void SerReadWriteSproutProof(Stream& s, T& proof, bool useGroth, const SERIALIZE_ACTION ser_action)
{
    if (ser_action == SERIALIZE_ACTION::Write)
    {
        auto ps = SproutProofSerializer<Stream>(s, useGroth);
        std::visit(ps, proof);
    }
    else
    {
        if (useGroth)
        {
            libzcash::GrothProof grothProof;
            ::Unserialize(s, grothProof);
            proof = grothProof;
        }
        else
        {
            libzcash::PHGRProof pghrProof;
            ::Unserialize(s, pghrProof);
            proof = pghrProof;
        }
    }
}

class BaseOutPoint
{
public:
    uint256 hash;
    uint32_t n;

    BaseOutPoint() noexcept { SetNull(); }
    BaseOutPoint(const uint256 &hashIn, const uint32_t nIn) noexcept
    { 
        hash = hashIn; 
        n = nIn;
    }

    BaseOutPoint(BaseOutPoint&& other) noexcept :
        hash(std::move(other.hash)),
        n(other.n)
    {
        other.SetNull();
    }

    BaseOutPoint(const BaseOutPoint& other) noexcept :
        hash(other.hash),
        n(other.n)
    {}

    BaseOutPoint& operator=(const BaseOutPoint& other) noexcept
    {
        if (this != &other)
        {
			hash = other.hash;
			n = other.n;
		}
		return *this;
    }

    BaseOutPoint& operator=(BaseOutPoint&& other) noexcept
    {
        if (this != &other)
        {
            hash = std::move(other.hash);
            n = other.n;
            other.SetNull();
        }
        return *this;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(hash);
        READWRITE(n);
    }

    void SetNull() noexcept
    { 
        hash.SetNull(); 
        n = std::numeric_limits<uint32_t>::max();
    }
    bool IsNull() const noexcept { return (hash.IsNull() && n == std::numeric_limits<uint32_t>::max()); }

    friend bool operator<(const BaseOutPoint& a, const BaseOutPoint& b) noexcept
    {
        return (a.hash < b.hash || (a.hash == b.hash && a.n < b.n));
    }

    friend bool operator==(const BaseOutPoint& a, const BaseOutPoint& b) noexcept
    {
        return (a.hash == b.hash && a.n == b.n);
    }

    friend bool operator!=(const BaseOutPoint& a, const BaseOutPoint& b) noexcept
    {
        return !(a == b);
    }
};

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint : public BaseOutPoint
{
public:
    COutPoint() noexcept : 
        BaseOutPoint()
    {}
    COutPoint(const uint256 &hashIn, const uint32_t nIn) noexcept : 
        BaseOutPoint(hashIn, nIn)
    {}

    COutPoint(COutPoint&& other) noexcept :
        BaseOutPoint(std::move(other))
    {}
    COutPoint(const COutPoint& other) noexcept :
		BaseOutPoint(other)
	{}

    COutPoint& operator=(const COutPoint& other) noexcept
    {
        if (this != &other)
			BaseOutPoint::operator=(other);
		return *this;
    }

    COutPoint& operator=(COutPoint&& other) noexcept
    {
        if (this != &other)
            BaseOutPoint::operator=(std::move(other));
        return *this;
    }

    std::string ToString() const;
    // returns outpoint in short form txid-index
    std::string ToStringShort() const;
};

using v_outpoints = std::vector<COutPoint>;

/** An outpoint - a combination of a transaction hash and an index n into its sapling
 * output description (vShieldedOutput) */
class SaplingOutPoint : public BaseOutPoint
{
public:
    SaplingOutPoint() noexcept : 
        BaseOutPoint()
    {}
    SaplingOutPoint(const uint256 &hashIn, const uint32_t nIn) noexcept : 
        BaseOutPoint(hashIn, nIn)
    {}

    SaplingOutPoint(SaplingOutPoint&& other) noexcept :
        BaseOutPoint(std::move(other))
    {}

    SaplingOutPoint(const SaplingOutPoint& other) noexcept :
		BaseOutPoint(other)
	{}
    SaplingOutPoint& operator=(SaplingOutPoint&& other) noexcept
    {
        if (this != &other)
            BaseOutPoint::operator=(std::move(other));
        return *this;
    }
    SaplingOutPoint& operator=(const SaplingOutPoint& other) noexcept
    {
		if (this != &other)
            BaseOutPoint::operator=(other);
        return *this;
    }

    std::string ToString() const;
};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    // The only use of nSequence (via IsFinal) is in TransactionSignatureChecker::CheckLockTime
    // It disables the nLockTime feature when set to maxint.
    uint32_t nSequence;

    CTxIn() noexcept
    {
        nSequence = std::numeric_limits<unsigned int>::max();
    }
    CTxIn(const CTxIn& other);
    CTxIn(CTxIn&& other) noexcept;
    CTxIn& operator=(CTxIn&& other) noexcept;
    CTxIn& operator=(const CTxIn& other) noexcept;
    explicit CTxIn(const COutPoint &prevoutIn, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=std::numeric_limits<uint32_t>::max());
    CTxIn(const uint256 &hashPrevTx, const uint32_t nOut, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=std::numeric_limits<uint32_t>::max());

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(prevout);
        READWRITE(*(CScriptBase*)(&scriptSig));
        READWRITE(nSequence);
    }

    bool IsFinal() const noexcept
    {
        return (nSequence == std::numeric_limits<uint32_t>::max());
    }

    friend bool operator==(const CTxIn& a, const CTxIn& b)
    {
        return (a.prevout   == b.prevout &&
                a.scriptSig == b.scriptSig &&
                a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn& a, const CTxIn& b)
    {
        return !(a == b);
    }

    friend bool operator<(const CTxIn& a, const CTxIn& b)
    {
        return a.prevout < b.prevout;
    }

    std::string ToString() const;
};

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
    CAmount nValue;
    CScript scriptPubKey;

    CTxOut() noexcept
    {
        Clear();
    }
    CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn) noexcept;
    CTxOut(const CTxOut& other);
    CTxOut(CTxOut&& other) noexcept;
    CTxOut& operator=(CTxOut&& other) noexcept;
    CTxOut& operator=(const CTxOut& other) noexcept;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(nValue);
        READWRITE(*(CScriptBase*)(&scriptPubKey));
    }

    void Clear() noexcept
    {
        nValue = -1;
        scriptPubKey.clear();
    }

    bool IsNull() const noexcept
    {
        return (nValue == -1);
    }

    uint256 GetHash() const;

    CAmount GetDustThreshold(const CFeeRate &minRelayTxFee) const
    {
        // "Dust" is defined in terms of CTransaction::minRelayTxFee,
        // which has units patoshis-per-kilobyte.
        // If you'd pay more than 1/3 in fees
        // to spend something, then we consider it dust.
        // A typical spendable txout is 34 bytes big, and will
        // need a CTxIn of at least 148 bytes to spend:
        // so dust is a spendable txout less than 54 patoshis
        // with default minRelayTxFee.
        if (scriptPubKey.IsUnspendable())
            return 0;

        const size_t nSize = GetSerializeSize(*this, SER_DISK, 0) + 148u;

        return 3*minRelayTxFee.GetFee(nSize);
    }

    bool IsDust(const CFeeRate &minRelayTxFee) const
    {
        auto dustThreshold =  GetDustThreshold(minRelayTxFee);
        return (nValue < dustThreshold);
    }

    friend bool operator==(const CTxOut& a, const CTxOut& b)
    {
        return (a.nValue       == b.nValue &&
                a.scriptPubKey == b.scriptPubKey);
    }

    friend bool operator!=(const CTxOut& a, const CTxOut& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

using v_txouts = std::vector<CTxOut>;
using v_txins = std::vector<CTxIn>;

// Overwinter version group id
static constexpr uint32_t OVERWINTER_VERSION_GROUP_ID = 0x03C48270;
static_assert(OVERWINTER_VERSION_GROUP_ID != 0, "version group id must be non-zero as specified in ZIP 202");

// Sapling version group id
static constexpr uint32_t SAPLING_VERSION_GROUP_ID = 0x892F2085;
static_assert(SAPLING_VERSION_GROUP_ID != 0, "version group id must be non-zero as specified in ZIP 202");

struct CMutableTransaction;

/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction
{
private:
    /** Memory only. */
    const uint256 hash;
    void UpdateHash() const;

protected:
    /** Developer testing only.  Set evilDeveloperFlag to true.
     * Convert a CMutableTransaction into a CTransaction without invoking UpdateHash()
     */
    CTransaction(const CMutableTransaction &tx, bool evilDeveloperFlag);

public:
    typedef std::array<unsigned char, 64> binding_sig_t;

    // Transactions that include a list of JoinSplits are >= version 2.
    static constexpr int32_t SPROUT_MIN_CURRENT_VERSION = 1;
    static constexpr int32_t SPROUT_MAX_CURRENT_VERSION = 2;
    static constexpr int32_t OVERWINTER_MIN_CURRENT_VERSION = 3;
    static constexpr int32_t OVERWINTER_MAX_CURRENT_VERSION = 3;
    static constexpr int32_t SAPLING_MIN_CURRENT_VERSION = 4;
    static constexpr int32_t SAPLING_MAX_CURRENT_VERSION = 4;

    static_assert(SPROUT_MIN_CURRENT_VERSION >= SPROUT_MIN_TX_VERSION,
                  "standard rule for tx version should be consistent with network rule");

    static_assert(OVERWINTER_MIN_CURRENT_VERSION >= OVERWINTER_MIN_TX_VERSION,
                  "standard rule for tx version should be consistent with network rule");

    static_assert( (OVERWINTER_MAX_CURRENT_VERSION <= OVERWINTER_MAX_TX_VERSION &&
                    OVERWINTER_MAX_CURRENT_VERSION >= OVERWINTER_MIN_CURRENT_VERSION),
                  "standard rule for tx version should be consistent with network rule");

    static_assert(SAPLING_MIN_CURRENT_VERSION >= SAPLING_MIN_TX_VERSION,
                  "standard rule for tx version should be consistent with network rule");

    static_assert( (SAPLING_MAX_CURRENT_VERSION <= SAPLING_MAX_TX_VERSION &&
                    SAPLING_MAX_CURRENT_VERSION >= SAPLING_MIN_CURRENT_VERSION),
                  "standard rule for tx version should be consistent with network rule");

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
    const bool fOverwintered;
    const int32_t nVersion;
    const uint32_t nVersionGroupId;
    const v_txins vin;       // tx inputs
    const v_txouts vout;     // tx outputs
    // Represents the earliest time or block height at which a transaction is considered valid and 
    // can be added to a block.
    // There are two possible values of nLockTime: 
    // lock-by-blockheight and lock-by-blocktime, distinguished by whether nLockTime < LOCKTIME_THRESHOLD.
    // lock-by-blockheight: the transaction can be added to any block which has this height or higher.
    // lock-by-blocktime: the transaction can be added to any block whose block time is greater than or equal to 
    // the provided timestamp (Unix epoch timestamp).
    // 0: the transaction is considered "final" and can be included in a block regardless of its timestamp or height.
    const uint32_t nLockTime;
    const uint32_t nExpiryHeight;
    const CAmount valueBalance;
    const std::vector<SpendDescription> vShieldedSpend;
    const std::vector<OutputDescription> vShieldedOutput;
    const binding_sig_t bindingSig = {{0}};

    /** Construct a CTransaction that qualifies as IsNull() */
    CTransaction();

    /** Convert a CMutableTransaction into a CTransaction. */
    CTransaction(const CMutableTransaction &tx);
    CTransaction(CMutableTransaction &&tx);

    CTransaction& operator=(const CTransaction& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        uint32_t header;
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        if (bRead)
        {
            // When deserializing, unpack the 4 byte header to extract fOverwintered and nVersion.
            READWRITE(header);
            *const_cast<bool*>(&fOverwintered) = header >> 31;
            *const_cast<int32_t*>(&this->nVersion) = header & 0x7FFFFFFF;
        } else {
            header = GetHeader();
            READWRITE(header);
        }
        if (fOverwintered)
        {
            READWRITE(*const_cast<uint32_t*>(&this->nVersionGroupId));
        }

        bool isOverwinterV3 =
            fOverwintered &&
            nVersionGroupId == OVERWINTER_VERSION_GROUP_ID &&
            nVersion == OVERWINTER_TX_VERSION;

        bool isSaplingV4 =
            fOverwintered &&
            nVersionGroupId == SAPLING_VERSION_GROUP_ID &&
            nVersion == SAPLING_TX_VERSION;
        if (fOverwintered && !(isOverwinterV3 || isSaplingV4))
        {
            throw std::ios_base::failure("Unknown transaction format");
        }

        READWRITE(*const_cast<v_txins*>(&vin));
        READWRITE(*const_cast<v_txouts*>(&vout));
        READWRITE(*const_cast<uint32_t*>(&nLockTime));
        if (isOverwinterV3 || isSaplingV4)
        {
            READWRITE(*const_cast<uint32_t*>(&nExpiryHeight));
        }
        if (isSaplingV4)
        {
            READWRITE(*const_cast<CAmount*>(&valueBalance));
            READWRITE(*const_cast<std::vector<SpendDescription>*>(&vShieldedSpend));
            READWRITE(*const_cast<std::vector<OutputDescription>*>(&vShieldedOutput));
        }
        if (nVersion >= 2)
        {
            auto os = WithVersion(&s, static_cast<int>(header));
            std::vector<int> v;
            ::SerReadWrite(os, v, ser_action);
        }
        if (isSaplingV4 && !(vShieldedSpend.empty() && vShieldedOutput.empty()))
        {
            READWRITE(*const_cast<binding_sig_t*>(&bindingSig));
        }
        if (bRead)
            UpdateHash();
    }

    template <typename Stream>
    CTransaction(deserialize_type, Stream& s) : CTransaction(CMutableTransaction(deserialize, s)) {}

    bool IsNull() const { return vin.empty() && vout.empty(); }

    const uint256& GetHash() const noexcept { return hash; }

    uint32_t GetHeader() const noexcept
    {
        // When serializing v1 and v2, the 4 byte header is nVersion
        uint32_t header = this->nVersion;
        // When serializing Overwintered tx, the 4 byte header is the combination of fOverwintered and nVersion
        if (fOverwintered)
            header |= 1 << 31;
        return header;
    }

    /*
     * Context for the two methods below:
     * As at most one of vpub_new and vpub_old is non-zero in every JoinSplit,
     * we can think of a JoinSplit as an input or output according to which one
     * it is (e.g. if vpub_new is non-zero the joinSplit is "giving value" to
     * the outputs in the transaction). Similarly, we can think of the Sapling
     * shielded part of the transaction as an input or output according to
     * whether valueBalance - the sum of shielded input values minus the sum of
     * shielded output values - is positive or negative.
     */

    // Return sum of txouts, (negative valueBalance or zero)
    CAmount GetValueOut() const;
    // GetValueIn() is a method on CCoinsViewCache, because
    // inputs must be known to compute value in.

    // Return sum of (positive valueBalance or zero)
    CAmount GetShieldedValueIn() const;

    // Compute priority, given priority of inputs and (optionally) tx size
    double ComputePriority(double dPriorityInputs, const size_t nTxSize = 0) const;

    // Compute modified tx size for priority calculation (optionally given tx size)
    size_t CalculateModifiedSize(const size_t nTxSize = 0) const;

    bool IsCoinBase() const noexcept
    {
        return (vin.size() == 1 && vin[0].prevout.IsNull());
    }

    friend bool operator==(const CTransaction& a, const CTransaction& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CTransaction& a, const CTransaction& b)
    {
        return a.hash != b.hash;
    }

    std::string ToString() const;
};

/** A mutable version of CTransaction. */
struct CMutableTransaction
{
    bool fOverwintered;
    int32_t nVersion;
    uint32_t nVersionGroupId;
    v_txins vin;
    v_txouts vout;
    uint32_t nLockTime;
    uint32_t nExpiryHeight;
    CAmount valueBalance;
    std::vector<SpendDescription> vShieldedSpend;
    std::vector<OutputDescription> vShieldedOutput;
    CTransaction::binding_sig_t bindingSig = {{0}};

    CMutableTransaction() noexcept;
    CMutableTransaction(const CTransaction& tx) noexcept;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        uint32_t header;
        if (ser_action == SERIALIZE_ACTION::Read)
        {
            // When deserializing, unpack the 4 byte header to extract fOverwintered and nVersion.
            READWRITE(header);
            fOverwintered = header >> 31;
            this->nVersion = header & 0x7FFFFFFF;
        } else {
            // When serializing v1 and v2, the 4 byte header is nVersion
            header = this->nVersion;
            // When serializing Overwintered tx, the 4 byte header is the combination of fOverwintered and nVersion
            if (fOverwintered)
                header |= 1 << 31;
            READWRITE(header);
        }
        if (fOverwintered)
        {
            READWRITE(nVersionGroupId);
        }

        bool isOverwinterV3 =
            fOverwintered &&
            nVersionGroupId == OVERWINTER_VERSION_GROUP_ID &&
            nVersion == OVERWINTER_TX_VERSION;
        bool isSaplingV4 =
            fOverwintered &&
            nVersionGroupId == SAPLING_VERSION_GROUP_ID &&
            nVersion == SAPLING_TX_VERSION;
        if (fOverwintered && !(isOverwinterV3 || isSaplingV4))
        {
            throw std::ios_base::failure("Unknown transaction format");
        }

        READWRITE(vin);
        READWRITE(vout);
        READWRITE(nLockTime);
        if (isOverwinterV3 || isSaplingV4)
        {
            READWRITE(nExpiryHeight);
        }
        if (isSaplingV4)
        {
            READWRITE(valueBalance);
            READWRITE(vShieldedSpend);
            READWRITE(vShieldedOutput);
        }
        if (nVersion >= 2) {
            auto os = WithVersion(&s, static_cast<int>(header));
            std::vector<int> v;
            ::SerReadWrite(os, v, ser_action);
        }
        if (isSaplingV4 && !(vShieldedSpend.empty() && vShieldedOutput.empty())) {
            READWRITE(bindingSig);
        }
    }

    template <typename Stream>
    CMutableTransaction(deserialize_type, Stream& s)
    {
        Unserialize(s);
    }

    std::string ToString() const;

    /** Compute the hash of this CMutableTransaction. This is computed on the
     * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
     */
    uint256 GetHash() const;
};
