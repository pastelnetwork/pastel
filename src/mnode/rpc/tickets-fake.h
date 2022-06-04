#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <univalue.h>
#include <mnode/tickets/ticket-types.h>

#ifdef FAKE_TICKET
UniValue tickets_fake(const UniValue& params, const bool bSend);
#endif // FAKE_TICKET
