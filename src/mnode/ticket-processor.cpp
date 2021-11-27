// Copyright (c) 2019-2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "deprecation.h"
#include "script/sign.h"
#include "core_io.h"
#include "key_io.h"
#include "init.h"
#include "str_utils.h"
#include "mnode/mnode-pastel.h"
#include "mnode/mnode-controller.h"
#include "mnode/ticket-processor.h"
#include "mnode/ticket-txmempool.h"

#include "json/json.hpp"
#include <inttypes.h>

using json = nlohmann::json;
using namespace std;

static shared_ptr<ITxMemPoolTracker> TicketTxMemPoolTracker;

/**
 * Get height of the active blockchain + 1.
 * 
 * \return 0 if chain is not initialized or height of the active chain
 */
unsigned int GetActiveChainHeight()
{
    LOCK(cs_main);
    return static_cast<unsigned int>(chainActive.Height()) + 1;
}

void CPastelTicketProcessor::InitTicketDB()
{
    fs::path ticketsDir = GetDataDir() / "tickets";
    if (!fs::exists(ticketsDir))
        fs::create_directories(ticketsDir);

    uint64_t nTotalCache = (GetArg("-dbcache", 450) << 20);
    constexpr uint64_t nMinDbCache = 4;
    constexpr uint64_t nMaxDbCache = 16384; //16KB
    nTotalCache = max(nTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    nTotalCache = min(nTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbCache
    const uint64_t nTicketDBCache = nTotalCache / 8 / uint8_t(TicketID::COUNT);

    // create DB for each ticket type
    for (uint8_t id = to_integral_type<TicketID>(TicketID::PastelID); id != to_integral_type<TicketID>(TicketID::COUNT); ++id)
        dbs.emplace(static_cast<TicketID>(id), make_unique<CDBWrapper>(ticketsDir / TICKET_INFO[id].szDBSubFolder, nTicketDBCache, false, fReindex));
}

/**
 * Create ticket unique_ptr by type.
 * 
 * \param ticketId - ticket type
 */
unique_ptr<CPastelTicket> CPastelTicketProcessor::CreateTicket(const TicketID ticketId)
{
    unique_ptr<CPastelTicket> ticket;
    switch (ticketId)
    {
    case TicketID::PastelID:
        ticket = make_unique<CPastelIDRegTicket>();
        break;

    case TicketID::NFT:
        ticket = make_unique<CNFTRegTicket>();
        break;

    case TicketID::Activate:
        ticket = make_unique<CNFTActivateTicket>();
        break;

    case TicketID::Sell:
        ticket = make_unique<CNFTSellTicket>();
        break;

    case TicketID::Buy:
        ticket = make_unique<CNFTBuyTicket>();
        break;

    case TicketID::Trade:
        ticket = make_unique<CNFTTradeTicket>();
        break;

    case TicketID::Royalty:
        ticket = make_unique<CNFTRoyaltyTicket>();
        break;

    case TicketID::Down:
        ticket = make_unique<CTakeDownTicket>();
        break;

    case TicketID::Username:
        ticket = make_unique<CChangeUsernameTicket>();
        break;

    case TicketID::EthereumAddress:
        ticket = make_unique<CChangeEthereumAddressTicket>();
        break;

    default: // to suppress compiler warning for not handling TicketID::COUNT
        break;
    }
    return ticket;
}

void CPastelTicketProcessor::UpdatedBlockTip(const CBlockIndex* cBlockIndex, bool fInitialDownload)
{
    if (!cBlockIndex)
        return;

    if (fInitialDownload) {
        //??
    }

    CBlock block;
    if (!ReadBlockFromDisk(block, cBlockIndex, Params().GetConsensus()))
    {
        LogPrintf("CPastelTicket::UpdatedBlockTip -- ERROR: Can't read block from disk\n");
        return;
    }

    for (const CTransaction& tx : block.vtx)
    {
        CMutableTransaction mtx(tx);
        ParseTicketAndUpdateDB(mtx, cBlockIndex->nHeight);
    }
}

void CPastelTicketProcessor::UpdateDB_MVK(const CPastelTicket& ticket, const string& mvKey)
{
    v_strings mainKeys;
    auto realMVKey = RealMVKey(mvKey);
    dbs.at(ticket.ID())->Read(realMVKey, mainKeys);
    if (find(mainKeys.cbegin(), mainKeys.cend(), ticket.KeyOne()) == mainKeys.cend())
    {
        mainKeys.emplace_back(ticket.KeyOne());
        dbs.at(ticket.ID())->Write(realMVKey, mainKeys, true);
    }
}

bool CPastelTicketProcessor::UpdateDB(CPastelTicket &ticket, string& txid, const unsigned int nBlockHeight)
{
    if (!txid.empty())
        ticket.SetTxId(move(txid));
    if (nBlockHeight != 0)
        ticket.SetBlock(nBlockHeight);
    dbs.at(ticket.ID())->Write(ticket.KeyOne(), ticket, true);
    if (ticket.HasKeyTwo())
    {
        auto realKeyTwo = RealKeyTwo(ticket.KeyTwo());
        dbs.at(ticket.ID())->Write(realKeyTwo, ticket.KeyOne(), true);
    }

    if (ticket.HasMVKeyOne())
        UpdateDB_MVK(ticket, ticket.MVKeyOne());
    if (ticket.HasMVKeyTwo())
        UpdateDB_MVK(ticket, ticket.MVKeyTwo());
    if (ticket.HasMVKeyThree())
    {
        UpdateDB_MVK(ticket, ticket.MVKeyThree());
    }
        
    //LogPrintf("tickets", "CPastelTicketProcessor::UpdateDB -- Ticket added into DB with key %s (txid - %s)\n", ticket.KeyOne(), ticket.ticketTnx);
    return true;
}

/**
 * Reads P2FMS (Pay-to-Fake-Multisig) transaction into CDataStream object.
 * Parses the first byte from the stream - it defines ticket id.
 * 
 * \param tx - transaction
 * \param data_stream - data stream
 * \param ticket_id - ticket id (first byte in the data stream)
 * \param error - returns error message if any
 * \param bLog
 * \return 
 */
bool CPastelTicketProcessor::preParseTicket(const CMutableTransaction& tx, CDataStream& data_stream, TicketID& ticket_id, string& error, const bool bLog)
{
    v_uint8 output_data;
    bool bRet = false;
    do
    {
        if (!ParseP2FMSTransaction(tx, output_data, error))
            break;
        if (output_data.empty())
        {
            error = "No correct data found in transaction - empty data";
            break;
        }
        data_stream.write(reinterpret_cast<char*>(output_data.data()), output_data.size());
        uint8_t u;
        data_stream >> u;

        // validate ticket id
        if (u >= to_integral_type<TicketID>(TicketID::COUNT))
        {
            error = strprintf("Unknown ticket type (%hhu) found in P2FMS transaction", u);
            break;
        }
        ticket_id = static_cast<TicketID>(u);
        bRet = true;
    } while (false);
    return bRet;
}

// Called from ContextualCheckTransaction, which called from:
//      AcceptToMemoryPool <- CheckTransaction - this is to validate the new transaction
//      ProcessNewBlock <- AcceptBlock <- CheckBlock <- CheckTransaction <- ContextualCheckBlock - this is to validate transaction in the blocks (new or not)
bool CPastelTicketProcessor::ValidateIfTicketTransaction(const int nHeight, const CTransaction& tx)
{
    CMutableTransaction mtx(tx);

    string error_ret;
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    TicketID ticket_id;

    if (!preParseTicket(tx, data_stream, ticket_id, error_ret, false))
        return true; // this is not a ticket

    CAmount storageFee = 0;
    CAmount tradePrice = 0;
    CAmount royaltyFee = 0;
    CAmount greenFee = 0;
    CAmount expectedTicketFee = 0;

    // use if low fee that will be rounded to 0. can be like?
    bool hasRoyaltyFee{false};
    bool hasGreenFee{false};

    // this is a ticket and it needs to be validated
    bool bOk = false;
    try
    {
        string ticketBlockTxIdStr = tx.GetHash().GetHex();
        LogPrintf("CPastelTicketProcessor::ValidateIfTicketTransaction -- Processing ticket ['%s', txid=%s, nHeight=%d, stream size=%zu]\n", 
           GetTicketDescription(ticket_id),
           ticketBlockTxIdStr, nHeight, data_stream.size());

        auto ticket = CreateTicket(ticket_id);
        if (!ticket)
            error_ret = "unknown ticket_id";
        else
        {
            data_stream >> *ticket;
            ticket->SetTxId(move(ticketBlockTxIdStr));
            ticket->SetBlock(nHeight);
            bOk = ticket->IsValid(false, 0);
            expectedTicketFee = ticket->TicketPrice(nHeight) * COIN;
            storageFee = ticket->GetStorageFee();
            if (ticket_id == TicketID::Trade)
            {
                auto trade_ticket = dynamic_cast<CNFTTradeTicket *>(ticket.get());
                if (!trade_ticket)
                    throw runtime_error("Invalid NFT Trade ticket");

                auto NFTTicket = trade_ticket->FindNFTRegTicket();
                if (!NFTTicket)
                  throw runtime_error("NFT Reg ticket not found");

                auto NFTRegTicket = dynamic_cast<CNFTRegTicket*>(NFTTicket.get());
                if (!NFTRegTicket)
                  throw runtime_error("Invalid NFT Reg ticket");

                tradePrice = trade_ticket->price * COIN;
                if (NFTRegTicket->nRoyalty > 0)
                {
                    hasRoyaltyFee = true;
                    royaltyFee = static_cast<CAmount>(tradePrice * NFTRegTicket->nRoyalty);
                }
                if (!NFTRegTicket->strGreenAddress.empty())
                {
                    hasGreenFee = true;
                    greenFee = tradePrice * CNFTRegTicket::GreenPercent(nHeight) / 100;
                }
                tradePrice -= (royaltyFee + greenFee);
            }
        }
    }
    catch (const exception& ex)
    {
        error_ret = strprintf("Failed to parse and unpack ticket - %s", ex.what());
    }
    catch (...)
    {
        error_ret = "Failed to parse and unpack ticket - Unknown exception";
    }

    if (bOk)
    {
        // Validate various Fees
        const auto num = tx.vout.size();
        CAmount ticketFee = 0;

        CAmount allMNFee = storageFee * COIN * 9 / 10;
        CAmount mn1Fee = allMNFee * 3 / 5;
        CAmount mn23Fee = allMNFee / 5;

        for (size_t i = 0; i < num; i++)
        {
            if ((ticket_id == TicketID::PastelID ||
                 ticket_id == TicketID::NFT ||
                 ticket_id == TicketID::Sell ||
                 ticket_id == TicketID::Buy ||
                 ticket_id == TicketID::Royalty ||
                 ticket_id == TicketID::Username ||
                 ticket_id == TicketID::EthereumAddress ) &&
                i == num - 1) // in these tickets last output is change
                break;
            const auto& txOut = tx.vout[i];
            // in these tickets last 4 outputs are: change and payments to 3 MNs
            if (ticket_id == TicketID::Activate)
            { 
                if (i == num - 4)
                    continue;
                if (i == num - 3)
                {
                    if (mn1Fee != txOut.nValue)
                    {
                        bOk = false;
                        error_ret = strprintf("Wrong main MN fee: expected - %" PRId64 ", real - %" PRId64, mn1Fee, txOut.nValue);
                        break;
                    }
                    continue;
                }
                if (i >= num - 2)
                {
                    if (mn23Fee != txOut.nValue)
                    {
                        bOk = false;
                        error_ret = strprintf("Wrong MN%zu fee: expected - %" PRId64 ", real - %" PRId64, i - num - 4, mn23Fee, txOut.nValue);
                        break;
                    }
                    continue;
                }
            }
            // in these tickets last 2 outputs are: change and payment to the seller
            if (ticket_id == TicketID::Trade)
            {
                if (i == (hasRoyaltyFee && hasGreenFee ? num - 4 :
                          hasRoyaltyFee || hasGreenFee ? num - 3 : num - 2))
                    continue;
                if (i == (hasRoyaltyFee && hasGreenFee ? num - 3 :
                                 hasRoyaltyFee || hasGreenFee ? num - 2 : num - 1))
                {
                    if (tradePrice != txOut.nValue)
                    {
                        bOk = false;
                        error_ret = strprintf("Wrong payment to the seller: expected - %" PRId64 ", real - %" PRId64, tradePrice, txOut.nValue);
                        break;
                    }
                    continue;
                }
                if (hasRoyaltyFee && i == (hasGreenFee ? num - 2 : num - 1))
                {
                    if (royaltyFee != txOut.nValue)
                    {
                        bOk = false;
                        error_ret = strprintf("Wrong payment to the royalty owner: expected - %" PRId64 ", real - %" PRId64, royaltyFee, txOut.nValue);
                        break;
                    }
                    continue;
                }
                if (hasGreenFee && i == num - 1)
                {
                    if (greenFee != txOut.nValue)
                    {
                        bOk = false;
                        error_ret = strprintf("Wrong payment to the green NFT: expected - %" PRId64 ", real - %" PRId64, greenFee, txOut.nValue);
                        break;
                    }
                    continue;
                }
            }

            ticketFee += txOut.nValue;
        }

        if (expectedTicketFee != ticketFee)
        {
            bOk = false;
            error_ret = strprintf("Wrong ticket fee: expected - %" PRId64 ", real - %" PRId64, expectedTicketFee, ticketFee);
        }
    }

    if (!bOk)
        LogPrintf("CPastelTicketProcessor::ValidateIfTicketTransaction -- Invalid ticket ['%s', txid=%s, nHeight=%d]. ERROR: %s\n", 
            GetTicketDescription(ticket_id), tx.GetHash().GetHex(), nHeight, error_ret);

    return bOk;
}

bool CPastelTicketProcessor::ParseTicketAndUpdateDB(CMutableTransaction& tx, const unsigned int nBlockHeight)
{
    string error_ret;
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    TicketID ticket_id;

    if (!preParseTicket(tx, data_stream, ticket_id, error_ret))
        return false;

    try {
        string txid = tx.GetHash().GetHex();

        LogPrintf("CPastelTicketProcessor::ParseTicketAndUpdateDB (called from UpdatedBlockTip) -- Processing ticket ['%s', txid=%s, nBlockHeight=%u]\n", 
            GetTicketDescription(ticket_id), txid, nBlockHeight);

        auto ticket = CreateTicket(ticket_id);
        if (!ticket)
            error_ret = "unknown ticket_id";
        else
        {
            data_stream >> *ticket;
            return UpdateDB(*ticket, txid, nBlockHeight);
        }
    }
    catch (const exception& ex)
    {
        error_ret = strprintf("Failed to parse and unpack ticket - %s", ex.what());
    }
    catch (...)
    {
        error_ret = "Failed to parse and unpack ticket - Unknown exception";
    }

    LogPrintf("CPastelTicketProcessor::ParseTicketAndUpdateDB -- Invalid ticket ['%s', txid=%s, nBlockHeight=%u]. ERROR: %s\n", 
        GetTicketDescription(ticket_id), tx.GetHash().GetHex(), nBlockHeight, error_ret);

    return false;
}

string CPastelTicketProcessor::GetTicketJSON(const uint256 &txid)
{
    auto ticket = GetTicket(txid);
    if (ticket)
        return ticket->ToJSON();
    return "";
}

unique_ptr<CPastelTicket> CPastelTicketProcessor::GetTicket(const uint256 &txid)
{
    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(txid, tx, Params().GetConsensus(), hashBlock, true))
        throw runtime_error(strprintf("No information available about transaction"));

    CMutableTransaction mtx(tx);

    TicketID ticket_id;
    string error_ret;
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);

    if (!preParseTicket(mtx, data_stream, ticket_id, error_ret))
        throw runtime_error(strprintf("Failed to parse P2FMS transaction from data provided. %s", error_ret));

    unique_ptr<CPastelTicket> ticket;
    try
    {
        string ticketBlockTxIdStr = tx.GetHash().GetHex();
        int ticketBlockHeight = -1;
        if (mapBlockIndex.count(hashBlock) != 0)
            ticketBlockHeight = mapBlockIndex[hashBlock]->nHeight;

        ticket = CreateTicket(ticket_id);
        if (ticket)
        {
            data_stream >> *ticket;
            ticket->SetTxId(move(ticketBlockTxIdStr));
            ticket->SetBlock(ticketBlockHeight);
        }
        else
            error_ret = "unknown ticket_id";
    }
    catch (const exception& ex)
    {
        error_ret = strprintf("Failed to parse and unpack ticket - %s", ex.what());
    }
    catch (...)
    {
        error_ret = "Failed to parse and unpack ticket - Unknown exception";
    }

    if (!ticket)
        LogPrintf("CPastelTicketProcessor::ParseTicketAndUpdateDB -- Invalid ticket ['%s', txid=%s]. ERROR: %s\n", 
            GetTicketDescription(ticket_id), tx.GetHash().GetHex(), error_ret);

    return ticket;
}

unique_ptr<CPastelTicket> CPastelTicketProcessor::GetTicket(const string& _txid, const TicketID ticketID)
{
    uint256 txid;
    txid.SetHex(_txid);
    auto ticket = GetTicket(txid);
    if (!ticket || ticketID != ticket->ID())
        throw runtime_error(strprintf("The ticket with this txid [%s] is not in the blockchain", _txid));
    return ticket;
}

bool CPastelTicketProcessor::CheckTicketExist(const CPastelTicket& ticket)
{
    auto key = ticket.KeyOne();
    return dbs.at(ticket.ID())->Exists(key);
}

bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey(const CPastelTicket& ticket)
{
    if (ticket.HasKeyTwo())
    {
        string mainKey;
        const auto sRealKeyTwo = RealKeyTwo(ticket.KeyTwo());
        if (dbs.at(ticket.ID())->Read(sRealKeyTwo, mainKey))
            return dbs.at(ticket.ID())->Exists(mainKey);
    }
    return false;
}

/**
 * Find ticket in DB by key.
 * 
 * \param ticket - ticket object to return
 * \return true if ticket was found
 */
bool CPastelTicketProcessor::FindTicket(CPastelTicket& ticket) const
{
    const auto sKey = ticket.KeyOne();
    return dbs.at(ticket.ID())->Read(sKey, ticket);
}

bool CPastelTicketProcessor::FindTicketBySecondaryKey(CPastelTicket& ticket)
{
    if (ticket.HasKeyTwo())
    {
        string sMainKey;
        const auto sRealKeyTwo = RealKeyTwo(ticket.KeyTwo());
        if (dbs.at(ticket.ID())->Read(sRealKeyTwo, sMainKey))
            return dbs.at(ticket.ID())->Read(sMainKey, ticket);
    }
    return false;
}

template <class _TicketType, typename F>
void CPastelTicketProcessor::ProcessTicketsByMVKey(const string& mvKey, F f) const
{
    v_strings vMainKeys;
    const auto realMVKey = RealMVKey(mvKey);
    dbs.at(_TicketType::GetID())->Read(realMVKey, vMainKeys);
    for (const auto& key : vMainKeys)
    {
        _TicketType ticket;
        if (dbs.at(_TicketType::GetID())->Read(key, ticket))
        {
            if (!f(ticket))
                break; // stop processing tickets if functor returned false
        }
    }
}

template <class _TicketType>
vector<_TicketType> CPastelTicketProcessor::FindTicketsByMVKey(const string& mvKey)
{
    vector<_TicketType> tickets;
    v_strings vMainKeys;
    auto realMVKey = RealMVKey(mvKey);
    dbs.at(_TicketType::GetID())->Read(realMVKey, vMainKeys);
    for (const auto& key : vMainKeys)
    {
        _TicketType ticket;
        if (dbs.at(_TicketType::GetID())->Read(key, ticket))
            tickets.emplace_back(ticket);
    }
    return tickets;
}

string CPastelTicketProcessor::getValueBySecondaryKey(const CPastelTicket& ticket) const
{
    string retVal;
    if (ticket.HasKeyTwo()) {
        string sMainKey;
        const auto sRealKeyTwo = RealKeyTwo(ticket.KeyTwo());
        if (dbs.at(ticket.ID())->Read(sRealKeyTwo, sMainKey))
            retVal = sMainKey;
    }
    return retVal;
}

template PastelIDRegTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CPastelIDRegTicket>(const string&);
template NFTRegTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CNFTRegTicket>(const string&);
template NFTActivateTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CNFTActivateTicket>(const string&);
template NFTSellTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CNFTSellTicket>(const string&);
template NFTBuyTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CNFTBuyTicket>(const string&);
template NFTTradeTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CNFTTradeTicket>(const string&);
template NFTRoyaltyTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CNFTRoyaltyTicket>(const string&);
template ChangeUsernameTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CChangeUsernameTicket>(const string&);
template ChangeEthereumAddressTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CChangeEthereumAddressTicket>(const string&);

v_strings CPastelTicketProcessor::GetAllKeys(const TicketID id) const
{
    v_strings vResults;
    unique_ptr<CDBIterator> pcursor(dbs.at(id)->NewIterator());
    pcursor->SeekToFirst();
    string sKey;
    while (pcursor->Valid())
    {
        sKey.clear();
        if (pcursor->GetKey(sKey))
            vResults.emplace_back(move(sKey));
        pcursor->Next();
    }
    return vResults;
}

/**
 * Apply functor F for all tickets with type _TicketType.
 * 
 * \param f - functor to apply
 *      if functor returns false - enumerations will be stopped
 */
template <class _TicketType, typename F>
void CPastelTicketProcessor::listTickets(F f) const
{
    auto vKeys = GetAllKeys(_TicketType::GetID());
    for (const auto& key : vKeys)
    {
        if (key.front() == '@')
            continue;
        _TicketType ticket;
        ticket.SetKeyOne(key);
        if (!FindTicket(ticket))
            continue;
        if (!f(ticket))
            break;
    }
}

template <class _TicketType>
string CPastelTicketProcessor::ListTickets() const
{
    json jArray;
    listTickets<_TicketType>([&](const _TicketType& ticket) -> bool
    {
        jArray.push_back(json::parse(ticket.ToJSON()));
        return true;
    });
    return jArray.dump();
}
template string CPastelTicketProcessor::ListTickets<CPastelIDRegTicket>() const;
template string CPastelTicketProcessor::ListTickets<CNFTRegTicket>() const;
template string CPastelTicketProcessor::ListTickets<CNFTActivateTicket>() const;
template string CPastelTicketProcessor::ListTickets<CNFTSellTicket>() const;
template string CPastelTicketProcessor::ListTickets<CNFTBuyTicket>() const;
template string CPastelTicketProcessor::ListTickets<CNFTTradeTicket>() const;
template string CPastelTicketProcessor::ListTickets<CNFTRoyaltyTicket>() const;
template string CPastelTicketProcessor::ListTickets<CChangeUsernameTicket>() const;
template string CPastelTicketProcessor::ListTickets<CChangeEthereumAddressTicket>() const;

template <class _TicketType, typename F>
string CPastelTicketProcessor::filterTickets(F f, const bool bCheckConfirmation) const
{
    json jArray;
    const unsigned int nChainHeight = GetActiveChainHeight();
    // list tickets with the specific type (_TicketType) and add to json array if functor f applies
    listTickets<_TicketType>([&](const _TicketType& ticket) -> bool
    {
        //check if the ticket is confirmed
        if (bCheckConfirmation && (nChainHeight - ticket.GetBlock() < masterNodeCtrl.MinTicketConfirmations))
            return true;
        // apply functor to the current ticket
        if (f(ticket, nChainHeight))
            return true;
        jArray.push_back(json::parse(ticket.ToJSON()));
        return true;
    });
    return jArray.dump();
}

/**
 * List Pastel NFT registration tickets using filter.
 *
 * \param filter - ticket filter
 *      1 - mn, include only masternode PastelIDs
 *      2 - personal, include only personal PastelIDs
 *      3 - mine, include only tickets for locally stored PastelIDs in pvPastelIDs 
 * \param pmapIDs - map of locally stored PastelIDs -> LegRoast public key
 * \return json with filtered tickets
 */
string CPastelTicketProcessor::ListFilterPastelIDTickets(const short filter, const pastelid_store_t* pmapIDs) const
{
    return filterTickets<CPastelIDRegTicket>(
        [&](const CPastelIDRegTicket& t, const unsigned int chainHeight) -> bool
        {
            if ((filter == 1 && // don't skip mn
                    !t.outpoint.IsNull()) || 
                (filter == 2 && // don't skip personal
                    t.outpoint.IsNull()) || 
                (filter == 3 && // don't skip locally stored tickets
                    pmapIDs && pmapIDs->find(t.pastelID) != pmapIDs->cend()))
                return false;
            return true;
        });
}

// 1 - active;    2 - inactive;     3 - sold
string CPastelTicketProcessor::ListFilterNFTTickets(const short filter) const
{
    return filterTickets<CNFTRegTicket>(
        [&](const CNFTRegTicket& t, const unsigned int nChainHeight) -> bool
        {
            if (filter == 3)
            {
                CNFTActivateTicket actTicket;
                // find Act ticket for this Reg ticket
                if (CNFTActivateTicket::FindTicketInDb(t.GetTxId(), actTicket))
                {
                    //find Trade tickets listing that Act ticket txid as NFT ticket
                    const auto vTradeTickets = CNFTTradeTicket::FindAllTicketByNFTTxnID(actTicket.GetTxId());
                    if (vTradeTickets.size() >= t.totalCopies)
                        return false; //don't skip sold
                }
            }

            // check if there is Act ticket for this Reg ticket
            if (CNFTActivateTicket::CheckTicketExistByNFTTicketID(t.GetTxId()))
            {
                if (filter == 1)
                    return false; //don't skip active
            } else if (filter == 2)
                return false; //don't skip inactive
            return true;
        });
}

// 1 - available;      2 - sold
string CPastelTicketProcessor::ListFilterActTickets(const short filter) const
{
    return filterTickets<CNFTActivateTicket>(
        [&](const CNFTActivateTicket& t, const unsigned int chainHeight) -> bool
        {
            //find Trade tickets listing this Act ticket txid as NFT ticket
            auto vTradeTickets = CNFTTradeTicket::FindAllTicketByNFTTxnID(t.GetTxId());
            auto ticket = GetTicket(t.regTicketTxnId, TicketID::NFT);
            auto NFTRegTicket = dynamic_cast<CNFTRegTicket*>(ticket.get());
            if (!NFTRegTicket)
                return true;
            if (vTradeTickets.size() < NFTRegTicket->totalCopies)
            {
                if (filter == 1)
                    return false; //don't skip available
            } else if (filter == 2)
                return false; //don't skip sold
            return true;
        });
}
// 0 - all, 1 - available; 2 - unavailable; 3 - expired; 4 - sold
string CPastelTicketProcessor::ListFilterSellTickets(const short filter, const string& pastelID) const
{
    const bool checkConfirmation{filter > 0};
    if (filter == 0 && pastelID.empty()) {
            return ListTickets<CNFTSellTicket>(); // get all
    }
    return filterTickets<CNFTSellTicket>(
        [&](const CNFTSellTicket& t, const unsigned int chainHeight) -> bool
        {
            if (!pastelID.empty() && t.pastelID != pastelID)
            {
                return true; // ignore tickets that do not belong to this pastelID
            }
            CNFTBuyTicket existingBuyTicket;
            //find buy ticket for this sell ticket, if any
            if (CNFTBuyTicket::FindTicketInDb(t.GetTxId(), existingBuyTicket))
            {
                //check if trade ticket exists for this sell ticket
                if (CNFTTradeTicket::CheckTradeTicketExistByBuyTicket(existingBuyTicket.GetTxId()))
                {
                    if (filter == 4)
                        return false; // don't skip sold
                    else
                        return true;
                };
                //if not - check age
                if (existingBuyTicket.GetBlock() + masterNodeCtrl.MaxBuyTicketAge <= chainHeight)
                    return true;
            }
            if (filter == 1)
            {
                //skip sell ticket that is not yet active
                if (t.activeAfter > 0 && chainHeight <= t.activeAfter)
                    return true;
                //skip sell ticket that is already not active
                if (t.activeBefore > 0 && chainHeight >= t.activeBefore)
                    return true;
            } else if (filter == 2) {
                //skip sell ticket that is already active
                if (t.activeAfter > 0 && chainHeight >= t.activeAfter)
                    return true;
            } else if (filter == 3) {
                //skip sell ticket that is still active
                if (t.activeBefore > 0 && chainHeight <= t.activeBefore)
                    return true;
            }
            return false;
        }, checkConfirmation);
}

// 0 - all, 1 - expired;    2 - sold
string CPastelTicketProcessor::ListFilterBuyTickets(const short filter, const string& pastelID) const
{
    const bool checkConfirmation{filter > 0};
    if (filter == 0 && pastelID.empty()) {
            return ListTickets<CNFTBuyTicket>(); // get all
    }
    return filterTickets<CNFTBuyTicket>(
        [&](const CNFTBuyTicket& t, const unsigned int chainHeight) -> bool
        {
            if (!pastelID.empty() && t.pastelID != pastelID)
                return true; // ignore tickets that do not belong to this pastelID
            if (filter == 0)
                return false; // get all belong to this pastel ID
            if (CNFTTradeTicket::CheckTradeTicketExistByBuyTicket(t.GetTxId())) {
                if (filter == 2)
                    return false; //don't skip traded
            } else if (filter == 1 && t.GetBlock() + masterNodeCtrl.MaxBuyTicketAge < chainHeight)
                return false; //don't skip non sold, and expired
            return true;
        }, checkConfirmation);
}

// 0 - all, 1 - available;      2 - sold
string CPastelTicketProcessor::ListFilterTradeTickets(const short filter, const string& pastelID) const
{
    const bool checkConfirmation{filter > 0};
    if (filter == 0 && pastelID.empty()) {
            return ListTickets<CNFTTradeTicket>(); // get all
    }
    return filterTickets<CNFTTradeTicket>(
        [&](const CNFTTradeTicket& t, const unsigned int chainHeight) -> bool
        {
            //find Trade tickets listing this Trade ticket txid as NFT ticket
            auto tradeTickets = CNFTTradeTicket::FindAllTicketByNFTTxnID(t.GetTxId());
            if (!pastelID.empty() && t.pastelID != pastelID)
                return true; // ignore tickets that do not belong to this pastelID
            if (filter == 0)
                return false; // get all belong to this pastel ID
            if (tradeTickets.empty()) {
                if (filter == 1)
                    return false; //don't skip available
            } else if (filter == 2)
                return false; //don't skip sold
            return true;
        }, checkConfirmation);
}

/*static*/ bool CPastelTicketProcessor::WalkBackTradingChain(
        const string& sTxId,
        PastelTickets_t& chain,
        bool shortPath,
        string& errRet) noexcept
{
    unique_ptr<CPastelTicket> pastelTicket;
    
    uint256 txid;
    txid.SetHex(sTxId);
    //  Get ticket pointed by NFTTxnId. This is either Activation or Trade tickets (Sell, Buy, Trade)
    try
    {
        pastelTicket = CPastelTicketProcessor::GetTicket(txid);
    }
    catch ([[maybe_unused]] runtime_error& ex)
    {
        errRet = strprintf("Ticket [txid=%s] is not in the blockchain.", sTxId);
        return false;
    }
    
    bool bOk = false;
    do {
        if (pastelTicket->ID() == TicketID::Trade) {
            auto tradeTicket = dynamic_cast<CNFTTradeTicket *>(pastelTicket.get());
            if (!tradeTicket) {
                errRet = strprintf("The Trade ticket [txid=%s] referred by this ticket [txid=%s] is invalid",
                                   pastelTicket->GetTxId(), sTxId);
                break;
            }
            if (!WalkBackTradingChain(shortPath ? tradeTicket->NFTTxnId : tradeTicket->buyTxnId, chain, shortPath,
                                      errRet))
                break;
        } else if (pastelTicket->ID() == TicketID::Buy) {
            auto tradeTicket = dynamic_cast<CNFTBuyTicket *>(pastelTicket.get());
            if (!tradeTicket) {
                errRet = strprintf("The Buy ticket [txid=%s] referred by this ticket [txid=%s] is invalid",
                                   pastelTicket->GetTxId(), sTxId);
                break;
            }
            if (!WalkBackTradingChain(tradeTicket->sellTxnId, chain, shortPath, errRet))
                break;
        } else if (pastelTicket->ID() == TicketID::Sell) {
            auto tradeTicket = dynamic_cast<CNFTSellTicket *>(pastelTicket.get());
            if (!tradeTicket) {
                errRet = strprintf("The Sell ticket [txid=%s] referred by this ticket [txid=%s] is invalid",
                                   pastelTicket->GetTxId(), sTxId);
                break;
            }
            if (!WalkBackTradingChain(tradeTicket->NFTTxnId, chain, shortPath, errRet))
                break;
        } else if (pastelTicket->ID() == TicketID::Activate) {
            auto actTicket = dynamic_cast<CNFTActivateTicket *>(pastelTicket.get());
            if (!actTicket) {
                errRet = strprintf("The Activation ticket [txid=%s] referred by ticket [txid=%s] ticket is invalid",
                                   pastelTicket->GetTxId(), sTxId);
                break;
            }
            if (!WalkBackTradingChain(actTicket->regTicketTxnId, chain, shortPath, errRet))
                break;
        } else if (pastelTicket->ID() == TicketID::NFT) {
            auto tradeTicket = dynamic_cast<CNFTRegTicket *>(pastelTicket.get());
            if (!tradeTicket) {
                errRet = strprintf("The NFT Registration ticket [txid=%s] referred by ticket [txid=%s] is invalid",
                                   pastelTicket->GetTxId(), sTxId);
                break;
            }
        } else {
            errRet = strprintf("The NFT ticket [txid=%s] referred by ticket [txid=%s] has wrong type - %s]",
                               pastelTicket->GetTxId(), sTxId, pastelTicket->GetTicketName());
            break;
        }
        chain.emplace_back(move(pastelTicket));
        bOk = true;
    }
    while(false);
    
    return bOk;
}

/**
 * Create ticket transaction in the mempool.
 * 
 * \param ticket - Pastel ticket
 * \return - transaction hash in hex format (txid)
 */
string CPastelTicketProcessor::SendTicket(const CPastelTicket& ticket, const opt_string_t& sFundingAddress)
{
    string error;

    try
    {
        ticket.IsValid(true, 0);
    }
    catch (const exception& ex) {
        throw runtime_error(strprintf("Ticket (%s) is invalid - %s", ticket.GetTicketName(), ex.what()));
    }
    catch (...) {
        throw runtime_error(strprintf("Ticket (%s) is invalid - Unknown exception", ticket.GetTicketName()));
    }

    vector<CTxOut> extraOutputs;
    const CAmount extraAmount = ticket.GetExtraOutputs(extraOutputs);

    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << to_integral_type<TicketID>(ticket.ID());
    data_stream << ticket;

    unsigned int chainHeight = GetActiveChainHeight();

    CMutableTransaction tx;
    if (!CreateP2FMSTransactionWithExtra(data_stream, extraOutputs, extraAmount, tx, 
        ticket.TicketPrice(chainHeight), sFundingAddress, error))
        throw runtime_error(strprintf("Failed to create P2FMS from data provided - %s", error));

    if (!StoreP2FMSTransaction(tx, error))
        throw runtime_error(strprintf("Failed to send P2FMS transaction - %s", error));
    return tx.GetHash().GetHex();
}

#ifdef ENABLE_WALLET
bool CPastelTicketProcessor::CreateP2FMSTransaction(const string& input_string, CMutableTransaction& tx_out, 
    const CAmount price, const opt_string_t& sFundingAddress, string& error_ret)
{
    //Convert string data into binary buffer
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << input_string;
    return CreateP2FMSTransaction(data_stream, tx_out, price, sFundingAddress, error_ret);
}

bool CPastelTicketProcessor::CreateP2FMSTransaction(const CDataStream& input_stream, CMutableTransaction& tx_out, 
    const CAmount price, const opt_string_t& sFundingAddress, string& error_ret)
{
    return CreateP2FMSTransactionWithExtra(input_stream, vector<CTxOut>{}, 0, tx_out, price, sFundingAddress, error_ret);
}

/**
 * Check if json value passes fuzzy search filter.
 * 
 * \param jProp - json property value
 * \param sPropFilterValue - filter value
 * \return true if value passes the filter
 */
bool isValuePassFuzzyFilter(const json &jProp, const string &sPropFilterValue) noexcept
{
    bool bPassedFilter = false;
    string sPropValue;
    do
    {
        if (jProp.is_string())
        {
            jProp.get_to(sPropValue);
            // just make case insensitive substring search
            if (!str_ifind(sPropValue, sPropFilterValue))
                break;
        } else if (jProp.is_boolean())
        {
            bool bValue = false;
            // filter value should be convertable to bool
            if (!str_tobool(sPropFilterValue, bValue) || 
                (jProp.get<bool>() != bValue))
                break;
        } else if (jProp.is_number())
        {
            // number should be convertible to string
            try
            {
                sPropValue = to_string(jProp);
                if (sPropValue != sPropFilterValue)
                    break;
            } catch ([[maybe_unused]] const std::bad_alloc& ex)
            {
                break;
            }
        } else
            break;
        bPassedFilter = true;
    } while (false);
    return bPassedFilter;
}

/**
 * Search for NFT tickets using multiple criterias (defined in 'tickets tools searchthumbids').
 * 
 * \param p - structure with search criterias
 * \param fnMatchFound - functor to apply when NFT registration ticket found that matches all search criterias
 */
void CPastelTicketProcessor::SearchForNFTs(const search_thumbids_t& p, function<size_t(const CPastelTicket*, const json&)> &fnMatchFound) const
{
    v_strings vPastelIDs;
    // Creator PastelID can have special 'mine' value
    if (str_icmp(p.sCreatorPastelId, "mine"))
    {
        const auto mapIDs = CPastelID::GetStoredPastelIDs(true);
        if (!mapIDs.empty())
        {
            vPastelIDs.reserve(mapIDs.size());
            for (const auto& [sPastelID, sLegRoastPubKey] : mapIDs)
                vPastelIDs.push_back(sPastelID);
        }
    } else
        vPastelIDs.push_back(p.sCreatorPastelId);
    size_t nResultCount = 0;
    string sDataBase64, sData;
    // process NFT activation tickets by PastelID (mvkey #1)
    for (const auto &sPastelID : vPastelIDs)
    {
        ProcessTicketsByMVKey<CNFTActivateTicket>(sPastelID, [&](const CNFTActivateTicket& actTicket) -> bool
        {
            // check if we exceeded max results
            if (p.nMaxResultCount.has_value() && (nResultCount >= p.nMaxResultCount.value()))
                return false; // stop enumeration

            do
            {
                // filter NFT activation tickets by block height range
                if (p.blockRange.has_value() && !p.blockRange.value().contains(actTicket.GetBlock()))
                    break;

                // find NFT registration ticket by txid
                auto pNftTicketPtr = CPastelTicketProcessor::GetTicket(actTicket.regTicketTxnId, TicketID::NFT);
                if (!pNftTicketPtr)
                    break;
                auto pNftTicket = dynamic_cast<const CNFTRegTicket*>(pNftTicketPtr.get());
                if (!pNftTicket)
                    break;
                // filter by number of copies
                if (p.copyCount.has_value() && !p.copyCount.value().contains(pNftTicket->totalCopies))
                    break;
                // NFT ticket data are base64 encoded
                bool bInvalid = false;
                sData = DecodeBase64(pNftTicket->sNFTTicket, &bInvalid);
                if (bInvalid)
                {
                    LogPrintf("ERROR: failed to decode base64 encoded NFT ticket (%s)", actTicket.regTicketTxnId);
                    break;
                }
                json j;
                try
                {
                    // parse NFT ticket json
                    j = json::parse(sData);
                } catch (const json::exception& ex)
                {
                    LogPrintf("ERROR: failed to parse NFT ticket json (%s). %s", actTicket.regTicketTxnId, SAFE_SZ(ex.what()));
                    break;
                }
                json jApp; // app ticket json
                if (j.contains("app_ticket"))
                {
                    const json &jAppTicketBase64 = j["app_ticket"];
                    if (jAppTicketBase64.is_string())
                    {
                        bInvalid = false;
                        jAppTicketBase64.get_to(sDataBase64);
                        sData = DecodeBase64(sDataBase64, &bInvalid);
                        if (bInvalid)
                        {
                            LogPrintf("ERROR: failed to decode base64 encoded NFT app ticket (%s)", actTicket.regTicketTxnId);
                            break;
                        }
                        try
                        {
                            // parse app ticket json
                            jApp = json::parse(sData);
                        } catch (const json::exception& ex)
                        {
                            LogPrintf("ERROR: failed to parse NFT app ticket json (%s). %s", actTicket.regTicketTxnId, SAFE_SZ(ex.what()));
                            break;
                        }
                    }
                }
                uint32_t nScore;
                // filter by rareness score
                if (p.rarenessScore.has_value() && jApp.contains("rareness_score"))
                {
                    jApp["rareness_score"].get_to(nScore);
                    if (!p.rarenessScore.value().contains(nScore))
                        break;
                }
                // filter by nsfw score
                if (p.nsfwScore.has_value() && jApp.contains("nsfw_score"))
                {
                    jApp["nsfw_score"].get_to(nScore);
                    if (!p.nsfwScore.value().contains(nScore))
                        break;
                }
                // nft reg app ticket properties (name: value)
                string sPropName, sPropValue;
                // fuzzy search (key is lowercased in the fuzzySearchMap)
                bool bPassedFilter = true;
                for (const auto &[sSearchProp, sPropFilterValue] : p.fuzzySearchMap)
                {
                    // first let's check if we have any fuzzy search mappings (search keyword->nft ticket property name)
                    const auto itMapping = p.fuzzyMappings.find(sSearchProp);
                    if (itMapping != p.fuzzyMappings.cend())
                        sPropName = itMapping->second; // found property name in the map -> use it
                    else
                        sPropName = sSearchProp; // try to use search property and nft ticket property name as is
                    if (!jApp.contains(sPropName))
                        continue; // just skip unknown properties
                    bPassedFilter = isValuePassFuzzyFilter(jApp[sPropName], sPropFilterValue);
                }
                if (!bPassedFilter)
                    break;
                // add NFT reg ticket info to the json array
                nResultCount = fnMatchFound(pNftTicket, jApp);
            } while (false);
            return true;
        });
    } // for
}

/**
 * Create scripts for P2FMS (Pay-to-Fake-Multisig) transaction.
 * 
 * \param input_stream - input data stream
 * \param vOutScripts - returns vector of output scripts for P2FMS transaction
 * \return returns input data size
 */
size_t CPastelTicketProcessor::CreateP2FMSScripts(const CDataStream& input_stream, vector<CScript>& vOutScripts)
{
    // fake key size - transaction data should be aligned to this size
    constexpr size_t FAKE_KEY_SIZE = 33;
    // position of the input stream data in vInputData vector
    constexpr size_t STREAM_DATA_POS = uint256::SIZE + sizeof(uint64_t);

    // +--------------  vInputData ---------------------------+
    // |     8 bytes     |    32 bytes     |  nDataStreamSize | 
    // +-----------------+-----------------+------------------+
    // | nDataStreamSize | input data hash |    input data    |  
    // +-----------------+-----------------+------------------+
    v_uint8 vInputData;
    const uint64_t nDataStreamSize = input_stream.size();
    // input data size without padding
    const size_t nDataSizeNotPadded = STREAM_DATA_POS + nDataStreamSize;
    const size_t nInputDataSize = nDataSizeNotPadded + (FAKE_KEY_SIZE - (nDataSizeNotPadded % FAKE_KEY_SIZE));
    vInputData.resize(nInputDataSize, 0);
    input_stream.read_buf(vInputData.data() + STREAM_DATA_POS, input_stream.size());

    auto p = vInputData.data();
    // set size of the original data upfront
    auto* input_len_bytes = reinterpret_cast<const unsigned char*>(&nDataStreamSize);
    memcpy(p, input_len_bytes, sizeof(uint64_t)); // sizeof(uint64_t) == 8
    p += sizeof(uint64_t);

    // Calculate sha256 hash of the input data (without padding) and set it at offset 8
    const uint256 input_hash = Hash(vInputData.cbegin() + STREAM_DATA_POS, vInputData.cbegin() + nDataSizeNotPadded);
    memcpy(p, input_hash.begin(), input_hash.size());

    // Create output P2FMS scripts
    //    each CScript can hold up to 3 chunks (fake keys)
    v_uint8 vChunk;
    vChunk.resize(FAKE_KEY_SIZE);
    for (size_t nChunkPos = 0; nChunkPos < nInputDataSize;)
    {
        CScript script;
        script << CScript::EncodeOP_N(1);
        int m = 0;
        for (; m < 3 && nChunkPos < nInputDataSize; ++m, nChunkPos += FAKE_KEY_SIZE)
        {
            memcpy(vChunk.data(), vInputData.data() + nChunkPos, FAKE_KEY_SIZE);
            script << vChunk;
        }
        // add chunks count (up to 3)
        script << CScript::EncodeOP_N(m) << OP_CHECKMULTISIG;
        vOutScripts.push_back(script);
    }
    return nInputDataSize;
}

bool CPastelTicketProcessor::CreateP2FMSTransactionWithExtra(const CDataStream& input_stream, 
    const vector<CTxOut>& extraOutputs, const CAmount extraAmount, CMutableTransaction& tx_out, 
    const CAmount price, const opt_string_t& sFundingAddress, string& error_ret)
{
    assert(pwalletMain != nullptr);

    if (pwalletMain->IsLocked())
    {
        error_ret = "Wallet is locked. Try again later";
        return false;
    }
    if (input_stream.empty())
    {
        error_ret = "Input data is empty";
        return false;
    }

    // Create output P2FMS scripts
    vector<CScript> vOutScripts;
    const size_t nInputDataSize = CreateP2FMSScripts(input_stream, vOutScripts);
    if (vOutScripts.empty())
    {
        error_ret = "No fake transactions after parsing input data";
        return false;
    }
    const auto& chainParams = Params();

    // process funding address if specified
    CTxDestination fundingAddress;
    bool bUseFundingAddress = false;
    if (sFundingAddress.has_value() && !sFundingAddress.value().empty())
    {
        // support only taddr for now
        KeyIO keyIO(chainParams);
        fundingAddress = keyIO.DecodeDestination(sFundingAddress.value());
        if (!IsValidDestination(fundingAddress))
        {
            error_ret = strprintf("Not a valid transparent address [%s] used for funding the transaction", sFundingAddress.value());
            return false;
        }
        bUseFundingAddress = true;
    }

    //calculate aprox required amount
    CAmount nAproxFeeNeeded = payTxFee.GetFee(nInputDataSize) * 2;
    if (nAproxFeeNeeded < payTxFee.GetFeePerK())
        nAproxFeeNeeded = payTxFee.GetFeePerK();

    const size_t nFakeTxCount = vOutScripts.size();
    // Amount
    const CAmount perOutputAmount = price * COIN / nFakeTxCount;
    // MUST be precise!!!
    const CAmount lost = price * COIN - perOutputAmount * nFakeTxCount;
    // total amount to spend
    const CAmount allSpentAmount = price * COIN + nAproxFeeNeeded + extraAmount;

    unsigned int chainHeight = GetActiveChainHeight();
    if (!chainParams.IsRegTest())
        chainHeight = max(chainHeight, APPROX_RELEASE_HEIGHT);
    auto consensusBranchId = CurrentEpochBranchId(chainHeight, chainParams.GetConsensus());

    // Create empty transaction
    tx_out = CreateNewContextualCMutableTransaction(chainParams.GetConsensus(), chainHeight);

    // Find funding (unspent) transaction with enough coins to cover all outputs (single - for simplicity)
    bool bOk = false;
    {
        vector<COutput> vecOutputs;
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->AvailableCoins(vecOutputs, false, nullptr, true);
        for (const auto &out : vecOutputs)
        {
            const auto& txOut = out.tx->vout[out.i];
            if (bUseFundingAddress)
            {
                if (!out.fSpendable)
                    continue;
                // use utxo only from the specified funding address
                CTxDestination txOutAddress;
                if (!ExtractDestination(txOut.scriptPubKey, txOutAddress))
                    continue;
                if (txOutAddress != fundingAddress)
                    continue;
            }
            if (txOut.nValue > allSpentAmount)
            {
                // found coin transaction with needed amount - populate new transaction
                const CScript& prevPubKey = txOut.scriptPubKey;
                const CAmount& prevAmount = txOut.nValue;

                tx_out.vin.resize(1);
                tx_out.vin[0].prevout.n = out.i;
                tx_out.vin[0].prevout.hash = out.tx->GetHash();

                //Add fake output scripts
                tx_out.vout.resize(nFakeTxCount + 1); //+1 for change
                for (int i = 0; i < nFakeTxCount; i++)
                {
                    tx_out.vout[i].nValue = perOutputAmount;
                    tx_out.vout[i].scriptPubKey = vOutScripts[i];
                }
                // MUST be precise!!!
                tx_out.vout[0].nValue = perOutputAmount + lost;

                if (extraAmount != 0)
                    for (const auto& extra : extraOutputs)
                        tx_out.vout.emplace_back(extra);

                // Send change output back to input address
                tx_out.vout[nFakeTxCount].nValue = prevAmount - price * COIN - extraAmount;
                tx_out.vout[nFakeTxCount].scriptPubKey = prevPubKey;

                // sign transaction - unlock input
                SignatureData sigdata;
                ProduceSignature(MutableTransactionSignatureCreator(pwalletMain, &tx_out, 0, prevAmount, SIGHASH_ALL), prevPubKey, sigdata, consensusBranchId);
                UpdateTransaction(tx_out, 0, sigdata);

                // Calculate correct fee
                size_t tx_size = EncodeHexTx(tx_out).length();
                CAmount nFeeNeeded = payTxFee.GetFee(tx_size);
                if (nFeeNeeded < payTxFee.GetFeePerK())
                    nFeeNeeded = payTxFee.GetFeePerK();

                // nFakeTxCount is index of the change output
                tx_out.vout[nFakeTxCount].nValue -= nFeeNeeded;

                bOk = true;
                break;
            }
        }
    }

    if (!bOk)
        error_ret = strprintf("No unspent transaction found%s - cannot send data to the blockchain!", 
            bUseFundingAddress ? strprintf(" for address [%s]", sFundingAddress.value()) : "");
    return bOk;
}
#endif // ENABLE_WALLET

/**
 * Add P2FMS transaction to the memory pool.
 * 
 * \param tx_out - P2FMS transaction
 * \param error_ret - returns an error message if any
 * \return true if transaction was successfully added to the transaction memory pool (txmempool)
 */
bool CPastelTicketProcessor::StoreP2FMSTransaction(const CMutableTransaction& tx_out, string& error_ret)
{
    bool bRet = false;
    CValidationState state;
    do
    {
        error_ret.clear();
        bool fMissingInputs = false;
        if (AcceptToMemoryPool(Params(), mempool, state, tx_out, false, &fMissingInputs, true))
        {
            RelayTransaction(tx_out);
            bRet = true;
            break;
        }
        if (state.IsInvalid())
        {
            error_ret = strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason());
            break;
        }
        if (fMissingInputs)
        {
            error_ret = "Missing inputs";
            break;
        }
        error_ret = state.GetRejectReason();
    } while (false);
    return bRet;
}

/**
 * Reads P2FMS (Pay-to-Fake-Multisig) transaction into output_string.
 * 
 * \param tx_in - transaction
 * \param output_string - output strinf
 * \param error - returns an error message if any
 * \return true if P2FMS was found in the transaction and successfully parsed, validated and copied to the output string
 */
bool CPastelTicketProcessor::ParseP2FMSTransaction(const CMutableTransaction& tx_in, string& output_string, string& error)
{
    v_uint8 output_data;
    bool bOk = ParseP2FMSTransaction(tx_in, output_data, error);
    if (bOk)
        output_string = vector_to_string(output_data);
    return bOk;
}

/**
 * Reads P2FMS (Pay-to-Fake-Multisig) transaction into output_data byte vector.
 * 
 * \param tx_in - transaction
 * \param output_data - output byte vector
 * \param error_ret - returns an error if any
 * \return true if P2FMS was found in the transaction and successfully parsed, validated and copied to the output data
 */
bool CPastelTicketProcessor::ParseP2FMSTransaction(const CMutableTransaction& tx_in, v_uint8& output_data, string& error_ret)
{
    bool bFoundMS = false;
    // reuse vector to process tx outputs
    vector<v_uint8> vSolutions;

    for (const auto& vout : tx_in.vout)
    {
        txnouttype typeRet; // script type
        vSolutions.clear();

        if (!Solver(vout.scriptPubKey, typeRet, vSolutions) || typeRet != TX_MULTISIG)
            continue;

        bFoundMS = true;
        size_t nReserve = 0;
        for (size_t i = 1; vSolutions.size() - 1 > i; i++)
            nReserve += vSolutions[i].size();
        output_data.reserve(output_data.size() + nReserve);
        for (size_t i = 1; vSolutions.size() - 1 > i; i++)
            output_data.insert(output_data.end(), vSolutions[i].cbegin(), vSolutions[i].cend());
    }

    bool bRet = false;
    do
    {
        error_ret.clear();
        if (!bFoundMS)
        {
            error_ret = "No data multisigs found in transaction";
            break;
        }

        if (output_data.empty())
        {
            error_ret = "No data found in transaction";
            break;
        }

        constexpr auto DATA_POS = uint256::SIZE + sizeof(uint64_t);
        //size_t size = 8 bytes; hash size = 32 bytes
        if (output_data.size() < DATA_POS)
        {
            error_ret = "No correct data found in transaction";
            break;
        }

        // interpret first 8-bytes as an output size
        const auto nOutputLength = *reinterpret_cast<const uint64_t*>(output_data.data());
        v_uint8 input_hash_vec(output_data.cbegin() + sizeof(uint64_t), output_data.cbegin() + DATA_POS); //hash length == 32
        // erase [size(8 bytes) + hash(32-bytes)], leave data only
        output_data.erase(output_data.cbegin(), output_data.cbegin() + DATA_POS);

        if (output_data.size() < nOutputLength)
        {
            error_ret = "No correct data found in transaction - length is not matching";
            break;
        }

        if (output_data.size() > nOutputLength)
            output_data.erase(output_data.cbegin() + nOutputLength, output_data.cend());

        const uint256 input_hash_stored(input_hash_vec);
        const uint256 input_hash_real = Hash(output_data.cbegin(), output_data.cend());

        if (input_hash_stored != input_hash_real)
        {
            error_ret = "No correct data found in transaction - hash is not matching";
            break;
        }
        bRet = true;
    } while (false);

    return bRet;
}

/**
 * Validate NFT ownership.
 * 
 * \param _txid - NFT registration txid
 * \param _pastelID - Pastel ID of the owner to validate
 * \return optional tuple <NFT registration txid, NFT trade txid>
 */
optional<reg_trade_txid_t> CPastelTicketProcessor::ValidateOwnership(const string &_txid, const string &_pastelID)
{
    optional<reg_trade_txid_t> retVal;
    try
    {
        // Find ticket by txid
        auto ticket = CPastelTicketProcessor::GetTicket(_txid, TicketID::NFT);
        auto NFTTicket = dynamic_cast<CNFTRegTicket*>(ticket.get());
        if (!NFTTicket)
            return nullopt;

        // Check if creator and _pastelID are equal
        if((NFTTicket->pastelIDs[0]== _pastelID) && CNFTActivateTicket::CheckTicketExistByNFTTicketID(NFTTicket->GetTxId()))
            return make_tuple(_txid, "");

    }
    catch(const runtime_error& e)
    {
        LogPrintf("Was not able to process ValidateOwnership request due to: %s\n", e.what()); 
    }

    //If we are here it means it is a nested trade ticket 
    //List trade tickets by reg txID and rearrange them by blockheight
    const auto tradeTickets = CNFTTradeTicket::FindAllTicketByRegTnxID(_txid);
    
    // Go through each if not empty and rearrange them by block-height
    if(!tradeTickets.empty())
    {
        //sort(tradeTickets.begin(), tradeTickets.end(), [](CNFTTradeTicket & one, CNFTTradeTicket & two){return one.GetBlock() < two.GetBlock();});
        const auto ownersPastelIds_with_TnxIds = CNFTTradeTicket::GetPastelIdAndTxIdWithTopHeightPerCopy(tradeTickets);
        const auto it = ownersPastelIds_with_TnxIds.find(_pastelID);
        if (it != ownersPastelIds_with_TnxIds.cend())
            retVal = make_tuple(_txid, it->second);
    }

    return retVal;
}

#ifdef FAKE_TICKET
string CPastelTicketProcessor::CreateFakeTransaction(CPastelTicket& ticket, CAmount ticketPrice, const vector<pair<string, CAmount>>& extraPayments, const string& strVerb, bool bSend)
{
    string error;

    if (ticket.ID() == TicketID::PastelID) {
        if (strVerb == "1") {
            auto t = (CPastelIDRegTicket*)&ticket;
            t->pslid_signature.clear();
        } else if (strVerb == "2") {
            auto t = (CPastelIDRegTicket*)&ticket;
            t->mn_signature.clear();
        } else if (strVerb == "3") {
            auto t = (CPastelIDRegTicket*)&ticket;
            t->outpoint.SetNull();
        }
    } else if (ticket.ID() == TicketID::NFT) {
        if (strVerb == "1") {
            auto t = (CNFTRegTicket*)&ticket;
            t->ticketSignatures[CNFTRegTicket::mn2sign].clear();
            t->ticketSignatures[CNFTRegTicket::mn3sign].clear();
        }
    } else if (ticket.ID() == TicketID::Activate) {
        if (strVerb == "1") {
            auto t = (CNFTActivateTicket*)&ticket;
            t->signature.clear();
        }
        if (strVerb == "2") {
            auto t = (CNFTActivateTicket*)&ticket;
            t->creatorHeight = 1;
        }
    } else if (ticket.ID() == TicketID::Sell) {
        if (strVerb == "1") {
            auto t = (CNFTSellTicket*)&ticket;
            t->signature.clear();
        }
    } else if (ticket.ID() == TicketID::Buy) {
        ;
    } else if (ticket.ID() == TicketID::Trade) {
        ;
    }

    KeyIO keyIO(Params());
    vector<CTxOut> extraOutputs;
    CAmount extraAmount = 0;
    if (!extraPayments.empty())
    {
        for (auto& p : extraPayments)
        {
            auto dest = keyIO.DecodeDestination(p.first);
            if (!IsValidDestination(dest))
                return string{};
            extraOutputs.emplace_back(p.second, GetScriptForDestination(dest));
            extraAmount += p.second;
        }
    }

    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << to_integral_type<TicketID>(ticket.ID());
    data_stream << ticket;

    CMutableTransaction tx;
    string sFundingAddress;
    if (!CreateP2FMSTransactionWithExtra(data_stream, extraOutputs, extraAmount, tx, ticketPrice, sFundingAddress, error))
        throw runtime_error(strprintf("Failed to create P2FMS from data provided - %s", error));

    if (bSend)
    {
        if (!StoreP2FMSTransaction(tx, error))
            throw runtime_error(strprintf("Failed to send P2FMS transaction - %s", error));
        return tx.GetHash().GetHex();
    }
    return EncodeHexTx(tx);
}
#endif

shared_ptr<ITxMemPoolTracker> CPastelTicketProcessor::GetTxMemPoolTracker()
{
    if (!TicketTxMemPoolTracker)
        TicketTxMemPoolTracker = make_shared<CTicketTxMemPoolTracker>();
    return TicketTxMemPoolTracker;
}
