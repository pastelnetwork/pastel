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
	virtual std::string ToJSON() const = 0;
	virtual bool IsValid(std::string& errRet, bool preReg, int depth) const = 0;    //if preReg = true - validate pre registration conditions
	                                                                                //      ex.: address has enough coins for registration
	                                                                                //else - validate ticket in general
    virtual CAmount TicketPrice(int nHeight) const = 0;
    virtual std::string ToStr() const = 0;
    
    virtual CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const {return 0;}
	   
	std::string ticketTnx;
	int ticketBlock{};
    std::int64_t timestamp{};
    short nVersion{};
};

template<TicketID ticketId>
class CPastelTicket : public CPastelTicketBase {
public:
	CPastelTicket() = default;
	
	TicketID ID() const {return ticketId;}
	short GetVersion() const {return nVersion;}
	virtual bool HasKeyTwo() const {return false;}
	virtual bool HasMVKeyOne() const {return false;}
	virtual bool HasMVKeyTwo() const {return false;}
    
    virtual std::string KeyOne() const = 0; 		        //Key to the object itself
    virtual std::string KeyTwo() const {return "";}
    virtual std::string MVKeyOne() const {return "";}
    virtual std::string MVKeyTwo() const {return "";}
    
    virtual void SetKeyOne(std::string val) = 0;
};

// PastelID Ticket //////////////////////////////////////////////////////////////////////////////////////////////////////
class CPastelIDRegTicket : public CPastelTicket<TicketID::PastelID>
{
public:
	std::string pastelID;
	std::string address;
    COutPoint outpoint{};
	std::vector<unsigned char> mn_signature;
	std::vector<unsigned char> pslid_signature;
    
    std::string secondKey; //local only

public:
    CPastelIDRegTicket() = default;
    explicit CPastelIDRegTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {nVersion = 1;}

    std::string TicketName() const override {return "pastelid";}
    
    std::string KeyOne() const override {return pastelID;}
    std::string KeyTwo() const override {return outpoint.IsNull() ? (secondKey.empty() ? address : secondKey) : outpoint.ToStringShort();}
    
    bool HasKeyTwo() const override {return true;}
    void SetKeyOne(std::string val) override { pastelID = std::move(val); }
    
    std::string ToJSON() const override;
    std::string ToStr() const override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    
    CAmount TicketPrice(int nHeight) const override {return nHeight<=10000? 10: 1000;}
    
    std::string PastelIDType() const {return outpoint.IsNull()? "personal": "masternode";}
    
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
    static std::vector<CPastelIDRegTicket> FindAllTicketByPastelAddress(const std::string& address);
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
                "author": bytes             //same as above
                "order_block_txid": bytes   //
                "blocknum": integer,        //same as above   
                "imagedata_hash": bytes,    //same as above

                "artist_name": string,
                "artist_website": string,
                "artist_written_statement": string,
                "artwork_title": string,
                "artwork_series_name": string,
                "artwork_creation_video_youtube_url": string,
                "artwork_keyword_set": string,
                "total_copies": integer,    //same as above
            
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
class CArtRegTicket : public CPastelTicket<TicketID::Art>
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
    explicit CArtRegTicket(std::string _ticket) : artTicket(std::move(_ticket)) {nVersion = 1;}
    std::string TicketName() const override {return "art-reg";}
    
    std::string KeyOne() const override {return keyOne;}
    std::string KeyTwo() const override {return keyTwo;}
    std::string MVKeyOne() const override {return pastelIDs[artistsign];}
    
    bool HasKeyTwo() const override {return true;}
    bool HasMVKeyOne() const override {return true;}
    void SetKeyOne(std::string val) override { keyOne = std::move(val); }
    
    std::string ToJSON() const override;
    std::string ToStr() const override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    CAmount TicketPrice(int nHeight) const override {return nHeight<=10000? 10: 1000;}
    
    ADD_SERIALIZE_METHODS;
	
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(artTicket);
        READWRITE(nVersion);
		
        for (int mn=0; mn<allsigns; mn++) {
            READWRITE(pastelIDs[mn]);
            READWRITE(ticketSignatures[mn]);
        }

        READWRITE(keyOne);
        READWRITE(keyTwo);
        READWRITE(artistHeight);
        READWRITE(totalCopies);
        READWRITE(storageFee);
        READWRITE(timestamp);
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
class CArtActivateTicket : public CPastelTicket<TicketID::Activate>
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

	explicit CArtActivateTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {nVersion = 1;}
    std::string TicketName() const override {return "art-act";}
    
    std::string KeyOne() const override {return regTicketTnxId;}
    std::string MVKeyOne() const override {return pastelID;}
    std::string MVKeyTwo() const override {return std::to_string(artistHeight);}

    bool HasMVKeyOne() const override {return true;}
    bool HasMVKeyTwo() const override {return true;}
    void SetKeyOne(std::string val) override { regTicketTnxId = std::move(val); }
    
    std::string ToJSON() const override;
    std::string ToStr() const override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    CAmount TicketPrice(int nHeight) const override {return nHeight<=10000? 10: 1000;}
	
	ADD_SERIALIZE_METHODS;
	
	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(pastelID);
        READWRITE(nVersion);
		READWRITE(regTicketTnxId);
		READWRITE(artistHeight);
        READWRITE(storageFee);
        READWRITE(signature);
        READWRITE(timestamp);
		READWRITE(ticketTnx);
		READWRITE(ticketBlock);
	}
	
    CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const override;
    
    static CArtActivateTicket Create(std::string _regTicketTxId, int _artistHeight, int _storageFee, std::string _pastelID, const SecureString& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CArtActivateTicket& ticket);

    static std::vector<CArtActivateTicket> FindAllTicketByPastelID(const std::string& pastelID);
    static std::vector<CArtActivateTicket> FindAllTicketByArtistHeight(int height);
    static bool CheckTicketExistByArtTicketID(const std::string& regTicketTnxId);
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

class CArtSellTicket : public CPastelTicket<TicketID::Sell>
{
public:
    std::string pastelID;
    std::string artTnxId;
    unsigned int askedPrice{};
    unsigned int activeAfter{};              //as a block height
    unsigned int activeBefore{};             //as a block height
    unsigned short copyNumber{};
    std::string reserved;
    std::vector<unsigned char> signature;
    
    std::string key;

public:
    CArtSellTicket() = default;
    
    explicit CArtSellTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {nVersion = 1;}
    std::string TicketName() const override {return "art-sell";}
    
    std::string KeyOne() const override {return !key.empty()? key: artTnxId+":"+to_string(copyNumber);} //txid:#
    std::string MVKeyOne() const override {return pastelID;}
    std::string MVKeyTwo() const override {return artTnxId;}
    
    bool HasMVKeyOne() const override {return true;}
    bool HasMVKeyTwo() const override {return true;}
    void SetKeyOne(std::string val) override { key = std::move(val); }
    
    std::string ToJSON() const override;
    std::string ToStr() const override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    CAmount TicketPrice(int nHeight) const override {return askedPrice/50;}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(pastelID);
        READWRITE(nVersion);
        READWRITE(artTnxId);
        READWRITE(askedPrice);
        READWRITE(activeAfter);
        READWRITE(activeBefore);
        READWRITE(copyNumber);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(timestamp);
        READWRITE(ticketTnx);
        READWRITE(ticketBlock);
    }
    
    static CArtSellTicket Create(std::string _artTnxId, int _askedPrice, int _validAfter, int _validBefore, int _copy_number, std::string _pastelID, const SecureString& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CArtSellTicket& ticket);
    
    static std::vector<CArtSellTicket> FindAllTicketByPastelID(const std::string& pastelID);
    static std::vector<CArtSellTicket> FindAllTicketByArtTnxID(const std::string& artTnxId);
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
class CArtBuyTicket : public CPastelTicket<TicketID::Buy>
{
public:
    std::string pastelID;
    std::string sellTnxId;
    unsigned int price{};
    std::string reserved;
    std::vector<unsigned char> signature;
    
public:
    CArtBuyTicket() = default;
    
    explicit CArtBuyTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {nVersion = 1;}
    
    std::string TicketName() const override {return "art-buy";}
    
    std::string KeyOne() const override {return sellTnxId;} // this is the latest (active) buy ticket for this sell ticket
    std::string MVKeyOne() const override {return pastelID;}
//    std::string MVKeyTwo() const override {return sellTnxId;} // these are all buy (1 active and many inactive) tickets for this sell ticket
    
    bool HasMVKeyOne() const override {return true;}
    bool HasMVKeyTwo() const override {return false;}
    void SetKeyOne(std::string val) override { sellTnxId = std::move(val); }

    CAmount TicketPrice(int nHeight) const override {return price/100;}
    
    std::string ToJSON() const override;
    std::string ToStr() const override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(pastelID);
        READWRITE(nVersion);
        READWRITE(sellTnxId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(timestamp);
        READWRITE(ticketTnx);
        READWRITE(ticketBlock);
    }
    
    static CArtBuyTicket Create(std::string _sellTnxId, int _price, std::string _pastelID, const SecureString& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CArtBuyTicket& ticket);

    static bool CheckBuyTicketExistBySellTicket(const std::string& _sellTnxId);
    
    static std::vector<CArtBuyTicket> FindAllTicketByPastelID(const std::string& pastelID);
};

/*
	"ticket": {
		"type": "trade",
		"pastelID": "",     //PastelID of the buyer
		"sell_txid": "",    //txid with sale ticket
		"buy_txid": "",     //txid with buy ticket
		"art_txid": "",     //txid with either 1) art activation ticket or 2) trade ticket in it
		"price": "",
		"reserved": "",
		"signature": ""
	},
 */
class CArtTradeTicket : public CPastelTicket<TicketID::Trade>
{
public:
    std::string pastelID;
    std::string sellTnxId;
    std::string buyTnxId;
    std::string artTnxId;
    unsigned int price{};
    std::string reserved;
    std::vector<unsigned char> signature;

public:
    CArtTradeTicket() = default;

    explicit CArtTradeTicket(std::string _pastelID) : pastelID(std::move(_pastelID)) {nVersion = 1;}

    std::string TicketName() const override {return "art-trade";}
    
    std::string KeyOne() const override {return sellTnxId;}
    std::string KeyTwo() const override {return buyTnxId;}
    std::string MVKeyOne() const override {return pastelID;}
    std::string MVKeyTwo() const override {return artTnxId;}
    
    bool HasKeyTwo() const override {return true;}
    bool HasMVKeyOne() const override {return true;}
    bool HasMVKeyTwo() const override {return true;}
    
    void SetKeyOne(std::string val) override { sellTnxId = std::move(val); }
    
    std::string ToJSON() const override;
    std::string ToStr() const override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    CAmount TicketPrice(int nHeight) const override {return nHeight<=10000? 10: 1000;}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(pastelID);
        READWRITE(nVersion);
        READWRITE(sellTnxId);
        READWRITE(buyTnxId);
        READWRITE(artTnxId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(timestamp);
        READWRITE(ticketTnx);
        READWRITE(ticketBlock);
    }

    CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const override;
    
    static CArtTradeTicket Create(std::string _sellTnxId, std::string _buyTnxId, std::string _pastelID, const SecureString& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CArtTradeTicket& ticket);
    
    static std::vector<CArtTradeTicket> FindAllTicketByPastelID(const std::string& pastelID);
    static std::vector<CArtTradeTicket> FindAllTicketByArtTnxID(const std::string& artTnxID);
    
    static bool CheckTradeTicketExistBySellTicket(const std::string& _sellTnxId);
    static bool CheckTradeTicketExistByBuyTicket(const std::string& _buyTnxId);
    static bool GetTradeTicketBySellTicket(const std::string& _sellTnxId, CArtTradeTicket& ticket);
    static bool GetTradeTicketByBuyTicket(const std::string& _buyTnxId, CArtTradeTicket& ticket);
};

// Take Down Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
class CTakeDownTicket : public CPastelTicket<TicketID::Down>
{
public:
    static bool FindTicketInDb(const std::string& key, CTakeDownTicket& ticket);
    CAmount TicketPrice(int nHeight) const override {return nHeight<=10000? 1000: 100000;}
};

#define FAKE_TICKET
// Ticket  Processor ////////////////////////////////////////////////////////////////////////////////////////////////////
class CPastelTicketProcessor {
	map<TicketID, std::unique_ptr<CDBWrapper> > dbs;
    
    template<class T, TicketID ticketId, typename F>
    void listTickets(F f);
    
    template<class T, TicketID ticketId, typename F>
    std::string filterTickets(F f);

public:
	CPastelTicketProcessor() = default;
	
	void InitTicketDB();
	void UpdatedBlockTip(const CBlockIndex *cBlockIndex, bool fInitialDownload);
	bool ParseTicketAndUpdateDB(CMutableTransaction& tx, int nBlockHeight);
 
    static std::string RealKeyTwo(const std::string& key) {return "@2@" + key;}
    static std::string RealMVKey(const std::string& key) {return "@M@" + key;}
    
    template<class T>
	bool UpdateDB(T& ticket, string &txid, int nBlockHeight);
    template<class T>
    void UpdateDB_MVK(const T& ticket, const std::string& mvKey);
	
	template<class T>
	bool CheckTicketExist(const T& ticket);
	template<class T>
	bool FindTicket(T& ticket);

	template<class T>
	bool CheckTicketExistBySecondaryKey(const T& ticket);
	template<class T>
	bool FindTicketBySecondaryKey(T& ticket);
    template<class T>
    std::vector<T> FindTicketsByMVKey(TicketID ticketId, const std::string& mvKey);
    
    std::vector<std::string> GetAllKeys(TicketID id);

    template<class T, TicketID ticketId>
    std::string ListTickets();
    
    std::string ListFilterPastelIDTickets(short filter = 0);    // 1 - mn;        2 - personal
    std::string ListFilterArtTickets(short filter = 0);         // 1 - active;    2 - inactive;     3 - sold
    std::string ListFilterActTickets(short filter = 0);         // 1 - available; 2 - sold
    std::string ListFilterSellTickets(short filter = 0);        // 1 - available; 2 - unavailable;  3 - expired; 4 - sold
    std::string ListFilterBuyTickets(short filter = 0);         // 1 - traded;    2 - expired
    std::string ListFilterTradeTickets(short filter = 0);       // 1 - available; 2 - sold

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
    template<class T>
    static std::unique_ptr<T> GetTicket(const std::string& _txid, TicketID _ticketID);
	static std::string GetTicketJSON(uint256 txid);

	static bool ValidateIfTicketTransaction(const int nHeight, const CTransaction& tx);
	
/*
    std::string TicketNames[] = {
            "PastelID"
            "Art"
            "Activate"
            "Sell"
            "Buy"
            "Trade"
            "Down"};
*/

#ifdef FAKE_TICKET
    template<class T>
    static std::string CreateFakeTransaction(T& ticket, CAmount ticketPrice, const std::vector<std::pair<std::string, CAmount>>& extraPayments, const std::string& strVerb, bool bSend);
#endif
};

#endif //MASTERNODEPASTEL_H
