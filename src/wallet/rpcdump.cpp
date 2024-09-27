// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <fstream>
#include <optional>
#include <cstdint>
#include <variant>

#include <univalue.h>

#include <utils/str_utils.h>
#include <utils/sync.h>
#include <utils/util.h>
#include <utils/utiltime.h>
#include <chain.h>
#include <key_io.h>
#include <rpc/server.h>
#include <rpc/chain-rpc-utils.h>
#include <init.h>
#include <main.h>
#include <script/script.h>
#include <script/standard.h>
#include <zcash/Address.hpp>
#include <wallet/wallet.h>

#include <boost/date_time/posix_time/posix_time.hpp>

using namespace std;

void EnsureWalletIsUnlocked();
bool EnsureWalletIsAvailable(bool avoidException);

UniValue dumpwallet_impl(const UniValue& params, bool fHelp, bool fDumpZKeys);
UniValue importwallet_impl(const UniValue& params, bool fHelp, bool fImportZKeys);


string static EncodeDumpTime(int64_t nTime) {
    return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime);
}

int64_t static DecodeDumpTime(const string &str)
{
    static const boost::posix_time::ptime epoch = boost::posix_time::from_time_t(0);
    static const locale loc(locale::classic(),
        new boost::posix_time::time_input_facet("%Y-%m-%dT%H:%M:%SZ"));
    istringstream iss(str);
    iss.imbue(loc);
    boost::posix_time::ptime ptime(boost::date_time::not_a_date_time);
    iss >> ptime;
    if (ptime.is_not_a_date_time())
        return 0;
    return (ptime - epoch).total_seconds();
}

string static EncodeDumpString(const string &str) {
    stringstream ret;
    for (const unsigned char &c : str)
    {
        if (c <= 32 || c >= 128 || c == '%') {
            ret << '%' << HexStr(&c, &c + 1);
        } else {
            ret << c;
        }
    }
    return ret.str();
}

string DecodeDumpString(const string &str) {
    stringstream ret;
    for (unsigned int pos = 0; pos < str.length(); pos++) {
        unsigned char c = str[pos];
        if (c == '%' && pos+2 < str.length()) {
            c = (((str[pos+1]>>6)*9+((str[pos+1]-'0')&15)) << 4) | 
                ((str[pos+2]>>6)*9+((str[pos+2]-'0')&15));
            pos += 2;
        }
        ret << c;
    }
    return ret.str();
}

UniValue importprivkey(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;
    
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
R"(importprivkey "zcashprivkey" ( "label" rescan rescan_start )

Adds a private key (as returned by dumpprivkey) to your wallet.

Arguments:
1. "zcashprivkey"  (string, required) The private key (see dumpprivkey)
2. "label"         (string, optional, default="") An optional label
3. rescan          (boolean, optional, default=true) Rescan the wallet for transactions
4. rescan_start    (numeric or string, optional, default=0) Block height or hash to start rescan from

Note: This call can take minutes to complete if rescan is true.

Examples:

Dump a private key
)" + HelpExampleCli("dumpprivkey", "\"myaddress\"") + R"(
Import the private key with rescan
)" + HelpExampleCli("importprivkey", "\"mykey\"") + R"(
Import using a label and without rescan
)" + HelpExampleCli("importprivkey", "\"mykey\" \"testing\" false") + R"(
As a JSON-RPC call
)" + HelpExampleRpc("importprivkey", "\"mykey\", \"testing\", false")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    string strSecret = params[0].get_str();
    string strLabel;
    if (params.size() > 1)
        strLabel = params[1].get_str();

    // Whether to perform rescan after import
    bool fRescan = true;
    if (params.size() > 2)
        fRescan = params[2].get_bool();

    // Height or hash to rescan from, default is 0
    block_id_t nRescanBlockId = 0u;
    if (params.size() > 3)
        nRescanBlockId = rpc_get_block_hash_or_height(params[3]);

    KeyIO keyIO(Params());

    string sKeyError;
    const CKey key = keyIO.DecodeSecret(strSecret, sKeyError);
    if (!key.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, tfm::format("Invalid private key, %s", sKeyError.c_str()));

    const CPubKey pubkey = key.GetPubKey();
    assert(key.VerifyPubKey(pubkey));
    CKeyID vchAddress = pubkey.GetID();
    {
        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBook(vchAddress, strLabel, "receive");

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress)) {
            return keyIO.EncodeDestination(vchAddress);
        }

        pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pwalletMain->AddKeyPubKey(key, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain
        pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'

        if (fRescan)
        {
            CBlockIndex* pindex = nullptr;
            if (holds_alternative<uint32_t>(nRescanBlockId))
                pindex = chainActive[get<uint32_t>(nRescanBlockId)];
            else
            {
                const auto it = mapBlockIndex.find(get<uint256>(nRescanBlockId));
                if (it != mapBlockIndex.cend())
                    pindex = it->second;
            }
            if (!pindex)
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                    strprintf("Block not found: %s", 
                        holds_alternative<uint32_t>(nRescanBlockId) ? to_string(get<uint32_t>(nRescanBlockId)) : get<uint256>(nRescanBlockId).ToString()));
            pwalletMain->ScanForWalletTransactions(pindex, true);
        }
    }

    return keyIO.EncodeDestination(vchAddress);
}

UniValue importaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;
    
    if (fHelp || params.size() < 1 || params.size() > 4)
        throw runtime_error(
R"(importaddress "address" ( "label" rescan rescan_start )

Adds an address or script (in hex) that can be watched as if it were in your wallet but cannot be used to spend.

Arguments:
1. "address"     (string, required) The address
2. "label"       (string, optional, default="") An optional label
3. rescan        (boolean, optional, default=true) Rescan the wallet for transactions
4. rescan_start  (numeric or string, optional, default=0) Block height or hash to start rescan from

Note: This call can take minutes to complete if rescan is true.

Examples:

Import an address with rescan
)" + HelpExampleCli("importaddress", "\"myaddress\"") + R"(
Import using a label without rescan
)" + HelpExampleCli("importaddress", "\"myaddress\" \"testing\" false") + R"(
As a JSON-RPC call
)" + HelpExampleRpc("importaddress", "\"myaddress\", \"testing\", false")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CScript script;

    KeyIO keyIO(Params());
    CTxDestination dest = keyIO.DecodeDestination(params[0].get_str());
    if (IsValidDestination(dest))
        script = GetScriptForDestination(dest);
    else if (IsHex(params[0].get_str())) {
        v_uint8 data(ParseHex(params[0].get_str()));
        script = CScript(data.begin(), data.end());
    } else
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pastel address or script");

    string strLabel;
    if (params.size() > 1)
        strLabel = params[1].get_str();

    // Whether to perform rescan after import
    bool fRescan = true;
    if (params.size() > 2)
        fRescan = params[2].get_bool();

    // Height or hash to rescan from, default is 0
    block_id_t nRescanBlockId = 0u;
    if (params.size() > 3)
        nRescanBlockId = rpc_get_block_hash_or_height(params[3]);

    {
        if (IsMineSpendable(::GetIsMine(*pwalletMain, script)))
            throw JSONRPCError(RPC_WALLET_ERROR, "The wallet already contains the private key for this address or script");

        // add to address book or update label
        if (IsValidDestination(dest))
            pwalletMain->SetAddressBook(dest, strLabel, "receive");

        // Don't throw error in case an address is already there
        if (pwalletMain->HaveWatchOnly(script))
            return NullUniValue;

        pwalletMain->MarkDirty();

        if (!pwalletMain->AddWatchOnly(script))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding address to wallet");

        if (fRescan)
        {
            CBlockIndex* pindex = nullptr;
            if (holds_alternative<uint32_t>(nRescanBlockId))
                pindex = chainActive[get<uint32_t>(nRescanBlockId)];
            else {
                const auto it = mapBlockIndex.find(get<uint256>(nRescanBlockId));
                if (it != mapBlockIndex.cend())
                    pindex = it->second;
            }
            if (!pindex)
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   strprintf("Block not found: %s",
                                             holds_alternative<uint32_t>(nRescanBlockId) ? to_string(get<uint32_t>(nRescanBlockId)) : get<uint256>(nRescanBlockId).ToString()));
            pwalletMain->ScanForWalletTransactions(pindex, true);
            pwalletMain->ReacceptWalletTransactions();
        }
    }

    return NullUniValue;
}

UniValue z_importwallet(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(z_importwallet "filename"

Imports taddr and zaddr keys from a wallet export file (see z_exportwallet).

Arguments:
1. "filename"    (string, required) The wallet file

Examples:

Dump the wallet
)"
+ HelpExampleCli("z_exportwallet", "\"nameofbackup\"") + R"(
Import the wallet
)" + HelpExampleCli("z_importwallet", "\"path/to/exportdir/nameofbackup\"") + R"(
Import using the json rpc call
)" + HelpExampleRpc("z_importwallet", "\"path/to/exportdir/nameofbackup\"")
);

	return importwallet_impl(params, fHelp, true);
}

UniValue importwallet(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;
    
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(importwallet "filename"

Imports taddr keys from a wallet dump file (see dumpwallet).

Arguments:
1. "filename"    (string, required) The wallet file

Examples:

Dump the wallet
)" + HelpExampleCli("dumpwallet", "\"nameofbackup\"") + R"(
Import the wallet
)" + HelpExampleCli("importwallet", "\"path/to/exportdir/nameofbackup\"") + R"(
Import using the json rpc call
)" + HelpExampleRpc("importwallet", "\"path/to/exportdir/nameofbackup\"")
);

	return importwallet_impl(params, fHelp, false);
}

UniValue importwallet_impl(const UniValue& params, bool fHelp, bool fImportZKeys)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    ifstream file;
    file.open(params[0].get_str().c_str(), ios::in | ios::ate);
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    int64_t nTimeBegin = chainActive.Tip()->GetBlockTime();

    bool fGood = true;

    int64_t nFilesize = max((int64_t)1, (int64_t)file.tellg());
    file.seekg(0, file.beg);

    KeyIO keyIO(Params());

    pwalletMain->ShowProgress(translate("Importing..."), 0); // show progress dialog in GUI
    string sKeyError;
    v_strings vstr;
    while (file.good())
    {
        pwalletMain->ShowProgress("", max(1, min(99, (int)(((double)file.tellg() / (double)nFilesize) * 100))));
        string line;
        getline(file, line);
        if (line.empty() || line[0] == '#')
            continue;

        str_split(vstr, line, ' ');
        if (vstr.size() < 2)
            continue;

        // Let's see if the address is a valid Pastel spending key
        if (fImportZKeys)
        {
            auto spendingkey = keyIO.DecodeSpendingKey(vstr[0]);
            int64_t nTime = DecodeDumpTime(vstr[1]);
            // Only include hdKeypath and seedFpStr if we have both
            optional<string> hdKeypath = (vstr.size() > 3) ? optional<string>(vstr[2]) : nullopt;
            optional<string> seedFpStr = (vstr.size() > 3) ? optional<string>(vstr[3]) : nullopt;
            if (IsValidSpendingKey(spendingkey))
            {
                auto addResult = visit(
                    AddSpendingKeyToWallet(pwalletMain, Params().GetConsensus(), nTime, hdKeypath, seedFpStr, true), spendingkey);
                if (addResult == KeyAlreadyExists)
                {
                    LogPrint("zrpc", "Skipping import of zaddr (key already present)\n");
                } else if (addResult == KeyNotAdded) {
                    // Something went wrong
                    fGood = false;
                }
                continue;
            }

            LogPrint("zrpc", "Importing detected an error: invalid spending key. Trying as a transparent key...\n");
            // Not a valid spending key, so carry on and see if it's a t-address.
        }

        const CKey key = keyIO.DecodeSecret(vstr[0], sKeyError);
        if (!key.IsValid())
            continue;
        const CPubKey pubkey = key.GetPubKey();
        assert(key.VerifyPubKey(pubkey));
        CKeyID keyid = pubkey.GetID();
        if (pwalletMain->HaveKey(keyid))
        {
            LogPrintf("Skipping import of %s (key already present)\n", keyIO.EncodeDestination(keyid));
            continue;
        }
        int64_t nTime = DecodeDumpTime(vstr[1]);
        string strLabel;
        bool fLabel = true;
        for (unsigned int nStr = 2; nStr < vstr.size(); nStr++)
        {
            if (str_starts_with(vstr[nStr], "#"))
                break;
            if (vstr[nStr] == "change=1")
                fLabel = false;
            if (vstr[nStr] == "reserve=1")
                fLabel = false;
            if (str_starts_with(vstr[nStr], "label=")) {
                strLabel = DecodeDumpString(vstr[nStr].substr(6));
                fLabel = true;
            }
        }
        LogPrintf("Importing %s...\n", keyIO.EncodeDestination(keyid));
        if (!pwalletMain->AddKeyPubKey(key, pubkey)) {
            fGood = false;
            continue;
        }
        pwalletMain->mapKeyMetadata[keyid].nCreateTime = nTime;
        if (fLabel)
            pwalletMain->SetAddressBook(keyid, strLabel, "receive");
        nTimeBegin = min(nTimeBegin, nTime);
    }
    file.close();
    pwalletMain->ShowProgress("", 100); // hide progress dialog in GUI

    CBlockIndex *pindex = chainActive.Tip();
    while (pindex && pindex->pprev && pindex->GetBlockTime() > nTimeBegin - 7200)
        pindex = pindex->pprev;

    if (!pwalletMain->nTimeFirstKey || nTimeBegin < pwalletMain->nTimeFirstKey)
        pwalletMain->nTimeFirstKey = nTimeBegin;

    LogPrintf("Rescanning last %i blocks\n", chainActive.Height() - pindex->nHeight + 1);
    pwalletMain->ScanForWalletTransactions(pindex);
    pwalletMain->MarkDirty();

    if (!fGood)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");

    return NullUniValue;
}

UniValue dumpprivkey(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;
    
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(dumpprivkey "t-addr"

Reveals the private key corresponding to 't-addr'.
Then the importprivkey can be used with this output

Arguments:
1. "t-addr"   (string, required) The transparent address for the private key

Result:
"key"         (string) The private key

Examples:
)"
+ HelpExampleCli("dumpprivkey", "\"myaddress\"")
+ HelpExampleCli("importprivkey", "\"mykey\"")
+ HelpExampleRpc("dumpprivkey", "\"myaddress\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    KeyIO keyIO(Params());

    string strAddress = params[0].get_str();
    CTxDestination dest = keyIO.DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pastel address");
    }
    const CKeyID *keyID = get_if<CKeyID>(&dest);
    if (!keyID) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    }
    CKey vchSecret;
    if (!pwalletMain->GetKey(*keyID, vchSecret)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    }
    return keyIO.EncodeSecret(vchSecret);
}



UniValue z_exportwallet(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;
    
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(z_exportwallet "filename"

Exports all wallet keys, for taddr and zaddr, in a human-readable format.  Overwriting an existing file is not permitted.

Arguments:
1. "filename"    (string, required) The filename, saved in folder set by pasteld -exportdir option

Result:
"path"           (string) The full path of the destination file

Examples:
)"
+ HelpExampleCli("z_exportwallet", "\"test\"")
+ HelpExampleRpc("z_exportwallet", "\"test\"")
);

	return dumpwallet_impl(params, fHelp, true);
}

UniValue dumpwallet(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(dumpwallet "filename"

Dumps taddr wallet keys in a human-readable format.  Overwriting an existing file is not permitted.

Arguments:
1. "filename"    (string, required) The filename, saved in folder set by pasteld -exportdir option

Result:
"path"           (string) The full path of the destination file

Examples:
)"
+ HelpExampleCli("dumpwallet", "\"test\"")
+ HelpExampleRpc("dumpwallet", "\"test\"")
);

	return dumpwallet_impl(params, fHelp, false);
}

UniValue dumpwallet_impl(const UniValue& params, bool fHelp, bool fDumpZKeys)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    fs::path exportdir;
    try
    {
        exportdir = GetExportDir();
    } catch (const runtime_error& e) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, e.what());
    }
    if (exportdir.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot export wallet until the pasteld -exportdir option has been set");
    }
    string unclean = params[0].get_str();
    string clean = SanitizeFilename(unclean);
    if (clean.compare(unclean) != 0)
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Filename is invalid as only alphanumeric characters are allowed.  Try '%s' instead.", clean));
    fs::path exportfilepath = exportdir / clean;

    if (fs::exists(exportfilepath))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot overwrite existing file " + exportfilepath.string());

    ofstream file;
    file.open(exportfilepath.string().c_str());
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    map<CKeyID, int64_t> mapKeyBirth;
    set<CKeyID> setKeyPool;
    pwalletMain->GetKeyBirthTimes(mapKeyBirth);
    pwalletMain->GetAllReserveKeys(setKeyPool);

    // sort time/key pairs
    vector<pair<int64_t, CKeyID> > vKeyBirth;
    vKeyBirth.reserve(mapKeyBirth.size());
    for (const auto& [keyID, nTime] : mapKeyBirth)
        vKeyBirth.emplace_back(nTime, keyID);
    mapKeyBirth.clear();
    std::sort(vKeyBirth.begin(), vKeyBirth.end());

    KeyIO keyIO(Params());

    // produce output
    file << strprintf("# Wallet dump created by Pastel %s (%s)\n", CLIENT_BUILD, CLIENT_DATE);
    file << strprintf("# * Created on %s\n", EncodeDumpTime(GetTime()));
    file << strprintf("# * Best block at time of backup was %i (%s),\n", chainActive.Height(), chainActive.Tip()->GetBlockHashString());
    file << strprintf("#   mined on %s\n", EncodeDumpTime(chainActive.Tip()->GetBlockTime()));
    {
        HDSeed hdSeed;
        pwalletMain->GetHDSeed(hdSeed);
        auto rawSeed = hdSeed.RawSeed();
        file << strprintf("# HDSeed=%s fingerprint=%s", HexStr(rawSeed.begin(), rawSeed.end()), hdSeed.Fingerprint().GetHex());
        file << "\n";
    }
    file << "\n";

    string strTime, strAddr;
    for (const auto &[nTime, keyid]: vKeyBirth)
    {
        strTime = EncodeDumpTime(nTime);
        strAddr = keyIO.EncodeDestination(keyid);
        CKey key;
        if (pwalletMain->GetKey(keyid, key))
        {
            if (pwalletMain->mapAddressBook.count(keyid))
            {
                file << strprintf("%s %s label=%s # addr=%s\n", keyIO.EncodeSecret(key), strTime, EncodeDumpString(pwalletMain->mapAddressBook[keyid].name), strAddr);
            } else if (setKeyPool.count(keyid)) {
                file << strprintf("%s %s reserve=1 # addr=%s\n", keyIO.EncodeSecret(key), strTime, strAddr);
            } else {
                file << strprintf("%s %s change=1 # addr=%s\n", keyIO.EncodeSecret(key), strTime, strAddr);
            }
        }
    }
    file << "\n";

    if (fDumpZKeys)
    {
        set<libzcash::SaplingPaymentAddress> saplingAddresses;
        pwalletMain->GetSaplingPaymentAddresses(saplingAddresses);
        file << "\n";
        file << "# Sapling keys\n";
        file << "\n";
        for (const auto &addr : saplingAddresses)
        {
            libzcash::SaplingExtendedSpendingKey extsk;
            if (pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk))
            {
                auto ivk = extsk.expsk.full_viewing_key().in_viewing_key();
                CKeyMetadata keyMeta = pwalletMain->mapSaplingZKeyMetadata[ivk];
                strTime = EncodeDumpTime(keyMeta.nCreateTime);
                // Keys imported with z_importkey do not have zip32 metadata
                if (keyMeta.hdKeypath.empty() || keyMeta.seedFp.IsNull())
                    file << strprintf("%s %s # zaddr=%s\n", keyIO.EncodeSpendingKey(extsk), strTime, keyIO.EncodePaymentAddress(addr));
                else
                    file << strprintf("%s %s %s %s # zaddr=%s\n", keyIO.EncodeSpendingKey(extsk), strTime, keyMeta.hdKeypath, keyMeta.seedFp.GetHex(), keyIO.EncodePaymentAddress(addr));
            }
        }
        file << "\n";
    }

    file << "# End of dump\n";
    file.close();

    return exportfilepath.string();
}


UniValue z_importkey(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
R"(z_importkey "zkey" ( rescan startHeight )

Adds a zkey (as returned by z_exportkey) to your wallet.

Arguments:
1. "zkey"             (string, required) The zkey (see z_exportkey)
2. rescan             (string, optional, default="whenkeyisnew") Rescan the wallet for transactions - can be "yes", "no" or "whenkeyisnew"
3. startHeight        (numeric, optional, default=0) Block height to start rescan from

Note: This call can take minutes to complete if rescan is true.

Result:
{
  "type" : "xxxx",                         (string) "sprout" or "sapling"
  "address" : "address|DefaultAddress",    (string) The address corresponding to the spending key (for Sapling, this is the default address).
}

Examples:

Export a zkey
)" + HelpExampleCli("z_exportkey", "\"myaddress\"") + R"(
Import the zkey with rescan
)" + HelpExampleCli("z_importkey", "\"mykey\"") + R"(
Import the zkey with partial rescan
)" + HelpExampleCli("z_importkey", "\"mykey\" whenkeyisnew 30000") + R"(
Re-import the zkey with longer partial rescan
)" + HelpExampleCli("z_importkey", "\"mykey\" yes 20000") + R"(
As a JSON-RPC call
)" + HelpExampleRpc("z_importkey", "\"mykey\", \"no\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    // Whether to perform rescan after import
    bool fRescan = true;
    bool fIgnoreExistingKey = true;
    if (params.size() > 1) {
        auto rescan = params[1].get_str();
        if (rescan.compare("whenkeyisnew") != 0) {
            fIgnoreExistingKey = false;
            if (rescan.compare("yes") == 0) {
                fRescan = true;
            } else if (rescan.compare("no") == 0) {
                fRescan = false;
            } else {
                // Handle older API
                UniValue jVal;
                if (!jVal.read(string("[")+rescan+string("]")) ||
                    !jVal.isArray() || jVal.size()!=1 || !jVal[0].isBool()) {
                    throw JSONRPCError(
                        RPC_INVALID_PARAMETER,
                        "rescan must be \"yes\", \"no\" or \"whenkeyisnew\"");
                }
                fRescan = jVal[0].getBool();
            }
        }
    }

    // Height to rescan from
    int nRescanHeight = 0;
    if (params.size() > 2)
        nRescanHeight = params[2].get_int();
    if (nRescanHeight < 0 || nRescanHeight > chainActive.Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }

    KeyIO keyIO(Params());
    string strSecret = params[0].get_str();
    auto spendingkey = keyIO.DecodeSpendingKey(strSecret);
    if (!IsValidSpendingKey(spendingkey)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid spending key");
    }

    auto addrInfo = visit(libzcash::AddressInfoFromSpendingKey{}, spendingkey);
    UniValue result(UniValue::VOBJ);
    result.pushKV("type", addrInfo.first);
    result.pushKV("address", keyIO.EncodePaymentAddress(addrInfo.second));

    // Sapling support
    auto addResult = visit(AddSpendingKeyToWallet(pwalletMain, Params().GetConsensus()), spendingkey);
    if (addResult == KeyAlreadyExists && fIgnoreExistingKey) {
        return result;
    }
    pwalletMain->MarkDirty();
    if (addResult == KeyNotAdded) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding spending key to wallet");
    }
    
    // whenever a key is imported, we need to scan the whole chain
    pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'
    
    // We want to scan for transactions and notes
    if (fRescan) {
        pwalletMain->ScanForWalletTransactions(chainActive[nRescanHeight], true);
    }

    return result;
}

UniValue z_importviewingkey(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
R"(z_importviewingkey "vkey" ( rescan startHeight )

Adds a viewing key (as returned by z_exportviewingkey) to your wallet.

Arguments:
1. "vkey"             (string, required) The viewing key (see z_exportviewingkey)
2. rescan             (string, optional, default="whenkeyisnew") Rescan the wallet for transactions - can be "yes", "no" or "whenkeyisnew"
3. startHeight        (numeric, optional, default=0) Block height to start rescan from

Note: This call can take minutes to complete if rescan is true.

Result:
{
  "type" : "xxxx",                         (string) "sprout" or "sapling"
  "address" : "address|DefaultAddress",    (string) The address corresponding to the viewing key (for Sapling, this is the default address).
}

Examples:

Import a viewing key
)" + HelpExampleCli("z_importviewingkey", "\"vkey\"") + R"(
Import the viewing key without rescan
)" + HelpExampleCli("z_importviewingkey", "\"vkey\", no") + R"(
Import the viewing key with partial rescan
)" + HelpExampleCli("z_importviewingkey", "\"vkey\" whenkeyisnew 30000") + R"(
Re-import the viewing key with longer partial rescan
)" + HelpExampleCli("z_importviewingkey", "\"vkey\" yes 20000") + R"(
As a JSON-RPC call
)" + HelpExampleRpc("z_importviewingkey", "\"vkey\", \"no\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    // Whether to perform rescan after import
    bool fRescan = true;
    bool fIgnoreExistingKey = true;
    if (params.size() > 1) {
        auto rescan = params[1].get_str();
        if (rescan.compare("whenkeyisnew") != 0) {
            fIgnoreExistingKey = false;
            if (rescan.compare("no") == 0) {
                fRescan = false;
            } else if (rescan.compare("yes") != 0) {
                throw JSONRPCError(
                    RPC_INVALID_PARAMETER,
                    "rescan must be \"yes\", \"no\" or \"whenkeyisnew\"");
            }
        }
    }

    // Height to rescan from
    int nRescanHeight = 0;
    if (params.size() > 2) {
        nRescanHeight = params[2].get_int();
    }
    if (nRescanHeight < 0 || nRescanHeight > chainActive.Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }

    KeyIO keyIO(Params());
    string strVKey = params[0].get_str();
    auto viewingkey = keyIO.DecodeViewingKey(strVKey);
    if (!IsValidViewingKey(viewingkey)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid viewing key");
    }

    auto addrInfo = visit(libzcash::AddressInfoFromViewingKey{}, viewingkey);
    UniValue result(UniValue::VOBJ);
    const string strAddress = keyIO.EncodePaymentAddress(addrInfo.second);
    result.pushKV("type", addrInfo.first);
    result.pushKV("address", strAddress);

    auto addResult = visit(AddViewingKeyToWallet(pwalletMain), viewingkey);
    if (addResult == SpendingKeyExists)
        throw JSONRPCError(
            RPC_WALLET_ERROR,
            "The wallet already contains the private key for this viewing key (address: " + strAddress + ")");
    // Don't throw error in case a viewing key is already there
    if (addResult == KeyAlreadyExists && fIgnoreExistingKey)
        return result;
    pwalletMain->MarkDirty();
    if (addResult == KeyNotAdded)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding viewing key to wallet");


    // We want to scan for transactions and notes
    if (fRescan)
        pwalletMain->ScanForWalletTransactions(chainActive[nRescanHeight], true);

    return result;
}

UniValue z_exportkey(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(z_exportkey "zaddr"

Reveals the zkey corresponding to 'zaddr'.
Then the z_importkey can be used with this output

Arguments:
1. "zaddr"   (string, required) The zaddr for the private key

Result:
"key"        (string) The private key

Examples:
)"
+ HelpExampleCli("z_exportkey", "\"myaddress\"")
+ HelpExampleCli("z_importkey", "\"mykey\"")
+ HelpExampleRpc("z_exportkey", "\"myaddress\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    string strAddress = params[0].get_str();

    KeyIO keyIO(Params());
    auto address = keyIO.DecodePaymentAddress(strAddress);
    if (!IsValidPaymentAddress(address)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid zaddr");
    }

    // Sapling support
    auto sk = visit(GetSpendingKeyForPaymentAddress(pwalletMain), address);
    if (!sk) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet does not hold private zkey for this zaddr");
    }
    return keyIO.EncodeSpendingKey(sk.value());
}

UniValue z_exportviewingkey(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(z_exportviewingkey "zaddr"

Reveals the viewing key corresponding to 'zaddr'.
Then the z_importviewingkey can be used with this output

Arguments:
1. "zaddr"   (string, required) The zaddr for the viewing key

Result:
"vkey"                  (string) The viewing key

Examples:
)"
+ HelpExampleCli("z_exportviewingkey", "\"myaddress\"")
+ HelpExampleRpc("z_exportviewingkey", "\"myaddress\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    string strAddress = params[0].get_str();

    KeyIO keyIO(Params());
    auto address = keyIO.DecodePaymentAddress(strAddress);
    if (!IsValidPaymentAddress(address)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid zaddr");
    }

    auto vk = visit(GetViewingKeyForPaymentAddress(pwalletMain), address);
    if (vk)
        return keyIO.EncodeViewingKey(vk.value());
    throw JSONRPCError(RPC_WALLET_ERROR, "Wallet does not hold private key or viewing key for this zaddr");
}
