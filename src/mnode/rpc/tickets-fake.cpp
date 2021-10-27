// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include "rpc/rpc_parser.h"
#include "mnode/rpc/mnode-rpc-utils.h"
#include "mnode/rpc/tickets-fake.h"
#include "mnode/ticket-processor.h"
#include "mnode/mnode-pastel.h"

using namespace std;

#ifdef FAKE_TICKET
UniValue tickets_fake(const UniValue& params, const bool bSend)
{
    RPC_CMD_PARSER2(FAKETICKET, params, mnid, id, nft, act, sell);
    switch (FAKETICKET.cmd()) {
    case RPC_CMD_FAKETICKET::mnid: {
        std::string pastelID = params[2].get_str();
        SecureString strKeyPass(params[3].get_str());
        string address;
        auto regTicket = CPastelIDRegTicket::Create(move(pastelID), std::move(strKeyPass), move(address));
        CAmount ticketPrice = get_long_number(params[4].get_str());
        std::string strVerb = params[5].get_str();
        return CPastelTicketProcessor::CreateFakeTransaction(regTicket, ticketPrice, std::vector<std::pair<std::string, CAmount>>{}, strVerb, bSend);
    } break;

    case RPC_CMD_FAKETICKET::id: {
        std::string pastelID = params[2].get_str();
        SecureString strKeyPass(params[3].get_str());
        std::string address = params[4].get_str();
        auto pastelIDRegTicket = CPastelIDRegTicket::Create(move(pastelID), move(strKeyPass), move(address));
        CAmount ticketPrice = get_long_number(params[5].get_str());
        std::string strVerb = params[6].get_str();
        return CPastelTicketProcessor::CreateFakeTransaction(pastelIDRegTicket, ticketPrice, std::vector<std::pair<std::string, CAmount>>{}, strVerb, bSend);
    } break;

    case RPC_CMD_FAKETICKET::nft: {
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Initial blocks download. Re-try later");

        std::string ticket = params[2].get_str();
        std::string signatures = params[3].get_str();
        std::string pastelID = params[4].get_str();
        SecureString strKeyPass(params[5].get_str());
        std::string key1 = params[6].get_str();
        std::string key2 = params[7].get_str();
        CAmount nStorageFee = get_long_number(params[8]);
        auto NFTRegTicket = CNFTRegTicket::Create(ticket, signatures,
                                                  pastelID, std::move(strKeyPass), key1, key2, nStorageFee);
        CAmount ticketPrice = get_long_number(params[10].get_str());
        std::string strVerb = params[11].get_str();
        return CPastelTicketProcessor::CreateFakeTransaction(NFTRegTicket, ticketPrice, std::vector<std::pair<std::string, CAmount>>{}, strVerb, bSend);
    } break;

    case RPC_CMD_FAKETICKET::act: {
        std::string regTicketTxID = params[2].get_str();
        int height = get_number(params[3]);
        int fee = get_number(params[4]);
        std::string pastelID = params[5].get_str();
        SecureString strKeyPass(params[6].get_str());
        auto NFTActTicket = CNFTActivateTicket::Create(regTicketTxID, height, fee, pastelID, std::move(strKeyPass));
        CAmount ticketPrice = get_long_number(params[7].get_str());
        std::string strVerb = params[8].get_str();
        auto addresses = std::vector<std::pair<std::string, CAmount>>{};
        if (params.size() >= 11) {
            addresses.emplace_back(params[9].get_str(), get_long_number(params[10].get_str()));
        }
        if (params.size() >= 13) {
            addresses.emplace_back(params[11].get_str(), get_long_number(params[12].get_str()));
        }
        if (params.size() == 15) {
            addresses.emplace_back(params[13].get_str(), get_long_number(params[14].get_str()));
        }
        return CPastelTicketProcessor::CreateFakeTransaction(NFTActTicket, ticketPrice, addresses, strVerb, bSend);
    } break;

    case RPC_CMD_FAKETICKET::sell: {
        std::string NFTTicketTxnID = params[2].get_str();
        int price = get_number(params[3]);

        std::string pastelID = params[4].get_str();
        SecureString strKeyPass(params[5].get_str());

        int after = get_number(params[6]);
        int before = get_number(params[7]);

        auto NFTSellTicket = CNFTSellTicket::Create(NFTTicketTxnID, price, after, before, 0, pastelID, std::move(strKeyPass));

        CAmount ticketPrice = get_long_number(params[8].get_str());
        std::string strVerb = params[9].get_str();
        return CPastelTicketProcessor::CreateFakeTransaction(NFTSellTicket, ticketPrice, std::vector<std::pair<std::string, CAmount>>{}, strVerb, bSend);
    } break;

    default:
        break;
    } // switch (FAKETICKET.cmd())
    return NullUniValue;
}
#endif // FAKE_TICKET
