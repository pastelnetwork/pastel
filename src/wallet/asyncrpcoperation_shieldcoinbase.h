#pragma once
// Copyright (c) 2017 The Zcash developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>
#include <tuple>

#include <univalue.h>

#include <asyncrpcoperation.h>
#include <amount.h>
#include <primitives/transaction.h>
#include <transaction_builder.h>
#include <zcash/Address.hpp>
#include <wallet/wallet.h>

// Default transaction fee if caller does not specify one.
constexpr CAmount SHIELD_COINBASE_DEFAULT_MINERS_FEE = 10000;

using namespace libzcash;

struct ShieldCoinbaseUTXO
{
    ShieldCoinbaseUTXO(const uint256 &txid, const int vout, const CAmount amount) :
        txid(txid),
        vout(vout),
        amount(amount)
    {}

    uint256 txid;
    int vout;
    CScript scriptPubKey;
    CAmount amount;
};

class AsyncRPCOperation_shieldcoinbase : public AsyncRPCOperation
{
public:
    AsyncRPCOperation_shieldcoinbase(
        std::unique_ptr<TransactionBuilder> builder,
        const CMutableTransaction &contextualTx,
        const std::vector<ShieldCoinbaseUTXO> &inputs,
        std::string toAddress,
        CAmount fee = SHIELD_COINBASE_DEFAULT_MINERS_FEE,
        UniValue contextInfo = NullUniValue);
    ~AsyncRPCOperation_shieldcoinbase() override;

    // We don't want to be copied or moved around
    AsyncRPCOperation_shieldcoinbase(AsyncRPCOperation_shieldcoinbase const&) = delete;             // Copy construct
    AsyncRPCOperation_shieldcoinbase(AsyncRPCOperation_shieldcoinbase&&) = delete;                  // Move construct
    AsyncRPCOperation_shieldcoinbase& operator=(AsyncRPCOperation_shieldcoinbase const&) = delete;  // Copy assign
    AsyncRPCOperation_shieldcoinbase& operator=(AsyncRPCOperation_shieldcoinbase &&) = delete;      // Move assign

    void main() override;

    UniValue getStatus() const override;

    bool testmode = false;  // Set to true to disable sending txs and generating proofs

    bool paymentDisclosureMode = false; // Set to true to save esk for encrypted notes in payment disclosure database.

private:
    friend class ShieldToAddress;
    friend class TEST_FRIEND_AsyncRPCOperation_shieldcoinbase;    // class for unit testing

    UniValue contextinfo_;     // optional data to include in return value from getStatus()

    CAmount fee_;
    PaymentAddress tozaddr_;

    std::vector<ShieldCoinbaseUTXO> inputs_;

    std::unique_ptr<TransactionBuilder> m_builder;
    CTransaction tx_;

    bool main_impl();

    void sign_send_raw_transaction(UniValue obj);     // throws exception if there was an error

    void lock_utxos();

    void unlock_utxos();
};

class ShieldToAddress
{
private:
    AsyncRPCOperation_shieldcoinbase *m_op;
    CAmount sendAmount;

public:
    ShieldToAddress(AsyncRPCOperation_shieldcoinbase *op, CAmount sendAmount) :
        m_op(op),
        sendAmount(sendAmount)
    {}

    bool operator()(const libzcash::SaplingPaymentAddress &zaddr) const;
    bool operator()(const libzcash::InvalidEncoding& no) const;
};


// To test private methods, a friend class can act as a proxy
class TEST_FRIEND_AsyncRPCOperation_shieldcoinbase {
public:
    std::shared_ptr<AsyncRPCOperation_shieldcoinbase> delegate;

    TEST_FRIEND_AsyncRPCOperation_shieldcoinbase(std::shared_ptr<AsyncRPCOperation_shieldcoinbase> ptr) : delegate(ptr) {}

    CTransaction getTx() {
        return delegate->tx_;
    }

    void setTx(CTransaction tx) {
        delegate->tx_ = tx;
    }

    // Delegated methods

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

