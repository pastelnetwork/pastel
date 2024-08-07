// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <rpc/rpc_parser.h>
#include <rpc/rpc-utils.h>
#include <mnode/tickets/tickets-all.h>
#include <mnode/rpc/tickets-fake.h>
#include <mnode/ticket-processor.h>

using namespace std;

#ifdef FAKE_TICKET
UniValue tickets_fake(const UniValue& params, const bool bSend)
{
    RPC_CMD_PARSER2(FAKETICKET, params, mnid, id, nft, act, sell, offer);

    UniValue resultObj(NullUniValue);

    switch (FAKETICKET.cmd()) {
    case RPC_CMD_FAKETICKET::mnid: {
        string pastelID = params[2].get_str();
        SecureString strKeyPass(params[3].get_str());
        string address;
        auto mnRegData = make_optional<CMNID_RegData>(true);
        auto regTicket = CPastelIDRegTicket::Create(std::move(pastelID), std::move(strKeyPass), address, mnRegData);
        const CAmount ticketPricePSL = get_long_number(params[4].get_str());
        string strVerb = params[5].get_str();
        resultObj = CPastelTicketProcessor::CreateFakeTransaction(regTicket, ticketPricePSL, vector<pair<string, CAmount>>{}, strVerb, bSend);
    } break;

    case RPC_CMD_FAKETICKET::id: {
        string pastelID = params[2].get_str();
        SecureString strKeyPass(params[3].get_str());
        string address = params[4].get_str();
        auto pastelIDRegTicket = CPastelIDRegTicket::Create(std::move(pastelID), std::move(strKeyPass), std::move(address));
        const CAmount ticketPricePSL = get_long_number(params[5].get_str());
        string strVerb = params[6].get_str();
        resultObj = CPastelTicketProcessor::CreateFakeTransaction(pastelIDRegTicket, ticketPricePSL, vector<pair<string, CAmount>>{}, strVerb, bSend);
    } break;

    case RPC_CMD_FAKETICKET::nft: {
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Initial blocks download. Re-try later");

        string nft_ticket = params[2].get_str();
        string signatures = params[3].get_str();
        string sPastelID = params[4].get_str();
        SecureString strKeyPass(params[5].get_str());
        string label = params[6].get_str();
        CAmount nStorageFee = get_long_number(params[7]);
        auto NFTRegTicket = CNFTRegTicket::Create(std::move(nft_ticket), signatures,
                                                  std::move(sPastelID), std::move(strKeyPass),
                                                  std::move(label), nStorageFee);
        const CAmount ticketPricePSL = get_long_number(params[9].get_str());
        string strVerb = params[10].get_str();
        resultObj = CPastelTicketProcessor::CreateFakeTransaction(NFTRegTicket, ticketPricePSL, vector<pair<string, CAmount>>{}, strVerb, bSend);
    } break;

    case RPC_CMD_FAKETICKET::act: {
        string regTicketTxID = params[2].get_str();
        int height = get_number(params[3]);
        int fee = get_number(params[4]);
        string pastelID = params[5].get_str();
        SecureString strKeyPass(params[6].get_str());
        auto NFTActTicket = CNFTActivateTicket::Create(std::move(regTicketTxID), height, fee, std::move(pastelID), std::move(strKeyPass));
        const CAmount ticketPricePSL = get_long_number(params[7].get_str());
        string strVerb = params[8].get_str();
        auto addresses = vector<pair<string, CAmount>>{};
        if (params.size() >= 11)
            addresses.emplace_back(params[9].get_str(), get_long_number(params[10].get_str()));
        if (params.size() >= 13)
            addresses.emplace_back(params[11].get_str(), get_long_number(params[12].get_str()));
        if (params.size() == 15)
            addresses.emplace_back(params[13].get_str(), get_long_number(params[14].get_str()));
        resultObj = CPastelTicketProcessor::CreateFakeTransaction(NFTActTicket, ticketPricePSL, addresses, strVerb, bSend);
    } break;

    case RPC_CMD_FAKETICKET::sell:
    case RPC_CMD_FAKETICKET::offer:
    {
        string offerTxID = params[2].get_str();
        const int price = get_number(params[3]);

        string pastelID = params[4].get_str();
        SecureString strKeyPass(params[5].get_str());

        const int after = get_number(params[6]);
        const int before = get_number(params[7]);
        
        string intendedFor;

        auto offerTicket = COfferTicket::Create(std::move(offerTxID), price, after, before, 0, 
            std::move(intendedFor), std::move(pastelID), std::move(strKeyPass));

        const CAmount ticketPricePSL = get_long_number(params[8].get_str());
        string strVerb = params[9].get_str();
        resultObj = CPastelTicketProcessor::CreateFakeTransaction(offerTicket, ticketPricePSL, vector<pair<string, CAmount>>{}, strVerb, bSend);
    } break;

    default:
        break;
    } // switch (FAKETICKET.cmd())
    return resultObj;
}
#endif // FAKE_TICKET
