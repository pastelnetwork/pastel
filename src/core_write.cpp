// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <univalue.h>

#include <utils/util.h>
#include <utils/utilstrencodings.h>
#include <core_io.h>
#include <key_io.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <serialize.h>
#include <streams.h>
#include <utilmoneystr.h>
#include <script/interpreter.h>

using namespace std;

string FormatScript(const CScript& script)
{
    string ret;
    auto it = script.begin();
    opcodetype op;
    v_uint8 vch;
    string str;
    while (it != script.end())
    {
        auto it2 = it;
        vch.clear();
        if (script.GetOp2(it, op, &vch))
        {
            if (op == OP_0)
            {
                ret += "0 ";
                continue;
            }
            if ((op >= OP_1 && op <= OP_16) || op == OP_1NEGATE)
            {
                ret += strprintf("%i ", op - OP_1NEGATE - 1);
                continue;
            }
            if (op >= OP_NOP && op <= OP_CHECKMULTISIGVERIFY)
            {
                str = GetOpName(op);
                if (str.substr(0, 3).compare("OP_") == 0)
                {
                    ret += str.substr(3, string::npos) + " ";
                    continue;
                }
            }
            if (!vch.empty())
                ret += strprintf("0x%x 0x%x ", 
                    HexStr(it2, it - static_cast<unsigned int>(vch.size())), 
                    HexStr(it - static_cast<unsigned int>(vch.size()), it));
            else
                ret += strprintf("0x%x", HexStr(it2, it));
            continue;
        }
        ret += strprintf("0x%x ", HexStr(it2, script.end()));
        break;
    }
    return ret.substr(0, ret.size() - 1);
}

const unordered_map<uint8_t, string> mapSigHashTypes =
    {
        { to_integral_type(SIGHASH::ALL),                 "ALL" },
        { enum_or(SIGHASH::ALL, SIGHASH::ANYONECANPAY),   "ALL|ANYONECANPAY" },
        { to_integral_type(SIGHASH::NONE),                "NONE" },
        { enum_or(SIGHASH::NONE, SIGHASH::ANYONECANPAY),  "NONE|ANYONECANPAY" },
        { to_integral_type(SIGHASH::SINGLE),               "SINGLE" },
        { enum_or(SIGHASH::SINGLE, SIGHASH::ANYONECANPAY), "SINGLE|ANYONECANPAY" } 
    };

/**
 * Create the assembly string representation of a CScript object.
 * @param[in] script    CScript object to convert into the asm string representation.
 * @param[in] fAttemptSighashDecode    Whether to attempt to decode sighash types on data within the script that matches the format
 *                                     of a signature. Only pass true for scripts you believe could contain signatures. For example,
 *                                     pass false, or omit the this argument (defaults to false), for scriptPubKeys.
 */
string ScriptToAsmStr(const CScript& script, const bool fAttemptSighashDecode)
{
    string str;
    opcodetype opcode;
    v_uint8 vch;
    CScript::const_iterator pc = script.begin();
    while (pc < script.end()) {
        if (!str.empty()) {
            str += " ";
        }
        if (!script.GetOp(pc, opcode, vch)) {
            str += "[error]";
            return str;
        }
        if (0 <= opcode && opcode <= OP_PUSHDATA4) {
            if (vch.size() <= static_cast<v_uint8::size_type>(4)) {
                str += strprintf("%d", CScriptNum(vch, false).getint());
            } else {
                // the IsUnspendable check makes sure not to try to decode OP_RETURN data that may match the format of a signature
                if (fAttemptSighashDecode && !script.IsUnspendable()) {
                    string strSigHashDecode;
                    // goal: only attempt to decode a defined sighash type from data that looks like a signature within a scriptSig.
                    // this won't decode correctly formatted public keys in Pubkey or Multisig scripts due to
                    // the restrictions on the pubkey formats (see IsCompressedOrUncompressedPubKey) being incongruous with the
                    // checks in CheckSignatureEncoding.
                    if (CheckSignatureEncoding(vch, SCRIPT_VERIFY_STRICTENC, nullptr)) {
                        const uint8_t chSigHashType = vch.back();
                        if (mapSigHashTypes.count(chSigHashType)) {
                            strSigHashDecode = "[" + mapSigHashTypes.find(chSigHashType)->second + "]";
                            vch.pop_back(); // remove the sighash type byte. it will be replaced by the decode.
                        }
                    }
                    str += HexStr(vch) + strSigHashDecode;
                } else {
                    str += HexStr(vch);
                }
            }
        } else {
            str += GetOpName(opcode);
        }
    }
    return str;
}

string EncodeHexTx(const CTransaction& tx)
{
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;
    return HexStr(ssTx.begin(), ssTx.end());
}

string EncodeHexOutPoint(const COutPoint& t)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << t;
    return HexStr(ss.begin(), ss.end());
}

void ScriptPubKeyToUniv(const CScript& scriptPubKey,
                        UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;

    out.pushKV("asm", ScriptToAsmStr(scriptPubKey));
    if (fIncludeHex)
        out.pushKV("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        out.pushKV("type", GetTxnOutputType(type));
        return;
    }

    out.pushKV("reqSigs", nRequired);
    out.pushKV("type", GetTxnOutputType(type));

    KeyIO keyIO(Params());
    UniValue a(UniValue::VARR);
    for (const CTxDestination& addr : addresses) {
        a.push_back(keyIO.EncodeDestination(addr));
    }
    out.pushKV("addresses", a);
}

void TxToUniv(const CTransaction& tx, const uint256& hashBlock, UniValue& entry)
{
    entry.pushKV("txid", tx.GetHash().GetHex());
    entry.pushKV("version", tx.nVersion);
    entry.pushKV("locktime", (int64_t)tx.nLockTime);

    UniValue vin(UniValue::VARR);
    for (const auto& txin : tx.vin)
    {
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.pushKV("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
        else {
            in.pushKV("txid", txin.prevout.hash.GetHex());
            in.pushKV("vout", (int64_t)txin.prevout.n);
            UniValue o(UniValue::VOBJ);
            o.pushKV("asm", ScriptToAsmStr(txin.scriptSig, true));
            o.pushKV("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
            in.pushKV("scriptSig", o);
        }
        in.pushKV("sequence", (int64_t)txin.nSequence);
        vin.push_back(in);
    }
    entry.pushKV("vin", vin);

    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];

        UniValue out(UniValue::VOBJ);

        UniValue outValue(UniValue::VNUM, FormatMoney(txout.nValue));
        out.pushKV("value", outValue);
        out.pushKV("n", (int64_t)i);

        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToUniv(txout.scriptPubKey, o, true);
        out.pushKV("scriptPubKey", o);
        vout.push_back(out);
    }
    entry.pushKV("vout", vout);

    if (!hashBlock.IsNull())
        entry.pushKV("blockhash", hashBlock.GetHex());

    entry.pushKV("hex", EncodeHexTx(tx)); // the hex-encoded transaction. used the name "hex" to be consistent with the verbose output of "getrawtransaction".
}
