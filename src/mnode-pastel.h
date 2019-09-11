// Copyright (c) 2019 The PASTEL-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEPASTEL_H
#define MASTERNODEPASTEL_H

#include "main.h"
#include "msgpack/msgpack.hpp"

enum class TicketID : uint8_t{
	PastelID,
	ArtRegistration,
	//...
};

template<TicketID ticketId>
class CPastelTicket {
public:
	CPastelTicket() = default;
	
	TicketID GetID(){return ticketId;}
	std::string TxId(){return tx.GetHash().GetHex();}
//	virtual bool GetTransaction(const CMutableTransaction& tx, std::string& error_ret) = 0;

public:
	CMutableTransaction tx;
};

class CPastelRegisterationTicket : public CPastelTicket<TicketID::PastelID> {
	std::vector<unsigned char> rawMnAddress;
	std::vector<unsigned char> txid;
	unsigned int txind;
	std::vector<unsigned char> rawPubKey;
	std::time_t timestamp;
	std::vector<unsigned char> signature;
public:
	CPastelRegisterationTicket() = default;
	CPastelRegisterationTicket(const std::string& pastelID, const SecureString& strKeyPass);
	
	MSGPACK_DEFINE(rawMnAddress, txid, txind, rawPubKey, timestamp, signature)
};

class CPastelTicketProcessor {
public:
	CPastelTicketProcessor() = default;

#ifdef ENABLE_WALLET
	static bool CreateP2FMSTransaction(const std::string& input_data, CMutableTransaction& tx_out, std::string& error_ret);
	static bool CreateP2FMSTransaction(const std::vector<unsigned char>& input_data, CMutableTransaction& tx_out, std::string& error_ret);
#endif // ENABLE_WALLET
	static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::vector<unsigned char>& output_data, std::string& error_ret);
	static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::string& output_data, std::string& error_ret);
	static bool StoreP2FMSTransaction(const CMutableTransaction& tx_out, std::string& error_ret);
	
	template<class T>
	static void SendTicket(T& ticket);
	
	void UpdatedBlockTip(const CBlockIndex *pindex, bool fInitialDownload);
};

#endif //MASTERNODEPASTEL_H
