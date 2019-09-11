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

#include "ed448/pastel_key.h"


void CPastelTicketProcessor::UpdatedBlockTip(const CBlockIndex *pindex, bool fInitialDownload)
{
	if(!pindex) return;
	
	if (fInitialDownload){
		//Delete ticket index files and create new
	}
	
	int nCachedBlockHeight = pindex->nHeight;
	LogPrint("tickets", "CPastelTicket::UpdatedBlockTip -- nCachedBlockHeight=%d\n", nCachedBlockHeight);
//
//	CBlock block;
//	if(!ReadBlockFromDisk(block, pindex)) {
//		LogPrintf("CMasternodeSync::ProcessTick -- ERROR: Can't read block from disk\n");
//		return;
//	}
//
//	for(const CTransaction& tx : block.vtx)
//	{
//		CMutableTransaction mtx(tx);
//		std::string output_data, error_ret;
//		if (ParseP2FMSTransaction(mtx, std::string& output_data, error_ret)){
//			msgpack::object_handle oh = msgpack::unpack(ss.str().data(), ss.str().size());
//			msgpack::object obj = oh.get();
//
//			std::cout << obj << std::endl;
//			assert((obj.as<std::array<int, 5>>()) == a);
//		}
//	}


//	ProcessBlock(nCachedBlockHeight);
}

#ifdef ENABLE_WALLET
bool CPastelTicketProcessor::CreateP2FMSTransaction(const std::string& input_data, CMutableTransaction& tx_out, std::string& error_ret)
{
    //Convert string data into binary buffer
    std::vector<unsigned char> input_bytes = ToByteVector(input_data);
    return CPastelTicketProcessor::CreateP2FMSTransaction(input_bytes, tx_out, error_ret);
}

bool CPastelTicketProcessor::CreateP2FMSTransaction(const std::vector<unsigned char>& input_data, CMutableTransaction& tx_out, std::string& error_ret)
{
    size_t input_len = input_data.size();
    if (input_len == 0) {
        error_ret = "Input data is empty";
        return false;
    }

    std::vector<unsigned char> input_bytes = input_data;

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
        chunks.push_back(std::vector<unsigned char>(it, it+fake_key_size));
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

    //Create address and script for change
    CKey key_change;
    key_change.MakeNewKey(true);
    CScript script_change;
    script_change = GetScriptForDestination(key_change.GetPubKey().GetID());

    //calcalute aprox required amount
    CAmount nAproxFeeNeeded = payTxFee.GetFee(input_bytes.size())*2;
    if (nAproxFeeNeeded < payTxFee.GetFeePerK()) nAproxFeeNeeded = payTxFee.GetFeePerK();
    CAmount outAmount = out_scripts.size()*30*CENT + nAproxFeeNeeded;

    int chainHeight = chainActive.Height() + 1;
    if (Params().NetworkIDString() != "regtest") {
        chainHeight = std::max(chainHeight, APPROX_RELEASE_HEIGHT);
    }
    auto consensusBranchId = CurrentEpochBranchId(chainHeight, Params().GetConsensus());

    //Create empty transaction
    tx_out = CreateNewContextualCMutableTransaction(Params().GetConsensus(), chainHeight);

    //Find funding (unspent) transaction with enough coins to cover all outputs (single - for simplisity)
    bool bOk = false;
    assert(pwalletMain != NULL);
    {
        vector<COutput> vecOutputs;
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->AvailableCoins(vecOutputs, false, NULL, true);
        for (auto out : vecOutputs) {
            if (out.tx->vout[out.i].nValue > outAmount) {

                //If found - populate transaction

                const CScript& prevPubKey = out.tx->vout[out.i].scriptPubKey;
                const CAmount& prevAmount = out.tx->vout[out.i].nValue;

                tx_out.vin.resize(1);
                tx_out.vin[0].prevout.n = out.i;
                tx_out.vin[0].prevout.hash = out.tx->GetHash();

                //Add fake output scripts
                tx_out.vout.resize(num_fake_txn+1); //+1 for change
                for (int i=0; i<num_fake_txn; i++) {
                    tx_out.vout[i].nValue = 30*CENT;
                    tx_out.vout[i].scriptPubKey = out_scripts[i];
                }
                //Add change output scripts
                tx_out.vout[num_fake_txn].nValue = prevAmount - (num_fake_txn*30*CENT);
                tx_out.vout[num_fake_txn].scriptPubKey = script_change;

                //sign transaction - unlock input
                SignatureData sigdata;
                ProduceSignature(MutableTransactionSignatureCreator(pwalletMain, &tx_out, 0, prevAmount, SIGHASH_ALL), prevPubKey, sigdata, consensusBranchId);
                UpdateTransaction(tx_out, 0, sigdata);

                //Calculate correct fee
                size_t tx_size = EncodeHexTx(tx_out).length();
                CAmount nFeeNeeded = payTxFee.GetFee(tx_size);
                if (nFeeNeeded < payTxFee.GetFeePerK()) nFeeNeeded = payTxFee.GetFeePerK();

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

bool CPastelTicketProcessor::ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::string& output_data, std::string& error_ret)
{
	std::vector<unsigned char> output_vector;
	bool bOk = CPastelTicketProcessor::ParseP2FMSTransaction(tx_in, output_vector, error_ret);
	if (bOk)
		output_data.assign(output_vector.begin(), output_vector.end());
	return bOk;
}
bool CPastelTicketProcessor::ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::vector<unsigned char>& output_data, std::string& error_ret)
{
	bool foundMS = false;
	
	for (auto vout : tx_in.vout) {
		
		txnouttype typeRet;
		vector<vector<unsigned char> > vSolutions;
		
		if (!Solver(vout.scriptPubKey, typeRet, vSolutions) ||
			typeRet != TX_MULTISIG)
			continue;
		
		foundMS = true;
		for (unsigned int i = 1; i < vSolutions.size()-1; i++)
		{
			output_data.insert(output_data.end(), vSolutions[i].begin(), vSolutions[i].end());
		}
	}
	
	if (!foundMS){
		error_ret = "No data Multisigs found in transaction";
		return false;
	}
	
	if (output_data.size() == 0){
		error_ret = "No data found in transaction";
		return false;
	}
	
	//size_t szie = 8 bytes; hash size = 32 bytes
	if (output_data.size() < 40){
		error_ret = "No correct data found in transaction";
		return false;
	}

//    std::vector<unsigned char> output_len_bytes(output_data.begin(), output_data.begin()+sizeof(size_t)); //sizeof(size_t) == 8
	size_t **output_len_ptr = reinterpret_cast<size_t**>(&output_data);
	size_t output_len = **output_len_ptr;
	if (output_len_ptr == nullptr || *output_len_ptr == nullptr){
		error_ret = "No correct data found in transaction - wrong length";
		return false;
	}
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

template<class T>
void CPastelTicketProcessor::SendTicket(T& ticket)
{
	msgpack::sbuffer buffer;
	msgpack::pack(buffer, ticket);
	
	unsigned char* pdata = reinterpret_cast<unsigned char*>(buffer.data());
	std::vector<unsigned char> data{pdata, pdata+buffer.size()};
	
	TicketID tid = ticket.GetID();
	auto* ticketid_byte = reinterpret_cast<unsigned char*>(&tid);
	data.insert(data.begin(), ticketid_byte, ticketid_byte+sizeof(TicketID)); //sizeof(size_t) == 8
	
	std::string error;
	if (!CPastelTicketProcessor::CreateP2FMSTransaction(data, ticket.tx, error)){
		throw std::runtime_error(strprintf("\"Failed to create P2FMS from data provided - %s", error));
	}
	
	if (!CPastelTicketProcessor::StoreP2FMSTransaction(ticket.tx, error)){
		throw std::runtime_error(strprintf("\"Failed to send P2FMS transaction - %s", error));
	}
}
template void CPastelTicketProcessor::SendTicket<CPastelRegisterationTicket>(CPastelRegisterationTicket&);


CPastelRegisterationTicket::CPastelRegisterationTicket(const std::string& pastelID, const SecureString& strKeyPass)
{
	CMasternode mn;
	if(!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, mn)) {
		throw std::runtime_error("This is not a active masternode. Only active MN can register its PastelID ");
	}
	
	//1. collateral address
	rawMnAddress = std::vector<unsigned char>{mn.pubKeyCollateralAddress.begin(), mn.pubKeyCollateralAddress.end()};
	//2. outpoint hash
	txid = std::vector<unsigned char>{masterNodeCtrl.activeMasternode.outpoint.hash.begin(), masterNodeCtrl.activeMasternode.outpoint.hash.end()};
	//3. outpoint n
	txind = masterNodeCtrl.activeMasternode.outpoint.n;
	//4. pastelid
	rawPubKey = CPastelID::DecodePastelID(pastelID);
	//5. Time
	timestamp = std::time(nullptr);
	
	//6. signature of ticket hash
	CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
	ss << rawMnAddress;
	ss << txid;
	ss << txind;
	ss << rawPubKey;
	ss << timestamp;
	uint256 hash = ss.GetHash();
	signature = CPastelID::SignB(hash.begin(), hash.size(), pastelID, strKeyPass);
}
//bool CPastelRegisterationTicket::GetTransaction(const CMutableTransaction& tx, std::string& error_ret)
//{
//	return false;
//}
