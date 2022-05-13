#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <tuple>
#include <string>

#include <univalue.h>

int get_number(const UniValue& v);
long long get_long_number(const UniValue& v);

UniValue GenerateSendTicketResult(std::tuple<std::string, std::string>&& resultIDs);
