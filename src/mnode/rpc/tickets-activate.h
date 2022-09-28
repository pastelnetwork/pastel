#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <univalue.h>

// "tickets activate" rpc API is called also from "tickets register ..."
UniValue tickets_activate_nft(const UniValue& params, const bool bRegisterAPI = false);
UniValue tickets_activate_action(const UniValue& params, const bool bRegisterAPI = false);
UniValue tickets_activate_nft_collection(const UniValue& params, const bool bRegisterAPI = false);

UniValue tickets_activate(const UniValue& params);
