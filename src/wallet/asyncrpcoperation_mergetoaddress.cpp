// Copyright (c) 2017 The Zcash developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>

#include <wallet/asyncrpcoperation_mergetoaddress.h>

#include <utils/util.h>
#include <utils/utiltime.h>
#include <asyncrpcqueue.h>
#include <core_io.h>
#include <init.h>
#include <key_io.h>
#include <main.h>
#include <mining/mining-settings.h>
#include <mining/miner.h>
#include <net.h>
#include <netbase.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <rpc/rpc_consts.h>
#include <script/interpreter.h>
#include <sodium.h>
#include <timedata.h>
#include <utilmoneystr.h>
#include <zcash/IncrementalMerkleTree.hpp>

using namespace libzcash;

extern UniValue sendrawtransaction(const UniValue& params, bool fHelp);

AsyncRPCOperation_mergetoaddress::AsyncRPCOperation_mergetoaddress
(
    unique_ptr<TransactionBuilder> builder,
    const CMutableTransaction &contextualTx,
    const vector<MergeToAddressInputUTXO> &utxoInputs,
    const vector<MergeToAddressInputSaplingNote> &saplingNoteInputs,
    MergeToAddressRecipient recipient,
    CAmount fee,
    UniValue contextInfo) :
        tx_(contextualTx),
        utxoInputs_(utxoInputs),
        saplingNoteInputs_(saplingNoteInputs),
        recipient_(recipient),
        fee_(fee),
        contextinfo_(contextInfo)
{
    if (fee < 0 || fee > MAX_MONEY)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Fee is out of range");

    if (utxoInputs.empty() && saplingNoteInputs.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No inputs");

    if (get<0>(recipient).size() == 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Recipient parameter missing");

    const auto& chainparams = Params();
    isUsingBuilder_ = false;
    if (builder)
    {
        isUsingBuilder_ = true;
        m_builder = std::move(builder);
    }
    else
        m_builder = make_unique<TransactionBuilder>(chainparams.GetConsensus(), 0);

    KeyIO keyIO(chainparams);
    toTaddr_ = keyIO.DecodeDestination(get<0>(recipient));
    isToTaddr_ = IsValidDestination(toTaddr_);
    isToZaddr_ = false;

    if (!isToTaddr_)
    {
        auto address = keyIO.DecodePaymentAddress(get<0>(recipient));
        if (!IsValidPaymentAddress(address))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid recipient address");
        isToZaddr_ = true;
        toPaymentAddress_ = address;
    }

    // Log the context info i.e. the call parameters to z_mergetoaddress
    if (LogAcceptCategory("zrpcunsafe")) {
        LogPrint("zrpcunsafe", "%s: z_mergetoaddress initialized (params=%s)\n", getId(), contextInfo.write());
    } else {
        LogPrint("zrpc", "%s: z_mergetoaddress initialized\n", getId());
    }

    // Lock UTXOs
    lock_utxos();
    lock_notes();

    // Enable payment disclosure if requested
    paymentDisclosureMode = fExperimentalMode && GetBoolArg("-paymentdisclosure", false);
}

void AsyncRPCOperation_mergetoaddress::main()
{
    if (isCancelled()) {
        unlock_utxos(); // clean up
        unlock_notes();
        return;
    }

    set_state(OperationStatus::EXECUTING);
    start_execution_clock();

    bool success = false;

#ifdef ENABLE_MINING
    const auto& chainparams = Params();
#ifdef ENABLE_WALLET
    GenerateBitcoins(false, nullptr, chainparams);
#else
    GenerateBitcoins(false, chainparams);
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
    const bool bGenerate = gl_MiningSettings.isLocalMiningEnabled();
#ifdef ENABLE_WALLET
    GenerateBitcoins(bGenerate, pwalletMain, chainparams);
#else
    GenerateBitcoins(bGenerate, chainparams);
#endif
#endif

    stop_execution_clock();

    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    string s = strprintf("%s: z_mergetoaddress finished (status=%s", getId(), getStateAsString());
    if (success) {
        s += strprintf(", txid=%s)\n", tx_.GetHash().ToString());
    } else {
        s += strprintf(", error=%s)\n", getErrorMessage());
    }
    LogPrintf("%s", s);

    unlock_utxos(); // clean up
    unlock_notes(); // clean up
}

// Notes:
// 1. #1359 Currently there is no limit set on the number of joinsplits, so size of tx could be invalid.
// 2. #1277 Spendable notes are not locked, so an operation running in parallel could also try to use them.
bool AsyncRPCOperation_mergetoaddress::main_impl()
{
    assert(isToTaddr_ != isToZaddr_);

    bool isPureTaddrOnlyTx = (saplingNoteInputs_.empty() && isToTaddr_);
    CAmount minersFee = fee_;

    size_t numInputs = utxoInputs_.size();

    CAmount t_inputs_total = 0;
    for (MergeToAddressInputUTXO& t : utxoInputs_) {
        t_inputs_total += get<1>(t);
    }

    CAmount z_inputs_total = 0;
    for (const MergeToAddressInputSaplingNote& t : saplingNoteInputs_) {
        z_inputs_total += get<2>(t);
    }

    CAmount targetAmount = z_inputs_total + t_inputs_total;

    if (targetAmount <= minersFee) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                           strprintf("Insufficient funds, have %s and miners fee is %s",
                                     FormatMoney(targetAmount), FormatMoney(minersFee)));
    }

    CAmount sendAmount = targetAmount - minersFee;

    // update the transaction with the UTXO inputs and output (if any)
    if (!isUsingBuilder_) {
        CMutableTransaction rawTx(tx_);
        for (const MergeToAddressInputUTXO& t : utxoInputs_) {
            CTxIn in(get<0>(t));
            rawTx.vin.push_back(in);
        }
        if (isToTaddr_) {
            CScript scriptPubKey = GetScriptForDestination(toTaddr_);
            CTxOut out(sendAmount, scriptPubKey);
            rawTx.vout.push_back(out);
        }
        tx_ = CTransaction(rawTx);
    }

    LogPrint(isPureTaddrOnlyTx ? "zrpc" : "zrpcunsafe", "%s: spending %s to send %s with fee %s\n",
             getId(), FormatMoney(targetAmount), FormatMoney(sendAmount), FormatMoney(minersFee));
    LogPrint("zrpc", "%s: transparent input: %s\n", getId(), FormatMoney(t_inputs_total));
    LogPrint("zrpcunsafe", "%s: private input: %s\n", getId(), FormatMoney(z_inputs_total));
    if (isToTaddr_) {
        LogPrint("zrpc", "%s: transparent output: %s\n", getId(), FormatMoney(sendAmount));
    } else {
        LogPrint("zrpcunsafe", "%s: private output: %s\n", getId(), FormatMoney(sendAmount));
    }
    LogPrint("zrpc", "%s: fee: %s\n", getId(), FormatMoney(minersFee));

    // Grab the current consensus branch ID
    {
        LOCK(cs_main);
        consensusBranchId_ = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    }

    /**
     * SCENARIO #0
     *
     * Sprout not involved, so we just use the TransactionBuilder and we're done.
     *
     * This is based on code from AsyncRPCOperation_sendmany::main_impl() and should be refactored.
     */
    if (isUsingBuilder_)
    {
        m_builder->SetFee(minersFee);


        for (const MergeToAddressInputUTXO& t : utxoInputs_) {
            COutPoint outPoint = get<0>(t);
            CAmount amount = get<1>(t);
            CScript scriptPubKey = get<2>(t);
            m_builder->AddTransparentInput(outPoint, scriptPubKey, amount);
        }

        optional<uint256> ovk;
        // Select Sapling notes
        vector<SaplingOutPoint> saplingOPs;
        vector<SaplingNote> saplingNotes;
        vector<SaplingExpandedSpendingKey> expsks;
        for (const MergeToAddressInputSaplingNote& saplingNoteInput: saplingNoteInputs_) {
            saplingOPs.push_back(get<0>(saplingNoteInput));
            saplingNotes.push_back(get<1>(saplingNoteInput));
            auto expsk = get<3>(saplingNoteInput);
            expsks.push_back(expsk);
            if (!ovk) {
                ovk = expsk.full_viewing_key().ovk;
            }
        }

        // Fetch Sapling anchor and witnesses
        uint256 anchor;
        vector<optional<SaplingWitness>> witnesses;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            pwalletMain->GetSaplingNoteWitnesses(saplingOPs, witnesses, anchor);
        }

        // Add Sapling spends
        for (size_t i = 0; i < saplingNotes.size(); i++) {
            if (!witnesses[i]) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Missing witness for Sapling note");
            }
            m_builder->AddSaplingSpend(expsks[i], saplingNotes[i], anchor, witnesses[i].value());
        }

        if (isToTaddr_) {
            m_builder->AddTransparentOutput(toTaddr_, sendAmount);
        } else {
            string zaddr = get<0>(recipient_);
            string memo = get<1>(recipient_);
            array<unsigned char, ZC_MEMO_SIZE> hexMemo = get_memo_from_hex_string(memo);
            auto saplingPaymentAddress = get_if<libzcash::SaplingPaymentAddress>(&toPaymentAddress_);
            if (saplingPaymentAddress == nullptr) {
                // This should never happen as we have already determined that the payment is to sapling
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Could not get Sapling payment address.");
            }
            if (saplingNoteInputs_.size() == 0 && utxoInputs_.size() > 0) {
                // Sending from t-addresses, which we don't have ovks for. Instead,
                // generate a common one from the HD seed. This ensures the data is
                // recoverable, while keeping it logically separate from the ZIP 32
                // Sapling key hierarchy, which the user might not be using.
                HDSeed seed;
                if (!pwalletMain->GetHDSeed(seed)) {
                    throw JSONRPCError(
                        RPC_WALLET_ERROR,
                        "AsyncRPCOperation_sendmany: HD seed not found");
                }
                ovk = ovkForShieldingFromTaddr(seed);
            }
            if (!ovk) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Sending to a Sapling address requires an ovk.");
            }
            m_builder->AddSaplingOutput(ovk.value(), *saplingPaymentAddress, sendAmount, hexMemo);
        }


        // Build the transaction
        tx_ = m_builder->Build().GetTxOrThrow();

        // Send the transaction
        // TODO: Use CWallet::CommitTransaction instead of sendrawtransaction
        auto signedtxn = EncodeHexTx(tx_);
        UniValue o(UniValue::VOBJ);
        if (!testmode)
        {
            UniValue params(UniValue::VARR);
            params.push_back(std::move(signedtxn));
            UniValue sendResultValue = sendrawtransaction(params, false);
            if (sendResultValue.isNull())
                throw JSONRPCError(RPC_WALLET_ERROR, "sendrawtransaction did not return an error or a txid.");

            auto txid = sendResultValue.get_str();
            o.pushKV(RPC_KEY_TXID, std::move(txid));
        } else {
            // Test mode does not send the transaction to the network.
            o.pushKV("test", 1);
            o.pushKV(RPC_KEY_TXID, tx_.GetHash().ToString());
            o.pushKV("hex", std::move(signedtxn));
        }
        set_result(std::move(o));

        return true;
    }
    /**
     * END SCENARIO #0
     */


    /**
     * SCENARIO #1
     *
     * taddrs -> taddr
     *
     * There are no zaddrs or joinsplits involved.
     */
    if (isPureTaddrOnlyTx) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("rawtxn", EncodeHexTx(tx_));
        sign_send_raw_transaction(obj);
        return true;
    }
    /**
     * END SCENARIO #1
     */

    return true;
}


extern UniValue signrawtransaction(const UniValue& params, bool fHelp);

/**
 * Sign and send a raw transaction.
 * Raw transaction as hex string should be in object field "rawtxn"
 */
void AsyncRPCOperation_mergetoaddress::sign_send_raw_transaction(UniValue obj)
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
        o.pushKV(RPC_KEY_TXID, std::move(txid));
    } else {
        // Test mode does not send the transaction to the network.

        CDataStream stream(ParseHex(signedtxn), SER_NETWORK, PROTOCOL_VERSION);
        CTransaction tx;
        stream >> tx;

        o.pushKV("test", 1);
        o.pushKV(RPC_KEY_TXID, tx.GetHash().ToString());
        o.pushKV("hex", signedtxn);
    }
    set_result(std::move(o));

    // Keep the signed transaction so we can hash to the same txid
    CDataStream stream(ParseHex(signedtxn), SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;
    stream >> tx;
    tx_ = tx;
}

array<unsigned char, ZC_MEMO_SIZE> AsyncRPCOperation_mergetoaddress::get_memo_from_hex_string(string s)
{
    array<unsigned char, ZC_MEMO_SIZE> memo = {{0x00}};

    v_uint8 rawMemo = ParseHex(s.c_str());

    // If ParseHex comes across a non-hex char, it will stop but still return results so far.
    size_t slen = s.length();
    if (slen % 2 != 0 || (slen > 0 && rawMemo.size() != slen / 2)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Memo must be in hexadecimal format");
    }

    if (rawMemo.size() > ZC_MEMO_SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Memo size of %d is too big, maximum allowed is %d", rawMemo.size(), ZC_MEMO_SIZE));
    }

    // copy vector into array
    size_t lenMemo = rawMemo.size();
    for (int i = 0; i < ZC_MEMO_SIZE && i < lenMemo; i++)
        memo[i] = rawMemo[i];
    return memo;
}

/**
 * Override getStatus() to append the operation's input parameters to the default status object.
 */
UniValue AsyncRPCOperation_mergetoaddress::getStatus() const
{
    UniValue v = AsyncRPCOperation::getStatus();
    if (contextinfo_.isNull()) {
        return v;
    }

    UniValue obj = v.get_obj();
    obj.pushKV("method", "z_mergetoaddress");
    obj.pushKV("params", contextinfo_);
    return obj;
}

/**
 * Lock input utxos
 */
 void AsyncRPCOperation_mergetoaddress::lock_utxos() {
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto utxo : utxoInputs_) {
        pwalletMain->LockCoin(get<0>(utxo));
    }
}

/**
 * Unlock input utxos
 */
void AsyncRPCOperation_mergetoaddress::unlock_utxos() {
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto utxo : utxoInputs_) {
        pwalletMain->UnlockCoin(get<0>(utxo));
    }
}


/**
 * Lock input notes
 */
 void AsyncRPCOperation_mergetoaddress::lock_notes()
 {
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (const auto &note : saplingNoteInputs_)
        pwalletMain->LockNote(get<0>(note));
}

/**
 * Unlock input notes
 */
void AsyncRPCOperation_mergetoaddress::unlock_notes()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (const auto &note : saplingNoteInputs_)
        pwalletMain->UnlockNote(get<0>(note));
}
