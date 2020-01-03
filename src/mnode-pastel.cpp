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
	nTotalCache = std::min(nTotalCache, nMaxDbCache << 20); // total cache cannot be greated than nMaxDbcache
	uint64_t nTicketDBCache = nTotalCache / 8 / uint8_t(TicketID::COUNT);
	
	dbs[TicketID::PastelID] = std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "pslids", nTicketDBCache, false, fReindex));
	dbs[TicketID::Art] 		= std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "artreg", nTicketDBCache, false, fReindex));
	dbs[TicketID::Activate] 	= std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "artcnf", nTicketDBCache, false, fReindex));
	dbs[TicketID::Trade] 	= std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "arttrd", nTicketDBCache, false, fReindex));
	dbs[TicketID::Down] 	= std::unique_ptr<CDBWrapper>(new CDBWrapper(GetDataDir() / "tickets" / "takedn", nTicketDBCache, false, fReindex));
}

void CPastelTicketProcessor::UpdatedBlockTip(const CBlockIndex *pindex, bool fInitialDownload)
{
	if(!pindex) return;
	
	if (fInitialDownload){
		//??
	}
	
	CBlock block;
	if(!ReadBlockFromDisk(block, pindex)) {
		LogPrintf("CPastelTicket::UpdatedBlockTip -- ERROR: Can't read block from disk\n");
		return;
	}

	for(const CTransaction& tx : block.vtx)
	{
		CMutableTransaction mtx(tx);
		ParseTicketAndUpdateDB(mtx, pindex->nHeight);
	}
}

template<class T>
bool CPastelTicketProcessor::UpdateDB(T& ticket, std::string txid, int nBlockHeight)
{
	if (!txid.empty()) ticket.ticketTnx = std::move(txid);
	if (nBlockHeight != 0) ticket.ticketBlock = nBlockHeight;
	dbs[ticket.ID()]->Write(ticket.KeyOne(), ticket);
	if (ticket.HasKeyTwo()) {
        dbs[ticket.ID()]->Write(ticket.KeyTwo(), ticket.KeyOne());
    }
    if (ticket.HasMultivalueKey()) {
//        dbs[ticket.ID()]->Write(ticket.KeyTwo(), ticket.KeyOne());
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

bool CPastelTicketProcessor::ValidateIfTicketTransaction(const CTransaction& tx)
{
    CMutableTransaction mtx(tx);
    
    std::string error_ret;
    CDataStream data_stream(SER_NETWORK, TICKETS_VERSION);
    TicketID ticket_id;
    
    if (!preParseTicket(tx, data_stream, ticket_id, error_ret, false))
        return true; // this is not a ticket
    
    CAmount storageFee = 0;
    
    //this is ticket and it need to be validated
    bool ok = false;
    try {
        if (ticket_id == TicketID::PastelID) {
            CPastelIDRegTicket ticket;
            data_stream >> ticket;
            ok = ticket.IsValid(false, error_ret);
        }
        else if (ticket_id == TicketID::Art) {
            CArtRegTicket ticket;
            data_stream >> ticket;
            ok = ticket.IsValid(false, error_ret);
        }
        else if (ticket_id == TicketID::Activate) {
            CArtActivateTicket ticket;
            data_stream >> ticket;
            ok = ticket.IsValid(false, error_ret);
            storageFee = ticket.storageFee;
        }
//		else if (ticket_id == TicketID::Trade) {
//            CArtTradeTicket ticket;
//            data_stream >> ticket;
//            return ticket.IsValid(true, error_ret);
//		}
//		else if (ticket_id == TicketID::Down) {
//            CTakeDownTicket ticket;
//            data_stream >> ticket;
//            return ticket.IsValid(true, error_ret);
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
        CAmount expectedTicketFee = GetTicketPrice(ticket_id) * COIN;
    
        CAmount allMNFee = storageFee * COIN * 9 / 10;
        CAmount mn1Fee = allMNFee * 3 / 5;
        CAmount mn23Fee = allMNFee / 5;
    
        for (int i=0; i<num; i++){
            if ((ticket_id == TicketID::PastelID ||
                 ticket_id == TicketID::Art) &&
                i==num-1) //in this tickets last output is change
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
            ticketFee += tx.vout[i].nValue;
        }
    
        if (expectedTicketFee != ticketFee) {
            ok = false;
            error_ret = strprintf("Wrong ticket fee: expected - %d, real - %d", expectedTicketFee, ticketFee);
        }
    }
    
    if (!ok)
        LogPrintf("CPastelTicketProcessor::ParseTicketAndUpdateDB -- Invalid ticket [ticket_id=%d, txid=%s]. ERROR: %s\n", (int)ticket_id, tx.GetHash().GetHex(), error_ret);
    
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
//		}
//		else if (ticket_id == TicketID::Trade) {
//            CArtTradeTicket ticket;
//            data_stream >> ticket;
//            return UpdateDB<CArtTradeTicket>(ticket, txid, nBlockHeight);
//		}
//		else if (ticket_id == TicketID::Down) {
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
	
    LogPrintf("CPastelTicketProcessor::ParseTicketAndUpdateDB -- Invalid ticket [ticket_id=%d, txid=%s]. ERROR: %s\n", (int)ticket_id, tx.GetHash().GetHex(), error_ret);
	
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
//		}
//		if (ticketId == TicketID::Trade) {
//			  Trade ticket;
//            data_stream >> ticket;
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
bool CPastelTicketProcessor::CheckTicketExist(const T& ticket)
{
	auto key = ticket.KeyOne();
	return dbs[ticket.ID()]->Exists(key);
}
template bool CPastelTicketProcessor::CheckTicketExist<CPastelIDRegTicket>(const CPastelIDRegTicket&);
template bool CPastelTicketProcessor::CheckTicketExist<CArtRegTicket>(const CArtRegTicket&);
template bool CPastelTicketProcessor::CheckTicketExist<CArtActivateTicket>(const CArtActivateTicket&);

template<class T>
bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey(const T& ticket)
{
    if (ticket.HasKeyTwo()) {
        decltype(ticket.KeyOne()) mainKey;
        if (dbs[ticket.ID()]->Read(ticket.KeyTwo(), mainKey))
            return dbs[ticket.ID()]->Exists(mainKey);
    }
	return false;
}
template bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey<CPastelIDRegTicket>(const CPastelIDRegTicket&);
template bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey<CArtRegTicket>(const CArtRegTicket&);
template bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey<CArtActivateTicket>(const CArtActivateTicket&);

template<class T>
bool CPastelTicketProcessor::FindTicket(T& ticket)
{
	auto key = ticket.KeyOne();
	return dbs[ticket.ID()]->Read(key, ticket);
}
template bool CPastelTicketProcessor::FindTicket<CPastelIDRegTicket>(CPastelIDRegTicket&);
template bool CPastelTicketProcessor::FindTicket<CArtRegTicket>(CArtRegTicket&);
template bool CPastelTicketProcessor::FindTicket<CArtActivateTicket>(CArtActivateTicket&);

template<class T>
bool CPastelTicketProcessor::FindTicketBySecondaryKey(T& ticket)
{
    if (ticket.HasKeyTwo()) {
        decltype(ticket.KeyOne()) mainKey;
        if (dbs[ticket.ID()]->Read(ticket.KeyTwo(), mainKey))
            return dbs[ticket.ID()]->Read(mainKey, ticket);
    }
	return false;
}
template bool CPastelTicketProcessor::FindTicketBySecondaryKey<CPastelIDRegTicket>(CPastelIDRegTicket&);
template bool CPastelTicketProcessor::FindTicketBySecondaryKey<CArtRegTicket>(CArtRegTicket&);
template bool CPastelTicketProcessor::FindTicketBySecondaryKey<CArtActivateTicket>(CArtActivateTicket&);

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

template<class T>
std::string CPastelTicketProcessor::SendTicket(const T& ticket)
{
    std::string error;

    if (!ticket.IsValid(true, error)) {
        throw std::runtime_error(strprintf("Ticket (%s) is invalid - %s", ticket.TicketName(), error));
    }
    
    std::vector<CTxOut> extraOutputs;
    CAmount extraAmount = ticket.GetExtraOutputs(extraOutputs);
    
    CDataStream data_stream(SER_NETWORK, TICKETS_VERSION);
    data_stream << (uint8_t)ticket.ID();
    data_stream << ticket;
	
    CMutableTransaction tx;
	if (!CPastelTicketProcessor::CreateP2FMSTransactionWithExtra(data_stream, extraOutputs, extraAmount, tx, GetTicketPrice(ticket.ID()), error)){
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

CAmount CPastelTicketProcessor::GetTicketPrice(TicketID tid)
{
	switch(tid)
	{
		case TicketID::PastelID: 	return 10;
		case TicketID::Art: 		return 10;
		case TicketID::Activate: 	return 10;
		case TicketID::Trade: 		return 1000;
		case TicketID::Down: 		return 1000;
	};
	return 0;
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

bool CPastelIDRegTicket::IsValid(bool preReg, std::string& errRet) const
{
    if (preReg) { // Something to check ONLY before ticket made into transaction
        //1. check that activation ticket for the regTicketTnxId is not already in the blockchain
        
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
            // 1. check if TicketDB already has PatelID with the same outpoint, and if yes, reject if it has different signature
            CPastelIDRegTicket _ticket;
            _ticket.outpoint = outpoint;
            if (masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(_ticket)){
                if (_ticket.mn_signature != mn_signature){
                    errRet = strprintf("Masternode's outpoint - [%s] is already registered as a ticket. Your PastelID - [%s]", outpoint.ToStringShort(), pastelID);
                    return false;
                }
            }
            
            // 2. Check outpoint belongs to active MN
            CMasternode mnInfo;
            if (!masterNodeCtrl.masternodeManager.Get(outpoint, mnInfo)){
                errRet = strprintf("Unknown Masternode - [%s]. PastelID - [%s]", outpoint.ToStringShort(), pastelID);
                return false;
            }
            if (!mnInfo.IsEnabled()){
                errRet = strprintf("Non an active Masternode - [%s]. PastelID - [%s]", outpoint.ToStringShort(), pastelID);
                return false;
            }
            
            // 3. Validate MN signature using public key of MN identified by outpoint
            if (!CMessageSigner::VerifyMessage(mnInfo.pubKeyMasternode, mn_signature, ss.str(), errRet)) {
                errRet = strprintf("Ticket's MN signature is invalid. Error - %s. Outpoint - [%s]; PastelID - [%s]", errRet, outpoint.ToStringShort(), pastelID);
                return false;
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

std::string CPastelIDRegTicket::ToJSON()
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

// CArtRegTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ CArtRegTicket CArtRegTicket::Create(std::string _ticket, const std::string& signatures,
        std::string _pastelID, const SecureString& strKeyPass,
        std::string _keyOne, std::string _keyTwo,
        int _artistHeight, CAmount _storageFee)
{
    CArtRegTicket ticket(std::move(_ticket));
    
    ticket.keyOne = std::move(_keyOne);
    ticket.keyTwo = std::move(_keyTwo);
    ticket.artistHeight = _artistHeight;
    ticket.storageFee = _storageFee;
    
    ticket.pastelIDs[mainmnsign] = std::move(_pastelID);
    
    //signature of ticket hash
    ticket.ticketSignatures[mainmnsign] = CPastelID::Sign(reinterpret_cast<const unsigned char*>(ticket.artTicket.c_str()), ticket.artTicket.size(),
                                                            ticket.pastelIDs[mainmnsign], strKeyPass);

    //other signatures
    auto jsonObj = json::parse(signatures);
    
    if (jsonObj.size() != 3){
        throw std::runtime_error("Signatures json is incorrect");
    }
    
    for (auto& el : jsonObj.items()) {
        
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
    
    return ticket;
}

bool CArtRegTicket::IsValid(bool preReg, std::string& errRet) const
{
    if (preReg){
        // Something to check ONLY before ticket made into transaction
        //1. check that activation ticket for the regTicketTnxId is not already in the blockchain
        
        if (CheckIfTicketInDb(keyOne)) {
            errRet = strprintf("The art with this key - [%s] is already registered in blockchain", keyOne);
            return false;
        }
        if (CheckIfTicketInDb(keyTwo)) {
            errRet = strprintf("The art with this secondary key - [%s] is already registered in blockchain", keyTwo);
            return false;
        }

        //TODO Pastel: validate that address has coins to pay for registration - 10PSL + fee
        // ...

    }
    
    // Something to always validate
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
        if (!pastelIdRegTicket.IsValid(false, err)){
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

std::string CArtRegTicket::ToJSON()
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

// CArtActivateTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*static*/ CArtActivateTicket CArtActivateTicket::Create(std::string _regTicketTxId, int _artistHeight, int _storageFee, std::string _pastelID, const SecureString& strKeyPass)
{
    CArtActivateTicket ticket(std::move(_pastelID));
    
    ticket.regTicketTnxId = std::move(_regTicketTxId);
    ticket.artistHeight = _artistHeight;
    ticket.storageFee = _storageFee;
    
    
    std::stringstream ss;
    ss << ticket.pastelID;
    ss << ticket.regTicketTnxId;
    ss << ticket.artistHeight;
    ss << ticket.storageFee;
    std::string strTicket = ss.str();
    ticket.signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(strTicket.c_str()), strTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

bool CArtActivateTicket::IsValid(bool preReg, std::string& errRet) const
{
    if (!masterNodeCtrl.masternodeSync.IsSynced()) //Activation ticket references other blocks, cannot validate if not fully synced
        return true;
    
    if (preReg){
        // Something to check ONLY before ticket made into transaction
        //1. check that activation ticket for the regTicketTnxId is not already in the blockchain
        
        if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this)) {
            errRet = strprintf("The art ticket with this txid [%s] is already activated", regTicketTnxId);
            return false;
        }
        
        //2. check there are enough coins to pay 90% of storageFee
        //...
    }
    
//    if (isInitialDownload) { // Something to validate only if Initial Download
//    } else { // Something to validate only if NOT Initial Download
//        //1. check if TicketDB has the same outpoint and if yes, reject if it has different signature
//    }
    
    // Something to always validate
    std::string err;

    //1. check there are ArtReg ticket with txid = regTicketTnxId
    uint256 txid;
    txid.SetHex(regTicketTnxId);
    TicketID ticketId;
    auto pastelTicket = CPastelTicketProcessor::GetTicket(txid, ticketId);
    if (pastelTicket == nullptr || ticketId != TicketID::Art)
    {
        errRet = strprintf("The art ticket with this txid [%s] is not in the blockchain", regTicketTnxId);
        return false;
    }
    auto artTicket = dynamic_cast<CArtRegTicket*>(pastelTicket.get());
    if (artTicket == nullptr)
    {
        errRet = strprintf("The art ticket with this txid [%s] is not in the blockchain or is invalid", regTicketTnxId);
        return false;
    }
    
    //2. check ArtReg ticket is valid
    if (!artTicket->IsValid(false, err))
    {
        errRet = strprintf("The art ticket with this txid [%s] is invalid - %s", regTicketTnxId, err);
        return false;
    }
    
    //3. check Artist PastelID in ArtReg ticket matches PastelID from this ticket
    std::string& artistPastelID = artTicket->pastelIDs[CArtRegTicket::artistsign];
    if (artistPastelID != pastelID)
    {
        errRet = strprintf("The PastelID [%s] is not matching the Artist's PastelID [%s] in the Art Reg ticket with this txid [%s]",
                           pastelID, artistPastelID,
                           regTicketTnxId);
        return false;
    }

    //4. check ArtReg ticket is at the assumed height
    if (artTicket->artistHeight != artistHeight)
    {
        errRet = strprintf("The artistHeight [%d] is not matching the artistHeight [%d] in the Art Reg ticket with this txid [%s]",
                           artistHeight, artTicket->artistHeight,
                           regTicketTnxId);
        return false;
    }
    
    //5. check ArtReg ticket fee is same as storageFee
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
    uint256 txid;
    txid.SetHex(regTicketTnxId);
    TicketID ticketId;
    auto pastelTicket = CPastelTicketProcessor::GetTicket(txid, ticketId);
    if (pastelTicket == nullptr || ticketId != TicketID::Art)
        throw std::runtime_error(strprintf("The art ticket with this txid [%s] is not in the blockchain", regTicketTnxId));
    
    auto artTicket = dynamic_cast<CArtRegTicket*>(pastelTicket.get());
    if (artTicket == nullptr)
        throw std::runtime_error(strprintf("The art ticket with this txid [%s] is not in the blockchain or is invalid", regTicketTnxId));
    
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

std::string CArtActivateTicket::ToJSON()
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

// CArtTradeTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*static*/ bool CArtTradeTicket::FindTicketInDb(const std::string& key, CArtTradeTicket& ticket)
{
    return false;
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
#endif