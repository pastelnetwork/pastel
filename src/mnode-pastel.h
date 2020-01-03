// Copyright (c) 2019 The PASTEL-Coin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEPASTEL_H
#define MASTERNODEPASTEL_H

#include "main.h"
#include "dbwrapper.h"

#define TICKETS_VERSION 0x01

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
    
    virtual CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const {return 0;}
	   
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
	virtual bool HasKeyTwo() const {return true;}		//another key, points to the main key
	virtual bool HasMultivalueKey() const {return false;}		//another key, points to the main key
};

// PastelID Ticket //////////////////////////////////////////////////////////////////////////////////////////////////////
class CPastelIDRegTicket : public CPastelTicket<TicketID::PastelID, std::string, std::string>
{
public:
	std::string pastelID;
	std::string address;
    COutPoint outpoint{};
	std::time_t timestamp{};
	std::vector<unsigned char> mn_signature;
	std::vector<unsigned char> pslid_signature;
    
    std::string secondKey; //local only
    
public:
	CPastelIDRegTicket() = default;
	explicit CPastelIDRegTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {}
	
	std::string TicketName() const override {return "pastelid";}
	std::string KeyOne() const override {return pastelID;}
	std::string KeyTwo() const override {return outpoint.IsNull() ? (secondKey.empty() ? address : secondKey) : outpoint.ToStringShort();}
	
    std::string ToJSON() override;
    bool IsValid(bool preReg, std::string& errRet) const override;
    
    std::string PastelIDType() {return outpoint.IsNull()? "personal": "masternode";}
    
    ADD_SERIALIZE_METHODS;
	
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(pastelID);
		READWRITE(address);
		READWRITE(outpoint);
		READWRITE(timestamp);
		READWRITE(mn_signature);
		READWRITE(pslid_signature);
		READWRITE(ticketTnx);
		READWRITE(ticketBlock);
	}
	
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
public:
    static constexpr short allsigns = 4;
    static constexpr short artistsign = 0;
    static constexpr short mainmnsign = 1;
    static constexpr short mn2sign = 2;
    static constexpr short mn3sign = 3;

public:
	std::string artTicket;
    
    std::string pastelIDs[allsigns];
    std::vector<unsigned char> ticketSignatures[allsigns];
    
    std::string keyOne;
    std::string keyTwo;
    int artistHeight{}; //blocknum when the ticket was created by the wallet
    CAmount storageFee{};

public:
	CArtRegTicket() = default;
	explicit CArtRegTicket(std::string _ticket) : artTicket(std::move(_ticket)) {}
    
    std::string TicketName() const override {return "art-reg";}
	std::string KeyOne() const override {return keyOne;}
	std::string KeyTwo() const override {return keyTwo;}
	
	std::string ToJSON() override;
    bool IsValid(bool preReg, std::string& errRet) const override;
    
    ADD_SERIALIZE_METHODS;
	
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(artTicket);
		
        for (int mn=0; mn<allsigns; mn++) {
            READWRITE(pastelIDs[mn]);
            READWRITE(ticketSignatures[mn]);
        }

        READWRITE(keyOne);
        READWRITE(keyTwo);
        READWRITE(artistHeight);
        READWRITE(storageFee);
        READWRITE(ticketTnx);
        READWRITE(ticketBlock);
	}
	
    static CArtRegTicket Create(std::string _ticket, const std::string& signatures,
                                std::string _pastelID, const SecureString& strKeyPass,
                                std::string _keyOne, std::string _keyTwo,
                                int _artistHeight, CAmount _storageFee);
    static bool FindTicketInDb(const std::string& key, CArtRegTicket& _ticket);
    static bool CheckIfTicketInDb(const std::string& key);
};

// Art Activation Ticket ////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "activation",
		"pastelID": "",
		"reg_txid": "",
		"reg_height": "",
		"reg_fee": "",
		"signature": ""
	},
 */
class CArtActivateTicket : public CPastelTicket<TicketID::Activate, std::string, int>
{
private:

public:
	std::string pastelID;
	std::string regTicketTnxId;
    int artistHeight{};
    int storageFee{};
	std::vector<unsigned char> signature;

public:
	CArtActivateTicket() = default;
	explicit CArtActivateTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {}
	
	std::string TicketName() const override {return "art-act";}
    std::string KeyOne() const override {return regTicketTnxId;}
    int KeyTwo() const override {return 0;}
    bool HasKeyTwo() const override {return false;}
	
	std::string ToJSON() override;
    bool IsValid(bool preReg, std::string& errRet) const override;
	
	ADD_SERIALIZE_METHODS;
	
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(pastelID);
		READWRITE(regTicketTnxId);
		READWRITE(artistHeight);
        READWRITE(storageFee);
        READWRITE(signature);
		READWRITE(ticketTnx);
		READWRITE(ticketBlock);
	}
	
    CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const override;
    
    static CArtActivateTicket Create(std::string _regTicketTxId, int _artistHeight, int _storageFee, std::string _pastelID, const SecureString& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CArtActivateTicket& ticket);
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

#define FAKE_TICKET
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
	static bool CreateP2FMSTransaction(const std::string& input_string, CMutableTransaction& tx_out, CAmount price, std::string& error_ret);
	static bool CreateP2FMSTransaction(const CDataStream& input_stream, CMutableTransaction& tx_out, CAmount price, std::string& error_ret);
	static bool CreateP2FMSTransactionWithExtra(const CDataStream& input_data, const std::vector<CTxOut>& extraOutputs, CAmount extraAmount, CMutableTransaction& tx_out, CAmount price, std::string& error_ret);
#endif // ENABLE_WALLET
	static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, vector<unsigned char>& output_data, std::string& error_ret);
	static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::string& output_string, std::string& error_ret);
	static bool StoreP2FMSTransaction(const CMutableTransaction& tx_out, std::string& error_ret);
	
	template<class T>
	static std::string SendTicket(const T& ticket);
	
	static std::unique_ptr<CPastelTicketBase> GetTicket(uint256 txid, TicketID& ticketId);
	static std::string GetTicketJSON(uint256 txid);

	static bool ValidateIfTicketTransaction(const CTransaction& tx);
	
	static CAmount GetTicketPrice(TicketID tid);

#ifdef FAKE_TICKET
    template<class T>
    static std::string CreateFakeTransaction(T& ticket, CAmount ticketPrice, const std::vector<std::pair<std::string, CAmount>>& extraPayments, const std::string& strVerb, bool bSend);
#endif
};

#endif //MASTERNODEPASTEL_H
