#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <tuple>
#include <string>

#include <univalue.h>

UniValue GenerateSendTicketResult(std::tuple<std::string, std::string>&& resultIDs);

/**
 * Generate json array by the list of tickets.
 * 
 * \param vTickets - list of tickets
 * \return json array
 */
template <class _TicketType>
UniValue getJSONforTickets(const std::vector<_TicketType> &vTickets)
{
    if (vTickets.empty())
        return NullUniValue;
	UniValue tArray(UniValue::VARR);
    tArray.reserve(vTickets.size());
    for (const auto& tkt : vTickets)
    {
		UniValue obj(UniValue::VOBJ);
		obj.read(tkt.ToJSON());
		tArray.push_back(obj);
	}
	return tArray;
}
