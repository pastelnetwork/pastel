// Copyright (c) 2017 The Zcash developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <array>
#include <iostream>
#include <chrono>
#include <thread>
#include <string>
#include <variant>

#include <utils/util.h>
#include <asyncrpcqueue.h>
#include <consensus/upgrades.h>
#include <core_io.h>
#include <init.h>
#include <key_io.h>
#include <main.h>
#include <net.h>
#include <netbase.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <timedata.h>
#include <utilmoneystr.h>
#include <script/interpreter.h>
#include <utiltime.h>
#include <zcash/IncrementalMerkleTree.hpp>
#include <sodium.h>
#include <miner.h>

#include <wallet/asyncrpcoperation_shieldcoinbase.h>

using namespace libzcash;

AsyncRPCOperation_shieldcoinbase::AsyncRPCOperation_shieldcoinbase(
        unique_ptr<TransactionBuilder> builder,
        const CMutableTransaction &contextualTx,
        const vector<ShieldCoinbaseUTXO> &inputs,
        string toAddress,
        CAmount fee,
        UniValue contextInfo) :
            tx_(contextualTx),
            inputs_(inputs),
            fee_(fee),
            contextinfo_(contextInfo)
{
    assert(contextualTx.nVersion >= 2);  // transaction format version must support vjoinsplit

    if (fee < 0 || fee > MAX_MONEY)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Fee is out of range");

    if (inputs.empty())
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Empty inputs");

    const auto &chainparams = Params();

    if (builder)
        m_builder = move(builder);
    else
        m_builder = make_unique<TransactionBuilder>(chainparams.GetConsensus(), 0);

    //  Check the destination address is valid for this network i.e. not testnet being used on mainnet
    KeyIO keyIO(chainparams);
    auto address = keyIO.DecodePaymentAddress(toAddress);
    if (!IsValidPaymentAddress(address))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid to address");
    tozaddr_ = address;

    // Log the context info
    if (LogAcceptCategory("zrpcunsafe")) {
        LogPrint("zrpcunsafe", "%s: z_shieldcoinbase initialized (context=%s)\n", getId(), contextInfo.write());
    } else {
        LogPrint("zrpc", "%s: z_shieldcoinbase initialized\n", getId());
    }

    // Lock UTXOs
    lock_utxos();

    // Enable payment disclosure if requested
    paymentDisclosureMode = fExperimentalMode && GetBoolArg("-paymentdisclosure", false);
}

AsyncRPCOperation_shieldcoinbase::~AsyncRPCOperation_shieldcoinbase() {
}

void AsyncRPCOperation_shieldcoinbase::main() {
    if (isCancelled()) {
        unlock_utxos(); // clean up
        return;
    }

    set_state(OperationStatus::EXECUTING);
    start_execution_clock();

    bool success = false;

    const auto& chainparams = Params();
#ifdef ENABLE_MINING
  #ifdef ENABLE_WALLET
    GenerateBitcoins(false, nullptr, 0, chainparams);
  #else
    GenerateBitcoins(false, 0, chainparams);
  #endif
#endif

    try {
        success = main_impl();
    } catch (const UniValue& objError) {
        int code = find_value(objError, "code").get_int();
        string message = find_value(objError, "message").get_str();
        set_error_code(code);
        set_error_message(message);
    } catch (const runtime_error& e) {
        set_error_code(-1);
        set_error_message("runtime error: " + string(e.what()));
    } catch (const logic_error& e) {
        set_error_code(-1);
        set_error_message("logic error: " + string(e.what()));
    } catch (const exception& e) {
        set_error_code(-1);
        set_error_message("general exception: " + string(e.what()));
    } catch (...) {
        set_error_code(-2);
        set_error_message("unknown error");
    }

#ifdef ENABLE_MINING
    const int nThreadCount = static_cast<int>(GetArg("-genproclimit", 1));
    const bool bGenerate = GetBoolArg("-gen", false);
#ifdef ENABLE_WALLET
    GenerateBitcoins(bGenerate, pwalletMain, nThreadCount, chainparams);
#else
    GenerateBitcoins(bGenerate, nThreadCount, chainparams);
#endif
#endif

    stop_execution_clock();

    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    string s = strprintf("%s: z_shieldcoinbase finished (status=%s", getId(), getStateAsString());
    if (success) {
        s += strprintf(", txid=%s)\n", tx_.GetHash().ToString());
    } else {
        s += strprintf(", error=%s)\n", getErrorMessage());
    }
    LogPrintf("%s",s);

    unlock_utxos(); // clean up

}

bool AsyncRPCOperation_shieldcoinbase::main_impl() {

    CAmount minersFee = fee_;

    size_t numInputs = inputs_.size();

    CAmount targetAmount = 0;
    for (ShieldCoinbaseUTXO & utxo : inputs_) {
        targetAmount += utxo.amount;
    }

    if (targetAmount <= minersFee) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient coinbase funds, have %s and miners fee is %s",
            FormatMoney(targetAmount), FormatMoney(minersFee)));
    }

    CAmount sendAmount = targetAmount - minersFee;
    LogPrint("zrpc", "%s: spending %s to shield %s with fee %s\n",
            getId(), FormatMoney(targetAmount), FormatMoney(sendAmount), FormatMoney(minersFee));

    return visit(ShieldToAddress(this, sendAmount), tozaddr_);
}

extern UniValue signrawtransaction(const UniValue& params, bool fHelp);
extern UniValue sendrawtransaction(const UniValue& params, bool fHelp);

bool ShieldToAddress::operator()(const libzcash::SaplingPaymentAddress &zaddr) const
{
    m_op->m_builder->SetFee(m_op->fee_);

    // Sending from a t-address, which we don't have an ovk for. Instead,
    // generate a common one from the HD seed. This ensures the data is
    // recoverable, while keeping it logically separate from the ZIP 32
    // Sapling key hierarchy, which the user might not be using.
    HDSeed seed;
    if (!pwalletMain->GetHDSeed(seed)) {
        throw JSONRPCError(
            RPC_WALLET_ERROR,
            "CWallet::GenerateNewSaplingZKey(): HD seed not found");
    }
    uint256 ovk = ovkForShieldingFromTaddr(seed);

    // Add transparent inputs
    for (auto t : m_op->inputs_) {
        m_op->m_builder->AddTransparentInput(COutPoint(t.txid, t.vout), t.scriptPubKey, t.amount);
    }

    // Send all value to the target z-addr
    m_op->m_builder->SendChangeTo(zaddr, ovk);

    // Build the transaction
    m_op->tx_ = m_op->m_builder->Build().GetTxOrThrow();

    // Send the transaction
    // TODO: Use CWallet::CommitTransaction instead of sendrawtransaction
    auto signedtxn = EncodeHexTx(m_op->tx_);
    UniValue o(UniValue::VOBJ);
    if (!m_op->testmode)
    {
        UniValue params(UniValue::VARR);
        params.push_back(move(signedtxn));
        UniValue sendResultValue = sendrawtransaction(params, false);
        if (sendResultValue.isNull())
            throw JSONRPCError(RPC_WALLET_ERROR, "sendrawtransaction did not return an error or a txid.");

        auto txid = sendResultValue.get_str();
        o.pushKV("txid", move(txid));
    } else {
        // Test mode does not send the transaction to the network.
        o.pushKV("test", 1);
        o.pushKV("txid", m_op->tx_.GetHash().ToString());
        o.pushKV("hex", move(signedtxn));
    }
    m_op->set_result(move(o));

    return true;
}

bool ShieldToAddress::operator()(const libzcash::InvalidEncoding& no) const {
    return false;
}


/**
 * Sign and send a raw transaction.
 * Raw transaction as hex string should be in object field "rawtxn"
 */
void AsyncRPCOperation_shieldcoinbase::sign_send_raw_transaction(UniValue obj)
{
    // Sign the raw transaction
    UniValue rawtxnValue = find_value(obj, "rawtxn");
    if (rawtxnValue.isNull()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for raw transaction");
    }
    string rawtxn = rawtxnValue.get_str();

    UniValue params = UniValue(UniValue::VARR);
    params.push_back(rawtxn);
    UniValue signResultValue = signrawtransaction(params, false);
    UniValue signResultObject = signResultValue.get_obj();
    UniValue completeValue = find_value(signResultObject, "complete");
    bool complete = completeValue.get_bool();
    if (!complete) {
        // TODO: #1366 Maybe get "errors" and print array vErrors into a string
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to sign transaction");
    }

    UniValue hexValue = find_value(signResultObject, "hex");
    if (hexValue.isNull()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for signed transaction");
    }
    auto signedtxn = hexValue.get_str();
    UniValue o(UniValue::VOBJ);

    // Send the signed transaction
    if (!testmode)
    {
        params.clear();
        params.setArray();
        params.push_back(signedtxn);
        UniValue sendResultValue = sendrawtransaction(params, false);
        if (sendResultValue.isNull())
            throw JSONRPCError(RPC_WALLET_ERROR, "Send raw transaction did not return an error or a txid.");

        auto txid = sendResultValue.get_str();
        o.pushKV("txid", move(txid));
    } else {
        // Test mode does not send the transaction to the network.

        CDataStream stream(ParseHex(signedtxn), SER_NETWORK, PROTOCOL_VERSION);
        CTransaction tx;
        stream >> tx;

        o.pushKV("test", 1);
        o.pushKV("txid", tx.GetHash().ToString());
        o.pushKV("hex", signedtxn);
    }
    set_result(move(o));

    // Keep the signed transaction so we can hash to the same txid
    CDataStream stream(ParseHex(signedtxn), SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;
    stream >> tx;
    tx_ = tx;
}

/**
 * Override getStatus() to append the operation's context object to the default status object.
 */
UniValue AsyncRPCOperation_shieldcoinbase::getStatus() const {
    UniValue v = AsyncRPCOperation::getStatus();
    if (contextinfo_.isNull()) {
        return v;
    }

    UniValue obj = v.get_obj();
    obj.pushKV("method", "z_shieldcoinbase");
    obj.pushKV("params", contextinfo_ );
    return obj;
}

/**
 * Lock input utxos
 */
 void AsyncRPCOperation_shieldcoinbase::lock_utxos() {
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto utxo : inputs_) {
        COutPoint outpt(utxo.txid, utxo.vout);
        pwalletMain->LockCoin(outpt);
    }
}

/**
 * Unlock input utxos
 */
void AsyncRPCOperation_shieldcoinbase::unlock_utxos() {
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto utxo : inputs_) {
        COutPoint outpt(utxo.txid, utxo.vout);
        pwalletMain->UnlockCoin(outpt);
    }
}
