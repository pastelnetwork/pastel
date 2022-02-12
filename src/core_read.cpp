// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <streams.h>
#include <univalue.h>
#include <util.h>
#include <utilstrencodings.h>
#include <version.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;

CScript ParseScript(const string& s)
{
    CScript result;

    static map<string, opcodetype> mapOpNames;

    if (mapOpNames.empty())
    {
        for (int op = 0; op <= OP_NOP10; op++)
        {
            // Allow OP_RESERVED to get into mapOpNames
            if (op < OP_NOP && op != OP_RESERVED)
                continue;

            string strName = GetOpName(static_cast<opcodetype>(op));
            if (strName == "OP_UNKNOWN")
                continue;
            mapOpNames[strName] = static_cast<opcodetype>(op);
            // Convenience: OP_ADD and just ADD are both recognized:
            if (strName.compare(0, 3, "OP_") == 0)
            {
                mapOpNames[strName.substr(3)] = static_cast<opcodetype>(op);
            }
        }
    }

    v_strings words;
    boost::algorithm::split(words, s, boost::algorithm::is_any_of(" \t\n"), boost::algorithm::token_compress_on);

    for (const auto &w : words)
    {
        if (w.empty())
        {
            // Empty string, ignore. (boost::split given '' will return one word)
        }
        else if (all(w, boost::algorithm::is_digit()) ||
            (boost::algorithm::starts_with(w, "-") && all(string(w.begin()+1, w.end()), boost::algorithm::is_digit())))
        {
            // Number
            int64_t n = atoi64(w);
            result << n;
        }
        else if (boost::algorithm::starts_with(w, "0x") && (w.begin()+2 != w.end()) && IsHex(string(w.begin()+2, w.end())))
        {
            // Raw hex data, inserted NOT pushed onto stack:
            v_uint8 raw = ParseHex(string(w.begin() + 2, w.end()));
            result.insert(result.end(), raw.begin(), raw.end());
        }
        else if (w.size() >= 2 && boost::algorithm::starts_with(w, "'") && boost::algorithm::ends_with(w, "'"))
        {
            // Single-quoted string, pushed as data. NOTE: this is poor-man's
            // parsing, spaces/tabs/newlines in single-quoted strings won't work.
            v_uint8 value(w.begin()+1, w.end()-1);
            result << value;
        }
        else if (mapOpNames.count(w))
        {
            // opcode, e.g. OP_ADD or ADD:
            result << mapOpNames[w];
        }
        else
        {
            throw runtime_error("script parse error");
        }
    }

    return result;
}

bool DecodeHexTx(CTransaction& tx, const string& strHexTx)
{
    if (!IsHex(strHexTx))
        return false;

    v_uint8 txData(ParseHex(strHexTx));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> tx;
    }
    catch (const exception&) {
        return false;
    }

    return true;
}

bool DecodeHexBlk(CBlock& block, const string& strHexBlk)
{
    if (!IsHex(strHexBlk))
        return false;

    v_uint8 blockData(ParseHex(strHexBlk));
    CDataStream ssBlock(blockData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssBlock >> block;
    }
    catch (const exception&) {
        return false;
    }

    return true;
}

uint256 ParseHashUV(const UniValue& v, const string& strName)
{
    string strHex;
    if (v.isStr())
        strHex = v.getValStr();
    return ParseHashStr(strHex, strName);  // Note: ParseHashStr("") throws a runtime_error
}

uint256 ParseHashStr(const string& strHex, const string& strName)
{
    if (!IsHex(strHex)) // Note: IsHex("") is false
        throw runtime_error(strName+" must be hexadecimal string (not '"+strHex+"')");

    uint256 result;
    result.SetHex(strHex);
    return result;
}

v_uint8 ParseHexUV(const UniValue& v, const string& strName)
{
    string strHex;
    if (v.isStr())
        strHex = v.getValStr();
    if (!IsHex(strHex))
        throw runtime_error(strName+" must be hexadecimal string (not '"+strHex+"')");
    return ParseHex(strHex);
}
