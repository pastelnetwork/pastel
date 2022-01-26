// Copyright (c) 2017 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ASYNCRPCOPERATION_MERGETOADDRESS_H
#define ASYNCRPCOPERATION_MERGETOADDRESS_H

#include <amount.h>
#include <asyncrpcoperation.h>
#include <primitives/transaction.h>
#include <transaction_builder.h>
#include <wallet/wallet.h>
#include <zcash/Address.hpp>

#include <array>
#include <tuple>
#include <unordered_map>

#include <univalue.h>

// Default transaction fee if caller does not specify one.
constexpr CAmount MERGE_TO_ADDRESS_OPERATION_DEFAULT_MINERS_FEE = static_cast<CAmount>(0.1 * COIN); //10 time more then regular fee

using namespace libzcash;

// Input UTXO is a tuple of txid, vout, amount, script
typedef std::tuple<COutPoint, CAmount, CScript> MergeToAddressInputUTXO;

typedef std::tuple<SaplingOutPoint, SaplingNote, CAmount, SaplingExpandedSpendingKey> MergeToAddressInputSaplingNote;

// A recipient is a tuple of address, memo (optional if zaddr)
typedef std::tuple<std::string, std::string> MergeToAddressRecipient;

// A struct to help us track the witness and anchor for a given JSOutPoint
struct MergeToAddressWitnessAnchorData {
    std::optional<SproutWitness> witness;
    uint256 anchor;
};

class AsyncRPCOperation_mergetoaddress : public AsyncRPCOperation
{
public:
    AsyncRPCOperation_mergetoaddress(
        std::optional<TransactionBuilder> builder,
        CMutableTransaction contextualTx,
        std::vector<MergeToAddressInputUTXO> utxoInputs,
        std::vector<MergeToAddressInputSaplingNote> saplingNoteInputs,
        MergeToAddressRecipient recipient,
        CAmount fee = MERGE_TO_ADDRESS_OPERATION_DEFAULT_MINERS_FEE,
        UniValue contextInfo = NullUniValue);
    virtual ~AsyncRPCOperation_mergetoaddress();

    // We don't want to be copied or moved around
    AsyncRPCOperation_mergetoaddress(AsyncRPCOperation_mergetoaddress const&) = delete;            // Copy construct
    AsyncRPCOperation_mergetoaddress(AsyncRPCOperation_mergetoaddress&&) = delete;                 // Move construct
    AsyncRPCOperation_mergetoaddress& operator=(AsyncRPCOperation_mergetoaddress const&) = delete; // Copy assign
    AsyncRPCOperation_mergetoaddress& operator=(AsyncRPCOperation_mergetoaddress&&) = delete;      // Move assign

    virtual void main();

    virtual UniValue getStatus() const;

    bool testmode = false; // Set to true to disable sending txs and generating proofs

    bool paymentDisclosureMode = false; // Set to true to save esk for encrypted notes in payment disclosure database.

private:
    friend class TEST_FRIEND_AsyncRPCOperation_mergetoaddress; // class for unit testing

    UniValue contextinfo_; // optional data to include in return value from getStatus()
    
    bool isUsingBuilder_; // Indicates that no Sprout addresses are involved
    uint32_t consensusBranchId_;
    CAmount fee_;
    int mindepth_;
    MergeToAddressRecipient recipient_;
    bool isToTaddr_;
    bool isToZaddr_;
    CTxDestination toTaddr_;
    PaymentAddress toPaymentAddress_;

    uint256 joinSplitPubKey_;
    unsigned char joinSplitPrivKey_[crypto_sign_SECRETKEYBYTES];

    // The key is the result string from calling JSOutPoint::ToString()
    std::unordered_map<std::string, MergeToAddressWitnessAnchorData> jsopWitnessAnchorMap;

    std::vector<MergeToAddressInputUTXO> utxoInputs_;
    std::vector<MergeToAddressInputSaplingNote> saplingNoteInputs_;

    TransactionBuilder builder_;
    CTransaction tx_;

    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(std::string s);
    bool main_impl();

    void sign_send_raw_transaction(UniValue obj); // throws exception if there was an error

    void lock_utxos();

    void unlock_utxos();

    void lock_notes();

    void unlock_notes();
};


// To test private methods, a friend class can act as a proxy
class TEST_FRIEND_AsyncRPCOperation_mergetoaddress
{
public:
    std::shared_ptr<AsyncRPCOperation_mergetoaddress> delegate;

    TEST_FRIEND_AsyncRPCOperation_mergetoaddress(std::shared_ptr<AsyncRPCOperation_mergetoaddress> ptr) : delegate(ptr) {}

    CTransaction getTx()
    {
        return delegate->tx_;
    }

    void setTx(CTransaction tx)
    {
        delegate->tx_ = tx;
    }

    // Delegated methods

    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(std::string s)
    {
        return delegate->get_memo_from_hex_string(s);
    }

    bool main_impl()
    {
        return delegate->main_impl();
    }

    void sign_send_raw_transaction(UniValue obj)
    {
        delegate->sign_send_raw_transaction(obj);
    }

    void set_state(OperationStatus state)
    {
        delegate->state_.store(state);
    }
};


#endif /* ASYNCRPCOPERATION_MERGETOADDRESS_H */
