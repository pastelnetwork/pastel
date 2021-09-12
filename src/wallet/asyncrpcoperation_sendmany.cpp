// Copyright (c) 2016 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "asyncrpcoperation_sendmany.h"
#include "asyncrpcqueue.h"
#include "amount.h"
#include "consensus/upgrades.h"
#include "core_io.h"
#include "init.h"
#include "key_io.h"
#include "main.h"
#include "net.h"
#include "netbase.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include "rpc/rpc_consts.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet.h"
#include "walletdb.h"
#include "script/interpreter.h"
#include "utiltime.h"
#include "zcash/IncrementalMerkleTree.hpp"
#include "sodium.h"
#include "miner.h"

#include <array>
#include <iostream>
#include <chrono>
#include <thread>
#include <string>
#include <variant>

using namespace libzcash;

extern UniValue signrawtransaction(const UniValue& params, bool fHelp);
extern UniValue sendrawtransaction(const UniValue& params, bool fHelp);

AsyncRPCOperation_sendmany::AsyncRPCOperation_sendmany(
        std::optional<TransactionBuilder> builder,
        CMutableTransaction contextualTx,
        std::string fromAddress,
        std::vector<SendManyRecipient> tOutputs,
        std::vector<SendManyRecipient> zOutputs,
        int minDepth,
        CAmount fee,
        UniValue contextInfo,
        const bool returnChangeToSenderAddr) :
        tx_(contextualTx), fromaddress_(fromAddress), t_outputs_(tOutputs), z_outputs_(zOutputs), mindepth_(minDepth), fee_(fee), contextinfo_(contextInfo), returnChangeToSenderAddr_(returnChangeToSenderAddr)
{
    assert(fee_ >= 0);

    if (minDepth < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minconf cannot be negative");
    }

    if (fromAddress.size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "From address parameter missing");
    }

    if (tOutputs.size() == 0 && zOutputs.size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No recipients");
    }

    isUsingBuilder_ = false;
    if (builder) {
        isUsingBuilder_ = true;
        builder_ = builder.value();
    }

    KeyIO keyIO(Params());

    fromtaddr_ = keyIO.DecodeDestination(fromAddress);
    isfromtaddr_ = IsValidDestination(fromtaddr_);
    isfromzaddr_ = false;

    if (!isfromtaddr_) {
        auto address = keyIO.DecodePaymentAddress(fromAddress);
        if (IsValidPaymentAddress(address)) {
            // We don't need to lock on the wallet as spending key related methods are thread-safe
            if (!std::visit(HaveSpendingKeyForPaymentAddress(pwalletMain), address)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, no spending key found for zaddr");
            }

            isfromzaddr_ = true;
            frompaymentaddress_ = address;
            spendingkey_ = std::visit(GetSpendingKeyForPaymentAddress(pwalletMain), address).value();
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address");
        }
    }

    if (isfromzaddr_ && minDepth==0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minconf cannot be zero when sending from zaddr");
    }

    // Log the context info i.e. the call parameters to z_sendmany
    if (LogAcceptCategory("zrpcunsafe")) {
        LogPrint("zrpcunsafe", "%s: z_sendmany initialized (params=%s)\n", getId(), contextInfo.write());
    } else {
        LogPrint("zrpc", "%s: z_sendmany initialized\n", getId());
    }

    // Enable payment disclosure if requested
    paymentDisclosureMode = fExperimentalMode && GetBoolArg("-paymentdisclosure", false);
}

AsyncRPCOperation_sendmany::~AsyncRPCOperation_sendmany() {
}

void AsyncRPCOperation_sendmany::main() {
    if (isCancelled())
        return;

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
        std::string message = find_value(objError, "message").get_str();
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
  #ifdef ENABLE_WALLET
    GenerateBitcoins(GetBoolArg("-gen", false), pwalletMain, GetArg("-genproclimit", 1), chainparams);
  #else
    GenerateBitcoins(GetBoolArg("-gen", false), GetArg("-genproclimit", 1), chainparams);
  #endif
#endif

    stop_execution_clock();

    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    std::string s = strprintf("%s: z_sendmany finished (status=%s", getId(), getStateAsString());
    if (success) {
        s += strprintf(", txid=%s)\n", tx_.GetHash().ToString());
    } else {
        s += strprintf(", error=%s)\n", getErrorMessage());
    }
    LogPrintf("%s",s);
}

// Notes:
// 2. #1360 Note selection is not optimal
// 3. #1277 Spendable notes are not locked, so an operation running in parallel could also try to use them
bool AsyncRPCOperation_sendmany::main_impl() {

    assert(isfromtaddr_ != isfromzaddr_);

    const bool isSingleZaddrOutput = (t_outputs_.size()==0 && z_outputs_.size()==1);
    const bool isMultipleZaddrOutput = (t_outputs_.size()==0 && z_outputs_.size()>=1);
    const bool isPureTaddrOnlyTx = (isfromtaddr_ && z_outputs_.size() == 0);
    CAmount minersFee = fee_;

    // When spending coinbase utxos, you can only specify a single zaddr as the change must go somewhere
    // and if there are multiple zaddrs, we don't know where to send it.
    if (isfromtaddr_)
    {
        const bool b = find_utxos(isSingleZaddrOutput);
        if (isSingleZaddrOutput)
        {
            if (!b)
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds, no UTXOs found for taddr from address.");
        } else {
            if (!b)
            {
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strprintf("Could not find any non-coinbase UTXOs to spend.%s", 
                    isMultipleZaddrOutput ? " Coinbase UTXOs can only be sent to a single zaddr recipient." : ""));
            }
        }
    }

    if (isfromzaddr_ && !find_unspent_notes())
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds, no unspent notes found for zaddr from address.");

    CAmount t_inputs_total = 0;
    for (const auto &t : t_inputs_)
        t_inputs_total += std::get<2>(t);

    CAmount z_inputs_total = 0;
    for (const auto &t : z_sapling_inputs_)
        z_inputs_total += t.note.value();

    CAmount t_outputs_total = 0;
    for (const auto &t : t_outputs_)
        t_outputs_total += std::get<1>(t);

    CAmount z_outputs_total = 0;
    for (const auto &t : z_outputs_)
        z_outputs_total += std::get<1>(t);

    const CAmount sendAmount = z_outputs_total + t_outputs_total;
    const CAmount targetAmount = sendAmount + minersFee;

    assert(!isfromtaddr_ || z_inputs_total == 0);
    assert(!isfromzaddr_ || t_inputs_total == 0);

    if (isfromtaddr_ && (t_inputs_total < targetAmount)) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient transparent funds, have %s, need %s",
            FormatMoney(t_inputs_total), FormatMoney(targetAmount)));
    }
    
    if (isfromzaddr_ && (z_inputs_total < targetAmount)) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient shielded funds, have %s, need %s",
            FormatMoney(z_inputs_total), FormatMoney(targetAmount)));
    }

    // If from address is a taddr, select UTXOs to spend
    CAmount selectedUTXOAmount = 0;
    bool selectedUTXOCoinbase = false;
    if (isfromtaddr_) {
        // Get dust threshold
        CKey secret;
        secret.MakeNewKey(true);
        CScript scriptPubKey = GetScriptForDestination(secret.GetPubKey().GetID());
        CTxOut out(CAmount(1), scriptPubKey);
        CAmount dustThreshold = out.GetDustThreshold(minRelayTxFee);
        CAmount dustChange = -1;

        std::vector<SendManyInputUTXO> selectedTInputs;
        for (const auto & t : t_inputs_)
        {
            const bool b = std::get<3>(t);
            if (b)
                selectedUTXOCoinbase = true;
            selectedUTXOAmount += std::get<2>(t);
            selectedTInputs.push_back(t);
            if (selectedUTXOAmount >= targetAmount)
            {
                // Select another utxo if there is change less than the dust threshold.
                dustChange = selectedUTXOAmount - targetAmount;
                if (dustChange == 0 || dustChange >= dustThreshold)
                    break;
            }
        }

        // If there is transparent change, is it valid or is it dust?
        if (dustChange < dustThreshold && dustChange != 0) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                strprintf("Insufficient transparent funds, have %s, need %s more to avoid creating invalid change output %s (dust threshold is %s)",
                FormatMoney(t_inputs_total), FormatMoney(dustThreshold - dustChange), FormatMoney(dustChange), FormatMoney(dustThreshold)));
        }

        t_inputs_ = selectedTInputs;
        t_inputs_total = selectedUTXOAmount;

        // update the transaction with these inputs
        if (isUsingBuilder_) {
            CScript scriptPubKey = GetScriptForDestination(fromtaddr_);
            for (const auto &t : t_inputs_)
            {
                uint256 txid = std::get<0>(t);
                int vout = std::get<1>(t);
                CAmount amount = std::get<2>(t);
                builder_.AddTransparentInput(COutPoint(txid, vout), scriptPubKey, amount);
            }
        } else {
            CMutableTransaction rawTx(tx_);
            for (const auto &t : t_inputs_)
            {
                uint256 txid = std::get<0>(t);
                int vout = std::get<1>(t);
                CAmount amount = std::get<2>(t);
                CTxIn in(COutPoint(txid, vout));
                rawTx.vin.push_back(in);
            }
            tx_ = CTransaction(rawTx);
        }
    }

    LogPrint((isfromtaddr_) ? "zrpc" : "zrpcunsafe", "%s: spending %s to send %s with fee %s\n",
            getId(), FormatMoney(targetAmount), FormatMoney(sendAmount), FormatMoney(minersFee));
    LogPrint("zrpc", "%s: transparent input: %s (to choose from)\n", getId(), FormatMoney(t_inputs_total));
    LogPrint("zrpcunsafe", "%s: private input: %s (to choose from)\n", getId(), FormatMoney(z_inputs_total));
    LogPrint("zrpc", "%s: transparent output: %s\n", getId(), FormatMoney(t_outputs_total));
    LogPrint("zrpcunsafe", "%s: private output: %s\n", getId(), FormatMoney(z_outputs_total));
    LogPrint("zrpc", "%s: fee: %s\n", getId(), FormatMoney(minersFee));

    KeyIO keyIO(Params());

    /**
     * SCENARIO #0
     *
     * Sprout not involved, so we just use the TransactionBuilder and we're done.
     * We added the transparent inputs to the builder earlier.
     */
    if (isUsingBuilder_) {
        builder_.SetFee(minersFee);

        // Get various necessary keys
        SaplingExpandedSpendingKey expsk;
        uint256 ovk;
        if (isfromzaddr_) {
            auto sk = std::get<libzcash::SaplingExtendedSpendingKey>(spendingkey_);
            expsk = sk.expsk;
            ovk = expsk.full_viewing_key().ovk;
        } else {
            // Sending from a t-address, which we don't have an ovk for. Instead,
            // generate a common one from the HD seed. This ensures the data is
            // recoverable, while keeping it logically separate from the ZIP 32
            // Sapling key hierarchy, which the user might not be using.
            HDSeed seed;
            if (!pwalletMain->GetHDSeed(seed)) {
                throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "AsyncRPCOperation_sendmany::main_impl(): HD seed not found");
            }
            ovk = ovkForShieldingFromTaddr(seed);
        }

        // Set change address if we are using transparent funds
        // TODO: Should we just use fromtaddr_ as the change address?
        if (isfromtaddr_) {
            LOCK2(cs_main, pwalletMain->cs_wallet);

            EnsureWalletIsUnlocked();

            CTxDestination changeAddr;
            if (!returnChangeToSenderAddr_) {
                // We generate a new address to send to
                CReserveKey keyChange(pwalletMain);
                CPubKey vchPubKey;
                bool ret = keyChange.GetReservedKey(vchPubKey);
                if (!ret) {
                    throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Could not generate a taddr to use as a change address"); // should never fail, as we just unlocked
                }
                changeAddr = vchPubKey.GetID();
            } else {
                // We send the change back to the sender
                if (isfromtaddr_) {
                    changeAddr = fromtaddr_;
                } else {
                    // This should never happen, because this API is called from t_addr case only
                    throw JSONRPCError(RPC_WALLET_ERROR, "Could not detect type if type of address is t address or z address");
                }
            }

            builder_.SendChangeTo(changeAddr);
        }

        // Select Sapling notes
        std::vector<SaplingOutPoint> ops;
        std::vector<SaplingNote> notes;
        CAmount sum = 0;
        for (const auto &t : z_sapling_inputs_)
        {
            ops.push_back(t.op);
            notes.push_back(t.note);
            sum += t.note.value();
            if (sum >= targetAmount)
                break;
        }

        // Fetch Sapling anchor and witnesses
        uint256 anchor;
        std::vector<std::optional<SaplingWitness>> witnesses;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            pwalletMain->GetSaplingNoteWitnesses(ops, witnesses, anchor);
        }

        // Add Sapling spends
        for (size_t i = 0; i < notes.size(); i++)
        {
            if (!witnesses[i])
                throw JSONRPCError(RPC_WALLET_ERROR, "Missing witness for Sapling note");
            builder_.AddSaplingSpend(expsk, notes[i], anchor, witnesses[i].value());
        }

        // Add Sapling outputs
        for (const auto &r : z_outputs_)
        {
            const auto &address = std::get<0>(r);
            const auto value = std::get<1>(r);
            const auto &hexMemo = std::get<2>(r);

            auto addr = keyIO.DecodePaymentAddress(address);
            assert(std::get_if<libzcash::SaplingPaymentAddress>(&addr) != nullptr);
            auto to = std::get<libzcash::SaplingPaymentAddress>(addr);

            auto memo = get_memo_from_hex_string(hexMemo);

            builder_.AddSaplingOutput(ovk, to, value, memo);
        }

        // Add transparent outputs
        for (const auto &r : t_outputs_)
        {
            const auto &outputAddress = std::get<0>(r);
            const auto amount = std::get<1>(r);

            auto address = keyIO.DecodeDestination(outputAddress);
            builder_.AddTransparentOutput(address, amount);
        }

        // Build the transaction
        tx_ = builder_.Build().GetTxOrThrow();

        // Send the transaction
        // TODO: Use CWallet::CommitTransaction instead of sendrawtransaction
        auto signedtxn = EncodeHexTx(tx_);
        UniValue o(UniValue::VOBJ);
        if (!testmode)
        {
            UniValue params(UniValue::VARR);
            params.push_back(move(signedtxn));
            UniValue sendResultValue = sendrawtransaction(params, false);
            if (sendResultValue.isNull())
                throw JSONRPCError(RPC_WALLET_ERROR, "sendrawtransaction did not return an error or a txid.");

            auto txid = sendResultValue.get_str();
            o.pushKV(RPC_KEY_TXID, move(txid));
        } else {
            // Test mode does not send the transaction to the network.
            o.pushKV("test", 1);
            o.pushKV("txid", tx_.GetHash().ToString());
            o.pushKV("hex", move(signedtxn));
        }
        set_result(move(o));
        return true;
    }
    /**
     * END SCENARIO #0
     */


    // Grab the current consensus branch ID
    {
        LOCK(cs_main);
        consensusBranchId_ = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    }

    /**
     * SCENARIO #1
     *
     * taddr -> taddrs
     *
     * There are no zaddrs or joinsplits involved.
     */
    if (isPureTaddrOnlyTx) {
        add_taddr_outputs_to_tx();

        CAmount funds = selectedUTXOAmount;
        CAmount fundsSpent = t_outputs_total + minersFee;
        CAmount change = funds - fundsSpent;

        if (change > 0) {
            add_taddr_change_output_to_tx(change);

            LogPrint("zrpc", "%s: transparent change in transaction output (amount=%s)\n",
                    getId(),
                    FormatMoney(change)
                    );
        }

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


/**
 * Sign and send a raw transaction.
 * Raw transaction as hex string should be in object field "rawtxn"
 */
void AsyncRPCOperation_sendmany::sign_send_raw_transaction(UniValue obj)
{   
    // Sign the raw transaction
    UniValue rawtxnValue = find_value(obj, "rawtxn");
    if (rawtxnValue.isNull()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for raw transaction");
    }
    std::string rawtxn = rawtxnValue.get_str();

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


bool AsyncRPCOperation_sendmany::find_utxos(bool fAcceptCoinbase=false) {
    std::set<CTxDestination> destinations;
    destinations.insert(fromtaddr_);
    vector<COutput> vecOutputs;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    pwalletMain->AvailableCoins(vecOutputs, false, NULL, true, fAcceptCoinbase);

    for (const auto& out : vecOutputs)
    {
        if (!out.fSpendable) {
            continue;
        }

        if (out.nDepth < mindepth_) {
            continue;
        }

        if (destinations.size()) { //-V547 This is always true, but should be kept to make code wasier to read/maintain
            CTxDestination address;
            if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address)) {
                continue;
            }

            if (!destinations.count(address)) {
                continue;
            }
        }

        // By default we ignore coinbase outputs
        bool isCoinbase = out.tx->IsCoinBase();
        if (isCoinbase && fAcceptCoinbase==false) {
            continue;
        }

        CAmount nValue = out.tx->vout[out.i].nValue;
        SendManyInputUTXO utxo(out.tx->GetHash(), out.i, nValue, isCoinbase);
        t_inputs_.push_back(utxo);
    }

    // sort in ascending order, so smaller utxos appear first
    std::sort(t_inputs_.begin(), t_inputs_.end(), [](SendManyInputUTXO i, SendManyInputUTXO j) -> bool {
        return ( std::get<2>(i) < std::get<2>(j));
    });

    return t_inputs_.size() > 0;
}


bool AsyncRPCOperation_sendmany::find_unspent_notes()
{
    std::vector<SaplingNoteEntry> saplingEntries;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->GetFilteredNotes(saplingEntries, fromaddress_, mindepth_);
    }

    // If using the TransactionBuilder, we only want Sapling notes.
    // If not using it, we only want Sprout notes.
    // TODO: Refactor `GetFilteredNotes()` so we only fetch what we need.
    if (!isUsingBuilder_)
        saplingEntries.clear();

    for (const auto &entry : saplingEntries)
    {
        z_sapling_inputs_.push_back(entry);
        std::string data(entry.memo.cbegin(), entry.memo.cend());
        LogPrint("zrpcunsafe", "%s: found unspent Sapling note (txid=%s, vShieldedSpend=%d, amount=%s, memo=%s)\n",
            getId(),
            entry.op.hash.ToString().substr(0, 10),
            entry.op.n,
            FormatMoney(entry.note.value()),
            HexStr(data).substr(0, 10));
    }

    std::sort(z_sapling_inputs_.begin(), z_sapling_inputs_.end(),
        [](SaplingNoteEntry i, SaplingNoteEntry j) -> bool {
            return i.note.value() > j.note.value();
        });

    return true;
}

void AsyncRPCOperation_sendmany::add_taddr_outputs_to_tx() {

    CMutableTransaction rawTx(tx_);

    KeyIO keyIO(Params());

    for (const auto &r : t_outputs_)
    {
        const auto &outputAddress = std::get<0>(r);
        const CAmount nAmount = std::get<1>(r);

        auto address = keyIO.DecodeDestination(outputAddress);
        if (!IsValidDestination(address))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid output address, not a valid taddr.");

        CScript scriptPubKey = GetScriptForDestination(address);

        CTxOut out(nAmount, scriptPubKey);
        rawTx.vout.push_back(out);
    }

    tx_ = CTransaction(rawTx);
}

void AsyncRPCOperation_sendmany::add_taddr_change_output_to_tx(CAmount amount) {

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();
    CScript scriptPubKey;

    if (!returnChangeToSenderAddr_) {
        // We generate a new address to send to
        CReserveKey keyChange(pwalletMain);
        CPubKey vchPubKey;
        bool ret = keyChange.GetReservedKey(vchPubKey);
        if (!ret) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Could not generate a taddr to use as a change address"); // should never fail, as we just unlocked
        }
        scriptPubKey = GetScriptForDestination(vchPubKey.GetID());
    } else {
        // We send the change back to the sender
        if (isfromtaddr_) {
            scriptPubKey = GetScriptForDestination(fromtaddr_);
        } else {
            // This should never happen, because this API is called from t_addr case only
            throw JSONRPCError(RPC_WALLET_ERROR, "Could not detect type if type of address is t address or z address"); 
        }
    }

    CTxOut out(amount, scriptPubKey);

    CMutableTransaction rawTx(tx_);
    rawTx.vout.push_back(out);
    tx_ = CTransaction(rawTx);
}

std::array<unsigned char, ZC_MEMO_SIZE> AsyncRPCOperation_sendmany::get_memo_from_hex_string(std::string s)
{
    // initialize to default memo (no_memo), see section 5.5 of the protocol spec
    std::array<unsigned char, ZC_MEMO_SIZE> memo = {{0xF6}};
    v_uint8 rawMemo = ParseHex(s.c_str());

    // If ParseHex comes across a non-hex char, it will stop but still return results so far.
    const size_t slen = s.length();
    if (slen % 2 !=0 || (slen>0 && rawMemo.size()!=slen/2))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Memo must be in hexadecimal format");

    if (rawMemo.size() > ZC_MEMO_SIZE)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Memo size of %d is too big, maximum allowed is %d", rawMemo.size(), ZC_MEMO_SIZE));

    // copy vector into boost array
    const size_t lenMemo = rawMemo.size();
    for (size_t i = 0; i < ZC_MEMO_SIZE && i < lenMemo; i++)
        memo[i] = rawMemo[i];
    return memo;
}

/**
 * Override getStatus() to append the operation's input parameters to the default status object.
 */
UniValue AsyncRPCOperation_sendmany::getStatus() const {
    UniValue v = AsyncRPCOperation::getStatus();
    if (contextinfo_.isNull()) {
        return v;
    }

    UniValue obj = v.get_obj();
    obj.pushKV("method", "z_sendmany");
    obj.pushKV("params", contextinfo_ );
    return obj;
}

