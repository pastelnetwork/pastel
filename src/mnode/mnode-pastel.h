#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "vector_types.h"
#include "map_types.h"

#include "mnode/mnode-consts.h"
#include "mnode/ticket.h"
#include "mnode/mnode-controller.h"

#include <tuple>
#include <optional>

// forward ticket class declarations
class CPastelIDRegTicket;
class CNFTRegTicket;
class CNFTActivateTicket;
class CNFTSellTicket;
class CNFTBuyTicket;
class CNFTTradeTicket;
class CNFTRoyaltyTicket;
class CTakeDownTicket;
class CChangeUsernameTicket;
class CChangeEthereumAddressTicket;

// ticket vector types
using PastelIDRegTickets_t = std::vector<CPastelIDRegTicket>;
using NFTRegTickets_t = std::vector<CNFTRegTicket>;
using NFTActivateTickets_t = std::vector<CNFTActivateTicket>;
using NFTSellTickets_t = std::vector<CNFTSellTicket>;
using NFTBuyTickets_t = std::vector<CNFTBuyTicket>;
using NFTTradeTickets_t = std::vector<CNFTTradeTicket>;
using NFTRoyaltyTickets_t = std::vector<CNFTRoyaltyTicket>;
using TakeDownTickets_t = std::vector<CTakeDownTicket>;
using ChangeUsernameTickets_t = std::vector<CChangeUsernameTicket>;
using ChangeEthereumAddressTickets_t = std::vector<CChangeEthereumAddressTicket>;

// PastelID Ticket //////////////////////////////////////////////////////////////////////////////////////////////////////
class CPastelIDRegTicket : public CPastelTicket
{
public:
	std::string pastelID;   // Pastel ID - base58 encoded public key (EdDSA448)
	std::string address;    // funding address associated with Pastel ID
    COutPoint outpoint{};
    std::string pq_key;     // Legendre Post-Quantum LegRoast public key (base58 encoded with prefix)
	v_uint8 mn_signature;
	v_uint8 pslid_signature;
    
    std::string secondKey; //local only

public:
    CPastelIDRegTicket() = default;
    explicit CPastelIDRegTicket(std::string &&_pastelID) : 
        pastelID(std::move(_pastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::PastelID; }
    static TicketID GetID() { return TicketID::PastelID; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        address.clear();
        pq_key.clear();
        mn_signature.clear();
        pslid_signature.clear();
        secondKey.clear();
    }

    std::string KeyOne() const noexcept override { return pastelID; }
    std::string KeyTwo() const noexcept override { return outpoint.IsNull() ? (secondKey.empty() ? address : secondKey) : outpoint.ToStringShort(); }
    
    bool HasKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { pastelID = std::move(val); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    void ToStrStream(std::stringstream& ss, const bool bIncludeMNsignature = true) const noexcept;
    bool IsValid(const bool bPreReg, const int nDepth) const override;
    
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return nHeight<=10000? 10: 1000; }
    
    std::string PastelIDType() const noexcept {return outpoint.IsNull()? "personal": "masternode";}
    
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
        {
            //if (v1 or higher) and ( (writing to stream) or (reading but not end of the stream yet))
            READWRITE(m_nVersion);
            READWRITE(pq_key);
        }
        else if (bRead) { // reading v0
            m_nVersion = 0;
            pq_key.clear();
        }
    }
	
    static CPastelIDRegTicket Create(std::string&& _pastelID, SecureString&& strKeyPass, const std::string &_address);
    static bool FindTicketInDb(const std::string& key, CPastelIDRegTicket& ticket);
    static PastelIDRegTickets_t FindAllTicketByPastelAddress(const std::string& address);
};

// NFT Registration Ticket //////////////////////////////////////////////////////////////////////////////////////////////
/*

Ticket as base64(RegistrationTicket({some data}))

bytes fields are base64 as strings

  "nft_ticket_version": integer  // 1
  "author": bytes,            // PastelID of the author (creator)
  "blocknum": integer,        // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes         // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "copies": integer,          // number of copies
  "royalty": float,           // (not yet supported by cNode) how much creator should get on all future resales
  "green": bool,              // is there Green NFT payment or not

  "app_ticket": bytes,        // cNode DOES NOT parse this part!!!!
  as base64(
  {
    "creator_name": string,
    "nft_title": string,
    "nft_series_name": string,
    "nft_keyword_set": string,
    "creator_website": string,
    "creator_written_statement": string,
    "nft_creation_video_youtube_url": string,

    "thumbnail_hash": bytes,         // hash of the thumbnail !!!!SHA3-256!!!!
    "data_hash": bytes,              // hash of the image (or any other asset) that this ticket represents !!!!SHA3-256!!!!

    "fingerprints_hash": bytes, 	 // hash of the fingerprint !!!!SHA3-256!!!!
    "fingerprints": bytes,      	 // compressed fingerprint
    "fingerprints_signature": bytes, // signature on raw image fingerprint

    "rq_ids": [list of strings],     // raptorq symbol identifiers -  !!!!SHA3-256 of symbol block!!!!
    "rq_coti": integer64,            // raptorq CommonOTI
    "rq_ssoti": integer64,           // raptorq SchemeSpecificOTI

    "dupe_detection_system_version": string,
    "pastel_rareness_score": float,  // 0 to 1

    "rareness_score": integer,       // 0 to 1000
    "nsfw_score": integer,           // 0 to 1000 
    "seen_score": integer,           // 0 to 1000
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

mvkey #1: creator PastelID
}
 */
class CNFTRegTicket : public CPastelTicket
{
public:
    static constexpr short allsigns = 4;
    static constexpr short creatorsign = 0;
    static constexpr short mainmnsign = 1;
    static constexpr short mn2sign = 2;
    static constexpr short mn3sign = 3;

public:
	std::string sNFTTicket;
    
    std::string pastelIDs[allsigns];
    v_uint8 ticketSignatures[allsigns];
    
    std::string keyOne;
    std::string keyTwo;
    CAmount storageFee{};

    int creatorHeight{}; //blocknum when the ticket was created by the wallet
    int totalCopies{}; //blocknum when the ticket was created by the wallet
    
    float nRoyalty{};
    std::string strGreenAddress;
    
public:
    CNFTRegTicket() = default;
    explicit CNFTRegTicket(std::string _ticket) :
        sNFTTicket(std::move(_ticket))
    {}
    
    TicketID ID() const noexcept override { return TicketID::NFT; }
    static TicketID GetID() { return TicketID::NFT; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        sNFTTicket.clear();
        for (size_t i = 0; i < allsigns; ++i)
        {
            pastelIDs[i].clear();
            ticketSignatures[i].clear();
        }
        keyOne.clear();
        keyTwo.clear();
        storageFee = 0;
        creatorHeight = 0;
        totalCopies = 0;
        nRoyalty = 0.0f;
        strGreenAddress.clear();
    }

    std::string KeyOne() const noexcept override { return keyOne; }
    std::string KeyTwo() const noexcept override { return keyTwo; }
    std::string MVKeyOne() const noexcept override { return pastelIDs[creatorsign]; }
    
    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { keyOne = std::move(val); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(const bool bPreReg, const int nDepth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return 10; }
    static CAmount GreenPercent(const unsigned int nHeight) { return 2; }
    static std::string GreenAddress(const unsigned int nHeight) { return masterNodeCtrl.TicketGreenAddress; }

	void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(sNFTTicket);
        READWRITE(m_nVersion);
		
        // v0
        for (int mn=0; mn<allsigns; mn++)
        {
            READWRITE(pastelIDs[mn]);
            READWRITE(ticketSignatures[mn]);
        }

        READWRITE(keyOne);
        READWRITE(keyTwo);
        READWRITE(creatorHeight);
        READWRITE(totalCopies);
        READWRITE(nRoyalty);
        READWRITE(strGreenAddress);
        READWRITE(storageFee);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
	}

    std::string GetRoyaltyPayeePastelID() const;
    std::string GetRoyaltyPayeeAddress() const;

    static CNFTRegTicket Create(std::string _ticket, const std::string& signatures,
                                std::string _pastelID, SecureString&& strKeyPass,
                                std::string _keyOne, std::string _keyTwo,
                                const CAmount _storageFee);
    static bool FindTicketInDb(const std::string& key, CNFTRegTicket& _ticket);
    static bool CheckIfTicketInDb(const std::string& key);
    
    static NFTRegTickets_t FindAllTicketByPastelID(const std::string& pastelID);
};

// NFT Activation Ticket ////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "activation",
		"pastelID": "",         //PastelID of the creator
		"reg_txid": "",         //tnx with registration ticket in it
		"creator_height": "",    //block at which creator created NFT Ticket
		                        //is used to check if the MN that created NFT registration ticket was indeed top MN when creator create ticket
		"reg_fee": "",          //should match the registration fee from NFT Reg Ticket
		"signature": ""
	}

    key   #1: NFT registration ticket txid
    mvkey #1: Pastel ID
    mvkey #2: creator height (converted to string)
 */
class CNFTActivateTicket : public CPastelTicket
{
private:

public:
	std::string pastelID;       //pastelID of the creator
	std::string regTicketTxnId; // txid of the NFT Reg ticket
    int creatorHeight{};
    int storageFee{};
	v_uint8 signature;

public:
    CNFTActivateTicket() = default;

	explicit CNFTActivateTicket(std::string _pastelID) :
        pastelID(std::move(_pastelID))
    {}
    
    TicketID ID() const noexcept override { return TicketID::Activate; }
    static TicketID GetID() { return TicketID::Activate; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        regTicketTxnId.clear();
        creatorHeight = 0;
        storageFee = 0;
        signature.clear();
    }
    std::string KeyOne() const noexcept override { return regTicketTxnId; }
    std::string MVKeyOne() const noexcept override { return pastelID; }
    std::string MVKeyTwo() const noexcept override { return std::to_string(creatorHeight); }

    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { regTicketTxnId = std::move(val); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(const bool bPreReg, const int nDepth) const override;
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
		READWRITE(regTicketTxnId);
		READWRITE(creatorHeight);
        READWRITE(storageFee);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
		READWRITE(m_txid);
		READWRITE(m_nBlock);
	}
	
    CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const override;
    
    static CNFTActivateTicket Create(std::string _regTicketTxId, int _creatorHeight, int _storageFee, std::string _pastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTActivateTicket& ticket);

    static NFTActivateTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTActivateTickets_t FindAllTicketByCreatorHeight(int height);
    static bool CheckTicketExistByNFTTicketID(const std::string& regTicketTxnId);
};

// NFT Trade Tickets /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "nft-sell",
		"pastelID": "",     //PastelID of the NFT owner - either 1) an original creator; or 2) a previous buyer,
		                    //should be the same in either 1) NFT activation ticket or 2) trade ticket
		"nft_txid": "",     //txid with either 1) NFT activation ticket or 2) trade ticket in it
		"asked_price": "",
		"valid_after": "",
		"valid_before": "",
		"reserved": "",
		"signature": ""
	}
 */

class CNFTSellTicket : public CPastelTicket
{
public:
    std::string pastelID;
    std::string NFTTxnId;
    unsigned int askedPrice{};
    unsigned int activeAfter{};              //as a block height
    unsigned int activeBefore{};             //as a block height
    unsigned short copyNumber{};
    std::string reserved;
    v_uint8 signature;
    
    std::string key;

public:
    CNFTSellTicket() = default;
    
    explicit CNFTSellTicket(std::string _pastelID) : 
        pastelID(std::move(_pastelID))
    {}
    
    TicketID ID() const noexcept override { return TicketID::Sell; }
    static TicketID GetID() { return TicketID::Sell; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        NFTTxnId.clear();
        askedPrice = 0;
        activeAfter = 0;
        activeBefore = 0;
        copyNumber = 0;
        reserved.clear();
        signature.clear();
        key.clear();
    }
    std::string KeyOne() const noexcept override { return !key.empty() ? key : NFTTxnId + ":" + std::to_string(copyNumber); } //txid:#
    std::string MVKeyOne() const noexcept override { return pastelID; }
    std::string MVKeyTwo() const noexcept override { return NFTTxnId; }
    
    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    void SetKeyOne(std::string val) override { key = std::move(val); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(const bool bPreReg, const int nDepth) const override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return std::max(10u, askedPrice / 50); }
    
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(NFTTxnId);
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
    
    static CNFTSellTicket Create(std::string _NFTTxnId, int _askedPrice, int _validAfter, int _validBefore, int _copy_number, std::string _pastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTSellTicket& ticket);
    
    static NFTSellTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTSellTickets_t FindAllTicketByNFTTxnID(const std::string& NFTTxnId);
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
class CNFTBuyTicket : public CPastelTicket
{
public:
    std::string pastelID;
    std::string sellTxnId;
    unsigned int price{};
    std::string reserved;
    v_uint8 signature;
    
public:
    CNFTBuyTicket() = default;
    
    explicit CNFTBuyTicket(std::string _pastelID) : 
        pastelID(std::move(_pastelID))
    {}
    
    TicketID ID() const noexcept override { return TicketID::Buy; }
    static TicketID GetID() { return TicketID::Buy; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        sellTxnId.clear();
        price = 0;
        reserved.clear();
        signature.clear();
    }
    std::string KeyOne() const noexcept override { return sellTxnId; } // this is the latest (active) buy ticket for this sell ticket
    std::string MVKeyOne() const noexcept override { return pastelID; }
    //    std::string MVKeyTwo() const override {return sellTxnId;} // these are all buy (1 active and many inactive) tickets for this sell ticket
    
    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return false; }
    void SetKeyOne(std::string val) override { sellTxnId = std::move(val); }

    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return std::max(10u, price / 100); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(const bool bPreReg, const int nDepth) const override;
    
    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(sellTxnId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }
    
    static CNFTBuyTicket Create(std::string _sellTxnId, int _price, std::string _pastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTBuyTicket& ticket);

    static bool CheckBuyTicketExistBySellTicket(const std::string& _sellTxnId);
    
    static NFTBuyTickets_t FindAllTicketByPastelID(const std::string& pastelID);
};

/*
	"ticket": {
		"type": "trade",
		"pastelID": "",     //PastelID of the buyer
		"sell_txid": "",    //txid with sale ticket
		"buy_txid": "",     //txid with buy ticket
		"nft_txid": "",     //txid with either 1) NFT activation ticket or 2) trade ticket in it
		"price": "",
		"reserved": "",
		"signature": ""
	},
 */
using txid_serial_tuple_t = std::tuple<std::string, std::string>;

class CNFTTradeTicket : public CPastelTicket
{
public:
    std::string pastelID;
    std::string sellTxnId;
    std::string buyTxnId;
    std::string NFTTxnId;
    std::string nftRegTxnId;
    std::string nftCopySerialNr;

    unsigned int price{};
    std::string reserved;
    v_uint8 signature;

public:
    CNFTTradeTicket() = default;

    explicit CNFTTradeTicket(std::string _pastelID) : 
        pastelID(std::move(_pastelID))
    {}

    TicketID ID() const noexcept override { return TicketID::Trade; }
    static TicketID GetID() { return TicketID::Trade; }

    void Clear() noexcept override
    {
        pastelID.clear();
        sellTxnId.clear();
        buyTxnId.clear();
        NFTTxnId.clear();
        nftRegTxnId.clear();
        nftCopySerialNr.clear();
        price = 0;
        reserved.clear();
        signature.clear();
    }
    std::string KeyOne() const noexcept override { return sellTxnId; }
    std::string KeyTwo() const noexcept override { return buyTxnId; }
    std::string MVKeyOne() const noexcept override { return pastelID; }
    std::string MVKeyTwo() const noexcept override { return NFTTxnId; }
    std::string MVKeyThree() const noexcept override { return nftRegTxnId; }
    
    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return true; }
    bool HasMVKeyTwo() const noexcept override { return true; }
    bool HasMVKeyThree() const noexcept override { return true; }
    
    void SetKeyOne(std::string val) override { sellTxnId = std::move(val); }
    
    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    bool IsValid(const bool bPreReg, const int nDepth) const override;
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
        READWRITE(sellTxnId);
        READWRITE(buyTxnId);
        READWRITE(NFTTxnId);
        READWRITE(price);
        READWRITE(reserved);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
        READWRITE(nftRegTxnId);
        READWRITE(nftCopySerialNr);
    }

    CAmount GetExtraOutputs(std::vector<CTxOut>& outputs) const override;
    
    static CNFTTradeTicket Create(std::string _sellTxnId, std::string _buyTxnId, std::string _pastelID, SecureString&& strKeyPass);
    static bool FindTicketInDb(const std::string& key, CNFTTradeTicket& ticket);
    
    static NFTTradeTickets_t FindAllTicketByPastelID(const std::string& pastelID);
    static NFTTradeTickets_t FindAllTicketByNFTTxnID(const std::string& NFTTxnId);
    static NFTTradeTickets_t FindAllTicketByRegTnxID(const std::string& nftRegTxnId);
    
    static bool CheckTradeTicketExistBySellTicket(const std::string& _sellTxnId);
    static bool CheckTradeTicketExistByBuyTicket(const std::string& _buyTxnId);
    static bool GetTradeTicketBySellTicket(const std::string& _sellTxnId, CNFTTradeTicket& ticket);
    static bool GetTradeTicketByBuyTicket(const std::string& _buyTxnId, CNFTTradeTicket& ticket);
    static mu_strings GetPastelIdAndTxIdWithTopHeightPerCopy(const NFTTradeTickets_t& allTickets);
    
    std::unique_ptr<CPastelTicket> FindNFTRegTicket() const;

    void SetNFTRegTicketTxid(const std::string& sNftRegTxid);
    const std::string GetNFTRegTicketTxid() const;
    void SetCopySerialNr(const std::string& nftCopySerialNr);
    const std::string& GetCopySerialNr() const;
    
    static std::optional<txid_serial_tuple_t> GetNFTRegTxIDAndSerialIfResoldNft(const std::string& _txid);
};

/*
  "ticket": {
    "type": "royalty",
    "version": "",
    "pastelID": "",     //pastelID of the old (current at moment of creation) royalty recipient
    "new_pastelID": "", //pastelID of the new royalty recipient
    "nft_txid": "",     //txid of the NFT for royalty payments
    "signature": ""
  }
*/

class CNFTRoyaltyTicket : public CPastelTicket
{
public:
  std::string pastelID;    //pastelID of the old (current at moment of creation) royalty recipient
  std::string newPastelID; //pastelID of the new royalty recipient
  std::string NFTTxnId;    //txid of the NFT for royalty payments
  v_uint8 signature;

public:
  CNFTRoyaltyTicket() = default;

  explicit CNFTRoyaltyTicket(std::string _pastelID, std::string _newPastelID)
      : pastelID(std::move(_pastelID)), newPastelID(std::move(_newPastelID)) {
  }

  TicketID ID() const noexcept final { return TicketID::Royalty; }
  static TicketID GetID() { return TicketID::Royalty; }

  void Clear() noexcept override
  {
      CPastelTicket::Clear();
      pastelID.clear();
      newPastelID.clear();
      NFTTxnId.clear();
      signature.clear();
  }
  std::string KeyOne() const noexcept final { return {signature.cbegin(), signature.cend()}; }
  std::string MVKeyOne() const noexcept final { return pastelID; }
  std::string MVKeyTwo() const noexcept final { return NFTTxnId; }

  bool HasMVKeyOne() const noexcept final { return true; }
  bool HasMVKeyTwo() const noexcept final { return true; }
  void SetKeyOne(std::string val) final { signature.assign(val.begin(), val.end()); }

  std::string ToJSON() const noexcept final;
  std::string ToStr() const noexcept final;
  bool IsValid(const bool bPreReg, const int nDepth) const final;
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
    READWRITE(NFTTxnId);
    READWRITE(signature);
    READWRITE(m_nTimestamp);
    READWRITE(m_txid);
    READWRITE(m_nBlock);
  }

  static CNFTRoyaltyTicket Create(std::string _NFTTxnId, std::string _newPastelID,
                                  std::string _pastelID, SecureString&& strKeyPass);
  static bool FindTicketInDb(const std::string& key, CNFTRoyaltyTicket& ticket);

  static NFTRoyaltyTickets_t FindAllTicketByPastelID(const std::string& pastelID);
  static NFTRoyaltyTickets_t FindAllTicketByNFTTxnID(const std::string& NFTTxnId);
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
    bool IsValid(const bool bPreReg, const int nDepth) const override { return false; }
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
    v_uint8 signature;

public:
    CChangeUsernameTicket() = default;

    explicit CChangeUsernameTicket(std::string _pastelID, std::string _username) :
        pastelID(std::move(_pastelID)), username(std::move(_username))
    {}

    TicketID ID() const noexcept override { return TicketID::Username; }
    static TicketID GetID() { return TicketID::Username; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        username.clear();
        fee = 100;
        signature.clear();
    }

    std::string KeyOne() const noexcept override { return username; }
    std::string KeyTwo() const noexcept override { return pastelID; }

    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return false; }
    bool HasMVKeyTwo() const noexcept override { return false; }

    void SetKeyOne(std::string val) override { username = std::move(val); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return fee; }
    bool IsValid(const bool bPreReg, const int nDepth) const override;
    /**
     * Disable changing username for this number of blocks since last change.
     * 
     * \return number of blocks since the last change when change username ticket is disabled
     */
    static unsigned int GetDisablePeriodInBlocks() noexcept
    {
        if (Params().IsRegTest())
            return 10;
        return 24 * 24;
    }

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

    static CChangeUsernameTicket Create(std::string _pastelID, std::string _username, SecureString&& strKeyPass);    
    static bool FindTicketInDb(const std::string& key, CChangeUsernameTicket& ticket);

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

// Ethereum Address Change Ticket /////////////////////////////////////////////////////////////////////////////////////////////////////
/*
	"ticket": {
		"type": "ethereumAddress",
		"pastelID": "",    //PastelID of the ethereum address
		"ethereumAddress": "",    //new valid ethereum address
		"fee": "",         // fee to change ethereum address
		"signature": ""
	},
 */
class CChangeEthereumAddressTicket : public CPastelTicket
{
public:
    std::string pastelID;
    std::string ethereumAddress;
    CAmount fee{100};
    v_uint8 signature;

public:
    CChangeEthereumAddressTicket() = default;

    explicit CChangeEthereumAddressTicket(std::string _pastelID, std::string _ethereumAddress) :
        pastelID(std::move(_pastelID)), ethereumAddress(std::move(_ethereumAddress))
    {}

    TicketID ID() const noexcept override { return TicketID::EthereumAddress; }
    static TicketID GetID() { return TicketID::EthereumAddress; }

    void Clear() noexcept override
    {
        CPastelTicket::Clear();
        pastelID.clear();
        ethereumAddress.clear();
        fee = 100;
        signature.clear();
    }
    std::string KeyOne() const noexcept override { return ethereumAddress; }
    std::string KeyTwo() const noexcept override { return pastelID; }

    bool HasKeyTwo() const noexcept override { return true; }
    bool HasMVKeyOne() const noexcept override { return false; }
    bool HasMVKeyTwo() const noexcept override { return false; }

    void SetKeyOne(std::string val) override { ethereumAddress = std::move(val); }

    std::string ToJSON() const noexcept override;
    std::string ToStr() const noexcept override;
    CAmount TicketPrice(const unsigned int nHeight) const noexcept override { return fee; }
    bool IsValid(const bool bPreReg, const int nDepth) const override;

    void SerializationOp(CDataStream& s, const SERIALIZE_ACTION ser_action) override
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        std::string error;
        if (!VersionMgmt(error, bRead))
            throw std::runtime_error(error);
        READWRITE(pastelID);
        READWRITE(m_nVersion);
        // v0
        READWRITE(ethereumAddress);
        READWRITE(fee);
        READWRITE(signature);
        READWRITE(m_nTimestamp);
        READWRITE(m_txid);
        READWRITE(m_nBlock);
    }

    static CChangeEthereumAddressTicket Create(std::string _pastelID, std::string _ethereumAddress, SecureString&& strKeyPass);    
    static bool FindTicketInDb(const std::string& key, CChangeEthereumAddressTicket& ticket);

    /** Some general checks to see if the ethereum address is invalid. Below cases will be considered as an invalid EthereumAddress
    *     - Contains characters that are different from hex digits
    *     - Not exactly 40 characters long
    *     - Doesn't start with 0x.
    * return: true if bad, false if good to use
    */
    static bool isEthereumAddressInvalid(const std::string& ethereumAddress, std::string& error);
};
