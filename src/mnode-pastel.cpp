// Copyright (c) 2018 The PASTELCoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "key_io.h"
#include "core_io.h"
#include "deprecation.h"
#include "script/sign.h"
#include "init.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

#include "mnode-controller.h"
#include "mnode-pastel.h"
#include "mnode-msgsigner.h"

#include "ed448/pastel_key.h"
#include "json/json.hpp"

#include <algorithm>

using json = nlohmann::json;

void CPastelTicketProcessor::InitTicketDB()
{
	boost::filesystem::path ticketsDir = GetDataDir() / "tickets";
	if (!boost::filesystem::exists(ticketsDir)) {
		boost::filesystem::create_directories(ticketsDir);
	}
	
	uint64_t nTotalCache = (GetArg("-dbcache", 450) << 20);
	uint64_t nMinDbCache = 4, nMaxDbCache = 16384; //16KB
	nTotalCache = std::max(nTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
	nTotalCache = std::min(nTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbCache
	uint64_t nTicketDBCache = nTotalCache / 8 / uint8_t(TicketID::COUNT);
	
	dbs[TicketID::PastelID]     = std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "pslids", nTicketDBCache, false, fReindex));
	dbs[TicketID::Art] 		    = std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "artreg", nTicketDBCache, false, fReindex));
	dbs[TicketID::Activate] 	= std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "artcnf", nTicketDBCache, false, fReindex));
	dbs[TicketID::Sell] 	    = std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "artsel", nTicketDBCache, false, fReindex));
	dbs[TicketID::Buy] 	        = std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "artbuy", nTicketDBCache, false, fReindex));
	dbs[TicketID::Trade] 	    = std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "arttrd", nTicketDBCache, false, fReindex));
	dbs[TicketID::Down] 	    = std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "takedn", nTicketDBCache, false, fReindex));
}

void CPastelTicketProcessor::UpdatedBlockTip(const CBlockIndex *cBlockIndex, bool fInitialDownload)
{
	if(!cBlockIndex) return;
	
	if (fInitialDownload){
		//??
	}
	
	CBlock block;
	if(!ReadBlockFromDisk(block, cBlockIndex)) {
		LogPrintf("CPastelTicket::UpdatedBlockTip -- ERROR: Can't read block from disk\n");
		return;
	}

	for(const CTransaction& tx : block.vtx)
	{
		CMutableTransaction mtx(tx);
		ParseTicketAndUpdateDB(mtx, cBlockIndex->nHeight);
	}
}

template<class T>
void CPastelTicketProcessor::UpdateDB_MVK(const T& ticket, const std::string& mvKey)
{
    std::vector<std::string> mainKeys;
    auto realMVKey = RealMVKey(mvKey);
    dbs[ticket.ID()]->Read(realMVKey, mainKeys);
    if (std::find(mainKeys.begin(), mainKeys.end(), ticket.KeyOne()) == mainKeys.end()) {
        mainKeys.emplace_back(ticket.KeyOne());
        dbs[ticket.ID()]->Write(realMVKey, mainKeys);
    }
}

template<class T>
bool CPastelTicketProcessor::UpdateDB(T& ticket, string &txid, int nBlockHeight)
{
	if (!txid.empty()) ticket.ticketTnx = std::move(txid);
	if (nBlockHeight != 0) ticket.ticketBlock = nBlockHeight;
	dbs[ticket.ID()]->Write(ticket.KeyOne(), ticket);
	if (ticket.HasKeyTwo()) {
        auto realKeyTwo = RealKeyTwo(ticket.KeyTwo());
        dbs[ticket.ID()]->Write(realKeyTwo, ticket.KeyOne());
    }
    
    if (ticket.HasMVKeyOne()) {
        UpdateDB_MVK(ticket, ticket.MVKeyOne());
    }
    if (ticket.HasMVKeyTwo()) {
        UpdateDB_MVK(ticket, ticket.MVKeyTwo());
    }
	//LogPrintf("tickets", "CPastelTicketProcessor::UpdateDB -- Ticket added into DB with key %s (txid - %s)\n", ticket.KeyOne(), ticket.ticketTnx);
	return true;
}

bool preParseTicket(const CMutableTransaction& tx, CDataStream& data_stream, TicketID& ticket_id, std::string& error, bool log = true)
{
    vector<unsigned char> output_data;
	if (!CPastelTicketProcessor::ParseP2FMSTransaction(tx, output_data, error)){
		return false;
	}
    data_stream.write(reinterpret_cast<char*>(output_data.data()), output_data.size());
	uint8_t u;
	data_stream >> u;
	ticket_id = (TicketID)u;
	return true;
}
// Called from ContextualCheckTransaction, which called from:
//      AcceptToMemoryPool (via CheckTransaction) - this is to validate the new transaction
//      ProcessNewBlock (via AcceptBlock via CheckBlock via CheckTransaction via ContextualCheckBlock) - this is to validate transaction in the blocks (new or not)
bool CPastelTicketProcessor::ValidateIfTicketTransaction(const int nHeight, const CTransaction& tx)
{
    CMutableTransaction mtx(tx);
    
    std::string error_ret;
    CDataStream data_stream(SER_NETWORK, TICKETS_VERSION);
    TicketID ticket_id;
    
    if (!preParseTicket(tx, data_stream, ticket_id, error_ret, false))
        return true; // this is not a ticket
    
    CAmount storageFee = 0;
    CAmount tradePrice = 0;
    CAmount expectedTicketFee = 0;
    
    //this is ticket and it need to be validated
    bool ok = false;
    try {
        std::string ticketBlockTxIdStr = tx.GetHash().GetHex();
        LogPrintf("CPastelTicketProcessor::ValidateIfTicketTransaction -- Processing ticket [ticket_id=%d, txid=%s, nHeight=%d]\n", (int)ticket_id, ticketBlockTxIdStr, nHeight);
        
        if (ticket_id == TicketID::PastelID) {
            CPastelIDRegTicket ticket;
            data_stream >> ticket;
            ticket.ticketTnx = std::move(ticketBlockTxIdStr);
            ticket.ticketBlock = nHeight;
            ok = ticket.IsValid(error_ret, false, 0);
            expectedTicketFee = ticket.TicketPrice(nHeight) * COIN;
        }
        else if (ticket_id == TicketID::Art) {
            CArtRegTicket ticket;
            data_stream >> ticket;
            ticket.ticketTnx = std::move(ticketBlockTxIdStr);
            ok = ticket.IsValid(error_ret, false, 0);
            expectedTicketFee = ticket.TicketPrice(nHeight) * COIN;
        }
        else if (ticket_id == TicketID::Activate) {
            CArtActivateTicket ticket;
            data_stream >> ticket;
            ticket.ticketTnx = std::move(ticketBlockTxIdStr);
            ticket.ticketBlock = nHeight;
            ok = ticket.IsValid(error_ret, false, 0);
            expectedTicketFee = ticket.TicketPrice(nHeight) * COIN;
            storageFee = ticket.storageFee;
        }
        else if (ticket_id == TicketID::Sell) {
            CArtSellTicket ticket;
            data_stream >> ticket;
            ticket.ticketTnx = std::move(ticketBlockTxIdStr);
            ticket.ticketBlock = nHeight;
            ok = ticket.IsValid(error_ret, false, 0);
            expectedTicketFee = ticket.TicketPrice(nHeight) * COIN;
        }
        else if (ticket_id == TicketID::Buy) {
            CArtBuyTicket ticket;
            data_stream >> ticket;
            ticket.ticketTnx = std::move(ticketBlockTxIdStr);
            ticket.ticketBlock = nHeight;
            ok = ticket.IsValid(error_ret, false, 0);
            expectedTicketFee = ticket.TicketPrice(nHeight) * COIN;
        }
        else if (ticket_id == TicketID::Trade) {
            CArtTradeTicket ticket;
            data_stream >> ticket;
            ticket.ticketTnx = std::move(ticketBlockTxIdStr);
            ticket.ticketBlock = nHeight;
            ok = ticket.IsValid(error_ret, false, 0);
            expectedTicketFee = ticket.TicketPrice(nHeight) * COIN;
            tradePrice = ticket.price * COIN;
        }
//		else if (ticket_id == TicketID::Down) {
//            CTakeDownTicket ticket;
//            data_stream >> ticket;
//            ticket.ticketTnx = std::move(ticketBlockTxIdStr);
//        ticket.ticketBlock = nHeight;
//            ok = ticket.IsValid(error_ret, false, 0);
//            expectedTicketFee = ticket.TicketPrice(nHeight) * COIN;
//		}
        else {
            error_ret = "unknown ticket_id";
        }
    }catch (std::runtime_error& ex){
        error_ret = strprintf("Failed to parse and unpack ticket - %s", ex.what());
    }catch (...){
        error_ret = strprintf("Failed to parse and unpack ticket - Unknown exception");
    }
    
    if (ok){
        //Validate various Fees
        
        int num = tx.vout.size();
        CAmount ticketFee = 0;
    
        CAmount allMNFee = storageFee * COIN * 9 / 10;
        CAmount mn1Fee = allMNFee * 3 / 5;
        CAmount mn23Fee = allMNFee / 5;
    
        for (int i=0; i<num; i++){
            if ((ticket_id == TicketID::PastelID ||
                 ticket_id == TicketID::Art ||
                 ticket_id == TicketID::Sell ||
                 ticket_id == TicketID::Buy) &&
                i==num-1) //in these tickets last output is change
                break;
            if (ticket_id == TicketID::Activate) { //in this tickets last 4 outputs is: change, and payments to 3 MNs
                if (i == num-4)
                    continue;
                if (i == num-3) {
                    if (mn1Fee != tx.vout[i].nValue)
                    {
                        ok = false;
                        error_ret = strprintf("Wrong main MN fee: expected - %d, real - %d", mn1Fee, tx.vout[i].nValue);
                        break;
                    }
                    continue;
                }
                if (i >= num-2) {
                    if (mn23Fee != tx.vout[i].nValue)
                    {
                        ok = false;
                        error_ret = strprintf("Wrong MN%d fee: expected - %d, real - %d", i-num-4, mn23Fee, tx.vout[i].nValue);
                        break;
                    }
                    continue;
                }
            }
            if (ticket_id == TicketID::Trade){ //in this tickets last 2 outputs is: change, and payment to the seller
                if (i == num-2)
                    continue;
                if (i == num-1) {
                    if (tradePrice != tx.vout[i].nValue)
                    {
                        ok = false;
                        error_ret = strprintf("Wrong payment to the seller: expected - %d, real - %d", tradePrice, tx.vout[i].nValue);
                        break;
                    }
                    continue;
                }
            }
            ticketFee += tx.vout[i].nValue;
        }
    
        if (expectedTicketFee != ticketFee) {
            ok = false;
            error_ret = strprintf("Wrong ticket fee: expected - %d, real - %d", expectedTicketFee, ticketFee);
        }
    }
    
    if (!ok)
        LogPrintf("CPastelTicketProcessor::ValidateIfTicketTransaction -- Invalid ticket [ticket_id=%d, txid=%s, nHeight=%d]. ERROR: %s\n", (int)ticket_id, tx.GetHash().GetHex(), nHeight, error_ret);
    
    return ok;
}

bool CPastelTicketProcessor::ParseTicketAndUpdateDB(CMutableTransaction& tx, int nBlockHeight)
{
	std::string error_ret;
    CDataStream data_stream(SER_NETWORK, TICKETS_VERSION);
	TicketID ticket_id;
	
	if (!preParseTicket(tx, data_stream, ticket_id, error_ret))
		return false;
	
	try {
		std::string txid = tx.GetHash().GetHex();
        
        LogPrintf("CPastelTicketProcessor::ParseTicketAndUpdateDB (called from UpdatedBlockTip) -- Processing ticket [ticket_id=%d, txid=%s, nBlockHeight=%d]\n", (int)ticket_id, txid, nBlockHeight);
		
		if (ticket_id == TicketID::PastelID) {
            CPastelIDRegTicket ticket;
            data_stream >> ticket;
			return UpdateDB<CPastelIDRegTicket>(ticket, txid, nBlockHeight);
		} else if (ticket_id == TicketID::Art) {
            CArtRegTicket ticket;
            data_stream >> ticket;
			return UpdateDB<CArtRegTicket>(ticket, txid, nBlockHeight);
		} else if (ticket_id == TicketID::Activate) {
            CArtActivateTicket ticket;
            data_stream >> ticket;
			return UpdateDB<CArtActivateTicket>(ticket, txid, nBlockHeight);
        } else if (ticket_id == TicketID::Sell) {
            CArtSellTicket ticket;
            data_stream >> ticket;
            return UpdateDB<CArtSellTicket>(ticket, txid, nBlockHeight);
        } else if (ticket_id == TicketID::Buy) {
            CArtBuyTicket ticket;
            data_stream >> ticket;
            return UpdateDB<CArtBuyTicket>(ticket, txid, nBlockHeight);
		} else if (ticket_id == TicketID::Trade) {
            CArtTradeTicket ticket;
            data_stream >> ticket;
            return UpdateDB<CArtTradeTicket>(ticket, txid, nBlockHeight);
//		} else if (ticket_id == TicketID::Down) {
//            CTakeDownTicket ticket;
//            data_stream >> ticket;
//            return UpdateDB<CTakeDownTicket>(ticket, txid, nBlockHeight);
		} else {
            error_ret = "unknown ticket_id";
        }
    }catch (std::runtime_error& ex){
        error_ret = strprintf("Failed to parse and unpack ticket - %s", ex.what());
    }catch (...){
        error_ret = strprintf("Failed to parse and unpack ticket - Unknown exception");
    }
	
    LogPrintf("CPastelTicketProcessor::ParseTicketAndUpdateDB -- Invalid ticket [ticket_id=%d, txid=%s, nBlockHeight=%d]. ERROR: %s\n", (int)ticket_id, tx.GetHash().GetHex(), nBlockHeight, error_ret);
	
	return false;
}

/*static*/ std::string CPastelTicketProcessor::GetTicketJSON(uint256 txid)
{
    TicketID ticketId;
	auto ticket = GetTicket(txid, ticketId);
	if (ticket != nullptr)
		return ticket->ToJSON();
	else
		return "";
}

/*static*/ std::unique_ptr<CPastelTicketBase> CPastelTicketProcessor::GetTicket(uint256 txid, TicketID& ticketId)
{
	CTransaction tx;
	uint256 hashBlock;
	if (!GetTransaction(txid, tx, hashBlock, true))
		throw std::runtime_error(strprintf("No information available about transaction"));

	CMutableTransaction mtx(tx);
	
	std::string error_ret;
    CDataStream data_stream(SER_NETWORK, TICKETS_VERSION);
	
	if (!preParseTicket(mtx, data_stream, ticketId, error_ret))
		throw std::runtime_error(strprintf("Failed to create P2FMS from data provided - %s", error_ret));
    
    std::unique_ptr<CPastelTicketBase> ticket = nullptr;
    
    try {
		std::string ticketBlockTxIdStr = tx.GetHash().GetHex();
		int ticketBlockHeight = -1;
		if (mapBlockIndex.count(hashBlock) != 0) {
            ticketBlockHeight = mapBlockIndex[hashBlock]->nHeight;
        }
		
		if (ticketId == TicketID::PastelID) {
            auto t = new CPastelIDRegTicket();
            data_stream >> *t;
            ticket.reset(t);
		} else if (ticketId == TicketID::Art) {
            auto t = new CArtRegTicket();
            data_stream >> *t;
            ticket.reset(t);
		} else if (ticketId == TicketID::Activate) {
            auto t = new CArtActivateTicket();
            data_stream >> *t;
            ticket.reset(t);
        }
        if (ticketId == TicketID::Sell) {
            auto t = new CArtSellTicket();
            data_stream >> *t;
            ticket.reset(t);
        }
        if (ticketId == TicketID::Buy) {
            auto t = new CArtBuyTicket();
            data_stream >> *t;
            ticket.reset(t);
		}
		if (ticketId == TicketID::Trade) {
            auto t = new CArtTradeTicket();
            data_stream >> *t;
            ticket.reset(t);
//		}
//		if (ticketId == TicketID::Down) {
//			  Down ticket;
//            data_stream >> ticket;
		} else {
            error_ret = "unknown ticket_id";
        }
    
        if (ticket != nullptr) {
            ticket->ticketTnx = std::move(ticketBlockTxIdStr);
            ticket->ticketBlock = ticketBlockHeight;
        }
    }catch (std::runtime_error& ex){
        error_ret = strprintf("Failed to parse and unpack ticket - %s", ex.what());
    }catch (...){
        error_ret = strprintf("Failed to parse and unpack ticket - Unknown exception");
    }
    
    
    if (ticket == nullptr)
        LogPrintf("CPastelTicketProcessor::ParseTicketAndUpdateDB -- Invalid ticket [ticket_id=%d, txid=%s]. ERROR: %s\n", (int)ticketId, tx.GetHash().GetHex(), error_ret);
	
	return ticket;
}

template<class T>
/*static*/ std::unique_ptr<T> CPastelTicketProcessor::GetTicket(const std::string& _txid, TicketID _ticketID)
{
    uint256 txid;
    txid.SetHex(_txid);
    TicketID ticketId;
    auto pastelTicket = CPastelTicketProcessor::GetTicket(txid, ticketId);
    if (pastelTicket == nullptr || ticketId != _ticketID)
        throw std::runtime_error(
                strprintf("The ticket with this txid [%s] is not in the blockchain", _txid));
    
    T* ticket = dynamic_cast<T *>(pastelTicket.release());
    if (ticket == nullptr)
        throw std::runtime_error(
                strprintf("The ticket with this txid [%s] is not in the blockchain or is invalid", _txid));
    
    return std::unique_ptr<T>(ticket);
}

template<class T>
bool CPastelTicketProcessor::CheckTicketExist(const T& ticket)
{
	auto key = ticket.KeyOne();
	return dbs[ticket.ID()]->Exists(key);
}
template bool CPastelTicketProcessor::CheckTicketExist<CPastelIDRegTicket>(const CPastelIDRegTicket&);
template bool CPastelTicketProcessor::CheckTicketExist<CArtRegTicket>(const CArtRegTicket&);
template bool CPastelTicketProcessor::CheckTicketExist<CArtActivateTicket>(const CArtActivateTicket&);
template bool CPastelTicketProcessor::CheckTicketExist<CArtSellTicket>(const CArtSellTicket&);
template bool CPastelTicketProcessor::CheckTicketExist<CArtBuyTicket>(const CArtBuyTicket&);
template bool CPastelTicketProcessor::CheckTicketExist<CArtTradeTicket>(const CArtTradeTicket&);

template<class T>
bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey(const T& ticket)
{
    if (ticket.HasKeyTwo()) {
        std::string mainKey;
        auto realKeyTwo = RealKeyTwo(ticket.KeyTwo());
        if (dbs[ticket.ID()]->Read(realKeyTwo, mainKey))
            return dbs[ticket.ID()]->Exists(mainKey);
    }
	return false;
}
template bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey<CPastelIDRegTicket>(const CPastelIDRegTicket&);
template bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey<CArtRegTicket>(const CArtRegTicket&);
template bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey<CArtActivateTicket>(const CArtActivateTicket&);
template bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey<CArtSellTicket>(const CArtSellTicket&);
template bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey<CArtBuyTicket>(const CArtBuyTicket&);
template bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey<CArtTradeTicket>(const CArtTradeTicket&);

template<class T>
bool CPastelTicketProcessor::FindTicket(T& ticket)
{
	auto key = ticket.KeyOne();
	return dbs[ticket.ID()]->Read(key, ticket);
}
template bool CPastelTicketProcessor::FindTicket<CPastelIDRegTicket>(CPastelIDRegTicket&);
template bool CPastelTicketProcessor::FindTicket<CArtRegTicket>(CArtRegTicket&);
template bool CPastelTicketProcessor::FindTicket<CArtActivateTicket>(CArtActivateTicket&);
template bool CPastelTicketProcessor::FindTicket<CArtSellTicket>(CArtSellTicket&);
template bool CPastelTicketProcessor::FindTicket<CArtBuyTicket>(CArtBuyTicket&);
template bool CPastelTicketProcessor::FindTicket<CArtTradeTicket>(CArtTradeTicket&);

template<class T>
bool CPastelTicketProcessor::FindTicketBySecondaryKey(T& ticket)
{
    if (ticket.HasKeyTwo()) {
        std::string mainKey;
        auto realKeyTwo = RealKeyTwo(ticket.KeyTwo());
        if (dbs[ticket.ID()]->Read(realKeyTwo, mainKey))
            return dbs[ticket.ID()]->Read(mainKey, ticket);
    }
	return false;
}
template bool CPastelTicketProcessor::FindTicketBySecondaryKey<CPastelIDRegTicket>(CPastelIDRegTicket&);
template bool CPastelTicketProcessor::FindTicketBySecondaryKey<CArtRegTicket>(CArtRegTicket&);
template bool CPastelTicketProcessor::FindTicketBySecondaryKey<CArtActivateTicket>(CArtActivateTicket&);
template bool CPastelTicketProcessor::FindTicketBySecondaryKey<CArtSellTicket>(CArtSellTicket&);
template bool CPastelTicketProcessor::FindTicketBySecondaryKey<CArtBuyTicket>(CArtBuyTicket&);
template bool CPastelTicketProcessor::FindTicketBySecondaryKey<CArtTradeTicket>(CArtTradeTicket&);

template<class T>
std::vector<T> CPastelTicketProcessor::FindTicketsByMVKey(TicketID ticketId, const std::string& mvKey)
{
    std::vector<T> tickets;
    std::vector<std::string> mainKeys;
    auto realMVKey = RealMVKey(mvKey);
    dbs[ticketId]->Read(realMVKey, mainKeys);
    for (const auto& key : mainKeys){
        T ticket;
        if (dbs[ticketId]->Read(key, ticket))
            tickets.emplace_back(ticket);
    }
    return tickets;
}

std::vector<std::string> CPastelTicketProcessor::GetAllKeys(TicketID id)
{
	std::vector<std::string> results;
	
	std::unique_ptr<CDBIterator> pcursor(dbs[id]->NewIterator());
	pcursor->SeekToFirst();
	while (pcursor->Valid()) {
		std::string key;
		if (pcursor->GetKey(key)) {
			results.emplace_back(key);
		}
		pcursor->Next();
	}
	return results;
}

template<class T, TicketID ticketId, typename F>
void CPastelTicketProcessor::listTickets(F f)
{
    std::vector<std::string> keys;
    keys = GetAllKeys(ticketId);
    for (const auto& key : keys){
        if (key.front() == '@') continue;
        T ticket;
        ticket.SetKeyOne(key);
        if (FindTicket(ticket)) {
            f(ticket);
        }
    }
}

template<class T, TicketID ticketId>
std::string CPastelTicketProcessor::ListTickets()
{
    json jArray;
    listTickets<T, ticketId>(
           [&](T& ticket){jArray.push_back(json::parse(ticket.ToJSON()));}
    );
    return jArray.dump();
}
template std::string CPastelTicketProcessor::ListTickets<CPastelIDRegTicket, TicketID::PastelID>();
template std::string CPastelTicketProcessor::ListTickets<CArtRegTicket, TicketID::Art>();
template std::string CPastelTicketProcessor::ListTickets<CArtActivateTicket, TicketID::Activate>();
template std::string CPastelTicketProcessor::ListTickets<CArtSellTicket, TicketID::Sell>();
template std::string CPastelTicketProcessor::ListTickets<CArtBuyTicket, TicketID::Buy>();
template std::string CPastelTicketProcessor::ListTickets<CArtTradeTicket, TicketID::Trade>();

template<class T, TicketID ticketId, typename F>
std::string CPastelTicketProcessor::filterTickets(F f)
{
    std::vector<T> allTickets;
    listTickets<T, ticketId>(
            [&](T& ticket){allTickets.push_back(ticket);}
    );
    
    int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = chainActive.Height() + 1;
    }
    
    json jArray;
    for (auto& t : allTickets) {
        //check if the sell ticket is confirmed
        if (chainHeight - t.ticketBlock < masterNodeCtrl.MinTicketConfirmations)
            continue;
        if (f(t, chainHeight)) continue;
        jArray.push_back(json::parse(t.ToJSON()));
    }
    return jArray.dump();
}

// 1 - mn;        2 - personal
std::string CPastelTicketProcessor::ListFilterPastelIDTickets(short filter)
{
    return filterTickets<CPastelIDRegTicket, TicketID::PastelID>(
            [&](CPastelIDRegTicket& t, int chainHeight)->bool
            {
                if ((filter == 1 && !t.outpoint.IsNull()) ||                //don't skip mn
                    (filter == 2 && t.outpoint.IsNull())) return false;     //don't skip personal
                return true;
            }
    );
}
// 1 - active;    2 - inactive;     3 - sold
std::string CPastelTicketProcessor::ListFilterArtTickets(short filter)
{
    return filterTickets<CArtRegTicket, TicketID::Art>(
            [&](CArtRegTicket& t, int chainHeight)->bool
            {
                if (filter == 3) {
                    CArtActivateTicket actTicket;
                    // find Act ticket for this Reg ticket
                    if (CArtActivateTicket::FindTicketInDb(t.ticketTnx, actTicket)) {
                        //find Trade tickets listing that Act ticket txid as art ticket
                        std::vector<CArtTradeTicket> tradeTickets = CArtTradeTicket::FindAllTicketByArtTnxID(
                                actTicket.ticketTnx);
                        if (tradeTickets.size() >= t.totalCopies)
                            return false;       //don't skip sold
                    }
                }
                
                // check if there is Act ticket for this Reg ticket
                if (CArtActivateTicket::CheckTicketExistByArtTicketID(t.ticketTnx)) {
                    if (filter == 1) return false;       //don't skip active
                } else if (filter == 2) return false;    //don't skip inactive
                return true;
            }
    );
}
// 1 - available;      2 - sold
std::string CPastelTicketProcessor::ListFilterActTickets(short filter)
{
    return filterTickets<CArtActivateTicket, TicketID::Activate>(
            [&](CArtActivateTicket& t, int chainHeight)->bool
            {
                //find Trade tickets listing this Act ticket txid as art ticket
                std::vector<CArtTradeTicket> tradeTickets = CArtTradeTicket::FindAllTicketByArtTnxID(t.ticketTnx);
                auto artTicket = CPastelTicketProcessor::GetTicket<CArtRegTicket>(t.regTicketTnxId, TicketID::Art);

                if (tradeTickets.size() < artTicket->totalCopies) {
                    if (filter == 1) return false;       //don't skip available
                } else if (filter == 2) return false;    //don't skip sold
                return true;
            }
    );
}
// 1 - available; 2 - unavailable; 3 - expired; 4 - sold
std::string CPastelTicketProcessor::ListFilterSellTickets(short filter)
{
    return filterTickets<CArtSellTicket, TicketID::Sell>(
            [&](CArtSellTicket& t, int chainHeight)->bool
            {
                CArtBuyTicket existingBuyTicket;
                //find buy ticket for this sell ticket, if any
                if (CArtBuyTicket::FindTicketInDb(t.ticketTnx, existingBuyTicket)) {
                    //check if trade ticket exists for this sell ticket
                    if (CArtTradeTicket::CheckTradeTicketExistByBuyTicket(existingBuyTicket.ticketTnx)) {
                        if (filter == 4) return false; // don't skip sold
                        else return true;
                    };
                    //if not - check age
                    if (existingBuyTicket.ticketBlock + masterNodeCtrl.MaxBuyTicketAge <= chainHeight) return true;
                }
                if (filter == 1) {
                    //skip sell ticket that is not yet active
                    if (t.activeAfter > 0 && chainHeight <= t.activeAfter)  return true;
                    //skip sell ticket that is already not active
                    if (t.activeBefore > 0 && chainHeight >= t.activeBefore) return true;
                } else if (filter == 2){
                    //skip sell ticket that is already active
                    if (t.activeAfter > 0 && chainHeight >= t.activeAfter) return true;
                } else if (filter == 3) {
                    //skip sell ticket that is still active
                    if (t.activeBefore > 0 && chainHeight <= t.activeBefore) return true;
                }
                return false;
            }
    );
}
// 1 - expired;    2 - sold
std::string CPastelTicketProcessor::ListFilterBuyTickets(short filter)
{
    return filterTickets<CArtBuyTicket, TicketID::Buy>(
            [&](CArtBuyTicket& t, int chainHeight)->bool
            {
                if (CArtTradeTicket::CheckTradeTicketExistByBuyTicket(t.ticketTnx)){
                    if (filter == 2) return false;          //don't skip traded
                } else if (filter == 1 && t.ticketBlock + masterNodeCtrl.MaxBuyTicketAge < chainHeight)
                    return false;   //don't skip non sold, and expired
                return true;
            }
    );
}
// 1 - available;      2 - sold
std::string CPastelTicketProcessor::ListFilterTradeTickets(short filter)
{
    return filterTickets<CArtTradeTicket, TicketID::Trade>(
            [&](CArtTradeTicket& t, int chainHeight)->bool
            {
                //find Trade tickets listing this Trade ticket txid as art ticket
                std::vector<CArtTradeTicket> tradeTickets = CArtTradeTicket::FindAllTicketByArtTnxID(t.ticketTnx);
                
                if (tradeTickets.empty()) {
                    if (filter == 1) return false;       //don't skip available
                } else if (filter == 2) return false;    //don't skip sold
                return true;
            }
    );
}

template<class T>
std::string CPastelTicketProcessor::SendTicket(const T& ticket)
{
    std::string error;

    if (!ticket.IsValid( error, true, 0)) {
        throw std::runtime_error(strprintf("Ticket (%s) is invalid - %s", ticket.TicketName(), error));
    }
    
    std::vector<CTxOut> extraOutputs;
    CAmount extraAmount = ticket.GetExtraOutputs(extraOutputs);
    
    CDataStream data_stream(SER_NETWORK, TICKETS_VERSION);
    data_stream << (uint8_t)ticket.ID();
    data_stream << ticket;
    
    int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = chainActive.Height() + 1;
    }
    
    CMutableTransaction tx;
	if (!CPastelTicketProcessor::CreateP2FMSTransactionWithExtra(data_stream, extraOutputs, extraAmount, tx, ticket.TicketPrice(chainHeight), error)){
		throw std::runtime_error(strprintf("Failed to create P2FMS from data provided - %s", error));
	}
	
	if (!CPastelTicketProcessor::StoreP2FMSTransaction(tx, error)){
		throw std::runtime_error(strprintf("Failed to send P2FMS transaction - %s", error));
	}
	return tx.GetHash().GetHex();
}
template std::string CPastelTicketProcessor::SendTicket<CPastelIDRegTicket>(const CPastelIDRegTicket&);
template std::string CPastelTicketProcessor::SendTicket<CArtRegTicket>(const CArtRegTicket&);
template std::string CPastelTicketProcessor::SendTicket<CArtActivateTicket>(const CArtActivateTicket&);
template std::string CPastelTicketProcessor::SendTicket<CArtSellTicket>(const CArtSellTicket&);
template std::string CPastelTicketProcessor::SendTicket<CArtBuyTicket>(const CArtBuyTicket&);
template std::string CPastelTicketProcessor::SendTicket<CArtTradeTicket>(const CArtTradeTicket&);

#ifdef ENABLE_WALLET
bool CPastelTicketProcessor::CreateP2FMSTransaction(const std::string& input_string, CMutableTransaction& tx_out, CAmount price, std::string& error_ret)
{
    //Convert string data into binary buffer
    CDataStream data_stream(SER_NETWORK, TICKETS_VERSION);
    data_stream << input_string;
    return CPastelTicketProcessor::CreateP2FMSTransaction(data_stream, tx_out, price, error_ret);
}
bool CPastelTicketProcessor::CreateP2FMSTransaction(const CDataStream& input_stream, CMutableTransaction& tx_out, CAmount price, std::string& error_ret)
{
    return CPastelTicketProcessor::CreateP2FMSTransactionWithExtra(input_stream, std::vector<CTxOut>{}, 0, tx_out, price, error_ret);
    
}
bool CPastelTicketProcessor::CreateP2FMSTransactionWithExtra(const CDataStream& input_stream, const std::vector<CTxOut>& extraOutputs, CAmount extraAmount, CMutableTransaction& tx_out, CAmount price, std::string& error_ret)
{
	assert(pwalletMain != nullptr);
	
	if (pwalletMain->IsLocked()) {
		error_ret = "Wallet is locked. Try again later";
		return false;
	}
	
    size_t input_len = input_stream.size();
    if (input_len == 0) {
        error_ret = "Input data is empty";
        return false;
    }

    std::vector<unsigned char> input_bytes{input_stream.begin(), input_stream.end()};

    //Get Hash(SHA256) of input buffer and insert it upfront
    uint256 input_hash = Hash(input_bytes.begin(), input_bytes.end());
    input_bytes.insert(input_bytes.begin(), input_hash.begin(), input_hash.end());

    //insert size of the original data upfront
    auto* input_len_bytes = reinterpret_cast<unsigned char*>(&input_len);
    input_bytes.insert(input_bytes.begin(), input_len_bytes, input_len_bytes+sizeof(size_t)); //sizeof(size_t) == 8

    //Add padding at the end if required -
    // final size is n*33 - (33 bytes, but 66 characters)
    int fake_key_size = 33;
    size_t non_padded_size = input_bytes.size();
    size_t padding_size = fake_key_size - (non_padded_size % fake_key_size);
    if (padding_size != 0){
        input_bytes.insert(input_bytes.end(), padding_size, 0);
    }

    //Break data into 33 bytes blocks
    std::vector<std::vector<unsigned char> > chunks;
    for (auto it = input_bytes.begin(); it != input_bytes.end(); it += fake_key_size){
        chunks.emplace_back(std::vector<unsigned char>(it, it+fake_key_size));
    }

    //Create output P2FMS scripts
    std::vector<CScript> out_scripts;
    for (auto it=chunks.begin(); it != chunks.end(); ) {
        CScript script;
        script << CScript::EncodeOP_N(1);
        int m=0;
        for (; m<3 && it != chunks.end(); m++, it++) {
            script << *it;
        }
        script << CScript::EncodeOP_N(m) << OP_CHECKMULTISIG;
        out_scripts.push_back(script);
    }
    int num_fake_txn = out_scripts.size();
    if (num_fake_txn == 0){
        error_ret = "No fake transactions after parsing input data";
        return false;
    }

    //calculate aprox required amount
    CAmount nAproxFeeNeeded = payTxFee.GetFee(input_bytes.size())*2;
    if (nAproxFeeNeeded < payTxFee.GetFeePerK()) nAproxFeeNeeded = payTxFee.GetFeePerK();
    
    //Amount
	CAmount perOutputAmount = price*COIN/num_fake_txn;
	//MUST be precise!!!
	CAmount lost = price*COIN - perOutputAmount*num_fake_txn;
			
    CAmount allSpentAmount = price*COIN + nAproxFeeNeeded + extraAmount;

    int chainHeight = chainActive.Height() + 1;
    if (Params().NetworkIDString() != "regtest") {
        chainHeight = std::max(chainHeight, APPROX_RELEASE_HEIGHT);
    }
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
                for (int i=0; i<num_fake_txn; i++) {
                    tx_out.vout[i].nValue = perOutputAmount;
                    tx_out.vout[i].scriptPubKey = out_scripts[i];
                }
                //MUST be precise!!!
                tx_out.vout[0].nValue = perOutputAmount + lost;

                if (extraAmount != 0)
                    for (const auto& extra : extraOutputs)
                        tx_out.vout.emplace_back(extra);
                
				//Send change output back to input address
				tx_out.vout[num_fake_txn].nValue = prevAmount - price*COIN - extraAmount;
				tx_out.vout[num_fake_txn].scriptPubKey = prevPubKey;
                
                //sign transaction - unlock input
                SignatureData sigdata;
                ProduceSignature(MutableTransactionSignatureCreator(pwalletMain, &tx_out, 0, prevAmount, SIGHASH_ALL), prevPubKey, sigdata, consensusBranchId);
                UpdateTransaction(tx_out, 0, sigdata);

                //Calculate correct fee
                size_t tx_size = EncodeHexTx(tx_out).length();
                CAmount nFeeNeeded = payTxFee.GetFee(tx_size);
                if (nFeeNeeded < payTxFee.GetFeePerK()) nFeeNeeded = payTxFee.GetFeePerK();

                //num_fake_txn is index of the change output
                tx_out.vout[num_fake_txn].nValue -= nFeeNeeded;

                bOk = true;
                break;
            }
        }
    }

    if (!bOk){
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
	bool bOk = CPastelTicketProcessor::ParseP2FMSTransaction(tx_in, output_data, error_ret);
	if (bOk)
        output_string.assign(output_data.begin(), output_data.end());
	return bOk;
}
bool CPastelTicketProcessor::ParseP2FMSTransaction(const CMutableTransaction& tx_in, vector<unsigned char>& output_data, std::string& error_ret)
{
	bool foundMS = false;
    
    for (const auto& vout : tx_in.vout) {
		
		txnouttype typeRet;
		vector<vector<unsigned char> > vSolutions;
		
		if (!Solver(vout.scriptPubKey, typeRet, vSolutions) ||
			typeRet != TX_MULTISIG)
			continue;
		
		foundMS = true;
		for (size_t i = 1; vSolutions.size() - 1 > i; i++)
		{
			output_data.insert(output_data.end(), vSolutions[i].begin(), vSolutions[i].end());
		}
	}
	
	if (!foundMS){
		error_ret = "No data Multisigs found in transaction";
		return false;
	}
	
	if (output_data.empty()){
		error_ret = "No data found in transaction";
		return false;
	}
	
	//size_t size = 8 bytes; hash size = 32 bytes
	if (output_data.size() < 40){
		error_ret = "No correct data found in transaction";
		return false;
	}

	auto output_len_ptr = reinterpret_cast<size_t**>(&output_data);
	if (output_len_ptr == nullptr || *output_len_ptr == nullptr){
		error_ret = "No correct data found in transaction - wrong length";
		return false;
	}
	auto output_len = **output_len_ptr;
	output_data.erase(output_data.begin(), output_data.begin()+sizeof(size_t));
	
	std::vector<unsigned char> input_hash_vec(output_data.begin(), output_data.begin()+32); //hash length == 32
	output_data.erase(output_data.begin(), output_data.begin()+32);
	
	if (output_data.size() < output_len){
		error_ret = "No correct data found in transaction - length is not matching";
		return false;
	}
	
	if (output_data.size() > output_len) {
		output_data.erase(output_data.begin()+output_len, output_data.end());
	}
	
	uint256 input_hash_stored(input_hash_vec);
	uint256 input_hash_real = Hash(output_data.begin(), output_data.end());
	
	if (input_hash_stored != input_hash_real) {
		error_ret = "No correct data found in transaction - hash is not matching";
		return false;
	}
	
	return true;
}

// CPastelIDRegTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ CPastelIDRegTicket CPastelIDRegTicket::Create(std::string _pastelID, const SecureString& strKeyPass, std::string _address)
{
    CPastelIDRegTicket ticket(std::move(_pastelID));
    
    bool isMN = _address.empty();
    
    if (isMN){
        CMasternode mn;
        if(!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, mn)) {
            throw std::runtime_error("This is not a active masternode. Only active MN can register its PastelID ");
        }
    
        //collateral address
        CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
        ticket.address = std::move(EncodeDestination(dest));
    
        //outpoint hash
        ticket.outpoint = masterNodeCtrl.activeMasternode.outpoint;
    } else {
        ticket.address = std::move(_address);
    }
    
    ticket.timestamp = std::time(nullptr);
    
    std::stringstream ss;
	ss << ticket.pastelID;
	ss << ticket.address;
	ss << ticket.outpoint.ToStringShort();
	ss << ticket.timestamp;
    if (isMN) {
        if(!CMessageSigner::SignMessage(ss.str(), ticket.mn_signature, masterNodeCtrl.activeMasternode.keyMasternode)) {
            throw std::runtime_error("MN Sign of the ticket has failed");
        }
        ss << std::string{ ticket.mn_signature.begin(), ticket.mn_signature.end() };
    }
    std::string fullTicket = ss.str();
    ticket.pslid_signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(fullTicket.c_str()), fullTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

std::string CPastelIDRegTicket::ToStr() const
{
    std::stringstream ss;
    ss << pastelID;
    ss << address;
    ss << outpoint.ToStringShort();
    ss << timestamp;
    if (address.empty()) {
        ss << std::string{ mn_signature.begin(), mn_signature.end() };
    }
    return ss.str();
}

bool CPastelIDRegTicket::IsValid(std::string& errRet, bool preReg, int depth) const
{
    if (preReg) { // Something to check ONLY before ticket made into transaction
        
        //1. check that PastelID ticket is not already in the blockchain.
        // Only done after Create
        if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this)) {
            errRet = strprintf("This PastelID is already registered in blockchain [%s]", pastelID);
            return false;
        }

        //TODO Pastel: validate that address has coins to pay for registration - 10PSL + fee
        // ...
    }
    
    std::stringstream ss;
    ss << pastelID;
    ss << address;
    ss << outpoint.ToStringShort();
    ss << timestamp;
    
    if (masterNodeCtrl.masternodeSync.IsSynced()) { // Validate only if both blockchain and MNs are synced
        if (!outpoint.IsNull()) { // validations only for MN PastelID
            // 1. check if TicketDB already has PatelID with the same outpoint,
            // and if yes, reject if it has different signature OR different blocks or transaction ID
            // (ticket transaction replay attack protection)
            CPastelIDRegTicket _ticket;
            _ticket.outpoint = outpoint;
            if (masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(_ticket)){
                if (_ticket.mn_signature != mn_signature || _ticket.ticketBlock != ticketBlock || _ticket.ticketTnx != ticketTnx){
                    errRet = strprintf("Masternode's outpoint - [%s] is already registered as a ticket. Your PastelID - [%s] "
                                       "[this ticket block = %d txid = %s; found ticket block  = %d txid = %s]",
                                       outpoint.ToStringShort(), pastelID,
                                       _ticket.ticketBlock, _ticket.ticketTnx, ticketBlock, ticketTnx);
                    return false;
                }
            }
            
            // 2. Check outpoint belongs to active MN
            // However! If this is validation of an old ticket, MN can be not active or eve alive anymore
            //So will skip the MN validation if ticket is fully confirmed (older then MinTicketConfirmations blocks)
            int currentHeight;
            {
                LOCK(cs_main);
                currentHeight = chainActive.Height();
            }
            //during transaction validation before ticket made in to the block_ticket.ticketBlock will == 0
            if (_ticket.ticketBlock == 0 || currentHeight - _ticket.ticketBlock < masterNodeCtrl.MinTicketConfirmations) {
                CMasternode mnInfo;
                if (!masterNodeCtrl.masternodeManager.Get(outpoint, mnInfo)) {
                    errRet = strprintf("Unknown Masternode - [%s]. PastelID - [%s]", outpoint.ToStringShort(),
                                       pastelID);
                    return false;
                }
                if (!mnInfo.IsEnabled()) {
                    errRet = strprintf("Non an active Masternode - [%s]. PastelID - [%s]", outpoint.ToStringShort(),
                                       pastelID);
                    return false;
                }
    
                // 3. Validate MN signature using public key of MN identified by outpoint
                if (!CMessageSigner::VerifyMessage(mnInfo.pubKeyMasternode, mn_signature, ss.str(), errRet)) {
                    errRet = strprintf("Ticket's MN signature is invalid. Error - %s. Outpoint - [%s]; PastelID - [%s]",
                                       errRet, outpoint.ToStringShort(), pastelID);
                    return false;
                }
            }
        }
    }
    
    // Something to always validate
    // 1. Ticket signature is valid
    ss << std::string{ mn_signature.begin(), mn_signature.end() };
    std::string fullTicket = ss.str();
    if (!CPastelID::Verify(reinterpret_cast<const unsigned char *>(fullTicket.c_str()), fullTicket.size(),
                           pslid_signature.data(), pslid_signature.size(),
                           pastelID)) {
        errRet = strprintf("Ticket's PastelID signature is invalid. Error - %s. PastelID - [%s]", errRet, pastelID);
        return false;
    }
        
    // 2. Ticket pay correct registration fee - in validated in ValidateIfTicketTransaction

    return true;
}

std::string CPastelIDRegTicket::ToJSON() const
{
	json jsonObj;
	jsonObj = {
		{"txid", ticketTnx},
		{"height", ticketBlock},
		{"ticket", {
			{"type", TicketName()},
			{"pastelID", pastelID},
			{"address", address},
			{"timeStamp", std::to_string(timestamp)},
			{"signature", ed_crypto::Hex_Encode(pslid_signature.data(), pslid_signature.size())},
			{"id_type", PastelIDType()}
		}}
	};

	if (!outpoint.IsNull())
		jsonObj["ticket"]["outpoint"] = outpoint.ToStringShort();
	
	return jsonObj.dump(4);
}

/*static*/ bool CPastelIDRegTicket::FindTicketInDb(const std::string& key, CPastelIDRegTicket& ticket)
{
    //first try by PastelID
    ticket.pastelID = key;
    if (!masterNodeCtrl.masternodeTickets.FindTicket(ticket))
    {
        //if not, try by outpoint
        ticket.secondKey = key;
        if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket)){
            //finally, clear outpoint and try by address
            ticket.secondKey.clear();
            ticket.address = key;
            if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket))
                return false;
        }
    }
    return true;
}

/*static*/ std::vector<CPastelIDRegTicket> CPastelIDRegTicket::FindAllTicketByPastelAddress(const std::string& address)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CPastelIDRegTicket>(TicketID::PastelID, address);
}

// CArtRegTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ CArtRegTicket CArtRegTicket::Create(
        std::string _ticket, const std::string& signatures,
        std::string _pastelID, const SecureString& strKeyPass,
        std::string _keyOne, std::string _keyTwo,
        CAmount _storageFee)
{
    CArtRegTicket ticket(std::move(_ticket));
    
    //Art Ticket
    auto jsonTicketObj = json::parse(ed_crypto::Base64_Decode(ticket.artTicket));
    if (jsonTicketObj.size() != 7){
        throw std::runtime_error("Art ticket json is incorrect");
    }
    if (jsonTicketObj["version"] != 1) {
        throw std::runtime_error("Only accept version 1 of Art ticket json");
    }
    ticket.artistHeight = jsonTicketObj["blocknum"];
    ticket.totalCopies = jsonTicketObj["copies"];
    
    //Artist's and MN2/3's signatures
    auto jsonSignaturesObj = json::parse(signatures);
    if (jsonSignaturesObj.size() != 3){
        throw std::runtime_error("Signatures json is incorrect");
    }
    for (auto& el : jsonSignaturesObj.items()) {
        if (el.key().empty()) {
            throw std::runtime_error("Signatures json is incorrect");
        }
        
        auto sigItem = el.value();
        if (sigItem.empty())
            throw std::runtime_error("Signatures json is incorrect");
        
        std::string pastelID = sigItem.begin().key();
        std::string signature = sigItem.begin().value();
        if (el.key() == "artist"){
            ticket.pastelIDs[artistsign] = std::move(pastelID);
            ticket.ticketSignatures[artistsign] = ed_crypto::Base64_Decode(signature);
        }
        else if (el.key() == "mn2"){
            ticket.pastelIDs[mn2sign] = std::move(pastelID);
            ticket.ticketSignatures[mn2sign] = ed_crypto::Base64_Decode(signature);
        }
        else if (el.key() == "mn3"){
            ticket.pastelIDs[mn3sign] = std::move(pastelID);
            ticket.ticketSignatures[mn3sign] = ed_crypto::Base64_Decode(signature);
        }
    }
    
    ticket.keyOne = std::move(_keyOne);
    ticket.keyTwo = std::move(_keyTwo);
    ticket.storageFee = _storageFee;

    ticket.timestamp = std::time(nullptr);
    
    ticket.pastelIDs[mainmnsign] = std::move(_pastelID);
    //signature of ticket hash
    ticket.ticketSignatures[mainmnsign] = CPastelID::Sign(reinterpret_cast<const unsigned char*>(ticket.artTicket.c_str()), ticket.artTicket.size(),
                                                            ticket.pastelIDs[mainmnsign], strKeyPass);
    return ticket;
}

std::string CArtRegTicket::ToStr() const
{
    return artTicket;
}

bool CArtRegTicket::IsValid(std::string& errRet, bool preReg, int depth) const
{
    int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = chainActive.Height() + 1;
    }

    if (preReg){
        // A. Something to check ONLY before ticket made into transaction.
        // Only done after Create
        
        // A.1 check that art ticket already in the blockchain
        if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this)) {
            errRet = strprintf("This Art is already registered in blockchain [Key1 = %s; Key2 = %s]", keyOne, keyTwo);
            return false;
        }
    
        // A.2 validate that address has coins to pay for registration - 10PSL
        unsigned int fullTicketPrice = TicketPrice(chainHeight); //10% of storage fee is paid by the 'artist' and this ticket is created by MN
        if (pwalletMain->GetBalance() < fullTicketPrice*COIN) {
            errRet = strprintf("Not enough coins to cover price [%d]", fullTicketPrice);
            return false;
        }
    }
    
    // (ticket transaction replay attack protection)
    CArtRegTicket _ticket;
    if ((FindTicketInDb(keyOne, _ticket) || (FindTicketInDb(keyTwo, _ticket))) &&
        (_ticket.ticketBlock != ticketBlock || _ticket.ticketTnx != ticketTnx)) {
        errRet = strprintf("This Art is already registered in blockchain [Key1 = %s; Key2 = %s]"
                           "[this ticket block = %d txid = %s; found ticket block  = %d txid = %s]",
                           keyOne, KeyTwo(),
                           _ticket.ticketBlock, _ticket.ticketTnx, ticketBlock, ticketTnx);
        return false;
    }
    
    // B. Something to always validate
    std::string err;
    
    std::map<std::string, int> pidCountMap{};
    std::map<COutPoint, int> outCountMap{};
    
    for (int mnIndex=0; mnIndex < allsigns; mnIndex++) {
        //1. PastelIDs are registered and are in the TicketDB - PastelID tnx can be in the blockchain and valid as tnx,
        // but the ticket this tnx represents can be invalid as ticket, in this case it will not be in the TicketDB!!!
        // and this will mark ArtReg tnx from being valid!!!
        CPastelIDRegTicket pastelIdRegTicket;
        if (!CPastelIDRegTicket::FindTicketInDb(pastelIDs[mnIndex], pastelIdRegTicket)){
            if (mnIndex == artistsign)
                errRet = strprintf("Artist PastelID is not registered [%s]", pastelIDs[mnIndex]);
            else
                errRet = strprintf("MN%d PastelID is not registered [%s]", mnIndex, pastelIDs[mnIndex]);
            return false;
        }
        //2. PastelIDs are valid
        if (!pastelIdRegTicket.IsValid(err, false, ++depth)){
            if (mnIndex == artistsign)
                errRet = strprintf("Artist PastelID is invalid [%s] - %s", pastelIDs[mnIndex], err);
            else
                errRet = strprintf("MN%d PastelID is invalid [%s] - %s", mnIndex, pastelIDs[mnIndex], err);
            return false;
        }
        //3. Artist PastelID is personal PastelID and MNs PastelIDs are not personal
        if (mnIndex == artistsign) {
            if (!pastelIdRegTicket.outpoint.IsNull()) {
                errRet = strprintf("Artist PastelID is NOT personal PastelID [%s]", pastelIDs[mnIndex]);
                return false;
            }
        } else {
    
            if (pastelIdRegTicket.outpoint.IsNull()) {
                errRet = strprintf("MN%d PastelID is NOT masternode PastelID [%s]", mnIndex, pastelIDs[mnIndex]);
                return false;
            }
     
            // Check that MN1, MN2 and MN3 are all different = here by just PastleId
            if (++pidCountMap[pastelIdRegTicket.pastelID] != 1){
                errRet = strprintf("MNs PastelIDs can not be the same - [%s]", pastelIdRegTicket.pastelID);
                return false;
            }
            if (++outCountMap[pastelIdRegTicket.outpoint] != 1){
                errRet = strprintf("MNs PastelID can not be from the same MN - [%s]", pastelIdRegTicket.outpoint.ToStringShort());
                return false;
            }
            
            //4. Masternodes beyond these PastelIDs, were in the top 10 at the block when the registration happened
            if (masterNodeCtrl.masternodeSync.IsSynced()) { //Art ticket needs synced MNs
                auto topBlockMNs = masterNodeCtrl.masternodeManager.GetTopMNsForBlock(artistHeight, true);
                auto found = find_if(topBlockMNs.begin(), topBlockMNs.end(),
                                     [&pastelIdRegTicket](CMasternode const &mn) {
                                         return mn.vin.prevout == pastelIdRegTicket.outpoint;
                                     });
    
                if (found == topBlockMNs.end()) { //not found
                    errRet = strprintf("MN%d was NOT in the top masternodes list for block %d", mnIndex, artistHeight);
                    return false;
                }
            }
        }
    }
    //5. Signatures matches included PastelIDs (signature verification is slower - hence separate loop)
    for (int mnIndex=0; mnIndex < allsigns; mnIndex++) {
        if (!CPastelID::Verify(reinterpret_cast<const unsigned char *>(artTicket.c_str()), artTicket.size(),
                               ticketSignatures[mnIndex].data(), ticketSignatures[mnIndex].size(),
                               pastelIDs[mnIndex])) {
            if (mnIndex == artistsign)
                errRet = strprintf("Artist signature is invalid");
            else
                errRet = strprintf("MN%d signature is invalid", mnIndex);
            return false;
        }
    }
    return true;
}

std::string CArtRegTicket::ToJSON() const
{
	json jsonObj;
	jsonObj = {
	    {"txid", ticketTnx},
        {"height", ticketBlock},
        {"ticket", {
            {"type", TicketName()},
            {"art_ticket", artTicket},
            {"signatures",{
                {"artist", {
                    {pastelIDs[artistsign], ed_crypto::Base64_Encode(ticketSignatures[artistsign].data(), ticketSignatures[artistsign].size())}
                }},
                {"mn1", {
                    {pastelIDs[mainmnsign], ed_crypto::Base64_Encode(ticketSignatures[mainmnsign].data(), ticketSignatures[mainmnsign].size())}
                }},
                {"mn2", {
                    {pastelIDs[mn2sign], ed_crypto::Base64_Encode(ticketSignatures[mn2sign].data(), ticketSignatures[mn2sign].size())}
                }},
                {"mn3", {
                    {pastelIDs[mn3sign], ed_crypto::Base64_Encode(ticketSignatures[mn3sign].data(), ticketSignatures[mn3sign].size())}
                }},
            }},
            {"key1", keyOne},
            {"key2", keyTwo},
            {"artist_height", artistHeight},
            {"total_copies", totalCopies},
            {"storage_fee", storageFee},
        }}
	};
    
    return jsonObj.dump(4);
}

/*static*/ bool CArtRegTicket::FindTicketInDb(const std::string& key, CArtRegTicket& _ticket)
{
    _ticket.keyOne = key;
    _ticket.keyTwo = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(_ticket) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(_ticket);
}
/*static*/ bool CArtRegTicket::CheckIfTicketInDb(const std::string& key)
{
    CArtRegTicket _ticket;
    _ticket.keyOne = key;
    _ticket.keyTwo = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket) ||
           masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(_ticket);
}

/*static*/ std::vector<CArtRegTicket> CArtRegTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtRegTicket>(TicketID::Art, pastelID);
}

template<class T, typename F>
bool common_validation(const T& ticket, bool preReg, const std::string& strTnxId,
                       std::unique_ptr<CPastelTicketBase>& pastelTicket, TicketID& ticketId,
                       F f,
                       const std::string& thisTicket, const std::string& prevTicket, int depth,
                       unsigned int ticketPrice,
                       std::string& errRet)
{
    // A. Something to check ONLY before ticket made into transaction
    if (preReg){
        // A. Validate that address has coins to pay for registration - 10PSL + fee
        if (pwalletMain->GetBalance() < ticketPrice*COIN) {
            errRet = strprintf("Not enough coins to cover price [%d]", ticketPrice);
            return false;
        }
    }
    
    // C. Something to always validate
    
    // C.1 Check there are ticket referred from that new ticket with this tnxId
    uint256 txid;
    txid.SetHex(strTnxId);
    //  Get ticket pointed by artTnxId. This is either Activation or Trade tickets (Sell, Buy, Trade)
    try {
        pastelTicket = CPastelTicketProcessor::GetTicket(txid, ticketId);
    }catch (std::runtime_error& ex){
        errRet = strprintf("The %s ticket [txid=%s] referred by this %s ticket is not in the blockchain. [txid=%s]",
                           prevTicket, strTnxId, thisTicket, ticket.ticketTnx);
        return false;
    }

    if (pastelTicket == nullptr || f(ticketId)) {
        errRet = strprintf("The %s ticket with this txid [%s] referred by this %s ticket is not in the blockchain", prevTicket, strTnxId, thisTicket);
        return false;
    }
    
    // B.1 Something to validate only if NOT Initial Download
    if (masterNodeCtrl.masternodeSync.IsSynced()) {
        int chainHeight = 0;
        {
            LOCK(cs_main);
            chainHeight = chainActive.Height() + 1;
        }
    
        // C.2 Verify Min Confirmations
        int height = (ticket.ticketBlock == 0) ? chainHeight : ticket.ticketBlock;
        if (chainHeight - pastelTicket->ticketBlock < masterNodeCtrl.MinTicketConfirmations) {
            errRet = strprintf(
                    "%s ticket can be created only after [%s] confirmations of the %s ticket. chainHeight=%d ticketBlock=%d",
                    thisTicket, masterNodeCtrl.MinTicketConfirmations, prevTicket,
                    chainHeight, ticket.ticketBlock);
            return false;
        }
    }
    // C.3 Verify signature
    // We will check that it is the correct PastelID and the one that belongs to the owner of the art in the following steps
    std::string strThisTicket = ticket.ToStr();
    if (!CPastelID::Verify(reinterpret_cast<const unsigned char *>(strThisTicket.c_str()), strThisTicket.size(),
                           ticket.signature.data(), ticket.signature.size(),
                           ticket.pastelID)) {
        errRet = strprintf("%s ticket's signature is invalid. Error - %s. PastelID - [%s]", thisTicket, errRet, ticket.pastelID);
        return false;
    }
    
    // C.3 check the referred ticket is valid
    // (IsValid of the referred ticket validates signatures as well!)
    if (depth > 0) return true;
    
    std::string err;
    if (!pastelTicket->IsValid(err, false, ++depth))
    {
        errRet = strprintf("The %s ticket with this txid [%s] is invalid - %s", prevTicket, strTnxId, err);
        return false;
    }
    
    return true;
}

// CArtActivateTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ CArtActivateTicket CArtActivateTicket::Create(std::string _regTicketTxId, int _artistHeight, int _storageFee, std::string _pastelID, const SecureString& strKeyPass)
{
    CArtActivateTicket ticket(std::move(_pastelID));
    
    ticket.regTicketTnxId = std::move(_regTicketTxId);
    ticket.artistHeight = _artistHeight;
    ticket.storageFee = _storageFee;
    
    ticket.timestamp = std::time(nullptr);
    
    std::string strTicket = ticket.ToStr();
    ticket.signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(strTicket.c_str()), strTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

std::string CArtActivateTicket::ToStr() const
{
    std::stringstream ss;
    ss << pastelID;
    ss << regTicketTnxId;
    ss << artistHeight;
    ss << storageFee;
    ss << timestamp;
    return ss.str();
}

bool CArtActivateTicket::IsValid(std::string& errRet, bool preReg, int depth) const
{
    int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = chainActive.Height() + 1;
    }
    
    // 0. Common validations
    std::unique_ptr<CPastelTicketBase> pastelTicket;
    TicketID ticketId;
    if  (!common_validation(*this, preReg, regTicketTnxId, pastelTicket, ticketId,
                     [](TicketID tid){return (tid != TicketID::Art);},
                     "Activation", "art",
                     depth,
                     TicketPrice(chainHeight)+(storageFee * 9 / 10), //fee for ticket + 90% of storage fee
                     errRet))
        return false;
    
    // Check the Activation ticket for that Registration ticket is already in the database
    // (ticket transaction replay attack protection)
    CArtActivateTicket existingTicket;
    if (CArtActivateTicket::FindTicketInDb(regTicketTnxId, existingTicket)) {
        if (preReg ||  // if pre reg - this is probably repeating call, so signatures can be the same
            existingTicket.signature != signature ||
            existingTicket.ticketBlock != ticketBlock ||
            existingTicket.ticketTnx != ticketTnx) { // check if this is not the same ticket!!
            errRet = strprintf("The Activation ticket for the Registration ticket with txid [%s] is already exist"
                               "[this ticket block = %d txid = %s; found ticket block  = %d txid = %s]",
                               regTicketTnxId,
                               existingTicket.ticketBlock, existingTicket.ticketTnx, ticketBlock, ticketTnx);
            return false;
        }
    }
    
    auto artTicket = dynamic_cast<CArtRegTicket*>(pastelTicket.get());
    if (artTicket == nullptr)
    {
        errRet = strprintf("The art ticket with this txid [%s] is not in the blockchain or is invalid", regTicketTnxId);
        return false;
    }
    
    // 1. check Artist PastelID in ArtReg ticket matches PastelID from this ticket
    std::string& artistPastelID = artTicket->pastelIDs[CArtRegTicket::artistsign];
    if (artistPastelID != pastelID)
    {
        errRet = strprintf("The PastelID [%s] is not matching the Artist's PastelID [%s] in the Art Reg ticket with this txid [%s]",
                           pastelID, artistPastelID,
                           regTicketTnxId);
        return false;
    }
    
    // 2. check ArtReg ticket is at the assumed height
    if (artTicket->artistHeight != artistHeight)
    {
        errRet = strprintf("The artistHeight [%d] is not matching the artistHeight [%d] in the Art Reg ticket with this txid [%s]",
                           artistHeight, artTicket->artistHeight,
                           regTicketTnxId);
        return false;
    }
    
    // 3. check ArtReg ticket fee is same as storageFee
    if (artTicket->storageFee != storageFee)
    {
        errRet = strprintf("The storage fee [%d] is not matching the storage fee [%d] in the Art Reg ticket with this txid [%s]",
                           storageFee, artTicket->storageFee,
                           regTicketTnxId);
        return false;
    }
    
    return true;
}

CAmount CArtActivateTicket::GetExtraOutputs(std::vector<CTxOut>& outputs) const
{
    auto artTicket = CPastelTicketProcessor::GetTicket<CArtRegTicket>(regTicketTnxId, TicketID::Art);
    
    CAmount nAllAmount = 0;
    CAmount nAllMNFee = storageFee * COIN * 9 / 10; //90%
    CAmount nMainMNFee = nAllMNFee * 3 / 5; //60% of 90%
    CAmount nOtherMNFee = nAllMNFee / 5;    //20% of 90%
    
    for (int mn = CArtRegTicket::mainmnsign; mn<CArtRegTicket::allsigns; mn++) {
        auto mnPastelID = artTicket->pastelIDs[mn];
        CPastelIDRegTicket mnPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(mnPastelID, mnPastelIDticket))
            throw std::runtime_error(strprintf(
                    "The PastelID [%s] from art ticket with this txid [%s] is not in the blockchain or is invalid",
                    mnPastelID, regTicketTnxId));
    
        auto dest = DecodeDestination(mnPastelIDticket.address);
        if (!IsValidDestination(dest))
            throw std::runtime_error(
                    strprintf("The PastelID [%s] from art ticket with this txid [%s] has invalid MN's address",
                              mnPastelID, regTicketTnxId));
    
        CScript scriptPubKey = GetScriptForDestination(dest);
        CAmount nAmount = (mn == CArtRegTicket::mainmnsign? nMainMNFee: nOtherMNFee);
        nAllAmount += nAmount;
    
        CTxOut out(nAmount, scriptPubKey);
        outputs.push_back(out);
    }
    
    return nAllAmount;
}

std::string CArtActivateTicket::ToJSON() const
{
	json jsonObj;
	jsonObj = {
			{"txid", ticketTnx},
			{"height", ticketBlock},
			{"ticket", {
				{"type", TicketName()},
				{"pastelID", pastelID},
				{"reg_txid", regTicketTnxId},
				{"artist_height", artistHeight},
                {"storage_fee", storageFee},
				{"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
		 }}
	};
	
	return jsonObj.dump(4);
}

/*static*/ bool CArtActivateTicket::FindTicketInDb(const std::string& key, CArtActivateTicket& ticket)
{
    ticket.regTicketTnxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

/*static*/ bool CArtActivateTicket::CheckTicketExistByArtTicketID(const std::string& regTicketTnxId)
{
    CArtActivateTicket ticket;
    ticket.regTicketTnxId = regTicketTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

/*static*/ std::vector<CArtActivateTicket> CArtActivateTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtActivateTicket>(TicketID::Activate, pastelID);
}
/*static*/ std::vector<CArtActivateTicket> CArtActivateTicket::FindAllTicketByArtistHeight(int height)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtActivateTicket>(TicketID::Activate, std::to_string(height));
}

// Art Trade Tickets ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CArtSellTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ CArtSellTicket CArtSellTicket::Create(std::string _artTnxId, int _askedPrice, int _validAfter, int _validBefore, int _copy_number, std::string _pastelID, const SecureString& strKeyPass)
{
    CArtSellTicket ticket(std::move(_pastelID));
    
    ticket.artTnxId = std::move(_artTnxId);
    ticket.askedPrice = _askedPrice;
    ticket.activeBefore = _validBefore;
    ticket.activeAfter = _validAfter;
    
    ticket.timestamp = std::time(nullptr);
    
    //NOTE: Sell ticket for Trade ticket will always has copyNumber = 1
    ticket.copyNumber = _copy_number > 0? _copy_number: CArtSellTicket::FindAllTicketByArtTnxID(ticket.artTnxId).size() + 1;
    ticket.key = ticket.artTnxId + ":" + to_string(ticket.copyNumber);
    
    std::string strTicket = ticket.ToStr();
    ticket.signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(strTicket.c_str()), strTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

std::string CArtSellTicket::ToStr() const
{
    std::stringstream ss;
    ss << pastelID;
    ss << artTnxId;
    ss << askedPrice;
    ss << copyNumber;
    ss << activeBefore;
    ss << activeAfter;
    ss << timestamp;
    return ss.str();
}

bool CArtSellTicket::IsValid(std::string& errRet, bool preReg, int depth) const
{
    int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = chainActive.Height() + 1;
    }
    
    // 0. Common validations
    std::unique_ptr<CPastelTicketBase> pastelTicket;
    TicketID ticketId;
    if  (!common_validation(*this, preReg, artTnxId, pastelTicket, ticketId,
                     [](TicketID tid){return (tid != TicketID::Activate && tid != TicketID::Trade);},
                     "Sell", "activation or trade",
                     depth,
                     TicketPrice(chainHeight),
                     errRet))
        return false;

    bool ticketFound = false;
    CArtSellTicket existingTicket;
    if (CArtSellTicket::FindTicketInDb(KeyOne(), existingTicket)) {
        if (existingTicket.signature == signature &&
            existingTicket.ticketBlock == ticketBlock &&
            existingTicket.ticketTnx == ticketTnx) { // if this ticket is already in the DB
            ticketFound = true;
        }
    }
    
    //1. check PastelID in this ticket matches PastelID in the referred ticket (Activation or Trade)
    //2. Verify the art is not already sold
    auto existingTradeTickets = CArtTradeTicket::FindAllTicketByArtTnxID(artTnxId);
    auto soldCopies = existingTradeTickets.size();
    auto existingSellTickets = CArtSellTicket::FindAllTicketByArtTnxID(artTnxId);
    auto sellTicketsNumber = existingSellTickets.size();
    auto totalCopies = 0;
    if (ticketId == TicketID::Activate) {
        // 1.a
        auto actTicket = dynamic_cast<CArtActivateTicket*>(pastelTicket.get());
        if (actTicket == nullptr) {
            errRet = strprintf("The activation ticket with this txid [%s] referred by this sell ticket is invalid", artTnxId);
            return false;
        }
        std::string& artistPastelID = actTicket->pastelID;
        if (artistPastelID != pastelID)
        {
            errRet = strprintf("The PastelID [%s] in this ticket is not matching the Artist's PastelID [%s] in the Art Activation ticket with this txid [%s]",
                               pastelID, artistPastelID,
                               artTnxId);
            return false;
        }
        //  Get ticket pointed by artTnxId. Here, this is an Activation ticket
        auto artTicket = CPastelTicketProcessor::GetTicket<CArtRegTicket>(actTicket->regTicketTnxId, TicketID::Art);
        totalCopies = artTicket->totalCopies;
    
        if (preReg || !ticketFound) {//else if this is already confirmed ticket - skip this check, otherwise it will failed
            // 2.a Verify the number of existing trade tickets less then number of copies in the registration ticket
            if (soldCopies >= artTicket->totalCopies) {
                errRet = strprintf(
                        "The Art you are trying to sell - from registration ticket [%s] - is already sold - there are already [%d] trade tickets, but only [%d] copies were available",
                        artTnxId, soldCopies, artTicket->totalCopies);
                return false;
            }
        }
    }
    else if (ticketId == TicketID::Trade) {
        // 1.b
        auto tradeTicket = dynamic_cast<CArtTradeTicket*>(pastelTicket.get());
        if (tradeTicket == nullptr) {
            errRet = strprintf("The trade ticket with this txid [%s] referred by this sell ticket is invalid", artTnxId);
            return false;
        }
        std::string& ownersPastelID = tradeTicket->pastelID;
        if (ownersPastelID != pastelID)
        {
            errRet = strprintf("The PastelID [%s] in this ticket is not matching the PastelID [%s] in the Trade ticket with this txid [%s]",
                               pastelID, ownersPastelID,
                               artTnxId);
            return false;
        }
        // 3.b Verify there is no already trade ticket referring to that trade ticket
        if (preReg || !ticketFound) {//else if this is already confirmed ticket - skip this check, otherwise it will failed
            if (soldCopies > 0) {
                errRet = strprintf(
                        "The Art you are trying to sell - from trade ticket [%s] - is already sold - see trade ticket with txid [%s]",
                        artTnxId, existingTradeTickets[0].ticketTnx);
                return false;
            }
        }
        totalCopies = 1;
    }
    
    if (copyNumber > totalCopies || copyNumber <= 0){
        errRet = strprintf(
                "Invalid Sell ticket - copy number [%d] cannot exceed the total number of available copies [%d] or be <= 0",
                copyNumber, totalCopies);
        return false;
    }
    
    //4. If this is replacement - verify that it is allowed (original ticket is not sold)
    // (ticket transaction replay attack protection)
    // If found similar ticket, replacement is possible if allowed
    auto it = find_if(existingSellTickets.begin(), existingSellTickets.end(),
                          [&](const CArtSellTicket& st) {
                            return (st.copyNumber == copyNumber &&
                                    st.ticketBlock != ticketBlock &&
                                    st.ticketTnx != ticketTnx); //skip ourself!
                          });
    if (it != existingSellTickets.end()){
        if (CArtTradeTicket::CheckTradeTicketExistBySellTicket(it->ticketTnx)) {
            errRet = strprintf(
                    "Cannot replace Sell ticket - it has been already sold. txid - [%s] copyNumber [%d].",
                    it->ticketTnx, copyNumber);
            return false;
        }
    
        if (masterNodeCtrl.masternodeSync.IsSynced()) { // Validate only if both blockchain and MNs are synced
            int chainHeight = 0;
            {
                LOCK(cs_main);
                chainHeight = chainActive.Height() + 1;
            }
    
            if (it->ticketBlock + 28800 < chainHeight) {//1 block per 2.5; 4 blocks per 10 min; 24 blocks per 1h; 576 blocks per 24 h;
                errRet = strprintf(
                        "Can only replace Sell ticket after 5 days. txid - [%s] copyNumber [%d].",
                        it->ticketTnx, copyNumber);
                return false;
            }
        }
    }
    
    return true;
}

std::string CArtSellTicket::ToJSON() const
{
    json jsonObj;
    jsonObj = {
            {"txid", ticketTnx},
            {"height", ticketBlock},
            {"ticket", {
                             {"type", TicketName()},
                             {"pastelID", pastelID},
                             {"art_txid", artTnxId},
                             {"copy_number", copyNumber},
                             {"asked_price", askedPrice},
                             {"valid_after", activeAfter},
                             {"valid_before", activeBefore},
                             {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
                     }}
    };
    return jsonObj.dump(4);
}

/*static*/ bool CArtSellTicket::FindTicketInDb(const std::string& key, CArtSellTicket& ticket)
{
    ticket.key = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

/*static*/ std::vector<CArtSellTicket> CArtSellTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtSellTicket>(TicketID::Sell, pastelID);
}

/*static*/ std::vector<CArtSellTicket> CArtSellTicket::FindAllTicketByArtTnxID(const std::string& artTnxId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtSellTicket>(TicketID::Sell, artTnxId);
}

// CArtBuyTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ CArtBuyTicket CArtBuyTicket::Create(std::string _sellTnxId, int _price, std::string _pastelID, const SecureString& strKeyPass)
{
    CArtBuyTicket ticket(std::move(_pastelID));
    
    ticket.sellTnxId = std::move(_sellTnxId);
    ticket.price = _price;
    
    ticket.timestamp = std::time(nullptr);
    
    string strTicket = ticket.ToStr();
    ticket.signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(strTicket.c_str()), strTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

std::string CArtBuyTicket::ToStr() const
{
    std::stringstream ss;
    ss << pastelID;
    ss << sellTnxId;
    ss << price;
    ss << timestamp;
    return ss.str();
}

bool CArtBuyTicket::IsValid(std::string& errRet, bool preReg, int depth) const
{
    int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = chainActive.Height() + 1;
    }

    // 0. Common validations
    std::unique_ptr<CPastelTicketBase> pastelTicket;
    TicketID ticketId;
    if  (!common_validation(*this, preReg, sellTnxId, pastelTicket, ticketId,
                            [](TicketID tid){return (tid != TicketID::Sell);},
                            "Buy", "sell",
                            depth,
                            price+TicketPrice(chainHeight),
                            errRet))
        return false;
    
    // 1. Verify that there is no another buy ticket for the same sell ticket
    // or if there are, it is older then 1h and there is no trade ticket for it
    //buyTicket->ticketBlock <= height+24 (2.5m per block -> 24blocks/per hour) - MaxBuyTicketAge
    CArtBuyTicket existingBuyTicket;
    if (CArtBuyTicket::FindTicketInDb(sellTnxId, existingBuyTicket)) {
    
        if (preReg) {// if pre reg - this is probably repeating call, so signatures can be the same
            errRet = strprintf("Buy ticket [%s] already exists for this sell ticket [%s]",
                               existingBuyTicket.ticketTnx, sellTnxId);
            return false;
        }
    
        // (ticket transaction replay attack protection)
        // though the similar transaction will be allowed if existing Buy ticket has expired
        if (existingBuyTicket.signature != signature ||
            existingBuyTicket.ticketBlock != ticketBlock ||
            existingBuyTicket.ticketTnx != ticketTnx) {
            //check age
            if (existingBuyTicket.ticketBlock + masterNodeCtrl.MaxBuyTicketAge <= chainHeight) {
                errRet = strprintf("Buy ticket [%s] already exists and is not yet 1h old for this sell ticket [%s]"
                                   "[this ticket block = %d txid = %s; found ticket block  = %d txid = %s]",
                                   existingBuyTicket.ticketTnx, sellTnxId,
                                   existingBuyTicket.ticketBlock, existingBuyTicket.ticketTnx, ticketBlock, ticketTnx);
                return false;
            }
    
            //check trade ticket
            if (CArtTradeTicket::CheckTradeTicketExistByBuyTicket(existingBuyTicket.ticketTnx)) {
                errRet = strprintf("The sell ticket you are trying to buy [%s] is already sold", sellTnxId);
                return false;
            }
        }
    }
    
    auto sellTicket = dynamic_cast<CArtSellTicket*>(pastelTicket.get());
    if (sellTicket == nullptr) {
        errRet = strprintf("The sell ticket with this txid [%s] referred by this buy ticket is invalid", sellTnxId);
        return false;
    }

    // 2. Verify Sell ticket is already or still active
    int height = (preReg || ticketBlock == 0)? chainHeight: ticketBlock;
    
    if (height < sellTicket->activeAfter) {
        errRet = strprintf("Sell ticket [%s] is only active after [%d] block height (Buy ticket block is [%d])", sellTicket->ticketTnx, sellTicket->activeAfter, height);
        return false;
    }
    if (sellTicket->activeBefore > 0 && sellTicket->activeBefore < height) {
        errRet = strprintf("Sell ticket [%s] is only active before [%d] block height (Buy ticket block is [%d])", sellTicket->ticketTnx, sellTicket->activeBefore, height);
        return false;
    }

    // 3. Verify that the price is correct
    if (price < sellTicket->askedPrice){
        errRet = strprintf("The offered price [%d] is less than asked in the sell ticket [%d]", price, sellTicket->askedPrice);
        return false;
    }
    
    return true;
}

std::string CArtBuyTicket::ToJSON() const
{
    json jsonObj;
    jsonObj = {
            {"txid", ticketTnx},
            {"height", ticketBlock},
            {"ticket", {
                             {"type", TicketName()},
                             {"pastelID", pastelID},
                             {"sell_txid", sellTnxId},
                             {"price", price},
                             {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
                     }}
    };
    return jsonObj.dump(4);
}

/*static*/ bool CArtBuyTicket::FindTicketInDb(const std::string& key, CArtBuyTicket& ticket)
{
    ticket.sellTnxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

/*static*/ bool CArtBuyTicket::CheckBuyTicketExistBySellTicket(const std::string& _sellTnxId)
{
    CArtBuyTicket _ticket;
    _ticket.sellTnxId = _sellTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket);
}

/*static*/ std::vector<CArtBuyTicket> CArtBuyTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtBuyTicket>(TicketID::Buy, pastelID);
}

// CArtTradeTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ CArtTradeTicket CArtTradeTicket::Create(std::string _sellTnxId, std::string _buyTnxId, std::string _pastelID, const SecureString& strKeyPass)
{
    CArtTradeTicket ticket(std::move(_pastelID));
    
    ticket.sellTnxId = std::move(_sellTnxId);
    ticket.buyTnxId = std::move(_buyTnxId);
    
    auto sellTicket = CPastelTicketProcessor::GetTicket<CArtSellTicket>(ticket.sellTnxId, TicketID::Sell);

    ticket.artTnxId = sellTicket->artTnxId;
    ticket.price = sellTicket->askedPrice;

    ticket.timestamp = std::time(nullptr);
    
    std::string strTicket = ticket.ToStr();
    ticket.signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(strTicket.c_str()), strTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

std::string CArtTradeTicket::ToStr() const
{
    std::stringstream ss;
    ss << pastelID;
    ss << sellTnxId;
    ss << buyTnxId;
    ss << artTnxId;
    ss << timestamp;
    return ss.str();
}

bool CArtTradeTicket::IsValid(std::string& errRet, bool preReg, int depth) const
{
    int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = chainActive.Height() + 1;
    }
    
    // 0. Common validations
    std::unique_ptr<CPastelTicketBase> sellTicket;
    TicketID sellTicketId;
    if  (!common_validation(*this, preReg, sellTnxId, sellTicket, sellTicketId,
                            [](TicketID tid){return (tid != TicketID::Sell);},
                            "Trade", "sell",
                            depth,
                            price+TicketPrice(chainHeight),
                            errRet))
        return false;
    
    std::unique_ptr<CPastelTicketBase> buyTicket;
    TicketID buyTicketId;
    if  (!common_validation(*this, preReg, buyTnxId, buyTicket, buyTicketId,
                            [](TicketID tid){return (tid != TicketID::Buy);},
                            "Trade", "buy",
                            depth,
                            price+TicketPrice(chainHeight),
                            errRet))
        return false;
    
    // 1. Verify that there is no another Trade ticket for the same Sell ticket
    CArtTradeTicket _tradeTicket;
    if (CArtTradeTicket::GetTradeTicketBySellTicket(sellTnxId, _tradeTicket)){
        // (ticket transaction replay attack protection)
        if (signature != _tradeTicket.signature ||
            ticketTnx != _tradeTicket.ticketTnx ||
            ticketBlock != _tradeTicket.ticketBlock) {
            errRet = strprintf("There is already exist trade ticket for the sell ticket with this txid [%s]. Signature - our=%s; their=%s"
                               "[this ticket block = %d txid = %s; found ticket block  = %d txid = %s]",
                               sellTnxId,
                               ed_crypto::Hex_Encode(signature.data(), signature.size()),
                               ed_crypto::Hex_Encode(_tradeTicket.signature.data(), _tradeTicket.signature.size()),
                               _tradeTicket.ticketBlock, _tradeTicket.ticketTnx, ticketBlock, ticketTnx);
            return false;
        }
    }
    // 1. Verify that there is no another Trade ticket for the same Buy ticket
    _tradeTicket.sellTnxId = "";
    if (CArtTradeTicket::GetTradeTicketByBuyTicket(buyTnxId, _tradeTicket)){
        //Compare signatures to skip if the same ticket
        if (signature != _tradeTicket.signature || ticketTnx != _tradeTicket.ticketTnx || ticketBlock != _tradeTicket.ticketBlock) {
            errRet = strprintf("There is already exist trade ticket for the buy ticket with this txid [%s]", buyTnxId);
            return false;
        }
    }
    
    // 2. Verify Trade ticket PastelID is the same as in Buy Ticket
    auto buyTicketReal = dynamic_cast<CArtBuyTicket*>(buyTicket.get());
    if (buyTicketReal == nullptr) {
        errRet = strprintf("The buy ticket with this txid [%s] referred by this trade ticket is invalid", buyTnxId);
        return false;
    }
    std::string& buyersPastelID = buyTicketReal->pastelID;
    if (buyersPastelID != pastelID)
    {
        errRet = strprintf("The PastelID [%s] in this Trade ticket is not matching the PastelID [%s] in the Buy ticket with this txid [%s]",
                           pastelID, buyersPastelID,
                           buyTnxId);
        return false;
    }
    
    return true;
}

CAmount CArtTradeTicket::GetExtraOutputs(std::vector<CTxOut>& outputs) const
{
    auto artTicket = CPastelTicketProcessor::GetTicket<CArtSellTicket>(sellTnxId, TicketID::Sell);
    
    CAmount nPriceAmount = artTicket->askedPrice * COIN;
    
    auto sellerPastelID = artTicket->pastelID;
    CPastelIDRegTicket sellerPastelIDticket;
    if (!CPastelIDRegTicket::FindTicketInDb(sellerPastelID, sellerPastelIDticket))
        throw std::runtime_error(strprintf(
                "The PastelID [%s] from sell ticket with this txid [%s] is not in the blockchain or is invalid",
                sellerPastelID, sellTnxId));
    
    auto dest = DecodeDestination(sellerPastelIDticket.address);
    if (!IsValidDestination(dest))
        throw std::runtime_error(
                strprintf("The PastelID [%s] from sell ticket with this txid [%s] has invalid address",
                          sellerPastelID, sellTnxId));
    
    CScript scriptPubKey = GetScriptForDestination(dest);
    
    CTxOut out(nPriceAmount, scriptPubKey);
    outputs.push_back(out);
    
    return nPriceAmount;
}

std::string CArtTradeTicket::ToJSON() const
{
    json jsonObj;
    jsonObj = {
            {"txid", ticketTnx},
            {"height", ticketBlock},
            {"ticket", {
                             {"type", TicketName()},
                             {"pastelID", pastelID},
                             {"sell_txid", sellTnxId},
                             {"buy_txid", buyTnxId},
                             {"art_txid", artTnxId},
                             {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
                     }}
    };
    return jsonObj.dump(4);
}

/*static*/ bool CArtTradeTicket::FindTicketInDb(const std::string& key, CArtTradeTicket& ticket)
{
    ticket.sellTnxId = key;
    ticket.buyTnxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket);
}

/*static*/ std::vector<CArtTradeTicket> CArtTradeTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtTradeTicket>(TicketID::Trade, pastelID);
}
/*static*/ std::vector<CArtTradeTicket> CArtTradeTicket::FindAllTicketByArtTnxID(const std::string& artTnxID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtTradeTicket>(TicketID::Trade, artTnxID);
}

/*static*/ bool CArtTradeTicket::CheckTradeTicketExistBySellTicket(const std::string& _sellTnxId)
{
    CArtTradeTicket _ticket;
    _ticket.sellTnxId = _sellTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket);
}
/*static*/ bool CArtTradeTicket::CheckTradeTicketExistByBuyTicket(const std::string& _buyTnxId)
{
    CArtTradeTicket _ticket;
    _ticket.buyTnxId = _buyTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(_ticket);
}

/*static*/ bool CArtTradeTicket::GetTradeTicketBySellTicket(const std::string& _sellTnxId, CArtTradeTicket& ticket)
{
    ticket.sellTnxId = _sellTnxId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}
/*static*/ bool CArtTradeTicket::GetTradeTicketByBuyTicket(const std::string& _buyTnxId, CArtTradeTicket& ticket)
{
    ticket.buyTnxId = _buyTnxId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

// CTakeDownTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*static*/ bool CTakeDownTicket::FindTicketInDb(const std::string& key, CTakeDownTicket& ticket)
{
    return false;
}


#ifdef FAKE_TICKET
template<class T>
std::string CPastelTicketProcessor::CreateFakeTransaction(T& ticket, CAmount ticketPrice, const std::vector<std::pair<std::string, CAmount>>& extraPayments, const std::string& strVerb, bool bSend)
{
    std::string error;
    
    if (ticket.ID() == TicketID::PastelID) {
        if (strVerb == "1") {
            auto t = (CPastelIDRegTicket *) &ticket;
            t->pslid_signature.clear();
        } else if (strVerb == "2") {
            auto t = (CPastelIDRegTicket *) &ticket;
            t->mn_signature.clear();
        } else if (strVerb == "3") {
            auto t = (CPastelIDRegTicket *) &ticket;
            t->outpoint.SetNull();
        }
    } else if (ticket.ID() == TicketID::Art) {
        if (strVerb == "1") {
            auto t = (CArtRegTicket *) &ticket;
            t->ticketSignatures[CArtRegTicket::mn2sign].clear();
            t->ticketSignatures[CArtRegTicket::mn3sign].clear();
        }
    } else if (ticket.ID() == TicketID::Activate) {
        if (strVerb == "1") {
            auto t = (CArtActivateTicket *) &ticket;
            t->signature.clear();
        }
        if (strVerb == "2") {
            auto t = (CArtActivateTicket *) &ticket;
            t->artistHeight = 1;
        }
    } else if (ticket.ID() == TicketID::Sell) {
        if (strVerb == "1") {
            auto t = (CArtSellTicket *) &ticket;
            t->signature.clear();
        }
    } else if (ticket.ID() == TicketID::Buy) {
        ;
    } else if (ticket.ID() == TicketID::Trade) {
        ;
    }
    
    std::vector<CTxOut> extraOutputs;
    CAmount extraAmount = 0;
    if (!extraPayments.empty()) {
        for (auto& p : extraPayments) {
            auto dest = DecodeDestination(p.first);
            if (!IsValidDestination(dest)) return std::string{};
            extraOutputs.emplace_back(CTxOut {p.second, GetScriptForDestination(dest)});
            extraAmount += p.second;
        }
    }
    
    CDataStream data_stream(SER_NETWORK, TICKETS_VERSION);
    data_stream << (uint8_t)ticket.ID();
    data_stream << ticket;
    
    CMutableTransaction tx;
    if (!CPastelTicketProcessor::CreateP2FMSTransactionWithExtra(data_stream, extraOutputs, extraAmount, tx, ticketPrice, error)){
        throw std::runtime_error(strprintf("Failed to create P2FMS from data provided - %s", error));
    }

    if (bSend) {
        if (!CPastelTicketProcessor::StoreP2FMSTransaction(tx, error)) {
            throw std::runtime_error(strprintf("Failed to send P2FMS transaction - %s", error));
        }
        return tx.GetHash().GetHex();
    }
    else
        return EncodeHexTx(tx);
}
template std::string CPastelTicketProcessor::CreateFakeTransaction<CPastelIDRegTicket>(CPastelIDRegTicket&,CAmount,const std::vector<std::pair<std::string, CAmount>>&,const std::string&,bool);
template std::string CPastelTicketProcessor::CreateFakeTransaction<CArtRegTicket>(CArtRegTicket&,CAmount,const std::vector<std::pair<std::string, CAmount>>&,const std::string&,bool);
template std::string CPastelTicketProcessor::CreateFakeTransaction<CArtActivateTicket>(CArtActivateTicket&,CAmount,const std::vector<std::pair<std::string, CAmount>>&,const std::string&,bool);
template std::string CPastelTicketProcessor::CreateFakeTransaction<CArtSellTicket>(CArtSellTicket&,CAmount,const std::vector<std::pair<std::string, CAmount>>&,const std::string&,bool);
template std::string CPastelTicketProcessor::CreateFakeTransaction<CArtBuyTicket>(CArtBuyTicket&,CAmount,const std::vector<std::pair<std::string, CAmount>>&,const std::string&,bool);
template std::string CPastelTicketProcessor::CreateFakeTransaction<CArtTradeTicket>(CArtTradeTicket&,CAmount,const std::vector<std::pair<std::string, CAmount>>&,const std::string&,bool);
#endif