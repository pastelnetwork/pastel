// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <utils/base58.h>
#include <rpc/protocol.h>
#include <rpc/rpc_parser.h>
#include <script/standard.h>
#include <key.h>
#include <key_io.h>
#include <rpc/server.h>
#include <init.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET
using namespace std;

/**
 * Decodes ANI address to CTxDestination object that represents Pastel address.
 * 
 * \param aniAddress - public or script ANI address
 * \return CTxDestination object that represents Pastel address
 */
CTxDestination ani2psl(const string& aniAddress)
{
    v_uint8 vchRet;
    if (!DecodeBase58Check(aniAddress, vchRet))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid ANI address\n");

    uint160 hash;
    copy(vchRet.cbegin() + 1, vchRet.cend(), hash.begin());
    // DecodeBase58Check checks that vchRet.size() >= 4
    if (vchRet.front() == 23) //ANI_PUBKEY_ADDRESS
        return CKeyID(hash);
    if (vchRet.front() == 9) //ANI_SCRIPT_ADDRESS
        return CScriptID(hash);

    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid ANI address type\n");
}

/**
 * Decodes private key string (base58 encoded) to CKey object.
 * 
 * \param str - private key string
 * \return CKey object that encapsulates private key
 */
CKey ani2psl_secret(const string& str, string& sKeyError)
{
    KeyIO keyIO(Params());
    return keyIO.DecodeSecret(str, sKeyError);
}

//INGEST->!!!
#define INGEST_RPC_CMD
UniValue ingest(const UniValue& params, bool fHelp)
{
    RPC_CMD_PARSER(INGEST, params, ingest, ani2psl, ani2psl_secret);

    string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

	if (fHelp || !INGEST.IsCmdSupported())
        throw runtime_error(
R"("ingest" ingest|ani2psl|ani2psl_secret ...

Examples:
)"
+ HelpExampleCli("ingest", "")
+ HelpExampleRpc("ingest", "")
);

    KeyIO keyIO(Params());
#ifdef INGEST_RPC_CMD
    if (INGEST.IsCmd(RPC_CMD_INGEST::ingest))
    {
        if (params.size() != 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "ingest ingest filepath max_tx_per_block\n");

        string path = params[1].get_str();
        int max_tx = stoi(params[2].get_str());
        if (max_tx <= 0)
            max_tx = 1000;

        EnsureWalletIsUnlocked();

        UniValue mnObj(UniValue::VOBJ);

        UniValue addressErrors(UniValue::VOBJ);
        UniValue tnxErrors(UniValue::VOBJ);

        size_t txCounter = 0;
        size_t lineCounter = 0;

        ifstream infile(path);
        if (!infile)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Cannot open file!!!\n");

        ofstream outfile(path + ".output");
        while (!infile.eof()) { //-V1024
            txCounter++;

            vector<CRecipient> vecSend;
            string line;
            CAmount totalAmount = 0;
            while (vecSend.size() < max_tx && getline(infile, line)) {
                //AW7rZFu6semXGqyUBsaxuXs6LymQh2kwRA,40101110000000
                //comma must be 35th character!!
                string aniAddress = line.substr(0, 34);

                CTxDestination dest = ani2psl(aniAddress);
                if (!IsValidDestination(dest)) {
                    addressErrors.pushKV(aniAddress, string("Invalid Pastel address converted from ANI address"));
                    continue;
                }

                //ani has the same as psl total amount (21 000 000 000)
                //and same number of decimals - 5 (x.00 000)
                //so no conversion of amount needed
                CAmount aniAmount = stoll(line.substr(35));
                if (aniAmount <= 0) {
                    addressErrors.pushKV(aniAddress, string("Invalid amount for send for ANI address"));
                    continue;
                }
                aniAmount *= INGEST_MULTIPLIER;
                totalAmount += aniAmount;

                CScript scriptPubKey = GetScriptForDestination(dest);
                CRecipient recipient = {scriptPubKey, aniAmount, false};
                vecSend.push_back(recipient);
            }

            auto lines = vecSend.size();

            if (lines == 0)
                continue;

            //        // Check funds
            //        CAmount nBalance = GetAccountBalance("", 1, ISMINE_SPENDABLE);
            //        if (totalAmount > nBalance)
            //        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

            //// Send
            CWalletTx wtx;
            wtx.strFromAccount = "";

            CReserveKey keyChange(pwalletMain);
            CAmount nFeeRequired = 0;
            int nChangePosRet = -1;

            string strFailReason;

            if (!pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason))
            {
                tnxErrors.pushKV(to_string(txCounter), string{"CreateTransaction failed - "} + strFailReason);
                lineCounter += lines;
                continue;
            }

            if (!pwalletMain->CommitTransaction(wtx, keyChange))
            {
                tnxErrors.pushKV(to_string(txCounter), "CommitTransaction failed");
                lineCounter += lines;
                continue;
            }

            UniValue obj(UniValue::VOBJ);
            obj.pushKV(wtx.GetHash().GetHex(), (uint64_t)lines);
            mnObj.pushKV(to_string(txCounter), obj);

            outfile << wtx.GetHash().GetHex() << " : " << lineCounter + 1 << "-" << lineCounter + lines << " (" << lines << ")\n";
            outfile.flush();
            lineCounter += lines;
        }

        mnObj.pushKV("address_errors", addressErrors);
        mnObj.pushKV("tnx_errors", tnxErrors);

        return mnObj;
    }
#endif // INGEST_RPC_CMD
    if (INGEST.IsCmd(RPC_CMD_INGEST::ani2psl))
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "ingest ani2psl ...\n");

        const string aniAddress = params[1].get_str();

        const CTxDestination dest = ani2psl(aniAddress);
        return keyIO.EncodeDestination(dest);
    }

    // ingest ani private key (32-byte)
    if (INGEST.IsCmd(RPC_CMD_INGEST::ani2psl_secret))
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "ingest ani2psl_secret ...\n");

        const string aniSecret = params[1].get_str();
        string sKeyError;
        const CKey pslKey = ani2psl_secret(aniSecret, sKeyError);
        if (!pslKey.IsValid())
            throw JSONRPCError(RPC_INVALID_PARAMETER, tinyformat::format("Invalid private key, %s", sKeyError.c_str()));
        return keyIO.EncodeSecret(pslKey);
    }
    return NullUniValue;
}

