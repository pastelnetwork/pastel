#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <string>

#include "mnode/mnode-consts.h"
#include "mnode/ticket.h"

// PastelID Ticket //////////////////////////////////////////////////////////////////////////////////////////////////////
class CPastelIDRegTicket : public CPastelTicket
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
    explicit CPastelIDRegTicket(std::string _pastelID) : 
        pastelID(std::move(_pastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::PastelID; }
    static TicketID GetID() { return TicketID::PastelID; }

    std::string KeyOne() const noexcept override {return pastelID;}
    std::string KeyTwo() const noexcept override { return outpoint.IsNull() ? (secondKey.empty() ? address : secondKey) : outpoint.ToStringShort(); }
    
    bool HasKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { pastelID = std::move(val); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return nHeight<=10000? 10: 1000; }
    
    std::string PastelIDType() const {return outpoint.IsNull()? "personal": "masternode";}
    
	void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        // v0
		READWRITE(pastelID);
		READWRITE(address);
		READWRITE(outpoint);
		READWRITE(m_nTimestamp);
		READWRITE(mn_signature);
		READWRITE(pslid_signature);
		READWRITE(m_txid);
		READWRITE(m_nBlock);
        // v1
        const bool bVersion = (GetVersion() >= 1) && (!bRead || !s.eof());
        if (bVersion)
            READWRITE(m_nVersion);
        else if (bRead)
            m_nVersion = 0;
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
class CArtRegTicket : public CPastelTicket
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
    explicit CArtRegTicket(std::string _ticket) :
        artTicket(std::move(_ticket))
    {}
    
    TicketID ID() const noexcept override { return TicketID::Art; }
    static TicketID GetID() { return TicketID::Art; }

    std::string KeyOne() const noexcept override { return keyOne; }
    std::string KeyTwo() const noexcept override { return keyTwo; }
    std::string MVKeyOne() const noexcept override { return pastelIDs[artistsign]; }
    
    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { keyOne = std::move(val); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return nHeight<=10000? 10: 1000; }
    
	void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(artTicket);
        READWRITE(m_nVersion);
		
        // v0
        for (int mn=0; mn<allsigns; mn++)
        {
            READWRITE(pastelIDs[mn]);
            READWRITE(ticketSignatures[mn]);
        }

        READWRITE(keyOne);
        READWRITE(keyTwo);
        READWRITE(artistHeight);
        READWRITE(totalCopies);
        READWRITE(storageFee);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
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
class CArtActivateTicket : public CPastelTicket
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

	explicit CArtActivateTicket(std::string _pastelID) :
        pastelID(std::move(_pastelID))
    {}
    
    TicketID ID() const noexcept override { return TicketID::Activate; }
    static TicketID GetID() { return TicketID::Activate; }

    std::string KeyOne() const noexcept override { return regTicketTnxId; }
    std::string MVKeyOne() const noexcept override { return pastelID; }
    std::string MVKeyTwo() const noexcept override { return std::to_string(artistHeight); }

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { regTicketTnxId = std::move(val); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return nHeight<=10000? 10: 1000; }
    CAmount GetStorageFee() const noexcept override { return storageFee; }
	
	void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
		READWRITE(regTicketTnxId);
		READWRITE(artistHeight);
        READWRITE(storageFee);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
		READWRITE(m_txid);
		READWRITE(m_nBlock);
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

class CArtSellTicket : public CPastelTicket
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
    
    explicit CArtSellTicket(std::string _pastelID) : 
        pastelID(std::move(_pastelID))
    {}
    
    TicketID ID() const noexcept override { return TicketID::Sell; }
    static TicketID GetID() { return TicketID::Sell; }

    std::string KeyOne() const noexcept override { return !key.empty() ? key : artTnxId + ":" + std::to_string(copyNumber); } //txid:#
    std::string MVKeyOne() const noexcept override { return pastelID; }
    std::string MVKeyTwo() const noexcept override { return artTnxId; }
    
    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { key = std::move(val); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return askedPrice/50; }
    
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(artTnxId);
        READWRITE(askedPrice);
        READWRITE(activeAfter);
        READWRITE(activeBefore);
        READWRITE(copyNumber);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
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
class CArtBuyTicket : public CPastelTicket
{
public:
    std::string pastelID;
    std::string sellTnxId;
    unsigned int price{};
    std::string reserved;
    std::vector<unsigned char> signature;
    
public:
    CArtBuyTicket() = default;
    
    explicit CArtBuyTicket(std::string _pastelID) : 
        pastelID(std::move(_pastelID))
    {}
    
    TicketID ID() const noexcept override { return TicketID::Buy; }
    static TicketID GetID() { return TicketID::Buy; }

    std::string KeyOne() const noexcept override { return sellTnxId; } // this is the latest (active) buy ticket for this sell ticket
    std::string MVKeyOne() const noexcept override { return pastelID; }
    //    std::string MVKeyTwo() const override {return sellTnxId;} // these are all buy (1 active and many inactive) tickets for this sell ticket
    
    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return false; }
    void SetKeyOne(std::string val) override { sellTnxId = std::move(val); }

    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return price/100; }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(sellTnxId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
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
class CArtTradeTicket : public CPastelTicket
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

    explicit CArtTradeTicket(std::string _pastelID) : 
        pastelID(std::move(_pastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::Trade; }
    static TicketID GetID() { return TicketID::Trade; }

    std::string KeyOne() const noexcept override { return sellTnxId; }
    std::string KeyTwo() const noexcept override { return buyTnxId; }
    std::string MVKeyOne() const noexcept override { return pastelID; }
    std::string MVKeyTwo() const noexcept override { return artTnxId; }
    
    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    
    void SetKeyOne(std::string val) override { sellTnxId = std::move(val); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(std::string& errRet, bool preReg, int depth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return nHeight<=10000? 10: 1000; }
    
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(sellTnxId);
        READWRITE(buyTnxId);
        READWRITE(artTnxId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
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
class CTakeDownTicket : public CPastelTicket
{
public:
    static bool FindTicketInDb(const std::string& key, CTakeDownTicket& ticket);
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return nHeight<=10000? 1000: 100000; }

    TicketID ID() const noexcept override { return TicketID::Down; }
    static TicketID GetID() { return TicketID::Down; }

    std::string ToJSON() const noexcept override { return "{}"; }
    std::string ToStr() const noexcept override { return ""; }
    bool IsValid(std::string& errRet, bool preReg, int depth) const override { return false; }
    std::string KeyOne() const noexcept override { return ""; }
    void SetKeyOne(std::string val) override {}

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override {}
};

