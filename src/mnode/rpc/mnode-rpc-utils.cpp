// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "mnode/rpc/mnode-rpc-utils.h"

int get_number(const UniValue& v)
{
    return v.isStr() ? std::stoi(v.get_str()) : v.get_int();
}

long long get_long_number(const UniValue& v)
{
    return v.isStr() ? std::stoll(v.get_str()) : (long long)v.get_int();
}
