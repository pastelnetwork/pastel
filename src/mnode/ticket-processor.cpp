// Copyright (c) 2019-2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "deprecation.h"
#include "script/sign.h"
#include "core_io.h"
#include "key_io.h"
#include "init.h"

#include "mnode/mnode-pastel.h"
#include "mnode/mnode-controller.h"
#include "mnode/ticket-processor.h"

#include "json/json.hpp"

using json = nlohmann::json;
using namespace std;

void CPastelTicketProcessor::InitTicketDB()
{
    fs::path ticketsDir = GetDataDir() / "tickets";
    if (!fs::exists(ticketsDir))
        fs::create_directories(ticketsDir);

    uint64_t nTotalCache = (GetArg("-dbcache", 450) << 20);
    constexpr uint64_t nMinDbCache = 4;
    constexpr uint64_t nMaxDbCache = 16384; //16KB
    nTotalCache = std::max(nTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    nTotalCache = std::min(nTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbCache
    const uint64_t nTicketDBCache = nTotalCache / 8 / uint8_t(TicketID::COUNT);

    // create DB for each ticket type
    for (uint8_t id = to_integral_type<TicketID>(TicketID::PastelID); id != to_integral_type<TicketID>(TicketID::COUNT); ++id)
        dbs.emplace(static_cast<TicketID>(id), make_unique<CDBWrapper>(ticketsDir / TICKET_INFO[id].szDBSubFolder, nTicketDBCache, false, fReindex));
}

/**
 * Create ticket unique_ptr by type.
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

    case TicketID::Art:
        ticket = make_unique<CArtRegTicket>();
        break;

    case TicketID::Activate:
        ticket = make_unique<CArtActivateTicket>();
        break;

    case TicketID::Sell:
        ticket = make_unique<CArtSellTicket>();
        break;

    case TicketID::Buy:
        ticket = make_unique<CArtBuyTicket>();
        break;

    case TicketID::Trade:
        ticket = make_unique<CArtTradeTicket>();
        break;

    case TicketID::Royalty:
        ticket = make_unique<CArtRoyaltyTicket>();
        break;

    case TicketID::Down:
        ticket = make_unique<CTakeDownTicket>();
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
    if (!ReadBlockFromDisk(block, cBlockIndex))
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

void CPastelTicketProcessor::UpdateDB_MVK(const CPastelTicket& ticket, const std::string& mvKey)
{
    std::vector<std::string> mainKeys;
    auto realMVKey = RealMVKey(mvKey);
    dbs.at(ticket.ID())->Read(realMVKey, mainKeys);
    if (std::find(mainKeys.cbegin(), mainKeys.cend(), ticket.KeyOne()) == mainKeys.cend())
    {
        mainKeys.emplace_back(ticket.KeyOne());
        dbs.at(ticket.ID())->Write(realMVKey, mainKeys);
    }
}

bool CPastelTicketProcessor::UpdateDB(CPastelTicket &ticket, string& txid, const unsigned int nBlockHeight)
{
    if (!txid.empty())
        ticket.SetTxId(std::move(txid));
    if (nBlockHeight != 0)
        ticket.SetBlock(nBlockHeight);
    dbs.at(ticket.ID())->Write(ticket.KeyOne(), ticket);
    if (ticket.HasKeyTwo())
    {
        auto realKeyTwo = RealKeyTwo(ticket.KeyTwo());
        dbs.at(ticket.ID())->Write(realKeyTwo, ticket.KeyOne());
    }

    if (ticket.HasMVKeyOne())
        UpdateDB_MVK(ticket, ticket.MVKeyOne());
    if (ticket.HasMVKeyTwo())
        UpdateDB_MVK(ticket, ticket.MVKeyTwo());
    //LogPrintf("tickets", "CPastelTicketProcessor::UpdateDB -- Ticket added into DB with key %s (txid - %s)\n", ticket.KeyOne(), ticket.ticketTnx);
    return true;
}

bool preParseTicket(const CMutableTransaction& tx, CDataStream& data_stream, TicketID& ticket_id, std::string& error, bool log = true)
{
    vector<unsigned char> output_data;
    if (!CPastelTicketProcessor::ParseP2FMSTransaction(tx, output_data, error))
        return false;
    data_stream.write(reinterpret_cast<char*>(output_data.data()), output_data.size());
    uint8_t u;
    data_stream >> u;
    ticket_id = static_cast<TicketID>(u);
    return true;
}

// Called from ContextualCheckTransaction, which called from:
//      AcceptToMemoryPool (via CheckTransaction) - this is to validate the new transaction
//      ProcessNewBlock (via AcceptBlock via CheckBlock via CheckTransaction via ContextualCheckBlock) - this is to validate transaction in the blocks (new or not)
bool CPastelTicketProcessor::ValidateIfTicketTransaction(const int nHeight, const CTransaction& tx)
{
    CMutableTransaction mtx(tx);

    std::string error_ret;
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
        std::string ticketBlockTxIdStr = tx.GetHash().GetHex();
        LogPrintf("CPastelTicketProcessor::ValidateIfTicketTransaction -- Processing ticket ['%s', txid=%s, nHeight=%d, stream size=%zu]\n", 
           GetTicketDescription(ticket_id),
           ticketBlockTxIdStr, nHeight, data_stream.size());

        auto ticket = CreateTicket(ticket_id);
        if (!ticket)
            error_ret = "unknown ticket_id";
        else
        {
            data_stream >> *ticket;
            ticket->SetTxId(std::move(ticketBlockTxIdStr));
            ticket->SetBlock(nHeight);
            bOk = ticket->IsValid(false, 0);
            expectedTicketFee = ticket->TicketPrice(nHeight) * COIN;
            storageFee = ticket->GetStorageFee();
            if (ticket_id == TicketID::Trade) {
                auto trade_ticket = dynamic_cast<CArtTradeTicket *>(ticket.get());
                if (!trade_ticket)
                    throw std::runtime_error("Invalid Art Trade ticket");

                auto artTicket = trade_ticket->FindArtRegTicket();
                if (!artTicket)
                  throw std::runtime_error("Art Reg ticket not found");

                auto artRegTicket = dynamic_cast<CArtRegTicket*>(artTicket.get());
                if (!artRegTicket)
                  throw std::runtime_error("Invalid Art Reg ticket");

                tradePrice = trade_ticket->price * COIN;
                if (artRegTicket->nRoyalty > 0) {
                  hasRoyaltyFee = true;
                  royaltyFee = tradePrice * artRegTicket->nRoyalty / 100;
                }
                if (!artRegTicket->strGreenAddress.empty()) {
                  hasGreenFee = true;
                  greenFee = tradePrice * artRegTicket->GreenPercent(nHeight) / 100;
                }
                tradePrice -= (royaltyFee + greenFee);
            }
        }
    }
    catch (const std::exception& ex)
    {
        error_ret = strprintf("Failed to parse and unpack ticket - %s", ex.what());
    }
    catch (...)
    {
        error_ret = "Failed to parse and unpack ticket - Unknown exception";
    }

    if (bOk)
    {
        //Validate various Fees

        auto num = tx.vout.size();
        CAmount ticketFee = 0;

        CAmount allMNFee = storageFee * COIN * 9 / 10;
        CAmount mn1Fee = allMNFee * 3 / 5;
        CAmount mn23Fee = allMNFee / 5;

        for (auto i = 0; i < num; i++)
        {
            if ((ticket_id == TicketID::PastelID ||
                 ticket_id == TicketID::Art ||
                 ticket_id == TicketID::Sell ||
                 ticket_id == TicketID::Buy ||
                 ticket_id == TicketID::Royalty) &&
                i == num - 1) // in these tickets last output is change
                break;
            // in this tickets last 4 outputs is: change, and payments to 3 MNs
            if (ticket_id == TicketID::Activate)
            { 
                if (i == num - 4) {
                  continue;
                } else if (i == num - 3) {
                  if (mn1Fee != tx.vout[i].nValue) {
                    bOk = false;
                    error_ret = strprintf("Wrong main MN fee: expected - %" PRId64 ", real - %" PRId64, mn1Fee, tx.vout[i].nValue);
                    break;
                  }
                  continue;
                } else if (i >= num - 2) {
                  if (mn23Fee != tx.vout[i].nValue) {
                    bOk = false;
                    error_ret = strprintf("Wrong MN%d fee: expected - %" PRId64 ", real - %" PRId64, i - num - 4, mn23Fee, tx.vout[i].nValue);
                    break;
                  }
                  continue;
                }
            }
            //in this tickets last 2 outputs is: change, and payment to the seller
            if (ticket_id == TicketID::Trade)
            {
                if (i == (hasRoyaltyFee && hasGreenFee ? num - 4 :
                          hasRoyaltyFee || hasGreenFee ? num - 3 : num - 2)) {
                  continue;
                } else if (i == (hasRoyaltyFee && hasGreenFee ? num - 3 :
                                 hasRoyaltyFee || hasGreenFee ? num - 2 : num - 1)) {
                  if (tradePrice != tx.vout[i].nValue) {
                    bOk = false;
                    error_ret = strprintf("Wrong payment to the seller: expected - %" PRId64 ", real - %" PRId64, tradePrice, tx.vout[i].nValue);
                    break;
                  }
                  continue;
                } else if (hasRoyaltyFee && i == (hasGreenFee ? num - 2 : num - 1)) {
                  if (royaltyFee != tx.vout[i].nValue) {
                    bOk = false;
                    error_ret = strprintf("Wrong payment to the royalty owner: expected - %" PRId64 ", real - %" PRId64, royaltyFee, tx.vout[i].nValue);
                    break;
                  }
                  continue;
                } else if (hasGreenFee && i == num - 1) {
                  if (greenFee != tx.vout[i].nValue) {
                    bOk = false;
                    error_ret = strprintf("Wrong payment to the green NFT: expected - %" PRId64 ", real - %" PRId64, greenFee, tx.vout[i].nValue);
                    break;
                  }
                  continue;
                }
            }

            ticketFee += tx.vout[i].nValue;
        }

        if (expectedTicketFee != ticketFee) {
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
    std::string error_ret;
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    TicketID ticket_id;

    if (!preParseTicket(tx, data_stream, ticket_id, error_ret))
        return false;

    try {
        std::string txid = tx.GetHash().GetHex();

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
    catch (const std::exception& ex)
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

std::string CPastelTicketProcessor::GetTicketJSON(const uint256 &txid)
{
    auto ticket = GetTicket(txid);
    if (ticket)
        return ticket->ToJSON();
    return "";
}

std::unique_ptr<CPastelTicket> CPastelTicketProcessor::GetTicket(const uint256 &txid)
{
    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(txid, tx, hashBlock, true))
        throw std::runtime_error(strprintf("No information available about transaction"));

    CMutableTransaction mtx(tx);

    TicketID ticket_id;
    std::string error_ret;
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);

    if (!preParseTicket(mtx, data_stream, ticket_id, error_ret))
        throw std::runtime_error(strprintf("Failed to create P2FMS from data provided - %s", error_ret));

    std::unique_ptr<CPastelTicket> ticket;
    try
    {
        std::string ticketBlockTxIdStr = tx.GetHash().GetHex();
        int ticketBlockHeight = -1;
        if (mapBlockIndex.count(hashBlock) != 0)
            ticketBlockHeight = mapBlockIndex[hashBlock]->nHeight;

        ticket = CreateTicket(ticket_id);
        if (ticket)
        {
            data_stream >> *ticket;
            ticket->SetTxId(std::move(ticketBlockTxIdStr));
            ticket->SetBlock(ticketBlockHeight);
        }
        else
            error_ret = "unknown ticket_id";
    }
    catch (const std::exception& ex)
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

std::unique_ptr<CPastelTicket> CPastelTicketProcessor::GetTicket(const std::string& _txid, const TicketID ticketID)
{
    uint256 txid;
    txid.SetHex(_txid);
    auto ticket = GetTicket(txid);
    if (!ticket || ticketID != ticket->ID())
        throw std::runtime_error(
            strprintf("The ticket with this txid [%s] is not in the blockchain", _txid));
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
        std::string mainKey;
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
        std::string sMainKey;
        const auto sRealKeyTwo = RealKeyTwo(ticket.KeyTwo());
        if (dbs.at(ticket.ID())->Read(sRealKeyTwo, sMainKey))
            return dbs.at(ticket.ID())->Read(sMainKey, ticket);
    }
    return false;
}

template <class _TicketType>
std::vector<_TicketType> CPastelTicketProcessor::FindTicketsByMVKey(const std::string& mvKey)
{
    std::vector<_TicketType> tickets;
    std::vector<std::string> mainKeys;
    auto realMVKey = RealMVKey(mvKey);
    dbs.at(_TicketType::GetID())->Read(realMVKey, mainKeys);
    for (const auto& key : mainKeys)
    {
        _TicketType ticket;
        if (dbs.at(_TicketType::GetID())->Read(key, ticket))
            tickets.emplace_back(ticket);
    }
    return tickets;
}
template std::vector<CPastelIDRegTicket> CPastelTicketProcessor::FindTicketsByMVKey<CPastelIDRegTicket>(const std::string&);
template std::vector<CArtRegTicket> CPastelTicketProcessor::FindTicketsByMVKey<CArtRegTicket>(const std::string&);
template std::vector<CArtActivateTicket> CPastelTicketProcessor::FindTicketsByMVKey<CArtActivateTicket>(const std::string&);
template std::vector<CArtSellTicket> CPastelTicketProcessor::FindTicketsByMVKey<CArtSellTicket>(const std::string&);
template std::vector<CArtBuyTicket> CPastelTicketProcessor::FindTicketsByMVKey<CArtBuyTicket>(const std::string&);
template std::vector<CArtTradeTicket> CPastelTicketProcessor::FindTicketsByMVKey<CArtTradeTicket>(const std::string&);
template std::vector<CArtRoyaltyTicket> CPastelTicketProcessor::FindTicketsByMVKey<CArtRoyaltyTicket>(const std::string&);

std::vector<std::string> CPastelTicketProcessor::GetAllKeys(const TicketID id) const
{
    std::vector<std::string> vResults;
    std::unique_ptr<CDBIterator> pcursor(dbs.at(id)->NewIterator());
    pcursor->SeekToFirst();
    std::string sKey;
    while (pcursor->Valid())
    {
        sKey.clear();
        if (pcursor->GetKey(sKey))
            vResults.emplace_back(sKey);
        pcursor->Next();
    }
    return vResults;
}

/**
 * Apply functor F for all tickets with type _TicketType.
 * 
 * \param f - functor to apply
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
        if (FindTicket(ticket))
            f(ticket);
    }
}

template <class _TicketType>
std::string CPastelTicketProcessor::ListTickets() const
{
    json jArray;
    listTickets<_TicketType>([&](const _TicketType& ticket)
        {
            jArray.push_back(json::parse(ticket.ToJSON()));
        });
    return jArray.dump();
}
template std::string CPastelTicketProcessor::ListTickets<CPastelIDRegTicket>() const;
template std::string CPastelTicketProcessor::ListTickets<CArtRegTicket>() const;
template std::string CPastelTicketProcessor::ListTickets<CArtActivateTicket>() const;
template std::string CPastelTicketProcessor::ListTickets<CArtSellTicket>() const;
template std::string CPastelTicketProcessor::ListTickets<CArtBuyTicket>() const;
template std::string CPastelTicketProcessor::ListTickets<CArtTradeTicket>() const;
template std::string CPastelTicketProcessor::ListTickets<CArtRoyaltyTicket>() const;

template <class _TicketType, typename F>
std::string CPastelTicketProcessor::filterTickets(F f) const
{
    std::vector<_TicketType> allTickets;
    listTickets<_TicketType>([&](const _TicketType& ticket)
        {
            allTickets.push_back(ticket);
        });

    unsigned int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
    }

    json jArray;
    for (const auto& t : allTickets)
    {
        //check if the sell ticket is confirmed
        if (chainHeight - t.GetBlock() < masterNodeCtrl.MinTicketConfirmations)
            continue;
        if (f(t, chainHeight))
            continue;
        jArray.push_back(json::parse(t.ToJSON()));
    }
    return jArray.dump();
}

/**
 * List Pastel registration tickets using filter.
 * For filter=3 include only tickets for locally stored PastelIDs in pvPastelIDs.
 *
 * \param filter - 1 - mn; 2 - personal; 3 - mine
 * \param pvPastelIDs - vector of locally stored PastelIDs
 * \return
 */
std::string CPastelTicketProcessor::ListFilterPastelIDTickets(const short filter, const std::vector<std::string>* pvPastelIDs) const
{
    return filterTickets<CPastelIDRegTicket>(
        [&](const CPastelIDRegTicket& t, const unsigned int chainHeight) -> bool {
            if ((filter == 1 &&
                 !t.outpoint.IsNull()) || // don't skip mn
                (filter == 2 &&
                 t.outpoint.IsNull()) || // don't skip personal
                (filter == 3 &&          // don't skip locally stored tickets
                 pvPastelIDs && find(pvPastelIDs->cbegin(), pvPastelIDs->cend(), t.pastelID) != pvPastelIDs->cend()))
                return false;
            return true;
        });
}

// 1 - active;    2 - inactive;     3 - sold
std::string CPastelTicketProcessor::ListFilterArtTickets(const short filter) const
{
    return filterTickets<CArtRegTicket>(
        [&](const CArtRegTicket& t, const unsigned int chainHeight) -> bool
        {
            if (filter == 3)
            {
                CArtActivateTicket actTicket;
                // find Act ticket for this Reg ticket
                if (CArtActivateTicket::FindTicketInDb(t.GetTxId(), actTicket))
                {
                    //find Trade tickets listing that Act ticket txid as art ticket
                    auto vTradeTickets = CArtTradeTicket::FindAllTicketByArtTnxID(actTicket.GetTxId());
                    if (vTradeTickets.size() >= t.totalCopies)
                        return false; //don't skip sold
                }
            }

            // check if there is Act ticket for this Reg ticket
            if (CArtActivateTicket::CheckTicketExistByArtTicketID(t.GetTxId()))
            {
                if (filter == 1)
                    return false; //don't skip active
            } else if (filter == 2)
                return false; //don't skip inactive
            return true;
        });
}

// 1 - available;      2 - sold
std::string CPastelTicketProcessor::ListFilterActTickets(const short filter) const
{
    return filterTickets<CArtActivateTicket>(
        [&](const CArtActivateTicket& t, const unsigned int chainHeight) -> bool
        {
            //find Trade tickets listing this Act ticket txid as art ticket
            auto vTradeTickets = CArtTradeTicket::FindAllTicketByArtTnxID(t.GetTxId());
            auto ticket = GetTicket(t.regTicketTnxId, TicketID::Art);
            auto artRegTicket = dynamic_cast<CArtRegTicket*>(ticket.get());
            if (!artRegTicket)
                return true;
            if (vTradeTickets.size() < artRegTicket->totalCopies)
            {
                if (filter == 1)
                    return false; //don't skip available
            } else if (filter == 2)
                return false; //don't skip sold
            return true;
        });
}
// 1 - available; 2 - unavailable; 3 - expired; 4 - sold
std::string CPastelTicketProcessor::ListFilterSellTickets(const short filter) const
{
    return filterTickets<CArtSellTicket>(
        [&](const CArtSellTicket& t, const unsigned int chainHeight) -> bool
        {
            CArtBuyTicket existingBuyTicket;
            //find buy ticket for this sell ticket, if any
            if (CArtBuyTicket::FindTicketInDb(t.GetTxId(), existingBuyTicket))
            {
                //check if trade ticket exists for this sell ticket
                if (CArtTradeTicket::CheckTradeTicketExistByBuyTicket(existingBuyTicket.GetTxId()))
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
        });
}

// 1 - expired;    2 - sold
std::string CPastelTicketProcessor::ListFilterBuyTickets(const short filter) const
{
    return filterTickets<CArtBuyTicket>(
        [&](const CArtBuyTicket& t, const unsigned int chainHeight) -> bool
        {
            if (CArtTradeTicket::CheckTradeTicketExistByBuyTicket(t.GetTxId())) {
                if (filter == 2)
                    return false; //don't skip traded
            } else if (filter == 1 && t.GetBlock() + masterNodeCtrl.MaxBuyTicketAge < chainHeight)
                return false; //don't skip non sold, and expired
            return true;
        });
}
// 1 - available;      2 - sold
std::string CPastelTicketProcessor::ListFilterTradeTickets(const short filter) const
{
    return filterTickets<CArtTradeTicket>(
        [&](const CArtTradeTicket& t, const unsigned int chainHeight) -> bool
        {
            //find Trade tickets listing this Trade ticket txid as art ticket
            auto tradeTickets = CArtTradeTicket::FindAllTicketByArtTnxID(t.GetTxId());

            if (tradeTickets.empty()) {
                if (filter == 1)
                    return false; //don't skip available
            } else if (filter == 2)
                return false; //don't skip sold
            return true;
        });
}

/*static*/ bool CPastelTicketProcessor::WalkBackTradingChain(
        const std::string& sTxId,
        std::vector< std::unique_ptr<CPastelTicket> >& chain,
        bool shortPath,
        std::string& errRet) noexcept
{
    std::unique_ptr<CPastelTicket> pastelTicket;
    
    uint256 txid;
    txid.SetHex(sTxId);
    //  Get ticket pointed by artTnxId. This is either Activation or Trade tickets (Sell, Buy, Trade)
    try
    {
        pastelTicket = CPastelTicketProcessor::GetTicket(txid);
    }
    catch ([[maybe_unused]] std::runtime_error& ex)
    {
        errRet = strprintf("Ticket [txid=%s] is not in the blockchain.", sTxId);
        return false;
    }
    
    bool bOk = false;
    do {
        if (pastelTicket->ID() == TicketID::Trade) {
            auto tradeTicket = dynamic_cast<CArtTradeTicket *>(pastelTicket.get());
            if (!tradeTicket) {
                errRet = strprintf("The Trade ticket [txid=%s] referred by this ticket [txid=%s] is invalid",
                                   pastelTicket->GetTxId(), sTxId);
                break;
            }
            if (!WalkBackTradingChain(shortPath ? tradeTicket->artTnxId : tradeTicket->buyTnxId, chain, shortPath,
                                      errRet))
                break;
        } else if (pastelTicket->ID() == TicketID::Buy) {
            auto tradeTicket = dynamic_cast<CArtBuyTicket *>(pastelTicket.get());
            if (!tradeTicket) {
                errRet = strprintf("The Buy ticket [txid=%s] referred by this ticket [txid=%s] is invalid",
                                   pastelTicket->GetTxId(), sTxId);
                break;
            }
            if (!WalkBackTradingChain(tradeTicket->sellTnxId, chain, shortPath, errRet))
                break;
        } else if (pastelTicket->ID() == TicketID::Sell) {
            auto tradeTicket = dynamic_cast<CArtSellTicket *>(pastelTicket.get());
            if (!tradeTicket) {
                errRet = strprintf("The Sell ticket [txid=%s] referred by this ticket [txid=%s] is invalid",
                                   pastelTicket->GetTxId(), sTxId);
                break;
            }
            if (!WalkBackTradingChain(tradeTicket->artTnxId, chain, shortPath, errRet))
                break;
        } else if (pastelTicket->ID() == TicketID::Activate) {
            auto actTicket = dynamic_cast<CArtActivateTicket *>(pastelTicket.get());
            if (!actTicket) {
                errRet = strprintf("The Activation ticket [txid=%s] referred by ticket [txid=%s] ticket is invalid",
                                   pastelTicket->GetTxId(), sTxId);
                break;
            }
            if (!WalkBackTradingChain(actTicket->regTicketTnxId, chain, shortPath, errRet))
                break;
        } else if (pastelTicket->ID() == TicketID::Art) {
            auto tradeTicket = dynamic_cast<CArtRegTicket *>(pastelTicket.get());
            if (!tradeTicket) {
                errRet = strprintf("The Art Registration ticket [txid=%s] referred by ticket [txid=%s] is invalid",
                                   pastelTicket->GetTxId(), sTxId);
                break;
            }
        } else {
            errRet = strprintf("The Art ticket [txid=%s] referred by ticket [txid=%s] has wrong type - %s]",
                               pastelTicket->GetTxId(), sTxId, pastelTicket->GetTicketName());
            break;
        }
        chain.emplace_back(std::move(pastelTicket));
        bOk = true;
    }
    while(false);
    
    return bOk;
}

std::string CPastelTicketProcessor::SendTicket(const CPastelTicket& ticket)
{
    std::string error;

    try {
      ticket.IsValid(true, 0);
    }
    catch (const std::exception& ex) {
      throw std::runtime_error(strprintf("Ticket (%s) is invalid - %s", ticket.GetTicketName(), ex.what()));
    }
    catch (...) {
      throw std::runtime_error(strprintf("Ticket (%s) is invalid - Unknown exception", ticket.GetTicketName()));
    }

    std::vector<CTxOut> extraOutputs;
    const CAmount extraAmount = ticket.GetExtraOutputs(extraOutputs);

    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << (uint8_t)ticket.ID();
    data_stream << ticket;

    unsigned int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
    }

    CMutableTransaction tx;
    if (!CreateP2FMSTransactionWithExtra(data_stream, extraOutputs, extraAmount, tx, ticket.TicketPrice(chainHeight), error))
        throw std::runtime_error(strprintf("Failed to create P2FMS from data provided - %s", error));

    if (!StoreP2FMSTransaction(tx, error))
        throw std::runtime_error(strprintf("Failed to send P2FMS transaction - %s", error));
    return tx.GetHash().GetHex();
}

#ifdef ENABLE_WALLET
bool CPastelTicketProcessor::CreateP2FMSTransaction(const std::string& input_string, CMutableTransaction& tx_out, CAmount price, std::string& error_ret)
{
    //Convert string data into binary buffer
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << input_string;
    return CreateP2FMSTransaction(data_stream, tx_out, price, error_ret);
}

bool CPastelTicketProcessor::CreateP2FMSTransaction(const CDataStream& input_stream, CMutableTransaction& tx_out, CAmount price, std::string& error_ret)
{
    return CreateP2FMSTransactionWithExtra(input_stream, std::vector<CTxOut>{}, 0, tx_out, price, error_ret);
}

bool CPastelTicketProcessor::CreateP2FMSTransactionWithExtra(const CDataStream& input_stream, const std::vector<CTxOut>& extraOutputs, CAmount extraAmount, CMutableTransaction& tx_out, CAmount price, std::string& error_ret)
{
    assert(pwalletMain != nullptr);

    if (pwalletMain->IsLocked())
    {
        error_ret = "Wallet is locked. Try again later";
        return false;
    }

    size_t input_len = input_stream.size();
    if (input_len == 0)
    {
        error_ret = "Input data is empty";
        return false;
    }

    std::vector<unsigned char> input_bytes{input_stream.begin(), input_stream.end()};

    //Get Hash(SHA256) of input buffer and insert it upfront
    uint256 input_hash = Hash(input_bytes.begin(), input_bytes.end());
    input_bytes.insert(input_bytes.begin(), input_hash.begin(), input_hash.end());

    //insert size of the original data upfront
    auto* input_len_bytes = reinterpret_cast<unsigned char*>(&input_len);
    input_bytes.insert(input_bytes.begin(), input_len_bytes, input_len_bytes + sizeof(size_t)); //sizeof(size_t) == 8

    //Add padding at the end if required -
    // final size is n*33 - (33 bytes, but 66 characters)
    int fake_key_size = 33;
    size_t non_padded_size = input_bytes.size();
    size_t padding_size = fake_key_size - (non_padded_size % fake_key_size);
    
    input_bytes.insert(input_bytes.end(), padding_size, 0);

    //Break data into 33 bytes blocks
    std::vector<std::vector<unsigned char>> chunks;
    for (auto it = input_bytes.begin(); it != input_bytes.end(); it += fake_key_size) {
        chunks.emplace_back(std::vector<unsigned char>(it, it + fake_key_size));
    }

    //Create output P2FMS scripts
    std::vector<CScript> out_scripts;
    for (auto it = chunks.cbegin(); it != chunks.cend();)
    {
        CScript script;
        script << CScript::EncodeOP_N(1);
        int m = 0;
        for (; m < 3 && it != chunks.cend(); m++, it++) {
            script << *it;
        }
        script << CScript::EncodeOP_N(m) << OP_CHECKMULTISIG;
        out_scripts.push_back(script);
    }
    auto num_fake_txn = out_scripts.size();
    if (num_fake_txn == 0)
    {
        error_ret = "No fake transactions after parsing input data";
        return false;
    }

    //calculate aprox required amount
    CAmount nAproxFeeNeeded = payTxFee.GetFee(input_bytes.size()) * 2;
    if (nAproxFeeNeeded < payTxFee.GetFeePerK())
        nAproxFeeNeeded = payTxFee.GetFeePerK();

    //Amount
    CAmount perOutputAmount = price * COIN / num_fake_txn;
    //MUST be precise!!!
    CAmount lost = price * COIN - perOutputAmount * num_fake_txn;

    CAmount allSpentAmount = price * COIN + nAproxFeeNeeded + extraAmount;

    int chainHeight = chainActive.Height() + 1;
    if (!Params().IsRegTest())
        chainHeight = std::max(chainHeight, APPROX_RELEASE_HEIGHT);
    auto consensusBranchId = CurrentEpochBranchId(chainHeight, Params().GetConsensus());

    //Create empty transaction
    tx_out = CreateNewContextualCMutableTransaction(Params().GetConsensus(), chainHeight);

    //Find funding (unspent) transaction with enough coins to cover all outputs (single - for simplicity)
    bool bOk = false;
    {
        vector<COutput> vecOutputs;
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->AvailableCoins(vecOutputs, false, nullptr, true);
        for (auto out : vecOutputs) {
            if (out.tx->vout[out.i].nValue > allSpentAmount) {
                //If found - populate transaction

                const CScript& prevPubKey = out.tx->vout[out.i].scriptPubKey;
                const CAmount& prevAmount = out.tx->vout[out.i].nValue;

                tx_out.vin.resize(1);
                tx_out.vin[0].prevout.n = out.i;
                tx_out.vin[0].prevout.hash = out.tx->GetHash();

                //Add fake output scripts
                tx_out.vout.resize(num_fake_txn + 1); //+1 for change
                for (int i = 0; i < num_fake_txn; i++) {
                    tx_out.vout[i].nValue = perOutputAmount;
                    tx_out.vout[i].scriptPubKey = out_scripts[i];
                }
                //MUST be precise!!!
                tx_out.vout[0].nValue = perOutputAmount + lost;

                if (extraAmount != 0)
                    for (const auto& extra : extraOutputs)
                        tx_out.vout.emplace_back(extra);

                //Send change output back to input address
                tx_out.vout[num_fake_txn].nValue = prevAmount - price * COIN - extraAmount;
                tx_out.vout[num_fake_txn].scriptPubKey = prevPubKey;

                //sign transaction - unlock input
                SignatureData sigdata;
                ProduceSignature(MutableTransactionSignatureCreator(pwalletMain, &tx_out, 0, prevAmount, SIGHASH_ALL), prevPubKey, sigdata, consensusBranchId);
                UpdateTransaction(tx_out, 0, sigdata);

                //Calculate correct fee
                size_t tx_size = EncodeHexTx(tx_out).length();
                CAmount nFeeNeeded = payTxFee.GetFee(tx_size);
                if (nFeeNeeded < payTxFee.GetFeePerK())
                    nFeeNeeded = payTxFee.GetFeePerK();

                //num_fake_txn is index of the change output
                tx_out.vout[num_fake_txn].nValue -= nFeeNeeded;

                bOk = true;
                break;
            }
        }
    }

    if (!bOk) {
        error_ret = "No unspent transaction found - cannot send data to the blockchain!";
    }
    return bOk;
}
#endif // ENABLE_WALLET

bool CPastelTicketProcessor::StoreP2FMSTransaction(const CMutableTransaction& tx_out, std::string& error_ret)
{
    CValidationState state;
    bool fMissingInputs;
    if (!AcceptToMemoryPool(mempool, state, tx_out, false, &fMissingInputs, true)) {
        if (state.IsInvalid()) {
            error_ret = strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason());
            return false;
        } else {
            if (fMissingInputs) {
                error_ret = "Missing inputs";
                return false;
            }
            error_ret = state.GetRejectReason();
            return false;
        }
    }

    RelayTransaction(tx_out);
    return true;
}

bool CPastelTicketProcessor::ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::string& output_string, std::string& error_ret)
{
    vector<unsigned char> output_data;
    bool bOk = ParseP2FMSTransaction(tx_in, output_data, error_ret);
    if (bOk)
        output_string.assign(output_data.begin(), output_data.end());
    return bOk;
}

bool CPastelTicketProcessor::ParseP2FMSTransaction(const CMutableTransaction& tx_in, vector<unsigned char>& output_data, std::string& error_ret)
{
    bool foundMS = false;

    for (const auto& vout : tx_in.vout) {
        txnouttype typeRet;
        vector<vector<unsigned char>> vSolutions;

        if (!Solver(vout.scriptPubKey, typeRet, vSolutions) ||
            typeRet != TX_MULTISIG)
            continue;

        foundMS = true;
        for (size_t i = 1; vSolutions.size() - 1 > i; i++) {
            output_data.insert(output_data.end(), vSolutions[i].begin(), vSolutions[i].end());
        }
    }

    if (!foundMS) {
        error_ret = "No data Multisigs found in transaction";
        return false;
    }

    if (output_data.empty()) {
        error_ret = "No data found in transaction";
        return false;
    }

    //size_t size = 8 bytes; hash size = 32 bytes
    if (output_data.size() < 40) {
        error_ret = "No correct data found in transaction";
        return false;
    }

    auto output_len_ptr = reinterpret_cast<size_t**>(&output_data);
    if (output_len_ptr == nullptr || *output_len_ptr == nullptr) {
        error_ret = "No correct data found in transaction - wrong length";
        return false;
    }
    auto output_len = **output_len_ptr;
    output_data.erase(output_data.begin(), output_data.begin() + sizeof(size_t));

    std::vector<unsigned char> input_hash_vec(output_data.begin(), output_data.begin() + 32); //hash length == 32
    output_data.erase(output_data.begin(), output_data.begin() + 32);

    if (output_data.size() < output_len) {
        error_ret = "No correct data found in transaction - length is not matching";
        return false;
    }

    if (output_data.size() > output_len) {
        output_data.erase(output_data.begin() + output_len, output_data.end());
    }

    uint256 input_hash_stored(input_hash_vec);
    uint256 input_hash_real = Hash(output_data.begin(), output_data.end());

    if (input_hash_stored != input_hash_real) {
        error_ret = "No correct data found in transaction - hash is not matching";
        return false;
    }

    return true;
}

#ifdef FAKE_TICKET
std::string CPastelTicketProcessor::CreateFakeTransaction(CPastelTicket& ticket, CAmount ticketPrice, const std::vector<std::pair<std::string, CAmount>>& extraPayments, const std::string& strVerb, bool bSend)
{
    std::string error;

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
    } else if (ticket.ID() == TicketID::Art) {
        if (strVerb == "1") {
            auto t = (CArtRegTicket*)&ticket;
            t->ticketSignatures[CArtRegTicket::mn2sign].clear();
            t->ticketSignatures[CArtRegTicket::mn3sign].clear();
        }
    } else if (ticket.ID() == TicketID::Activate) {
        if (strVerb == "1") {
            auto t = (CArtActivateTicket*)&ticket;
            t->signature.clear();
        }
        if (strVerb == "2") {
            auto t = (CArtActivateTicket*)&ticket;
            t->artistHeight = 1;
        }
    } else if (ticket.ID() == TicketID::Sell) {
        if (strVerb == "1") {
            auto t = (CArtSellTicket*)&ticket;
            t->signature.clear();
        }
    } else if (ticket.ID() == TicketID::Buy) {
        ;
    } else if (ticket.ID() == TicketID::Trade) {
        ;
    }

    KeyIO keyIO(Params());
    std::vector<CTxOut> extraOutputs;
    CAmount extraAmount = 0;
    if (!extraPayments.empty()) {
        for (auto& p : extraPayments) {
            auto dest = keyIO.DecodeDestination(p.first);
            if (!IsValidDestination(dest))
                return std::string{};
            extraOutputs.emplace_back(CTxOut{p.second, GetScriptForDestination(dest)});
            extraAmount += p.second;
        }
    }

    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << to_integral_type<TicketID>(ticket.ID());
    data_stream << ticket;

    CMutableTransaction tx;
    if (!CreateP2FMSTransactionWithExtra(data_stream, extraOutputs, extraAmount, tx, ticketPrice, error))
        throw std::runtime_error(strprintf("Failed to create P2FMS from data provided - %s", error));

    if (bSend)
    {
        if (!StoreP2FMSTransaction(tx, error))
            throw std::runtime_error(strprintf("Failed to send P2FMS transaction - %s", error));
        return tx.GetHash().GetHex();
    }
    return EncodeHexTx(tx);
}
#endif