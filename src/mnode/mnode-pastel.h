#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <string>

#include "mnode/mnode-consts.h"
#include "mnode/ticket.h"
#include <unordered_set>

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

// Username Change Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "username",
		"pastelID": "",    //PastelID of the username
		"username": "",    //new valid username
		"fee": "",         // fee to change username
		"signature": ""
	},
 */
class CChangeUsernameTicket : public CPastelTicket
{
public:
    std::string pastelID;
    std::string username;
    CAmount fee{100};
    std::vector<unsigned char> signature;

public:
    CChangeUsernameTicket() = default;

	explicit CChangeUsernameTicket(std::string _pastelID) :
        pastelID(std::move(_pastelID))
    {}
    std::string KeyOne() const noexcept override { return username; }
    std::string KeyTwo() const noexcept override { return pastelID; }

    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return false; }
    bool HasMVKeyTwo() const noexcept override { return false; }

    void SetKeyOne(std::string val) override { username = std::move(val); }

    static CChangeUsernameTicket Create(std::string _pastelID, std::string _username, const SecureString& strKeyPass);
    
    static bool FindTicketInDb(const std::string& key, CChangeUsernameTicket& ticket);
    
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return fee; }

    TicketID ID() const noexcept override { return TicketID::Username; }
    static TicketID GetID() { return TicketID::Username; }

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
        READWRITE(username);
        READWRITE(fee);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    /** Some general checks to see if the username is bad. Below cases will be considered as bad Username
    *     - Contains characters that is different than upper and lowercase Latin characters and numbers
    *     - Has only <4, or has more than 12 characters
    *     - Doesn't start with letters.
    *     - Username registered on the blockchain.
    *     - Contains bad words (swear, racist,...)
    * return: true if bad, false if good to use
    */
    static bool isUsernameBad(const std::string& username, std::string& error);

};

struct UsernameBadWords {
public:
    // The list was get from:
    // https://github.com/dariusk/wordfilter/blob/master/lib/badwords.json
    // and
    // https://github.com/stephenhaunts/ProfanityDetector/blob/main/ProfanityFilter/ProfanityFilter/ProfanityList.cs
    const std::unordered_set<std::string> wordSet{
        "abbo",
        "abo",
        "beeyotch",
        "biatch",
        "bitch",
        "chinaman",
        "chinamen",
        "chink",
        "coolie",
        "coon",
        "crazie",
        "crazy",
        "crip",
        "cuck",
        "cunt",
        "dago",
        "daygo",
        "dego",
        "dick",
        "douchebag",
        "dumb",
        "dyke",
        "eskimo",
        "fag",
        "faggot",
        "fatass",
        "fatso",
        "gash",
        "gimp",
        "golliwog",
        "gook",
        "goy",
        "goyim",
        "gyp",
        "gypsy",
        "half-breed",
        "halfbreed",
        "heeb",
        "homo",
        "hooker",
        "idiot",
        "insane",
        "insanitie",
        "insanity",
        "jap",
        "kaffer",
        "kaffir",
        "kaffir",
        "kaffre",
        "kafir",
        "kike",
        "kraut",
        "lame",
        "lardass",
        "lesbo",
        "lunatic",
        "mick",
        "negress",
        "negro",
        "nig",
        "nig-nog",
        "nigga",
        "nigger",
        "nigguh",
        "nip",
        "pajeet",
        "paki",
        "pickaninnie",
        "pickaninny",
        "prostitute",
        "pussie",
        "pussy",
        "raghead",
        "retard",
        "sambo",
        "shemale",
        "skank",
        "slut",
        "soyboy",
        "spade",
        "sperg",
        "spic",
        "spook",
        "squaw",
        "street-shitter",
        "tard",
        "tits",
        "titt",
        "trannie",
        "tranny",
        "twat",
        "wetback",
        "whore",
        "wigger",
        "wop",
        "yid",
        "zog",
        "2 girls 1 cup",
        "2 girls one cup",
        "2g1c",
        "4r5e",
        "5h1t",
        "5hit",
        "8===D",
        "8==D",
        "8=D",
        "a$$",
        "a$$hole",
        "a_s_s",
        "a2m",
        "a55",
        "a55hole",
        "acrotomophilia",
        "aeolus",
        "ahole",
        "alabama hot pocket",
        "alaskan pipeline",
        "anal",
        "anal impaler",
        "anal leakage",
        "analprobe",
        "anilingus",
        "angrydragon",
        "angry dragon",
        "anus",
        "apeshit",
        "ar5e",
        "arian",
        "arrse",
        "arse",
        "arses",
        "arsehole",
        "aryan",
        "ass",
        "ass fuck",
        "ass hole",
        "assbag",
        "assbandit",
        "assbang",
        "assbanged",
        "assbanger",
        "assbangs",
        "assbite",
        "assclown",
        "asscock",
        "asscracker",
        "asses",
        "assface",
        "assfaces",
        "assfuck",
        "assfucker",
        "ass-fucker",
        "assfukka",
        "assgoblin",
        "assh0le",
        "asshat",
        "ass-hat",
        "asshead",
        "assho1e",
        "asshole",
        "assholes",
        "asshopper",
        "ass-jabber",
        "assjacker",
        "asslick",
        "asslicker",
        "assmaster",
        "assmonkey",
        "assmucus",
        "assmunch",
        "assmuncher",
        "assnigger",
        "asspirate",
        "ass-pirate",
        "assshit",
        "assshole",
        "asssucker",
        "asswad",
        "asswhole",
        "asswipe",
        "asswipes",
        "auto erotic",
        "autoerotic",
        "axwound",
        "axewound",
        "axe wound",
        "azazel",
        "azz",
        "b!tch",
        "b00bs",
        "b17ch",
        "b1tch",
        "babeland",
        "baby batter",
        "baby juice",
        "ball gag",
        "ball gravy",
        "ball kicking",
        "ball licking",
        "ball sack",
        "ball sucking",
        "ballbag",
        "balls",
        "ballsack",
        "bampot",
        "bang (one's) box",
        "bangbros",
        "bareback",
        "barely legal",
        "barenaked",
        "barf",
        "bastard",
        "bastardo",
        "bastards",
        "bastinado",
        "batty boy",
        "bawdy",
        "bbw",
        "bdsm",
        "beaner",
        "beaners",
        "beardedclam",
        "beastial",
        "beastiality",
        "beatch",
        "beaver",
        "beaver cleaver",
        "beaver lips",
        "beef curtain",
        "beef curtains",
        "beeyotch",
        "bellend",
        "bender",
        "beotch",
        "bescumber",
        "bestial",
        "bestiality",
        "bi+ch",
        "biatch",
        "big black",
        "big breasts",
        "big knockers",
        "big tits",
        "bigtits",
        "bimbo",
        "bimbos",
        "bint",
        "birdlock",
        "bitch",
        "bitch tit",
        "bitchass",
        "bitched",
        "bitcher",
        "bitchers",
        "bitches",
        "bitchin",
        "bitching",
        "bitchtits",
        "bitchy",
        "black cock",
        "blonde action",
        "blonde on blonde action",
        "bloodclaat",
        "bloody",
        "bloody hell",
        "blow job",
        "blow me",
        "blow mud",
        "blow your load",
        "blowjob",
        "blowjobs",
        "blue waffle",
        "blumpkin",
        "bod",
        "bodily",
        "boink",
        "boiolas",
        "bollock",
        "bollocks",
        "bollok",
        "bollox",
        "bondage",
        "boned",
        "boner",
        "boners",
        "bong",
        "boob",
        "boobies",
        "boobs",
        "booby",
        "booger",
        "bookie",
        "boong",
        "booobs",
        "boooobs",
        "booooobs",
        "booooooobs",
        "bootee",
        "bootie",
        "booty",
        "booty call",
        "booze",
        "boozer",
        "boozy",
        "bosom",
        "bosomy",
        "breasts",
        "breeder",
        "brotherfucker",
        "brown showers",
        "brunette action",
        "buceta",
        "bugger",
        "bukkake",
        "bull shit",
        "bulldyke",
        "bullet vibe",
        "bullshit",
        "bullshits",
        "bullshitted",
        "bullturds",
        "bum",
        "bum boy",
        "bumblefuck",
        "bumclat",
        "bummer",
        "buncombe",
        "bung",
        "bung hole",
        "bunghole",
        "bunny fucker",
        "bust a load",
        "busty",
        "butt",
        "butt fuck",
        "butt plug",
        "buttcheeks",
        "buttfuck",
        "buttfucka",
        "buttfucker",
        "butthole",
        "buttmuch",
        "buttmunch",
        "butt-pirate",
        "buttplug",
        "c.0.c.k",
        "c.o.c.k.",
        "c.u.n.t",
        "c0ck",
        "c-0-c-k",
        "c0cksucker",
        "caca",
        "cacafuego",
        "cahone",
        "camel toe",
        "cameltoe",
        "camgirl",
        "camslut",
        "camwhore",
        "carpet muncher",
        "carpetmuncher",
        "cawk",
        "cervix",
        "chesticle",
        "chi-chi man",
        "chick with a dick",
        "child-fucker",
        "chinc",
        "chincs",
        "chink",
        "chinky",
        "choad",
        "choade",
        "choc ice",
        "chocolate rosebuds",
        "chode",
        "chodes",
        "chota bags",
        "cipa",
        "circlejerk",
        "cl1t",
        "cleveland steamer",
        "climax",
        "clit",
        "clit licker",
        "clitface",
        "clitfuck",
        "clitoris",
        "clits",
        "clitty",
        "clitty litter",
        "clover clamps",
        "clunge",
        "clusterfuck",
        "cnut",
        "cocain",
        "cocaine",
        "coccydynia",
        "cock",
        "c-o-c-k",
        "cock pocket",
        "cock snot",
        "cock sucker",
        "cockass",
        "cockbite",
        "cockblock",
        "cockburger",
        "cockeye",
        "cockface",
        "cockfucker",
        "cockhead",
        "cockholster",
        "cockjockey",
        "cockknocker",
        "cockknoker",
        "cocklump",
        "cockmaster",
        "cockmongler",
        "cockmongruel",
        "cockmonkey",
        "cockmunch",
        "cockmuncher",
        "cocknose",
        "cocknugget",
        "cocks",
        "cockshit",
        "cocksmith",
        "cocksmoke",
        "cocksmoker",
        "cocksniffer",
        "cocksuck",
        "cocksucked",
        "cocksucker",
        "cock-sucker",
        "cocksuckers",
        "cocksucking",
        "cocksucks",
        "cocksuka",
        "cocksukka",
        "cockwaffle",
        "coffin dodger",
        "coital",
        "cok",
        "cokmuncher",
        "coksucka",
        "commie",
        "condom",
        "coochie",
        "coochy",
        "coon",
        "coonnass",
        "coons",
        "cooter",
        "cop some wood",
        "coprolagnia",
        "coprophilia",
        "corksucker",
        "cornhole",
        "corp whore",
        "corpulent",
        "cox",
        "crabs",
        "crack",
        "cracker",
        "crackwhore",
        "crap",
        "crappy",
        "creampie",
        "cretin",
        "crikey",
        "cripple",
        "crotte",
        "cum",
        "cum chugger",
        "cum dumpster",
        "cum freak",
        "cum guzzler",
        "cumbubble",
        "cumdump",
        "cumdumpster",
        "cumguzzler",
        "cumjockey",
        "cummer",
        "cummin",
        "cumming",
        "cums",
        "cumshot",
        "cumshots",
        "cumslut",
        "cumstain",
        "cumtart",
        "cunilingus",
        "cunillingus",
        "cunnie",
        "cunnilingus",
        "cunny",
        "cunt",
        "c-u-n-t",
        "cunt hair",
        "cuntass",
        "cuntbag",
        "cuntface",
        "cunthole",
        "cunthunter",
        "cuntlick",
        "cuntlicker",
        "cuntrag",
        "cunts",
        "cuntsicle",
        "cuntslut",
        "cunt-struck",
        "cus",
        "cut rope",
        "cyalis",
        "cyberfuc",
        "cyberfuck",
        "cyberfucked",
        "cyberfucker",
        "cyberfucking",
        "d0ng",
        "d0uch3",
        "d0uche",
        "d1ck",
        "d1ld0",
        "d1ldo",
        "dago",
        "dagos",
        "dammit",
        "damn",
        "damned",
        "damnit",
        "darkie",
        "darn",
        "date rape",
        "daterape",
        "dawgie-style",
        "deep throat",
        "deepthroat",
        "deggo",
        "dendrophilia",
        "dick",
        "dick head",
        "dick hole",
        "dick shy",
        "dickbag",
        "dickbeaters",
        "dickdipper",
        "dickface",
        "dickflipper",
        "dickfuck",
        "dickfucker",
        "dickhead",
        "dickheads",
        "dickhole",
        "dickish",
        "dick-ish",
        "dickjuice",
        "dickmilk",
        "dickmonger",
        "dickripper",
        "dicks",
        "dicksipper",
        "dickslap",
        "dick-sneeze",
        "dicksucker",
        "dicksucking",
        "dicktickler",
        "dickwad",
        "dickweasel",
        "dickweed",
        "dickwhipper",
        "dickwod",
        "dickzipper",
        "diddle",
        "dike",
        "dildo",
        "dildos",
        "diligaf",
        "dillweed",
        "dimwit",
        "dingle",
        "dingleberries",
        "dingleberry",
        "dink",
        "dinks",
        "dipship",
        "dirsa",
        "dirty",
        "dirty pillows",
        "dirty sanchez",
        "div",
        "dlck",
        "dog style",
        "dog-fucker",
        "doggie style",
        "doggiestyle",
        "doggie-style",
        "doggin",
        "dogging",
        "doggy style",
        "doggystyle",
        "doggy-style",
        "dolcett",
        "domination",
        "dominatrix",
        "dommes",
        "dong",
        "donkey punch",
        "donkeypunch",
        "donkeyribber",
        "doochbag",
        "doofus",
        "dookie",
        "doosh",
        "dopey",
        "double dong",
        "double penetration",
        "doublelift",
        "douch3",
        "douche",
        "douchebag",
        "douchebags",
        "douche-fag",
        "douchewaffle",
        "douchey",
        "dp action",
        "drunk",
        "dry hump",
        "duche",
        "dumass",
        "dumb ass",
        "dumbass",
        "dumbasses",
        "dumbcunt",
        "dumbfuck",
        "dumbshit",
        "dummy",
        "dumshit",
        "dvda",
        "dyke",
        "dykes",
        "eat a dick",
        "eat hair pie",
        "eat my ass",
        "ecchi",
        "ejaculate",
        "ejaculated",
        "ejaculates",
        "ejaculating",
        "ejaculatings",
        "ejaculation",
        "ejakulate",
        "erect",
        "erection",
        "erotic",
        "erotism",
        "escort",
        "essohbee",
        "eunuch",
        "extacy",
        "extasy",
        "f u c k",
        "f u c k e r",
        "f.u.c.k",
        "f_u_c_k",
        "f4nny",
        "facial",
        "fack",
        "fag",
        "fagbag",
        "fagfucker",
        "fagg",
        "fagged",
        "fagging",
        "faggit",
        "faggitt",
        "faggot",
        "faggotcock",
        "faggots",
        "faggs",
        "fagot",
        "fagots",
        "fags",
        "fagtard",
        "faig",
        "faigt",
        "fanny",
        "fannybandit",
        "fannyflaps",
        "fannyfucker",
        "fanyy",
        "fart",
        "fartknocker",
        "fatass",
        "fcuk",
        "fcuker",
        "fcuking",
        "fecal",
        "feck",
        "fecker",
        "feist",
        "felch",
        "felcher",
        "felching",
        "fellate",
        "fellatio",
        "feltch",
        "feltcher",
        "female squirting",
        "femdom",
        "fenian",
        "fice",
        "figging",
        "fingerbang",
        "fingerfuck",
        "fingerfucked",
        "fingerfucker",
        "fingerfuckers",
        "fingerfucking",
        "fingerfucks",
        "fingering",
        "fist fuck",
        "fisted",
        "fistfuck",
        "fistfucked",
        "fistfucker",
        "fistfuckers",
        "fistfuckings",
        "fistfucks",
        "fisting",
        "fisty",
        "flamer",
        "flange",
        "flaps",
        "fleshflute",
        "flog the log",
        "floozy",
        "foad",
        "foah",
        "fondle",
        "foobar",
        "fook",
        "fooker",
        "foot fetish",
        "footjob",
        "foreskin",
        "freex",
        "frenchify",
        "frigg",
        "frigga",
        "frotting",
        "fubar",
        "fuc",
        "fuck",
        "f-u-c-k",
        "fuck buttons",
        "fuck hole",
        "fuck off",
        "fuck puppet",
        "fuck trophy",
        "fuck yo mama",
        "fuck you",
        "fucka",
        "fuckass",
        "fuck-ass",
        "fuckbag",
        "fuck bag",
        "fuck-bitch",
        "fuckboy",
        "fuckbrain",
        "fuckbutt",
        "fuckbutter",
        "fucked",
        "fuckedup",
        "fucked up",
        "fucker",
        "fuckers",
        "fuckersucker",
        "fuckface",
        "fuckhead",
        "fuckheads",
        "fuckhole",
        "fuckin",
        "fucking",
        "fuckings",
        "fuckme",
        "fuck me",
        "fuckmeat",
        "fucknugget",
        "fucknut",
        "fucknutt",
        "fuckoff",
        "fucks",
        "fuckstick",
        "fucktard",
        "fuck-tard",
        "fucktards",
        "fucktart",
        "fucktoy",
        "fucktwat",
        "fuckup",
        "fuckwad",
        "fuckwhit",
        "fuckwit",
        "fuckwitt",
        "fudge packer",
        "fudgepacker",
        "fudge-packer",
        "fuk",
        "fuker",
        "fukker",
        "fukkers",
        "fukkin",
        "fuks",
        "fukwhit",
        "fukwit",
        "fuq",
        "futanari",
        "fux",
        "fux0r",
        "fvck",
        "fxck",
        "gae",
        "gai",
        "gang bang",
        "gangbang",
        "gang-bang",
        "gangbanged",
        "gangbangs",
        "ganja",
        "gash",
        "gassy ass",
        "gay sex",
        "gayass",
        "gaybob",
        "gaydo",
        "gayfuck",
        "gayfuckist",
        "gaylord",
        "gays",
        "gaysex",
        "gaytard",
        "gaywad",
        "gender bender",
        "genitals",
        "gey",
        "gfy",
        "ghay",
        "ghey",
        "giant cock",
        "gigolo",
        "ginger",
        "gippo",
        "girl on",
        "girl on top",
        "girls gone wild",
        "glans",
        "goatcx",
        "goatse",
        "god",
        "god damn",
        "godamn",
        "godamnit",
        "goddam",
        "god-dam",
        "goddammit",
        "goddamn",
        "goddamned",
        "god-damned",
        "goddamnit",
        "godsdamn",
        "gokkun",
        "golden shower",
        "goldenshower",
        "golliwog",
        "gonad",
        "gonads",
        "goo girl",
        "gooch",
        "goodpoop",
        "gook",
        "gooks",
        "goregasm",
        "gringo",
        "grope",
        "group sex",
        "gspot",
        "g-spot",
        "gtfo",
        "guido",
        "guro",
        "h0m0",
        "h0mo",
        "ham flap",
        "hand job",
        "handjob",
        "hard core",
        "hard on",
        "hardcore",
        "hardcoresex",
        "he11",
        "hebe",
        "heeb",
        "hell",
        "hemp",
        "hentai",
        "heroin",
        "herp",
        "herpes",
        "herpy",
        "heshe",
        "he-she",
        "hircismus",
        "hitler",
        "hiv",
        "hoar",
        "hoare",
        "hobag",
        "hoe",
        "hoer",
        "holy shit",
        "hom0",
        "homey",
        "homo",
        "homodumbshit",
        "homoerotic",
        "homoey",
        "honkey",
        "honky",
        "hooch",
        "hookah",
        "hooker",
        "hoor",
        "hootch",
        "hooter",
        "hooters",
        "hore",
        "horniest",
        "horny",
        "hot carl",
        "hot chick",
        "hotsex",
        "how to kill",
        "how to murdep",
        "how to murder",
        "huge fat",
        "hump",
        "humped",
        "humping",
        "hun",
        "hussy",
        "hymen",
        "iap",
        "iberian slap",
        "inbred",
        "incest",
        "injun",
        "intercourse",
        "jack off",
        "jackass",
        "jackasses",
        "jackhole",
        "jackoff",
        "jack-off",
        "jaggi",
        "jagoff",
        "jail bait",
        "jailbait",
        "jap",
        "japs",
        "jelly donut",
        "jerk",
        "jerk off",
        "jerk0ff",
        "jerkass",
        "jerked",
        "jerkoff",
        "jerk-off",
        "jigaboo",
        "jiggaboo",
        "jiggerboo",
        "jism",
        "jiz",
        "jizm",
        "jizz",
        "jizzed",
        "jock",
        "juggs",
        "jungle bunny",
        "junglebunny",
        "junkie",
        "junky",
        "kafir",
        "kawk",
        "kike",
        "kikes",
        "kill",
        "kinbaku",
        "kinkster",
        "kinky",
        "klan",
        "knob",
        "knob end",
        "knobbing",
        "knobead",
        "knobed",
        "knobend",
        "knobhead",
        "knobjocky",
        "knobjokey",
        "kock",
        "kondum",
        "kondums",
        "kooch",
        "kooches",
        "kootch",
        "kraut",
        "kum",
        "kummer",
        "kumming",
        "kums",
        "kunilingus",
        "kunja",
        "kunt",
        "kwif",
        "kyke",
        "l3i+ch",
        "l3itch",
        "labia",
        "lameass",
        "lardass",
        "leather restraint",
        "leather straight jacket",
        "lech",
        "lemon party",
        "LEN",
        "leper",
        "lesbian",
        "lesbians",
        "lesbo",
        "lesbos",
        "lez",
        "lezza/lesbo",
        "lezzie",
        "lmao",
        "lmfao",
        "loin",
        "loins",
        "lolita",
        "looney",
        "lovemaking",
        "lube",
        "lust",
        "lusting",
        "lusty",
        "m0f0",
        "m0fo",
        "m45terbate",
        "ma5terb8",
        "ma5terbate",
        "mafugly",
        "make me come",
        "male squirting",
        "mams",
        "masochist",
        "massa",
        "masterb8",
        "masterbat*",
        "masterbat3",
        "masterbate",
        "master-bate",
        "masterbating",
        "masterbation",
        "masterbations",
        "masturbate",
        "masturbating",
        "masturbation",
        "maxi",
        "mcfagget",
        "menage a trois",
        "menses",
        "meth",
        "m-fucking",
        "mick",
        "microphallus",
        "middle finger",
        "midget",
        "milf",
        "minge",
        "minger",
        "missionary position",
        "mof0",
        "mofo",
        "mo-fo",
        "molest",
        "mong",
        "moo moo foo foo",
        "moolie",
        "moron",
        "mothafuck",
        "mothafucka",
        "mothafuckas",
        "mothafuckaz",
        "mothafucked",
        "mothafucker",
        "mothafuckers",
        "mothafuckin",
        "mothafucking",
        "mothafuckings",
        "mothafucks",
        "mother fucker",
        "motherfuck",
        "motherfucka",
        "motherfucked",
        "motherfucker",
        "motherfuckers",
        "motherfuckin",
        "motherfucking",
        "motherfuckings",
        "motherfuckka",
        "motherfucks",
        "mound of venus",
        "mr hands",
        "muff",
        "muff diver",
        "muff puff",
        "muffdiver",
        "muffdiving",
        "munging",
        "munter",
        "murder",
        "mutha",
        "muthafecker",
        "muthafuckker",
        "muther",
        "mutherfucker",
        "n1gga",
        "n1gger",
        "naked",
        "nambla",
        "napalm",
        "nappy",
        "nawashi",
        "nazi",
        "nazism",
        "need the dick",
        "negro",
        "neonazi",
        "nig nog",
        "nigaboo",
        "nigg3r",
        "nigg4h",
        "nigga",
        "niggah",
        "niggas",
        "niggaz",
        "nigger",
        "niggers",
        "niggle",
        "niglet",
        "nig-nog",
        "nimphomania",
        "nimrod",
        "ninny",
        "ninnyhammer",
        "nipple",
        "nipples",
        "nob",
        "nob jokey",
        "nobhead",
        "nobjocky",
        "nobjokey",
        "nonce",
        "nsfw images",
        "nude",
        "nudity",
        "numbnuts",
        "nut butter",
        "nut sack",
        "nutsack",
        "nutter",
        "nympho",
        "nymphomania",
        "octopussy",
        "old bag",
        "omg",
        "omorashi",
        "one cup two girls",
        "1 cup 2 girls",
        "one cup 2 girls",
        "1 cup two girls",
        "one guy one jar",
        "1 guy one jar",
        "one guy 1 jar",
        "opiate",
        "opium",
        "orally",
        "organ",
        "orgasim",
        "orgasims",
        "orgasm",
        "orgasmic",
        "orgasms",
        "orgies",
        "orgy",
        "ovary",
        "ovum",
        "ovums",
        "p.u.s.s.y.",
        "p.u.s.s.y",
        "p0rn",
        "paedophile",
        "paki",
        "panooch",
        "pansy",
        "pantie",
        "panties",
        "panty",
        "pawn",
        "pcp",
        "pecker",
        "peckerhead",
        "pedo",
        "pedobear",
        "pedophile",
        "pedophilia",
        "pedophiliac",
        "pee",
        "peepee",
        "pegging",
        "penetrate",
        "penetration",
        "penial",
        "penile",
        "penis",
        "penisbanger",
        "penisfucker",
        "penispuffer",
        "perversion",
        "phallic",
        "phone sex",
        "phonesex",
        "phuck",
        "phuk",
        "phuked",
        "phuking",
        "phukked",
        "phukking",
        "phuks",
        "phuq",
        "piece of shit",
        "pigfucker",
        "pikey",
        "pillowbiter",
        "pimp",
        "pimpis",
        "pinko",
        "piss",
        "piss off",
        "piss pig",
        "pissed",
        "pissed off",
        "pisser",
        "pissers",
        "pisses",
        "pissflaps",
        "piss flaps",
        "pissin",
        "pissing",
        "pissoff",
        "piss-off",
        "pisspig",
        "playboy",
        "pleasure chest",
        "polack",
        "pole smoker",
        "polesmoker",
        "pollock",
        "ponyplay",
        "poof",
        "poon",
        "poonani",
        "poonany",
        "poontang",
        "poop",
        "poop chute",
        "poopchute",
        "Poopuncher",
        "porch monkey",
        "porchmonkey",
        "porn",
        "porno",
        "pornography",
        "pornos",
        "potty",
        "prick",
        "pricks",
        "prickteaser",
        "prig",
        "prince albert piercing",
        "prod",
        "pron",
        "prone bone",
        "pronebone",
        "prone-bone",
        "prostitute",
        "prude",
        "psycho",
        "pthc",
        "pube",
        "pubes",
        "pubic",
        "pubis",
        "punani",
        "punanny",
        "punany",
        "punkass",
        "punky",
        "punta",
        "puss",
        "pusse",
        "pussi",
        "pussies",
        "pussy",
        "pussy fart",
        "pussy palace",
        "pussylicking",
        "pussypounder",
        "pussys",
        "pust",
        "puto",
        "queaf",
        "queef",
        "queer",
        "queerbait",
        "queerhole",
        "queero",
        "queers",
        "quicky",
        "quim",
        "racy",
        "raghead",
        "raging boner",
        "rape",
        "raped",
        "raper",
        "rapey",
        "raping",
        "rapist",
        "raunch",
        "rectal",
        "rectum",
        "rectus",
        "reefer",
        "reetard",
        "reich",
        "renob",
        "retard",
        "retarded",
        "reverse cowgirl",
        "revue",
        "rimjaw",
        "rimjob",
        "rimming",
        "ritard",
        "rosy palm",
        "rosy palm and her 5 sisters",
        "rtard",
        "r-tard",
        "rubbish",
        "rum",
        "rump",
        "rumprammer",
        "ruski",
        "rusty trombone",
        "s&m",
        "s.h.i.t.",
        "s.o.b.",
        "s_h_i_t",
        "s0b",
        "sadism",
        "sadist",
        "sambo",
        "sand nigger",
        "sandbar",
        "Sandler",
        "sandnigger",
        "sanger",
        "santorum",
        "sausage queen",
        "scag",
        "scantily",
        "scat",
        "schizo",
        "schlong",
        "scissoring",
        "screw",
        "screwed",
        "screwing",
        "scroat",
        "scrog",
        "scrot",
        "scrote",
        "scrotum",
        "scrud",
        "scum",
        "seaman",
        "seduce",
        "seks",
        "semen",
        "sex",
        "sexo",
        "sexual",
        "sexy",
        "sh!+",
        "sh!t",
        "sh1t",
        "s-h-1-t",
        "shag",
        "shagger",
        "shaggin",
        "shagging",
        "shamedame",
        "shaved beaver",
        "shaved pussy",
        "shemale",
        "shi+",
        "shibari",
        "shirt lifter",
        "shit",
        "s-h-i-t",
        "shit ass",
        "shit fucker",
        "shitass",
        "shitbag",
        "shitbagger",
        "shitblimp",
        "shitbrains",
        "shitbreath",
        "shitcanned",
        "shitcunt",
        "shitdick",
        "shite",
        "shiteater",
        "shited",
        "shitey",
        "shitface",
        "shitfaced",
        "shitfuck",
        "shitfull",
        "shithead",
        "shitheads",
        "shithole",
        "shithouse",
        "shiting",
        "shitings",
        "shits",
        "shitspitter",
        "shitstain",
        "shitt",
        "shitted",
        "shitter",
        "shitters",
        "shittier",
        "shittiest",
        "shitting",
        "shittings",
        "shitty",
        "shiz",
        "shiznit",
        "shota",
        "shrimping",
        "sissy",
        "skag",
        "skank",
        "skeet",
        "skullfuck",
        "slag",
        "slanteye",
        "slave",
        "sleaze",
        "sleazy",
        "slope",
        "slut",
        "slut bucket",
        "slutbag",
        "slutdumper",
        "slutkiss",
        "sluts",
        "smartass",
        "smartasses",
        "smeg",
        "smegma",
        "smut",
        "smutty",
        "snatch",
        "sniper",
        "snowballing",
        "snuff",
        "s-o-b",
        "sod off",
        "sodom",
        "sodomize",
        "sodomy",
        "son of a bitch",
        "son of a motherless goat",
        "son of a whore",
        "son-of-a-bitch",
        "souse",
        "soused",
        "spac",
        "spade",
        "sperm",
        "spic",
        "spick",
        "spik",
        "spiks",
        "splooge",
        "splooge moose",
        "spooge",
        "spook",
        "spread legs",
        "spunk",
        "stfu",
        "stiffy",
        "stoned",
        "strap on",
        "strapon",
        "strappado",
        "strip",
        "strip club",
        "stroke",
        "stupid",
        "style doggy",
        "suck",
        "suckass",
        "sucked",
        "sucking",
        "sucks",
        "suicide girls",
        "sultry women",
        "sumofabiatch",
        "swastikav",
        "swinger",
        "t1t",
        "t1tt1e5",
        "t1tties",
        "taff",
        "taig",
        "tainted love",
        "taking the piss",
        "tampon",
        "tard",
        "tart",
        "taste my",
        "tawdry",
        "tea bagging",
        "teabagging",
        "teat",
        "teets",
        "teez",
        "teste",
        "testee",
        "testes",
        "testical",
        "testicle",
        "testis",
        "threesome",
        "throating",
        "thrust",
        "thug",
        "thundercunt",
        "thunder cunt",
        "tied up",
        "tight white",
        "tinkle",
        "tit",
        "tit wank",
        "titfuck",
        "titi",
        "tities",
        "tits",
        "titt",
        "tittie5",
        "tittiefucker",
        "titties",
        "titty",
        "tittyfuck",
        "tittyfucker",
        "tittywank",
        "titwank",
        "toke",
        "tongue in a",
        "toots",
        "topless",
        "tosser",
        "towelhead",
        "tramp",
        "tranny",
        "trashy",
        "tribadism",
        "trumped",
        "tub girl",
        "tubgirl",
        "turd",
        "tush",
        "tushy",
        "tw4t",
        "twat",
        "twathead",
        "twatlips",
        "twats",
        "twatty",
        "twatting",
        "twatwaffle",
        "twink",
        "twinkie",
        "two fingers",
        "two fingers with tongue",
        "two girls 1 cup",
        "two girls one cup",
        "twunt",
        "twunter",
        "ugly",
        "unclefucker",
        "undies",
        "undressing",
        "unwed",
        "upskirt",
        "urethra play",
        "urinal",
        "urine",
        "urophilia",
        "uterus",
        "uzi",
        "v14gra",
        "v1gra",
        "vag",
        "vagina",
        "vajayjay",
        "va-j-j",
        "valium",
        "venus mound",
        "veqtable",
        "viagra",
        "vibrator",
        "violet wand",
        "virgin",
        "vixen",
        "vjayjay",
        "vodka",
        "vomit",
        "vorarephilia",
        "voyeur",
        "vulgar",
        "vulva",
        "w00se",
        "wad",
        "wang",
        "wank",
        "wanker",
        "wankjob",
        "wanky",
        "wazoo",
        "wedgie",
        "weed",
        "weenie",
        "weewee",
        "weiner",
        "weirdo",
        "wench",
        "wet dream",
        "wetback",
        "wh0re",
        "wh0reface",
        "white power",
        "whiz",
        "whoar",
        "whoralicious",
        "whore",
        "whorealicious",
        "whorebag",
        "whored",
        "whoreface",
        "whorehopper",
        "whorehouse",
        "whores",
        "whoring",
        "wigger",
        "willies",
        "willy",
        "window licker",
        "wiseass",
        "wiseasses",
        "wog",
        "womb",
        "wop",
        "wrapping men",
        "wrinkled starfish",
        "xrated",
        "x-rated",
        "xx",
        "xxx",
        "yaoi",
        "yeasty",
        "yellow showers",
        "yid",
        "yiffy",
        "yobbo",
        "zibbi",
        "zoophilia",
        "zubb",

    };

    static UsernameBadWords& Singleton()
    {
        static UsernameBadWords instance;
        return instance;
    }

private:
    UsernameBadWords() = default;
    ~UsernameBadWords() = default;
    UsernameBadWords(const UsernameBadWords&) = delete;
    UsernameBadWords& operator=(const UsernameBadWords&) = delete;
};