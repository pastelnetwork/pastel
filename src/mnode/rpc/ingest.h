#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <univalue.h>
#include <key_io.h>

UniValue ingest(const UniValue& params, bool fHelp);
CKey ani2psl_secret(const std::string& str, std::string& sKeyError);
