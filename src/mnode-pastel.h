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
	Activate,
	Trade,
	Down,
	
	COUNT
};

class CPastelTicketBase {
public:
	CPastelTicketBase() = default;
	
	virtual std::string TicketName() const = 0;
	virtual std::string ToJSON() = 0;
	virtual bool IsValid(bool preReg, std::string& errRet) const = 0;   //if preReg = true - validate pre registration conditions
	                                                                    //      ex.: address has enough coins for registration
	                                                                    //else - validate ticket in general
	
	std::string ticketTnx;
	int ticketBlock{};
};

template<TicketID ticketId, class Key1, class Key2>
class CPastelTicket : public CPastelTicketBase {
public:
	CPastelTicket() = default;
	
	TicketID ID() const {return ticketId;}
	virtual Key1 KeyOne() const = 0; 		//Key to the object itself
	virtual Key2 KeyTwo() const = 0;		//another key, points to the main key
};

// PastelID Ticket //////////////////////////////////////////////////////////////////////////////////////////////////////
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
	
	std::string TicketName() const override {return "pastelid";}
	std::string KeyOne() const override {return pastelID;}
	std::string KeyTwo() const override {return outpoint.empty() ? address : outpoint;}
	
    std::string ToJSON() override;
    bool IsValid(bool preReg, std::string& errRet) const override {return true;}
    //TODO: validate -  1)prereg - address has coins to pay for registration - 10PSL + fee
    //                  2)signature matches PastelID
    
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
    
    static CPastelIDRegTicket Create(std::string _pastelID, const SecureString& strKeyPass, std::string _address);
    static bool FindTicketInDb(const std::string& key, CPastelIDRegTicket& ticket);
};


// Art Registration Ticket //////////////////////////////////////////////////////////////////////////////////////////////
/*

Ticket as base64(RegistrationTicket({some data}))

fields are base64 as strings
{
    "author": bytes,
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
}

signatures
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
    }
}
 */
class CArtRegTicket : public CPastelTicket<TicketID::Art, std::string, std::string>
{
    static constexpr short allsigns = 4;
    static constexpr short artistsign = 0;
    static constexpr short mainmnsign = 1;

public:
	std::string ticket;
    
    std::string mnPastelIDs[allsigns];
    std::vector<unsigned char> mnSignatures[allsigns];
    
    std::string keyOne;
    std::string keyTwo;
    int ticketBlock{}; //blocknum when the ticket was created by the wallet

public:
	CArtRegTicket() = default;
	explicit CArtRegTicket(std::string _ticket) : ticket(std::move(_ticket)) {}
    
    std::string TicketName() const override {return "art-reg";}
	std::string KeyOne() const override {return keyOne;}
	std::string KeyTwo() const override {return keyTwo;}
	
	std::string ToJSON() override;
    bool IsValid(bool preReg, std::string& errRet) const override;
    
    ADD_SERIALIZE_METHODS;
	
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(ticket);
		
        for (int mn=0; mn<allsigns; mn++) {
            READWRITE(mnPastelIDs[mn]);
            READWRITE(mnSignatures[mn]);
        }

        READWRITE(keyOne);
        READWRITE(keyTwo);
        READWRITE(ticketBlock);
        
        READWRITE(ticketTnx);
		READWRITE(ticketBlock);
	}
	
	MSGPACK_DEFINE(ticket,
                   mnPastelIDs[0], mnSignatures[0],
                   mnPastelIDs[1], mnSignatures[1],
                   mnPastelIDs[2], mnSignatures[2],
                   mnPastelIDs[3], mnSignatures[3],
                   keyOne, keyTwo, ticketBlock);
    
    static CArtRegTicket Create(std::string _ticket, const std::string& signatures, std::string _pastelID, const SecureString& strKeyPass,
                                std::string _keyOne, std::string _keyTwo, int _ticketBlock);
    static bool FindTicketInDb(const std::string& key, CArtRegTicket& ticket);
};

// Art Activation Ticket ////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "activation",
		"pastelID": "",
		"reg_height": "",
		"reg_txid": "",
		"signature": ""
	},
 */
class CArtActivateTicket : public CPastelTicket<TicketID::Activate, int, std::string>
{
public:
	std::string pastelID;
	int regBlockHeight{};
	std::string regTicketTnxId;
	std::vector<unsigned char> signature;

public:
	CArtActivateTicket() = default;
	explicit CArtActivateTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {}
	CArtActivateTicket(std::string txid, std::string _pastelID, const SecureString& strKeyPass);
	
	std::string TicketName() const override {return "activation";}
	int KeyOne() const override {return regBlockHeight;}
	std::string  KeyTwo() const override {return regTicketTnxId;}
	
	std::string ToJSON() override;
    bool IsValid(bool preReg, std::string& errRet) const override {return true;}
	
	ADD_SERIALIZE_METHODS;
	
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(pastelID);
		READWRITE(regBlockHeight);
		READWRITE(regTicketTnxId);
		READWRITE(signature);
		READWRITE(ticketTnx);
		READWRITE(ticketBlock);
	}
	
	MSGPACK_DEFINE(pastelID, regBlockHeight, regTicketTnxId, signature)
    
    static bool FindTicketInDb(const std::string& key, CArtActivateTicket& ticket);

private:
	void init(std::string&& txid, std::string&& _pastelID, const SecureString& strKeyPass);
};

// Art Trade Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
class CArtTradeTicket : public CPastelTicket<TicketID::Trade, std::string, std::string>
{
public:
    static bool FindTicketInDb(const std::string& key, CArtTradeTicket& ticket);
};

// Take Down Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
class CTakeDownTicket : public CPastelTicket<TicketID::Down, std::string, std::string>
{
public:
    static bool FindTicketInDb(const std::string& key, CTakeDownTicket& ticket);
};

// Ticket  Processor ////////////////////////////////////////////////////////////////////////////////////////////////////
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
	static bool CreateP2FMSTransaction(const std::string& input_data, CMutableTransaction& tx_out, CAmount price, std::string& error_ret);
	static bool CreateP2FMSTransaction(const std::vector<unsigned char>& input_data, CMutableTransaction& tx_out, CAmount price, std::string& error_ret);
#endif // ENABLE_WALLET
	static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::vector<unsigned char>& output_data, std::string& error_ret);
	static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::string& output_data, std::string& error_ret);
	static bool StoreP2FMSTransaction(const CMutableTransaction& tx_out, std::string& error_ret);
	
	template<class T>
	static std::string SendTicket(const T& ticket);
	
	static CPastelTicketBase* GetTicket(uint256 txid);
	static std::string GetTicketJSON(uint256 txid);

	template<class T>
	static T ParseTicket(const std::vector<unsigned char>& data, int nOffset = 0);
	
	static CAmount GetTicketPrice(TicketID tid);
};

#endif //MASTERNODEPASTEL_H
