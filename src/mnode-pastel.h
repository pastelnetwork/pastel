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
	Sell,
	Buy,
	Trade,
	Down,
	
	COUNT
};

class CPastelTicketBase {
public:
	CPastelTicketBase() = default;
	
	virtual std::string TicketName() const = 0;
	virtual std::string ToJSON() = 0;
	virtual bool IsValid(std::string& errRet, bool preReg = false) const = 0;   //if preReg = true - validate pre registration conditions
	                                                                    //      ex.: address has enough coins for registration
	                                                                    //else - validate ticket in general
    virtual CAmount TicketPrice() const = 0;
    
    virtual CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const {return 0;}
	   
	std::string ticketTnx;
	int ticketBlock{};
};

typedef char NoKey;

template<TicketID ticketId, class Key1, class Key2, class MVKey1, class MVKey2>
class CPastelTicket : public CPastelTicketBase {
public:
	CPastelTicket() = default;
	
	TicketID ID() const {return ticketId;}
	virtual Key1 KeyOne() const = 0; 		            //Key to the object itself
	virtual Key2 KeyTwo() const {return Key2{};}		//another key, points to the main key
	virtual bool HasKeyTwo() const {return true;}
    
    virtual MVKey1 MVKeyOne() const {return MVKey1{};}
    virtual MVKey2 MVKeyTwo() const {return MVKey2{};}
	virtual bool HasMVKeyOne() const {return false;}
	virtual bool HasMVKeyTwo() const {return false;}
};

// PastelID Ticket //////////////////////////////////////////////////////////////////////////////////////////////////////
class CPastelIDRegTicket : public CPastelTicket<TicketID::PastelID, std::string, std::string, std::string, NoKey>
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
    bool IsValid(std::string& errRet, bool preReg = false) const override;
    CAmount TicketPrice() const override {return 10;}
    
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

bytes fields are base64 as strings
{
    "version": integer    // 1
    "author": bytes,      // PastelID of the author (artist) - this actually will be duplicated in the signatures block
    "blocknum": integer,  // block when the ticket was created - this is map the ticket to the MN's that should process it
    "data_hash": bytes,   // hash of the image (or any other asset) this ticket represent
    "copies": integer,    // number of copies

    "app_ticket": bytes,
        as:
            base64(
            {
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
    "reserved": bytes
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
class CArtRegTicket : public CPastelTicket<TicketID::Art, std::string, std::string, std::string, NoKey>
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
    CAmount storageFee{};

    int artistHeight{}; //blocknum when the ticket was created by the wallet
    int totalCopies{}; //blocknum when the ticket was created by the wallet

public:
	CArtRegTicket() = default;
	explicit CArtRegTicket(std::string _ticket) : artTicket(std::move(_ticket)) {}
    
    std::string TicketName() const override {return "art-reg";}
	std::string KeyOne() const override {return keyOne;}
	std::string KeyTwo() const override {return keyTwo;}
    bool HasMVKeyOne() const override {return true;}
    std::string MVKeyOne() const override {return pastelIDs[artistsign];}
    
    std::string ToJSON() override;
    bool IsValid(std::string& errRet, bool preReg = false) const override;
    CAmount TicketPrice() const override {return 10;}
    
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
        READWRITE(totalCopies);
        READWRITE(storageFee);
        READWRITE(ticketTnx);
        READWRITE(ticketBlock);
	}
	
    static CArtRegTicket Create(std::string _ticket, const std::string& signatures,
                                std::string _pastelID, const SecureString& strKeyPass,
                                std::string _keyOne, std::string _keyTwo,
                                CAmount _storageFee);
    static bool FindTicketInDb(const std::string& key, CArtRegTicket& _ticket);
    static bool CheckIfTicketInDb(const std::string& key);
    
    static std::vector<CArtRegTicket> FindAllTicketByPastelID(const std::string& pastelID);
};

// Art Activation Ticket ////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "activation",
		"pastelID": "",         //PastelID of the artist
		"reg_txid": "",         //tnx with registration ticket in it
		"artist_height": "",    //block at which artist created Art Ticket,
		                        //is used to check if the MN that created art registration ticket was indeed top MN when artist create ticket
		"reg_fee": "",          //should match the reg fee from Art Ticket
		"signature": ""
	},
 */
class CArtActivateTicket : public CPastelTicket<TicketID::Activate, std::string, NoKey, std::string, int>
{
private:

public:
	std::string pastelID;   //pastelID of the artist
	std::string regTicketTnxId;
    int artistHeight{};
    int storageFee{};
	std::vector<unsigned char> signature;

public:
	CArtActivateTicket() = default;
	explicit CArtActivateTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {}
	
	std::string TicketName() const override {return "art-act";}
    std::string KeyOne() const override {return regTicketTnxId;}
    bool HasKeyTwo() const override {return false;}
    bool HasMVKeyOne() const override {return true;}
    std::string MVKeyOne() const override {return pastelID;}
    bool HasMVKeyTwo() const override {return true;}
    int MVKeyTwo() const override {return artistHeight;}
    
    std::string ToJSON() override;
    bool IsValid(std::string& errRet, bool preReg = false) const override;
    CAmount TicketPrice() const override {return 10;}
	
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

    static std::vector<CArtActivateTicket> FindAllTicketByPastelID(const std::string& pastelID);
    static std::vector<CArtActivateTicket> FindAllTicketByArtistHeight(int height);
    
    static std::string ToStr(const CArtActivateTicket& ticket);
};

// Art Trade Tickets /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "art-sell",
		"pastelID": "",     //PastelID of the art owner - either 1) an original artist; or 2) a previous buyer,
		                    //should be the same in either 1) art activation ticket or 2) trade ticket
		"art_txid": "",     //txid with either 1) art activation ticket or 2) trade ticket in it
		"asked_price": "",
		"valid_after": "",
		"valid_before": "",
		"reserved": "",
		"signature": ""
	},
 */

class CArtSellTicket : public CPastelTicket<TicketID::Sell, std::string, NoKey, std::string, std::string>
{
public:
    std::string pastelID;
    std::string artTnxId;
    uint askedPrice{};
    uint validAfter{};              //as a block height
    uint validBefore{};             //as a block height
    ushort copyNumber{};
    std::string reserved;
    std::vector<unsigned char> signature;
    
    std::string key;
    
public:
    CArtSellTicket() = default;
    explicit CArtSellTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {}
    
    std::string TicketName() const override {return "art-sell";}
    std::string KeyOne() const override {return !key.empty()? key: artTnxId+":"+to_string(copyNumber);} //txid:#
    bool HasKeyTwo() const override {return false;}
    bool HasMVKeyOne() const override {return true;}
    std::string MVKeyOne() const override {return pastelID;}
    bool HasMVKeyTwo() const override {return true;}
    std::string MVKeyTwo() const override {return artTnxId;}
    
    std::string ToJSON() override;
    bool IsValid(std::string& errRet, bool preReg = false) const override;
    CAmount TicketPrice() const override {return askedPrice/50;}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(pastelID);
        READWRITE(artTnxId);
        READWRITE(askedPrice);
        READWRITE(validAfter);
        READWRITE(validBefore);
        READWRITE(copyNumber);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(ticketTnx);
        READWRITE(ticketBlock);
    }
    
    static CArtSellTicket Create(std::string _artTnxId, int _askedPrice, int _validAfter, int _validBefore, std::string _pastelID, const SecureString& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CArtSellTicket& ticket);
    
    static std::vector<CArtSellTicket> FindAllTicketByPastelID(const std::string& pastelID);
    static std::vector<CArtSellTicket> FindAllTicketByArtTnxID(const std::string& artTnxId);
    
    static std::string ToStr(const CArtSellTicket& ticket);
};

/*
	"ticket": {
		"type": "buy",
		"pastelID": "",     //PastelID of the buyer
		"sell_txid": "",    //txid with sale ticket
		"price": "",
		"reserved": "",
		"signature": ""
	},
 */
class CArtBuyTicket : public CPastelTicket<TicketID::Buy, std::string, NoKey, std::string, NoKey>
{
public:
    std::string pastelID;
    std::string sellTnxId;
    uint price{};
    std::string reserved;
    std::vector<unsigned char> signature;

public:
    CArtBuyTicket() = default;
    explicit CArtBuyTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {}
    
    std::string TicketName() const override {return "art-buy";}
    std::string KeyOne() const override {return sellTnxId;}
    bool HasKeyTwo() const override {return false;}
    bool HasMVKeyOne() const override {return true;}
    std::string MVKeyOne() const override {return pastelID;}
    bool HasMVKeyTwo() const override {return false;}
    CAmount TicketPrice() const override {return price/100;}
    
    std::string ToJSON() override;
    bool IsValid(std::string& errRet, bool preReg = false) const override;
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(pastelID);
        READWRITE(sellTnxId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(ticketTnx);
        READWRITE(ticketBlock);
    }
    
    static CArtBuyTicket Create(std::string _sellTnxId, int _price, std::string _pastelID, const SecureString& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CArtBuyTicket& ticket);

    static bool CheckBuyTicketExistBySellTicket(const std::string& _sellTnxId);
    
    static std::vector<CArtBuyTicket> FindAllTicketByPastelID(const std::string& pastelID);

    static std::string ToStr(const CArtBuyTicket& ticket);
};

/*
	"ticket": {
		"type": "trade",
		"pastelID": "",     //PastelID of the buyer
		"sell_txid": "",    //txid with sale ticket
		"buy_txid": "",    //txid with sale ticket
		"price": "",
		"reserved": "",
		"signature": ""
	},
 */
class CArtTradeTicket : public CPastelTicket<TicketID::Trade, std::string, std::string, std::string, std::string>
{
public:
    std::string pastelID;
    std::string sellTnxId;
    std::string buyTnxId;
    std::string artTnxId;
    uint price{};
    std::string reserved;
    std::vector<unsigned char> signature;

public:
    CArtTradeTicket() = default;
    explicit CArtTradeTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {}
    
    std::string TicketName() const override {return "art-trade";}
    std::string KeyOne() const override {return sellTnxId;}
    bool HasKeyTwo() const override {return true;}
    std::string KeyTwo() const override {return buyTnxId;}
    bool HasMVKeyOne() const override {return true;}
    std::string MVKeyOne() const override {return pastelID;}
    bool HasMVKeyTwo() const override {return true;}
    std::string MVKeyTwo() const override {return artTnxId;}
    
    std::string ToJSON() override;
    bool IsValid(std::string& errRet, bool preReg = false) const override;
    CAmount TicketPrice() const override {return 10;}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(pastelID);
        READWRITE(sellTnxId);
        READWRITE(buyTnxId);
        READWRITE(artTnxId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(ticketTnx);
        READWRITE(ticketBlock);
    }
    
    static CArtTradeTicket Create(std::string _sellTnxId, std::string _buyTnxId, std::string _pastelID, const SecureString& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CArtTradeTicket& ticket);
    
    static std::vector<CArtTradeTicket> FindAllTicketByPastelID(const std::string& pastelID);
    static std::vector<CArtTradeTicket> FindAllTicketByArtTnxID(const std::string& artTnxID);
    
    static std::string ToStr(const CArtTradeTicket& ticket);
};

// Take Down Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
class CTakeDownTicket : public CPastelTicket<TicketID::Down, NoKey, NoKey, NoKey, NoKey>
{
public:
    static bool FindTicketInDb(const std::string& key, CTakeDownTicket& ticket);
    CAmount TicketPrice() const override {return 1000;}
};

#define FAKE_TICKET
// Ticket  Processor ////////////////////////////////////////////////////////////////////////////////////////////////////
class CPastelTicketProcessor {
	map<TicketID, std::unique_ptr<CDBWrapper> > dbs;
	
public:
	CPastelTicketProcessor() = default;
	
	void InitTicketDB();
	void UpdatedBlockTip(const CBlockIndex *cBlockIndex, bool fInitialDownload);
	bool ParseTicketAndUpdateDB(CMutableTransaction& tx, int nBlockHeight);
	
	template<class T>
	bool UpdateDB(T& ticket, string &txid, int nBlockHeight);
    template<class T, class MVKey>
    void UpdateDB_MVK(const T& ticket, const MVKey& mvKey);
	
	template<class T>
	bool CheckTicketExist(const T& ticket);
	template<class T>
	bool FindTicket(T& ticket);

	template<class T>
	bool CheckTicketExistBySecondaryKey(const T& ticket);
	template<class T>
	bool FindTicketBySecondaryKey(T& ticket);
    template<class T, class MVKey>
    std::vector<T> FindTicketsByMVKey(TicketID ticketId, const MVKey& mvKey);
    
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
	
#ifdef FAKE_TICKET
    template<class T>
    static std::string CreateFakeTransaction(T& ticket, CAmount ticketPrice, const std::vector<std::pair<std::string, CAmount>>& extraPayments, const std::string& strVerb, bool bSend);
#endif
};

#endif //MASTERNODEPASTEL_H
