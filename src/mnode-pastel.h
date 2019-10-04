// Copyright (c) 2019 The PASTEL-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEPASTEL_H
#define MASTERNODEPASTEL_H

#include "main.h"
#include "dbwrapper.h"

#include "msgpack/msgpack.hpp"

enum class TicketID : uint8_t{
	PastelID,
	Art,
	Confirm,
	Trade,
	Down,
	
	COUNT
};

template<TicketID ticketId, class Key1, class Key2>
class CPastelTicket {
public:
	CPastelTicket() = default;
	
	TicketID ID() const {return ticketId;}
	virtual std::string TicketName() const = 0;
	virtual Key1 KeyOne() const = 0; 		//Key to the object itself
	virtual Key2 KeyTwo() const = 0;		//another key, points to the main key
	virtual std::string ToJSON() = 0;
	
	std::string ticketTnx;
	int ticketBlock{};
};

class CPastelIDRegTicket : public CPastelTicket<TicketID::PastelID, std::string, std::string>
{
public:
	std::string pastelID;
	std::string address;
	std::string outpoint;
	std::time_t timestamp{};
	std::vector<unsigned char> signature;

public:
	CPastelIDRegTicket() = default;
	explicit CPastelIDRegTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {}
	CPastelIDRegTicket(std::string _pastelID, const SecureString& strKeyPass); //For Masternode PastelID
	CPastelIDRegTicket(std::string _pastelID, const SecureString& strKeyPass, std::string _address); //For Personal PastelID
	
	std::string TicketName() const override {return "pastelid";}
	std::string KeyOne() const override {return pastelID;}
	std::string KeyTwo() const override {return outpoint.empty() ? address : outpoint;}
	
	std::string ToJSON() override;
	std::string PastelIDType() {return outpoint.empty()? "personal": "masternode";}
	
	ADD_SERIALIZE_METHODS;
	
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(pastelID);
		READWRITE(address);
		READWRITE(outpoint);
		READWRITE(timestamp);
		READWRITE(signature);
		READWRITE(ticketTnx);
		READWRITE(ticketBlock);
	}
	
	MSGPACK_DEFINE(pastelID, address, outpoint, timestamp, signature)
	
private:
	void init(std::string&& _pastelID, const SecureString& strKeyPass, std::string&& _address);
};

/*
fileds are base64 as strings
FinalRegistrationTicket = {
    "ticket": {
        "author": bytes,
        "order_block_txid": string,
        "blocknum": integer,
        "imagedata_hash": bytes,

        "artist_name": string,
        "artist_website": string,
        "artist_written_statement": string,
        "artwork_title": string,
        "artwork_series_name": string,
        "artwork_creation_video_youtube_url": string,
        "artwork_keyword_set": string,
        "total_copies": integer,

        "fingerprints": [list of floats],
        "lubyhashes": [list of floats],
        "lubyseeds": [list of floats],
        "thumbnailhash": bytes,
    },
    "signature_author": {
        "signature": bytes,
        "pubkey": bytes,
    },
    "signature_1": {
        "signature": bytes,
        "pubkey": bytes,
    },
    "signature_2": {
        "signature": bytes,
        "pubkey": bytes,
    },
    "signature_3": {
        "signature": bytes,
        "pubkey": bytes,
    },
    "nonce": string,
}
 */
class CArtRegTicket : public CPastelTicket<TicketID::Art, std::string, std::string>
{
public:
	std::string ticketBlob;
	std::string keyOne;
	std::string keyTwo;

public:
	CArtRegTicket() = default;
	CArtRegTicket(std::string _ticket, std::string _keyOne, std::string _keyTwo)
			: ticketBlob(std::move(_ticket)),
			  keyOne(std::move(_keyOne)),
			  keyTwo(std::move(_keyTwo))
	{}
	
	std::string TicketName() const override {return "art-reg";}
	std::string KeyOne() const override {return keyOne;}
	std::string KeyTwo() const override {return keyTwo;}
	
	std::string ToJSON() override;
	
	ADD_SERIALIZE_METHODS;
	
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(ticketBlob);
		READWRITE(keyOne);
		READWRITE(keyTwo);
		READWRITE(ticketTnx);
		READWRITE(ticketBlock);
	}
	
	MSGPACK_DEFINE(ticketBlob, keyOne, keyTwo)

private:
	void init(std::string&& _ticket, std::string&& _keyOne, std::string&& _keyTwo) {} //TODO: call from constructor to parse ticket
};

class CArtConfTicket : public CPastelTicket<TicketID::Confirm, std::string, std::string>
{};

class CArtTradeTicket : public CPastelTicket<TicketID::Trade, std::string, std::string>
{};

class CTakeDownTicket : public CPastelTicket<TicketID::Down, std::string, std::string>
{};

////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CPastelTicketProcessor {
	map<TicketID, std::unique_ptr<CDBWrapper> > dbs;
	
public:
	CPastelTicketProcessor() = default;
	
	void InitTicketDB();
	void UpdatedBlockTip(const CBlockIndex *pindex, bool fInitialDownload);
	bool ParseTicketAndUpdateDB(CMutableTransaction& tx, int nBlockHeight);
	
	template<class T>
	bool UpdateDB(T& ticket, std::string txid, int nBlockHeight);
	
	template<class T>
	bool CheckTicketExist(const T& ticket);
	template<class T>
	bool FindTicket(T& ticket);

	template<class T>
	bool CheckTicketExistBySecondaryKey(const T& ticket);
	template<class T>
	bool FindTicketBySecondaryKey(T& ticket);
	
	std::vector<std::string> GetAllKeys(TicketID id);

#ifdef ENABLE_WALLET
	static bool CreateP2FMSTransaction(const std::string& input_data, CMutableTransaction& tx_out, std::string& error_ret);
	static bool CreateP2FMSTransaction(const std::vector<unsigned char>& input_data, CMutableTransaction& tx_out, std::string& error_ret);
#endif // ENABLE_WALLET
	static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::vector<unsigned char>& output_data, std::string& error_ret);
	static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::string& output_data, std::string& error_ret);
	static bool StoreP2FMSTransaction(const CMutableTransaction& tx_out, std::string& error_ret);
	
	template<class T>
	static std::string SendTicket(const T& ticket);

	static std::string GetTicket(uint256 txid);

	template<class T>
	static T ParseTicket(const std::vector<unsigned char>& data, int nOffset = 0);
};

#endif //MASTERNODEPASTEL_H
