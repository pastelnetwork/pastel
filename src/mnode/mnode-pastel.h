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
    std::string pq_key;
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
    bool IsValid(bool preReg, int depth) const override;
    
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
        if (bVersion) {
            //if (v1 or higher) and ( (writing to stream) or (reading but not end of the stream yet))
            READWRITE(m_nVersion);
            READWRITE(pq_key);
        } else if (bRead) {
            m_nVersion = 0;
            pq_key = "";
        }
    }
	
    static CPastelIDRegTicket Create(std::string _pastelID, const SecureString& strKeyPass, std::string _address);
    static bool FindTicketInDb(const std::string& key, CPastelIDRegTicket& ticket);
    static std::vector<CPastelIDRegTicket> FindAllTicketByPastelAddress(const std::string& address);
};

// Art Registration Ticket //////////////////////////////////////////////////////////////////////////////////////////////
/*

Ticket as base64(RegistrationTicket({some data}))

bytes fields are base64 as strings

  "version": integer          // 1
  "author": bytes,            // PastelID of the author (artist)
  "blocknum": integer,        // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes         // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "copies": integer,          // number of copies
  "royalty": float,           // (not yet supported by cNode) how much artist should get on all future resales
  "green": string,            // address for Green NFT payment (not yet supported by cNode)

  "app_ticket": bytes,        // cNode DOES NOT parse this part!!!!
  as base64(
  {
    "artist_name": string,
    "artwork_title": string,
    "artwork_series_name": string,
    "artwork_keyword_set": string,
    "artist_website": string,
    "artist_written_statement": string,
    "artwork_creation_video_youtube_url": string,

    "thumbnail_hash": bytes,    //hash of the thumbnail !!!!SHA3-256!!!!
		"data_hash": bytes,         // hash of the image (or any other asset) that this ticket represents !!!!SHA3-256!!!!

    "fingerprints_hash": bytes, 			//hash of the fingerprint !!!!SHA3-256!!!!
    "fingerprints": bytes,      			//compressed fingerprint
    "fingerprints_signature": bytes,  //signature on raw image fingerprint

    "rq_ids": [list of strings],//raptorq symbol identifiers -  !!!!SHA3-256 of symbol block!!!!
    "rq_coti": integer64,       //raptorq CommonOTI
    "rq_ssoti": integer64,      //raptorq SchemeSpecificOTI

    "rareness_score": integer,  // 0 to 1000
    "nsfw_score": integer,      // 0 to 1000 0 to 1000
    "seen_score": integer,			//
	},
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
    
    uint16_t nRoyalty{};
    std::string strGreenAddress;
    
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
    bool IsValid(bool preReg, int depth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return 10; }
    CAmount GreenPercent(const unsigned int nHeight) const noexcept { return 2; }

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
        READWRITE(nRoyalty);
        READWRITE(strGreenAddress);
        READWRITE(storageFee);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
	}

    std::string GetRoyaltyPayeePastelID();
    std::string GetRoyaltyPayeeAddress();

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
    bool IsValid(bool preReg, int depth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return 10; }
    CAmount GetStorageFee() const noexcept override { return storageFee; }
	
	void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
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
    bool IsValid(bool preReg, int depth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return askedPrice/50; }
    
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
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
    bool IsValid(bool preReg, int depth) const override;
    
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
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
    bool IsValid(bool preReg, int depth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return 10; }
    
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
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
    
    std::unique_ptr<CPastelTicket> FindArtRegTicket() const;
};

/*
  "ticket": {
    "type": "royalty",
    "version": "",
    "pastelID": "",     //pastelID of the old (current at moment of creation) royalty recipient
    "new_pastelID": "", //pastelID of the new royalty recipient
    "art_txid": "",     //txid of the art for royalty payments
    "signature": ""
  }
*/

class CArtRoyaltyTicket : public CPastelTicket {
public:
  std::string pastelID;    //pastelID of the old (current at moment of creation) royalty recipient
  std::string newPastelID; //pastelID of the new royalty recipient
  std::string artTnxId;    //txid of the art for royalty payments
  std::vector<unsigned char> signature;

public:
  CArtRoyaltyTicket() = default;

  explicit CArtRoyaltyTicket(std::string _pastelID, std::string _newPastelID)
      : pastelID(std::move(_pastelID)), newPastelID(std::move(_newPastelID)) {
  }

  TicketID ID() const noexcept final { return TicketID::Royalty; }
  static TicketID GetID() { return TicketID::Royalty; }

  std::string KeyOne() const noexcept final { return {signature.cbegin(), signature.cend()}; }
  std::string MVKeyOne() const noexcept final { return pastelID; }
  std::string MVKeyTwo() const noexcept final { return artTnxId; }

  bool HasMVKeyOne() const noexcept final { return true; }
  bool HasMVKeyTwo() const noexcept final { return true; }
  void SetKeyOne(std::string val) final { signature.assign(val.begin(), val.end()); }

  std::string ToJSON() const noexcept final;
  std::string ToStr() const noexcept final;
  bool IsValid(bool preReg, int depth) const final;
  CAmount TicketPrice(const unsigned int nHeight) const noexcept final { return 10; }

  void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) final {
    const bool bRead{ser_action == SERIALIZE_ACTION::Read};
    std::string error;
    if (!VersionMgmt(error, bRead))
      throw std::runtime_error(error);
    READWRITE(pastelID);
    READWRITE(newPastelID);
    READWRITE(m_nVersion);
    // v0
    READWRITE(artTnxId);
    READWRITE(signature);
    READWRITE(m_nTimestamp);
    READWRITE(m_txid);
    READWRITE(m_nBlock);
  }

  static CArtRoyaltyTicket Create(std::string _pastelID, std::string _newPastelID,
                                  std::string _artTnxId, const SecureString& strKeyPass);
  static bool FindTicketInDb(const std::string& key, CArtRoyaltyTicket& ticket);

  static std::vector<CArtRoyaltyTicket> FindAllTicketByPastelID(const std::string& pastelID);
  static std::vector<CArtRoyaltyTicket> FindAllTicketByArtTnxId(const std::string& artTnxId);
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
    bool IsValid(bool preReg, int depth) const override { return false; }
    std::string KeyOne() const noexcept override { return ""; }
    void SetKeyOne(std::string val) override {}

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override {}
};
