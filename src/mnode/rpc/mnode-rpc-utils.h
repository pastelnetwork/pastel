#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <tuple>
#include <string>

#include <univalue.h>

UniValue GenerateSendTicketResult(std::tuple<std::string, std::string>&& resultIDs);
