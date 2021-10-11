#pragma once
// Copyright (c) 2016 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "asyncrpcoperation.h"
#include "amount.h"
#include "primitives/transaction.h"
#include "transaction_builder.h"
#include "zcash/Address.hpp"
#include "wallet.h"

#include <array>
#include <optional>
#include <unordered_map>
#include <tuple>

#include <univalue.h>

// Default transaction fee if caller does not specify one.
constexpr CAmount ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE = 10000;
constexpr auto RPC_METHOD_SENDMANY        = "z_sendmany";
constexpr auto RPC_METHOD_SENDMANY_CHANGE = "z_sendmanywithchangetosender";

using namespace libzcash;

// A recipient is a tuple of address, amount, memo (optional if zaddr)
typedef std::tuple<std::string, CAmount, std::string> SendManyRecipient;

// Input UTXO is a tuple (quadruple) of txid, vout, amount, coinbase)
typedef std::tuple<uint256, int, CAmount, bool> SendManyInputUTXO;

class AsyncRPCOperation_sendmany : public AsyncRPCOperation
{
public:
    AsyncRPCOperation_sendmany(
        std::optional<TransactionBuilder> builder,
        CMutableTransaction contextualTx,
        std::string fromAddress,
        std::vector<SendManyRecipient> tOutputs,
        std::vector<SendManyRecipient> zOutputs,
        int minDepth,
        CAmount fee = ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE,
        UniValue contextInfo = NullUniValue,
        const bool bReturnChangeToSenderAddr = false);
    virtual ~AsyncRPCOperation_sendmany() noexcept = default;
    
    // We don't want to be copied or moved around
    AsyncRPCOperation_sendmany(AsyncRPCOperation_sendmany const&) = delete;             // Copy construct
    AsyncRPCOperation_sendmany(AsyncRPCOperation_sendmany&&) = delete;                  // Move construct
    AsyncRPCOperation_sendmany& operator=(AsyncRPCOperation_sendmany const&) = delete;  // Copy assign
    AsyncRPCOperation_sendmany& operator=(AsyncRPCOperation_sendmany &&) = delete;      // Move assign
    
    virtual void main();

    virtual UniValue getStatus() const;

    bool testmode = false;  // Set to true to disable sending txs and generating proofs

    bool paymentDisclosureMode = false; // Set to true to save esk for encrypted notes in payment disclosure database.

private:
    friend class TEST_FRIEND_AsyncRPCOperation_sendmany;    // class for unit testing

    UniValue contextinfo_;     // optional data to include in return value from getStatus()

    bool isUsingBuilder_; // Indicates that no Sprout addresses are involved
    uint32_t consensusBranchId_;
    CAmount fee_;
    int mindepth_;
    std::string fromaddress_;
    bool isfromtaddr_;
    bool isfromzaddr_;
    bool m_bReturnChangeToSender;
    CTxDestination fromtaddr_;
    PaymentAddress frompaymentaddress_;
    SpendingKey spendingkey_;
    
    std::vector<SendManyRecipient> t_outputs_;
    std::vector<SendManyRecipient> z_outputs_;
    std::vector<SendManyInputUTXO> t_inputs_;
    std::vector<SaplingNoteEntry> z_sapling_inputs_;

    TransactionBuilder builder_;
    CTransaction tx_;
   
    void add_taddr_change_output_to_tx(CAmount amount);
    void add_taddr_outputs_to_tx();
    bool find_unspent_notes();
    bool find_utxos(const bool fAcceptCoinbase = false);
    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(std::string s);
    bool main_impl();

    void sign_send_raw_transaction(UniValue obj);     // throws exception if there was an error
};


// To test private methods, a friend class can act as a proxy
class TEST_FRIEND_AsyncRPCOperation_sendmany {
public:
    std::shared_ptr<AsyncRPCOperation_sendmany> delegate;

    TEST_FRIEND_AsyncRPCOperation_sendmany(std::shared_ptr<AsyncRPCOperation_sendmany> ptr) : delegate(ptr) {}

    CTransaction getTx() {
        return delegate->tx_;
    }

    void setTx(CTransaction tx) {
        delegate->tx_ = tx;
    }

    // Delegated methods

    void add_taddr_change_output_to_tx(CAmount amount) {
        delegate->add_taddr_change_output_to_tx(amount);
    }

    void add_taddr_outputs_to_tx() {
        delegate->add_taddr_outputs_to_tx();
    }

    bool find_unspent_notes() {
        return delegate->find_unspent_notes();
    }

    bool find_utxos(bool fAcceptCoinbase) {
        return delegate->find_utxos(fAcceptCoinbase);
    }
    
    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(std::string s) {
        return delegate->get_memo_from_hex_string(s);
    }

    bool main_impl() {
        return delegate->main_impl();
    }

    void sign_send_raw_transaction(UniValue obj) {
        delegate->sign_send_raw_transaction(obj);
    }
    
    void set_state(OperationStatus state) {
        delegate->state_.store(state);
    }
};

