#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <univalue.h>

#define FAKE_TICKET
UniValue tickets_fake(const UniValue& params, const bool bSend);
