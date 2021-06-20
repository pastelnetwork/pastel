// Copyright (c) 2018 The PASTELCoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "key_io.h"
#include "init.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

#include "mnode/mnode-controller.h"
#include "mnode/mnode-pastel.h"
#include "mnode/mnode-msgsigner.h"
#include "mnode/ticket-processor.h"

#include "ed448/pastel_key.h"
#include "json/json.hpp"

#include <algorithm>

using json = nlohmann::json;

// CPastelIDRegTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CPastelIDRegTicket CPastelIDRegTicket::Create(std::string _pastelID, const SecureString& strKeyPass, std::string _address)
{
    CPastelIDRegTicket ticket(std::move(_pastelID));
    
    bool isMN = _address.empty();
    
    if (isMN)
    {
        CMasternode mn;
        if(!masterNodeCtrl.masternodeManager.Get(masterNodeCtrl.activeMasternode.outpoint, mn))
            throw std::runtime_error("This is not a active masternode. Only active MN can register its PastelID");
    
        //collateral address
        KeyIO keyIO(Params());
        CTxDestination dest = mn.pubKeyCollateralAddress.GetID();
        ticket.address = std::move(keyIO.EncodeDestination(dest));
    
        //outpoint hash
        ticket.outpoint = masterNodeCtrl.activeMasternode.outpoint;
    }
    else
        ticket.address = std::move(_address);
    
    std::stringstream ss;
	ss << ticket.pastelID;
	ss << ticket.address;
	ss << ticket.outpoint.ToStringShort();
    ss << ticket.GenerateTimestamp();
    if (isMN)
    {
        if(!CMessageSigner::SignMessage(ss.str(), ticket.mn_signature, masterNodeCtrl.activeMasternode.keyMasternode))
            throw std::runtime_error("MN Sign of the ticket has failed");
        ss << std::string{ ticket.mn_signature.cbegin(), ticket.mn_signature.cend() };
    }
    std::string fullTicket = ss.str();
    ticket.pslid_signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(fullTicket.c_str()), fullTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

std::string CPastelIDRegTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << address;
    ss << outpoint.ToStringShort();
    ss << m_nTimestamp;
    if (address.empty())
        ss << std::string{ mn_signature.cbegin(), mn_signature.cend() };
    return ss.str();
}

bool CPastelIDRegTicket::IsValid(bool preReg, int depth) const
{
    if (preReg) { // Something to check ONLY before ticket made into transaction
        
        //1. check that PastelID ticket is not already in the blockchain.
        // Only done after Create
        if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this)) {
          throw std::runtime_error(strprintf("This PastelID is already registered in blockchain [%s]", pastelID));
        }

        //TODO Pastel: validate that address has coins to pay for registration - 10PSL + fee
        // ...
    }
    
    std::stringstream ss;
    ss << pastelID;
    ss << address;
    ss << outpoint.ToStringShort();
    ss << m_nTimestamp;
    
    if (masterNodeCtrl.masternodeSync.IsSynced())
    { // Validate only if both blockchain and MNs are synced
        if (!outpoint.IsNull())
        { // validations only for MN PastelID
            // 1. check if TicketDB already has PatelID with the same outpoint,
            // and if yes, reject if it has different signature OR different blocks or transaction ID
            // (ticket transaction replay attack protection)
            CPastelIDRegTicket _ticket;
            _ticket.outpoint = outpoint;
            if (masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(_ticket)){
                if (_ticket.mn_signature != mn_signature || !_ticket.IsBlock(m_nBlock) || _ticket.m_txid != m_txid)
                {
                  throw std::runtime_error(strprintf(
                    "Masternode's outpoint - [%s] is already registered as a ticket. Your PastelID - [%s] "
                    "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
                    outpoint.ToStringShort(), pastelID, m_nBlock, m_txid, _ticket.m_nBlock, _ticket.m_txid));
                }
            }
            
            // 2. Check outpoint belongs to active MN
            // However! If this is validation of an old ticket, MN can be not active or eve alive anymore
            //So will skip the MN validation if ticket is fully confirmed (older then MinTicketConfirmations blocks)
            unsigned int currentHeight;
            {
                LOCK(cs_main);
                currentHeight = static_cast<unsigned int>(chainActive.Height());
            }
            //during transaction validation before ticket made in to the block_ticket.ticketBlock will == 0
            if (_ticket.IsBlock(0) || currentHeight - _ticket.GetBlock() < masterNodeCtrl.MinTicketConfirmations)
            {
                CMasternode mnInfo;
                if (!masterNodeCtrl.masternodeManager.Get(outpoint, mnInfo))
                {
                  throw std::runtime_error(strprintf(
                    "Unknown Masternode - [%s]. PastelID - [%s]", outpoint.ToStringShort(), pastelID));
                }
                if (!mnInfo.IsEnabled())
                {
                  throw std::runtime_error(strprintf(
                    "Non an active Masternode - [%s]. PastelID - [%s]", outpoint.ToStringShort(), pastelID));
                }
    
                // 3. Validate MN signature using public key of MN identified by outpoint
                std::string errRet;
                if (!CMessageSigner::VerifyMessage(mnInfo.pubKeyMasternode, mn_signature, ss.str(), errRet))
                {
                  throw std::runtime_error(strprintf(
                    "Ticket's MN signature is invalid. Error - %s. Outpoint - [%s]; PastelID - [%s]",
                    errRet, outpoint.ToStringShort(), pastelID));
                }
            }
        }
    }
    
    // Something to always validate
    // 1. Ticket signature is valid
    ss << std::string{ mn_signature.cbegin(), mn_signature.cend() };
    std::string fullTicket = ss.str();
    if (!CPastelID::Verify(reinterpret_cast<const unsigned char *>(fullTicket.c_str()), fullTicket.size(),
                           pslid_signature.data(), pslid_signature.size(),
                           pastelID)) {
      throw std::runtime_error(strprintf("Ticket's PastelID signature is invalid. PastelID - [%s]", pastelID));
    }
        
    // 2. Ticket pay correct registration fee - in validated in ValidateIfTicketTransaction

    return true;
}

std::string CPastelIDRegTicket::ToJSON() const noexcept
{
	json jsonObj;
	jsonObj = {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", {
			{"type", GetTicketName()},
			{"version", GetStoredVersion()},
			{"pastelID", pastelID},
			{"pq_key", pq_key},
			{"address", address},
			{"timeStamp", std::to_string(m_nTimestamp)},
			{"signature", ed_crypto::Hex_Encode(pslid_signature.data(), pslid_signature.size())},
			{"id_type", PastelIDType()}
        }}
	};

	if (!outpoint.IsNull())
		jsonObj["ticket"]["outpoint"] = outpoint.ToStringShort();
	
	return jsonObj.dump(4);
}

bool CPastelIDRegTicket::FindTicketInDb(const std::string& key, CPastelIDRegTicket& ticket)
{
    //first try by PastelID
    ticket.pastelID = key;
    if (!masterNodeCtrl.masternodeTickets.FindTicket(ticket))
    {
        //if not, try by outpoint
        ticket.secondKey = key;
        if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket)){
            //finally, clear outpoint and try by address
            ticket.secondKey.clear();
            ticket.address = key;
            if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket))
                return false;
        }
    }
    return true;
}

std::vector<CPastelIDRegTicket> CPastelIDRegTicket::FindAllTicketByPastelAddress(const std::string& address)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CPastelIDRegTicket>(address);
}

// CArtRegTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Current art_ticket - 8 Items!!!!
 {
  "version": integer          // 1
  "author": bytes,            // PastelID of the author (artist)
  "blocknum": integer,        // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes         // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "copies": integer,          // number of copies
  "royalty": float,           // (not yet supported by cNode) how much artist should get on all future resales
  "green": string,            // address for Green NFT payment (not yet supported by cNode)

  "app_ticket": ...
  }
 */
CArtRegTicket CArtRegTicket::Create(
        std::string _ticket, const std::string& signatures,
        std::string _pastelID, const SecureString& strKeyPass,
        std::string _keyOne, std::string _keyTwo,
        CAmount _storageFee)
{
    CArtRegTicket ticket(std::move(_ticket));
    
    //Art Ticket
    auto jsonTicketObj = json::parse(ed_crypto::Base64_Decode(ticket.artTicket));
    if (jsonTicketObj.size() != 8){
        throw std::runtime_error("Art ticket json is incorrect");
    }
    if (jsonTicketObj["version"] != 1) {
        throw std::runtime_error("Only accept version 1 of Art ticket json");
    }
    ticket.artistHeight = jsonTicketObj["blocknum"];
    ticket.totalCopies = jsonTicketObj["copies"];
    ticket.nRoyalty = jsonTicketObj["royalty"];
    ticket.strGreenAddress = jsonTicketObj["green"];
    
    //Artist's and MN2/3's signatures
    auto jsonSignaturesObj = json::parse(signatures);
    if (jsonSignaturesObj.size() != 3){
        throw std::runtime_error("Signatures json is incorrect");
    }
    for (auto& el : jsonSignaturesObj.items())
    {
        if (el.key().empty()) {
            throw std::runtime_error("Signatures json is incorrect");
        }
        
        auto sigItem = el.value();
        if (sigItem.empty())
            throw std::runtime_error("Signatures json is incorrect");
        
        std::string pastelID = sigItem.begin().key();
        std::string signature = sigItem.begin().value();
        if (el.key() == "artist"){
            ticket.pastelIDs[artistsign] = std::move(pastelID);
            ticket.ticketSignatures[artistsign] = ed_crypto::Base64_Decode(signature);
        }
        else if (el.key() == "mn2"){
            ticket.pastelIDs[mn2sign] = std::move(pastelID);
            ticket.ticketSignatures[mn2sign] = ed_crypto::Base64_Decode(signature);
        }
        else if (el.key() == "mn3"){
            ticket.pastelIDs[mn3sign] = std::move(pastelID);
            ticket.ticketSignatures[mn3sign] = ed_crypto::Base64_Decode(signature);
        }
    }
    
    ticket.keyOne = std::move(_keyOne);
    ticket.keyTwo = std::move(_keyTwo);
    ticket.storageFee = _storageFee;

    ticket.GenerateTimestamp();
    
    ticket.pastelIDs[mainmnsign] = std::move(_pastelID);
    //signature of ticket hash
    ticket.ticketSignatures[mainmnsign] = CPastelID::Sign(reinterpret_cast<const unsigned char*>(ticket.artTicket.c_str()), ticket.artTicket.size(),
                                                            ticket.pastelIDs[mainmnsign], strKeyPass);
    return ticket;
}

std::string CArtRegTicket::ToStr() const noexcept
{
    return artTicket;
}

bool CArtRegTicket::IsValid(bool preReg, int depth) const
{
    unsigned int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
    }

    if (preReg)
    {
        // A. Something to check ONLY before ticket made into transaction.
        // Only done after Create
        
        // A.1 check that art ticket already in the blockchain
        if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this))
        {
          throw std::runtime_error(strprintf(
            "This Art is already registered in blockchain [Key1 = %s; Key2 = %s]", keyOne, keyTwo));
        }
    
        // A.2 validate that address has coins to pay for registration - 10PSL
        auto fullTicketPrice = TicketPrice(chainHeight); //10% of storage fee is paid by the 'artist' and this ticket is created by MN
        if (pwalletMain->GetBalance() < fullTicketPrice*COIN)
        {
          throw std::runtime_error(strprintf("Not enough coins to cover price [%d]", fullTicketPrice));
        }
    }
    
    // (ticket transaction replay attack protection)
    CArtRegTicket _ticket;
    if ((FindTicketInDb(keyOne, _ticket) || (FindTicketInDb(keyTwo, _ticket))) &&
        (!_ticket.IsBlock(m_nBlock) || _ticket.m_txid != m_txid))
    {
      throw std::runtime_error(strprintf(
        "This Art is already registered in blockchain [Key1 = %s; Key2 = %s]"
        "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
        keyOne, KeyTwo(), m_nBlock, m_txid, _ticket.GetBlock(), _ticket.m_txid));
    }

    // B. Something to always validate
    std::map<std::string, int> pidCountMap{};
    std::map<COutPoint, int> outCountMap{};
    
    for (int mnIndex=0; mnIndex < allsigns; mnIndex++)
    {
        //1. PastelIDs are registered and are in the TicketDB - PastelID tnx can be in the blockchain and valid as tnx,
        // but the ticket this tnx represents can be invalid as ticket, in this case it will not be in the TicketDB!!!
        // and this will mark ArtReg tnx from being valid!!!
        CPastelIDRegTicket pastelIdRegTicket;
        if (!CPastelIDRegTicket::FindTicketInDb(pastelIDs[mnIndex], pastelIdRegTicket)){
            if (mnIndex == artistsign) {
              throw std::runtime_error(strprintf("Artist PastelID is not registered [%s]", pastelIDs[mnIndex]));
            } else {
              throw std::runtime_error(strprintf("MN%d PastelID is not registered [%s]", mnIndex, pastelIDs[mnIndex]));
            }
        }
        //2. PastelIDs are valid
        try {
          pastelIdRegTicket.IsValid(false, ++depth);
        }
        catch (const std::exception& ex) {
          if (mnIndex == artistsign) {
             throw std::runtime_error(strprintf("Artist PastelID is invalid [%s] - %s", pastelIDs[mnIndex], ex.what()));
          } else {
            throw std::runtime_error(strprintf("MN%d PastelID is invalid [%s] - %s", mnIndex, pastelIDs[mnIndex], ex.what()));
          }
        }
        catch (...) {
          if (mnIndex == artistsign) {
            throw std::runtime_error(strprintf("Artist PastelID is invalid [%s] - Unknown exception", pastelIDs[mnIndex]));
          } else {
            throw std::runtime_error(strprintf("MN%d PastelID is invalid [%s] - Unknown exception", mnIndex, pastelIDs[mnIndex]));
          }
        }
        //3. Artist PastelID is personal PastelID and MNs PastelIDs are not personal
        if (mnIndex == artistsign)
        {
            if (!pastelIdRegTicket.outpoint.IsNull())
            {
              throw std::runtime_error(strprintf("Artist PastelID is NOT personal PastelID [%s]", pastelIDs[mnIndex]));
            }
        }
        else
        {
            if (pastelIdRegTicket.outpoint.IsNull())
            {
              throw std::runtime_error(strprintf("MN%d PastelID is NOT masternode PastelID [%s]", mnIndex, pastelIDs[mnIndex]));
            }
     
            // Check that MN1, MN2 and MN3 are all different = here by just PastleId
            if (++pidCountMap[pastelIdRegTicket.pastelID] != 1)
            {
              throw std::runtime_error(strprintf("MNs PastelIDs can not be the same - [%s]", pastelIdRegTicket.pastelID));
            }
            if (++outCountMap[pastelIdRegTicket.outpoint] != 1)
            {
              throw std::runtime_error(strprintf(
                "MNs PastelID can not be from the same MN - [%s]", pastelIdRegTicket.outpoint.ToStringShort()));
            }
            
            //4. Masternodes beyond these PastelIDs, were in the top 10 at the block when the registration happened
            if (masterNodeCtrl.masternodeSync.IsSynced())
            { //Art ticket needs synced MNs
                auto topBlockMNs = masterNodeCtrl.masternodeManager.GetTopMNsForBlock(artistHeight, true);
                auto found = find_if(topBlockMNs.begin(), topBlockMNs.end(),
                                     [&pastelIdRegTicket](CMasternode const &mn) {
                                         return mn.vin.prevout == pastelIdRegTicket.outpoint;
                                     });
    
                if (found == topBlockMNs.end())
                { //not found
                  throw std::runtime_error(strprintf(
                    "MN%d was NOT in the top masternodes list for block %d", mnIndex, artistHeight));
                }
            }
        }
    }
    //5. Signatures matches included PastelIDs (signature verification is slower - hence separate loop)
    for (int mnIndex=0; mnIndex < allsigns; mnIndex++)
    {
        if (!CPastelID::Verify(reinterpret_cast<const unsigned char *>(artTicket.c_str()), artTicket.size(),
                               ticketSignatures[mnIndex].data(), ticketSignatures[mnIndex].size(),
                               pastelIDs[mnIndex]))
        {
            if (mnIndex == artistsign) {
              throw std::runtime_error("Artist signature is invalid");
            } else {
              throw std::runtime_error(strprintf("MN%d signature is invalid", mnIndex));
            }
        }
    }
    
    if (nRoyalty > 20)
    {
      throw std::runtime_error(strprintf("Royalty can't be %hu per cent, Max is 20 per cent", nRoyalty));
    }
    if (!strGreenAddress.empty()) {
        KeyIO keyIO(Params());
        const auto dest = keyIO.DecodeDestination(strGreenAddress);
        if (!IsValidDestination(dest)) {
          throw std::runtime_error(strprintf("The Green NFT address [%s] is invalid", strGreenAddress));
        }
    }
    return true;
}

std::string CArtRegTicket::ToJSON() const noexcept
{
	json jsonObj;
	jsonObj = {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", {
            {"type", GetTicketName()},
            {"art_ticket", artTicket},
            {"version", GetStoredVersion()},
            {"signatures",{
                {"artist", {
                    {pastelIDs[artistsign], ed_crypto::Base64_Encode(ticketSignatures[artistsign].data(), ticketSignatures[artistsign].size())}
                }},
                {"mn1", {
                    {pastelIDs[mainmnsign], ed_crypto::Base64_Encode(ticketSignatures[mainmnsign].data(), ticketSignatures[mainmnsign].size())}
                }},
                {"mn2", {
                    {pastelIDs[mn2sign], ed_crypto::Base64_Encode(ticketSignatures[mn2sign].data(), ticketSignatures[mn2sign].size())}
                }},
                {"mn3", {
                    {pastelIDs[mn3sign], ed_crypto::Base64_Encode(ticketSignatures[mn3sign].data(), ticketSignatures[mn3sign].size())}
                }},
            }},
            {"key1", keyOne},
            {"key2", keyTwo},
            {"artist_height", artistHeight},
            {"total_copies", totalCopies},
            {"storage_fee", storageFee},
            {"royalty", nRoyalty},
            {"green", strGreenAddress},
        }}
	};
    
    return jsonObj.dump(4);
}

std::string CArtRegTicket::GetRoyaltyPayeePastelID() {
  if (!nRoyalty) { return {}; }

  int index{0};
  int foundIndex{-1};
  unsigned int highBlock{0};
  const auto tickets = CArtRoyaltyTicket::FindAllTicketByArtTnxId(m_txid);
  for (const auto& ticket: tickets) {
    if (ticket.GetBlock() > highBlock) {
      highBlock = ticket.GetBlock();
      foundIndex = index;
    }
    ++index;
  }
  return foundIndex >= 0 ? tickets.at(foundIndex).newPastelID : pastelIDs[CArtRegTicket::artistsign];
}

std::string CArtRegTicket::GetRoyaltyPayeeAddress() {
  const std::string pastelID = GetRoyaltyPayeePastelID();
  if (!pastelID.empty()) {
    CPastelIDRegTicket ticket;
    if (CPastelIDRegTicket::FindTicketInDb(pastelID, ticket)) {
      return ticket.address;
    }
  }
  return {};
}

bool CArtRegTicket::FindTicketInDb(const std::string& key, CArtRegTicket& _ticket)
{
    _ticket.keyOne = key;
    _ticket.keyTwo = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(_ticket) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(_ticket);
}

bool CArtRegTicket::CheckIfTicketInDb(const std::string& key)
{
    CArtRegTicket _ticket;
    _ticket.keyOne = key;
    _ticket.keyTwo = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket) ||
           masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(_ticket);
}

std::vector<CArtRegTicket> CArtRegTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtRegTicket>(pastelID);
}

template<class T, typename F>
bool common_validation(const T& ticket, bool preReg, const std::string& strTnxId,
                       std::unique_ptr<CPastelTicket>& pastelTicket,
                       F f,
                       const std::string& thisTicket, const std::string& prevTicket, int depth,
                       const CAmount ticketPrice)
{
    // A. Something to check ONLY before ticket made into transaction
    if (preReg)
    {
        // A. Validate that address has coins to pay for registration - 10PSL + fee
        if (pwalletMain->GetBalance() < ticketPrice*COIN)
        {
          throw std::runtime_error(strprintf("Not enough coins to cover price [%d]", ticketPrice));
        }
    }
    
    // C. Something to always validate
    
    // C.1 Check there are ticket referred from that new ticket with this tnxId
    uint256 txid;
    txid.SetHex(strTnxId);
    //  Get ticket pointed by artTnxId. This is either Activation or Trade tickets (Sell, Buy, Trade)
    try
    {
        pastelTicket = CPastelTicketProcessor::GetTicket(txid);
    }
    catch ([[maybe_unused]] std::runtime_error& ex)
    {
      throw std::runtime_error(strprintf(
        "The %s ticket [txid=%s] referred by this %s ticket is not in the blockchain. [txid=%s]",
        prevTicket, strTnxId, thisTicket, ticket.GetTxId()));
    }

    if (!pastelTicket || f(pastelTicket->ID()))
    {
      throw std::runtime_error(strprintf(
        "The %s ticket with this txid [%s] referred by this %s ticket is not in the blockchain",
        prevTicket, strTnxId, thisTicket));
    }
    
    // B.1 Something to validate only if NOT Initial Download
    if (masterNodeCtrl.masternodeSync.IsSynced())
    {
        unsigned int chainHeight = 0;
        {
            LOCK(cs_main);
            chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
        }
    
        // C.2 Verify Min Confirmations
        const unsigned int height = ticket.IsBlock(0) ? chainHeight : ticket.GetBlock();
        if (chainHeight - pastelTicket->GetBlock() < masterNodeCtrl.MinTicketConfirmations)
        {
          throw std::runtime_error(strprintf(
            "%s ticket can be created only after [%s] confirmations of the %s ticket. chainHeight=%d ticketBlock=%d",
            thisTicket, masterNodeCtrl.MinTicketConfirmations, prevTicket, chainHeight, ticket.GetBlock()));
        }
    }
    // C.3 Verify signature
    // We will check that it is the correct PastelID and the one that belongs to the owner of the art in the following steps
    std::string strThisTicket = ticket.ToStr();
    if (!CPastelID::Verify(reinterpret_cast<const unsigned char *>(strThisTicket.c_str()), strThisTicket.size(),
                           ticket.signature.data(), ticket.signature.size(),
                           ticket.pastelID))
    {
      throw std::runtime_error(strprintf("%s ticket's signature is invalid. PastelID - [%s]", thisTicket, ticket.pastelID));
    }
    
    // C.3 check the referred ticket is valid
    // (IsValid of the referred ticket validates signatures as well!)
    if (depth > 0)
        return true;
    
    std::string err;
    if (!pastelTicket->IsValid(false, ++depth)) {
      throw std::runtime_error(strprintf("The %s ticket with this txid [%s] is invalid - %s", prevTicket, strTnxId, err));
    }
    
    return true;
}

// CArtActivateTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CArtActivateTicket CArtActivateTicket::Create(std::string _regTicketTxId, int _artistHeight, int _storageFee, std::string _pastelID, const SecureString& strKeyPass)
{
    CArtActivateTicket ticket(std::move(_pastelID));
    
    ticket.regTicketTnxId = std::move(_regTicketTxId);
    ticket.artistHeight = _artistHeight;
    ticket.storageFee = _storageFee;
    
    ticket.GenerateTimestamp();
    
    std::string strTicket = ticket.ToStr();
    ticket.signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(strTicket.c_str()), strTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

std::string CArtActivateTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << regTicketTnxId;
    ss << artistHeight;
    ss << storageFee;
    ss << m_nTimestamp;
    return ss.str();
}

bool CArtActivateTicket::IsValid(bool preReg, int depth) const
{
    unsigned int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
    }
    
    // 0. Common validations
    std::unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(*this, preReg, regTicketTnxId, pastelTicket,
        [](const TicketID tid) { return (tid != TicketID::Art); },
        "Activation", "art", depth,
        TicketPrice(chainHeight) + (storageFee * 9 / 10))) { //fee for ticket + 90% of storage fee
      throw std::runtime_error(strprintf(
        "The Activation ticket for the Registration ticket with txid [%s] is not validated [block = %u txid = %s]",
        regTicketTnxId, m_nBlock, m_txid));
    }

    // Check the Activation ticket for that Registration ticket is already in the database
    // (ticket transaction replay attack protection)
    CArtActivateTicket existingTicket;
    if (CArtActivateTicket::FindTicketInDb(regTicketTnxId, existingTicket))
    {
        if (preReg ||  // if pre reg - this is probably repeating call, so signatures can be the same
            existingTicket.signature != signature ||
            !existingTicket.IsBlock(m_nBlock) ||
            existingTicket.m_txid != m_txid)
        { // check if this is not the same ticket!!
          throw std::runtime_error(strprintf(
            "The Activation ticket for the Registration ticket with txid [%s] is already exist"
            "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
            regTicketTnxId, m_nBlock, m_txid, existingTicket.m_nBlock, existingTicket.m_txid));
        }
    }
    
    auto artTicket = dynamic_cast<CArtRegTicket*>(pastelTicket.get());
    if (!artTicket)
    {
      throw std::runtime_error(strprintf(
        "The art ticket with this txid [%s] is not in the blockchain or is invalid", regTicketTnxId));
    }
    
    // 1. check Artist PastelID in ArtReg ticket matches PastelID from this ticket
    std::string& artistPastelID = artTicket->pastelIDs[CArtRegTicket::artistsign];
    if (artistPastelID != pastelID)
    {
      throw std::runtime_error(strprintf(
        "The PastelID [%s] is not matching the Artist's PastelID [%s] in the Art Reg ticket with this txid [%s]",
        pastelID, artistPastelID, regTicketTnxId));
    }
    
    // 2. check ArtReg ticket is at the assumed height
    if (artTicket->artistHeight != artistHeight)
    {
      throw std::runtime_error(strprintf(
        "The artistHeight [%d] is not matching the artistHeight [%d] in the Art Reg ticket with this txid [%s]",
        artistHeight, artTicket->artistHeight, regTicketTnxId));
    }
    
    // 3. check ArtReg ticket fee is same as storageFee
    if (artTicket->storageFee != storageFee)
    {
      throw std::runtime_error(strprintf(
        "The storage fee [%d] is not matching the storage fee [%d] in the Art Reg ticket with this txid [%s]",
        storageFee, artTicket->storageFee, regTicketTnxId));
    }
    
    return true;
}

CAmount CArtActivateTicket::GetExtraOutputs(std::vector<CTxOut>& outputs) const
{
    auto ticket = CPastelTicketProcessor::GetTicket(regTicketTnxId, TicketID::Art);
    auto artTicket = dynamic_cast<CArtRegTicket*>(ticket.get());
    if (!artTicket)
        return 0;
    
    CAmount nAllAmount = 0;
    CAmount nAllMNFee = storageFee * COIN * 9 / 10; //90%
    CAmount nMainMNFee = nAllMNFee * 3 / 5; //60% of 90%
    CAmount nOtherMNFee = nAllMNFee / 5;    //20% of 90%
    
    KeyIO keyIO(Params());
    for (int mn = CArtRegTicket::mainmnsign; mn<CArtRegTicket::allsigns; mn++)
    {
        auto mnPastelID = artTicket->pastelIDs[mn];
        CPastelIDRegTicket mnPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(mnPastelID, mnPastelIDticket))
            throw std::runtime_error(strprintf(
                    "The PastelID [%s] from art ticket with this txid [%s] is not in the blockchain or is invalid",
                    mnPastelID, regTicketTnxId));
    
        const auto dest = keyIO.DecodeDestination(mnPastelIDticket.address);
        if (!IsValidDestination(dest))
            throw std::runtime_error(
                    strprintf("The PastelID [%s] from art ticket with this txid [%s] has invalid MN's address",
                              mnPastelID, regTicketTnxId));
    
        CScript scriptPubKey = GetScriptForDestination(dest);
        CAmount nAmount = (mn == CArtRegTicket::mainmnsign? nMainMNFee: nOtherMNFee);
        nAllAmount += nAmount;
    
        CTxOut out(nAmount, scriptPubKey);
        outputs.push_back(out);
    }
    
    return nAllAmount;
}

std::string CArtActivateTicket::ToJSON() const noexcept
{
	json jsonObj;
	jsonObj = {
            {"txid", m_txid},
			{"height", m_nBlock},
			{"ticket", {
				{"type", GetTicketName()},
				{"version", GetStoredVersion()},
				{"pastelID", pastelID},
				{"reg_txid", regTicketTnxId},
				{"artist_height", artistHeight},
                {"storage_fee", storageFee},
				{"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
		 }}
	};
	
	return jsonObj.dump(4);
}

bool CArtActivateTicket::FindTicketInDb(const std::string& key, CArtActivateTicket& ticket)
{
    ticket.regTicketTnxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CArtActivateTicket::CheckTicketExistByArtTicketID(const std::string& regTicketTnxId)
{
    CArtActivateTicket ticket;
    ticket.regTicketTnxId = regTicketTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

std::vector<CArtActivateTicket> CArtActivateTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtActivateTicket>(pastelID);
}

std::vector<CArtActivateTicket> CArtActivateTicket::FindAllTicketByArtistHeight(int height)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtActivateTicket>(std::to_string(height));
}

// Art Trade Tickets ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CArtSellTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CArtSellTicket CArtSellTicket::Create(std::string _artTnxId, int _askedPrice, int _validAfter, int _validBefore, int _copy_number, std::string _pastelID, const SecureString& strKeyPass)
{
    CArtSellTicket ticket(std::move(_pastelID));
    
    ticket.artTnxId = std::move(_artTnxId);
    ticket.askedPrice = _askedPrice;
    ticket.activeBefore = _validBefore;
    ticket.activeAfter = _validAfter;
    
    ticket.GenerateTimestamp();
    
    //NOTE: Sell ticket for Trade ticket will always has copyNumber = 1
    ticket.copyNumber = _copy_number > 0 ? 
        _copy_number : static_cast<decltype(ticket.copyNumber)>(CArtSellTicket::FindAllTicketByArtTnxID(ticket.artTnxId).size()) + 1;
    ticket.key = ticket.artTnxId + ":" + to_string(ticket.copyNumber);
    
    std::string strTicket = ticket.ToStr();
    ticket.signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(strTicket.c_str()), strTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

std::string CArtSellTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << artTnxId;
    ss << askedPrice;
    ss << copyNumber;
    ss << activeBefore;
    ss << activeAfter;
    ss << m_nTimestamp;
    return ss.str();
}

bool CArtSellTicket::IsValid(bool preReg, int depth) const
{
    unsigned int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
    }
    
    // 0. Common validations
    std::unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(*this, preReg, artTnxId, pastelTicket,
        [](const TicketID tid) { return (tid != TicketID::Activate && tid != TicketID::Trade); },
        "Sell", "activation or trade", depth, TicketPrice(chainHeight))) {
      throw std::runtime_error(strprintf("The Sell ticket with this txid [%s] is not validated", artTnxId));
    }

    bool ticketFound = false;
    CArtSellTicket existingTicket;
    if (CArtSellTicket::FindTicketInDb(KeyOne(), existingTicket))
    {
        if (existingTicket.signature == signature &&
            existingTicket.IsBlock(m_nBlock) &&
            existingTicket.m_txid == m_txid) // if this ticket is already in the DB
            ticketFound = true;
    }
    
    //1. check PastelID in this ticket matches PastelID in the referred ticket (Activation or Trade)
    //2. Verify the art is not already sold
    auto existingTradeTickets = CArtTradeTicket::FindAllTicketByArtTnxID(artTnxId);
    auto soldCopies = existingTradeTickets.size();
    auto existingSellTickets = CArtSellTicket::FindAllTicketByArtTnxID(artTnxId);
    auto sellTicketsNumber = existingSellTickets.size();
    auto totalCopies = 0;
    if (pastelTicket->ID() == TicketID::Activate)
    {
        // 1.a
        auto actTicket = dynamic_cast<CArtActivateTicket*>(pastelTicket.get());
        if (!actTicket)
        {
          throw std::runtime_error(strprintf(
            "The activation ticket with this txid [%s] referred by this sell ticket is invalid", artTnxId));
        }
        std::string& artistPastelID = actTicket->pastelID;
        if (artistPastelID != pastelID)
        {
          throw std::runtime_error(strprintf(
            "The PastelID [%s] in this ticket is not matching the Artist's PastelID [%s] in the Art Activation ticket with this txid [%s]",
            pastelID, artistPastelID, artTnxId));
        }
        //  Get ticket pointed by artTnxId. Here, this is an Activation ticket
        auto pArtTicket = CPastelTicketProcessor::GetTicket(actTicket->regTicketTnxId, TicketID::Art);
        auto artTicket = dynamic_cast<CArtRegTicket*>(pArtTicket.get());
        if (!artTicket)
        {
          throw std::runtime_error(strprintf(
            "The Art Registration ticket with this txid [%s] referred by this Art Activation ticket is invalid",
            actTicket->regTicketTnxId));
        }
        totalCopies = artTicket->totalCopies;
    
        if (preReg || !ticketFound)
        { //else if this is already confirmed ticket - skip this check, otherwise it will failed
            // 2.a Verify the number of existing trade tickets less then number of copies in the registration ticket
            if (soldCopies >= totalCopies)
            {
              throw std::runtime_error(strprintf(
                "The Art you are trying to sell - from registration ticket [%s] - is already sold - "
                "there are already [%zu] trade tickets, but only [%zu] copies were available",
                artTnxId, soldCopies, totalCopies));
            }
        }
    }
    else if (pastelTicket->ID() == TicketID::Trade)
    {
        // 1.b
        auto tradeTicket = dynamic_cast<CArtTradeTicket*>(pastelTicket.get());
        if (!tradeTicket)
        {
          throw std::runtime_error(strprintf(
            "The trade ticket with this txid [%s] referred by this sell ticket is invalid", artTnxId));
        }
        std::string& ownersPastelID = tradeTicket->pastelID;
        if (ownersPastelID != pastelID)
        {
          throw std::runtime_error(strprintf(
            "The PastelID [%s] in this ticket is not matching the PastelID [%s] in the Trade ticket with this txid [%s]",
            pastelID, ownersPastelID, artTnxId));
        }
        // 3.b Verify there is no already trade ticket referring to that trade ticket
        if (preReg || !ticketFound)
        {  //else if this is already confirmed ticket - skip this check, otherwise it will failed
            if (soldCopies > 0)
            {
              throw std::runtime_error(strprintf(
                "The Art you are trying to sell - from trade ticket [%s] - is already sold - see trade ticket with txid [%s]",
                artTnxId, existingTradeTickets[0].GetTxId()));
            }
        }
        totalCopies = 1;
    }
    
    if (copyNumber > totalCopies || copyNumber <= 0)
    {
      throw std::runtime_error(strprintf(
        "Invalid Sell ticket - copy number [%d] cannot exceed the total number of available copies [%d] or be <= 0",
        copyNumber, totalCopies));
    }
    
    //4. If this is replacement - verify that it is allowed (original ticket is not sold)
    // (ticket transaction replay attack protection)
    // If found similar ticket, replacement is possible if allowed
    auto it = find_if(existingSellTickets.begin(), existingSellTickets.end(),
                          [&](const CArtSellTicket& st) {
                            return (st.copyNumber == copyNumber &&
                                    !st.IsBlock(m_nBlock) &&
                                    st.m_txid != m_txid); //skip ourself!
                          });
    if (it != existingSellTickets.end())
    {
        if (CArtTradeTicket::CheckTradeTicketExistBySellTicket(it->m_txid))
        {
          throw std::runtime_error(strprintf(
            "Cannot replace Sell ticket - it has been already sold. txid - [%s] copyNumber [%d].", it->m_txid, copyNumber));
        }
    
        if (masterNodeCtrl.masternodeSync.IsSynced())
        { // Validate only if both blockchain and MNs are synced
            unsigned int nChainHeight = 0;
            {
                LOCK(cs_main);
                nChainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
            }
    
            if (it->GetBlock() + 2880 < nChainHeight)
            {  //1 block per 2.5; 4 blocks per 10 min; 24 blocks per 1h; 576 blocks per 24 h;
              throw std::runtime_error(strprintf(
                "Can only replace Sell ticket after 5 days. txid - [%s] copyNumber [%d].", it->m_txid, copyNumber));
            }
        }
    }
    
    return true;
}

std::string CArtSellTicket::ToJSON() const noexcept
{
    json jsonObj;
    jsonObj = {
            {"txid", m_txid},
            {"height", m_nBlock},
            {"ticket", {
                {"type", GetTicketName()},
                {"version", GetStoredVersion()},
                {"pastelID", pastelID},
                {"art_txid", artTnxId},
                {"copy_number", copyNumber},
                {"asked_price", askedPrice},
                {"valid_after", activeAfter},
                {"valid_before", activeBefore},
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }}
    };
    return jsonObj.dump(4);
}

bool CArtSellTicket::FindTicketInDb(const std::string& key, CArtSellTicket& ticket)
{
    ticket.key = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

std::vector<CArtSellTicket> CArtSellTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtSellTicket>(pastelID);
}

std::vector<CArtSellTicket> CArtSellTicket::FindAllTicketByArtTnxID(const std::string& artTnxId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtSellTicket>(artTnxId);
}

// CArtBuyTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CArtBuyTicket CArtBuyTicket::Create(std::string _sellTnxId, int _price, std::string _pastelID, const SecureString& strKeyPass)
{
    CArtBuyTicket ticket(std::move(_pastelID));
    
    ticket.sellTnxId = std::move(_sellTnxId);
    ticket.price = _price;
    
    ticket.GenerateTimestamp();
    
    string strTicket = ticket.ToStr();
    ticket.signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(strTicket.c_str()), strTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

std::string CArtBuyTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << sellTnxId;
    ss << price;
    ss << m_nTimestamp;
    return ss.str();
}

bool CArtBuyTicket::IsValid(bool preReg, int depth) const
{
    unsigned int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
    }

    // 0. Common validations
    std::unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(*this, preReg, sellTnxId, pastelTicket,
        [](const TicketID tid) { return (tid != TicketID::Sell); },
        "Buy", "sell", depth, price + TicketPrice(chainHeight))) {
      throw std::runtime_error(strprintf("The Buy ticket with Sell txid [%s] is not validated", sellTnxId));
    }
    
    // 1. Verify that there is no another buy ticket for the same sell ticket
    // or if there are, it is older then 1h and there is no trade ticket for it
    //buyTicket->ticketBlock <= height+24 (2.5m per block -> 24blocks/per hour) - MaxBuyTicketAge
    CArtBuyTicket existingBuyTicket;
    if (CArtBuyTicket::FindTicketInDb(sellTnxId, existingBuyTicket))
    {
        if (preReg)
        {  // if pre reg - this is probably repeating call, so signatures can be the same
          throw std::runtime_error(strprintf(
            "Buy ticket [%s] already exists for this sell ticket [%s]", existingBuyTicket.m_txid, sellTnxId));
        }
    
        // (ticket transaction replay attack protection)
        // though the similar transaction will be allowed if existing Buy ticket has expired
        if (existingBuyTicket.signature != signature ||
            !existingBuyTicket.IsBlock(m_nBlock) ||
            existingBuyTicket.m_txid != m_txid)
        {
            //check age
            if (existingBuyTicket.m_nBlock + masterNodeCtrl.MaxBuyTicketAge <= chainHeight)
            {
              throw std::runtime_error(strprintf(
                "Buy ticket [%s] already exists and is not yet 1h old for this sell ticket [%s]"
                "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
                existingBuyTicket.m_txid, sellTnxId, m_nBlock, m_txid, existingBuyTicket.m_nBlock, existingBuyTicket.m_txid));
            }
    
            //check trade ticket
            if (CArtTradeTicket::CheckTradeTicketExistByBuyTicket(existingBuyTicket.m_txid))
            {
              throw std::runtime_error(strprintf(
                "The sell ticket you are trying to buy [%s] is already sold", sellTnxId));
            }
        }
    }
    
    auto sellTicket = dynamic_cast<CArtSellTicket*>(pastelTicket.get());
    if (!sellTicket)
    {
      throw std::runtime_error(strprintf(
        "The sell ticket with this txid [%s] referred by this buy ticket is invalid", sellTnxId));
    }

    // 2. Verify Sell ticket is already or still active
    const unsigned int height = (preReg || IsBlock(0)) ? chainHeight : m_nBlock;
    if (height < sellTicket->activeAfter)
    {
      throw std::runtime_error(strprintf(
        "Sell ticket [%s] is only active after [%d] block height (Buy ticket block is [%d])",
        sellTicket->GetTxId(), sellTicket->activeAfter, height));
    }
    if (sellTicket->activeBefore > 0 && sellTicket->activeBefore < height)
    {
      throw std::runtime_error(strprintf(
        "Sell ticket [%s] is only active before [%d] block height (Buy ticket block is [%d])",
        sellTicket->GetTxId(), sellTicket->activeBefore, height));
    }

    // 3. Verify that the price is correct
    if (price < sellTicket->askedPrice)
    {
      throw std::runtime_error(strprintf(
        "The offered price [%d] is less than asked in the sell ticket [%d]", price, sellTicket->askedPrice));
    }
    
    return true;
}

std::string CArtBuyTicket::ToJSON() const noexcept
{
    json jsonObj;
    jsonObj = {
            {"txid", m_txid},
            {"height", m_nBlock},
            {"ticket", {
                {"type", GetTicketName()},
                {"version", GetStoredVersion()},
                {"pastelID", pastelID},
                {"sell_txid", sellTnxId},
                {"price", price},
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }}
    };
    return jsonObj.dump(4);
}

bool CArtBuyTicket::FindTicketInDb(const std::string& key, CArtBuyTicket& ticket)
{
    ticket.sellTnxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CArtBuyTicket::CheckBuyTicketExistBySellTicket(const std::string& _sellTnxId)
{
    CArtBuyTicket _ticket;
    _ticket.sellTnxId = _sellTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket);
}

std::vector<CArtBuyTicket> CArtBuyTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtBuyTicket>(pastelID);
}

// CArtTradeTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CArtTradeTicket CArtTradeTicket::Create(std::string _sellTnxId, std::string _buyTnxId, std::string _pastelID, const SecureString& strKeyPass)
{
    CArtTradeTicket ticket(std::move(_pastelID));
    
    ticket.sellTnxId = std::move(_sellTnxId);
    ticket.buyTnxId = std::move(_buyTnxId);
    
    auto pSellTicket = CPastelTicketProcessor::GetTicket(ticket.sellTnxId, TicketID::Sell);
    auto sellTicket = dynamic_cast<CArtSellTicket*>(pSellTicket.get());
    if (!sellTicket)
        throw std::runtime_error(tfm::format("The Art Sell ticket [txid=%s] referred by this Art Buy ticket is not in the blockchain. [txid=%s]",
                                             ticket.sellTnxId, ticket.buyTnxId));

    ticket.artTnxId = sellTicket->artTnxId;
    ticket.price = sellTicket->askedPrice;

    ticket.GenerateTimestamp();
    
    std::string strTicket = ticket.ToStr();
    ticket.signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(strTicket.c_str()), strTicket.size(), ticket.pastelID, strKeyPass);
    
    return ticket;
}

std::string CArtTradeTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << sellTnxId;
    ss << buyTnxId;
    ss << artTnxId;
    ss << m_nTimestamp;
    return ss.str();
}

bool CArtTradeTicket::IsValid(bool preReg, int depth) const
{
    unsigned int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
    }
    
    // 0. Common validations
    std::unique_ptr<CPastelTicket> sellTicket;
    if (!common_validation(*this, preReg, sellTnxId, sellTicket,
        [](const TicketID tid) { return (tid != TicketID::Sell); },
        "Trade", "sell", depth, price + TicketPrice(chainHeight))) {
      throw std::runtime_error(strprintf("The Trade ticket with Sell txid [%s] is not validated", sellTnxId));
    }

    std::unique_ptr<CPastelTicket> buyTicket;
    if (!common_validation(*this, preReg, buyTnxId, buyTicket,
        [](const TicketID tid) { return (tid != TicketID::Buy); },
        "Trade", "buy", depth, price + TicketPrice(chainHeight))) {
      throw std::runtime_error(strprintf("The Trade ticket with Buy txid [%s] is not validated", buyTnxId));
    }

    // 1. Verify that there is no another Trade ticket for the same Sell ticket
    CArtTradeTicket _tradeTicket;
    if (CArtTradeTicket::GetTradeTicketBySellTicket(sellTnxId, _tradeTicket))
    {
        // (ticket transaction replay attack protection)
        if (signature != _tradeTicket.signature ||
            m_txid != _tradeTicket.m_txid ||
            !_tradeTicket.IsBlock(m_nBlock))
        {
          throw std::runtime_error(strprintf(
            "There is already exist trade ticket for the sell ticket with this txid [%s]. Signature - our=%s; their=%s"
            "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
            sellTnxId,
            ed_crypto::Hex_Encode(signature.data(), signature.size()),
            ed_crypto::Hex_Encode(_tradeTicket.signature.data(), _tradeTicket.signature.size()),
            m_nBlock, m_txid, _tradeTicket.GetBlock(), _tradeTicket.m_txid));
        }
    }
    // 1. Verify that there is no another Trade ticket for the same Buy ticket
    _tradeTicket.sellTnxId = "";
    if (CArtTradeTicket::GetTradeTicketByBuyTicket(buyTnxId, _tradeTicket))
    {
        //Compare signatures to skip if the same ticket
        if (signature != _tradeTicket.signature || m_txid != _tradeTicket.m_txid || !_tradeTicket.IsBlock(m_nBlock))
        {
          throw std::runtime_error(strprintf(
            "There is already exist trade ticket for the buy ticket with this txid [%s]", buyTnxId));
        }
    }
    
    // 2. Verify Trade ticket PastelID is the same as in Buy Ticket
    auto buyTicketReal = dynamic_cast<CArtBuyTicket*>(buyTicket.get());
    if (!buyTicketReal)
    {
      throw std::runtime_error(strprintf(
        "The buy ticket with this txid [%s] referred by this trade ticket is invalid", buyTnxId));
    }
    std::string& buyersPastelID = buyTicketReal->pastelID;
    if (buyersPastelID != pastelID)
    {
      throw std::runtime_error(strprintf(
        "The PastelID [%s] in this Trade ticket is not matching the PastelID [%s] in the Buy ticket with this txid [%s]",
        pastelID, buyersPastelID, buyTnxId));
    }
    
    return true;
}

CAmount CArtTradeTicket::GetExtraOutputs(std::vector<CTxOut>& outputs) const
{
    auto pArtSellTicket = CPastelTicketProcessor::GetTicket(sellTnxId, TicketID::Sell);
    auto artSellTicket = dynamic_cast<CArtSellTicket*>(pArtSellTicket.get());
    if (!artSellTicket)
        throw std::runtime_error(strprintf("The Art Sell ticket with this txid [%s] is not in the blockchain", sellTnxId));

    auto sellerPastelID = artSellTicket->pastelID;
    CPastelIDRegTicket sellerPastelIDticket;
    if (!CPastelIDRegTicket::FindTicketInDb(sellerPastelID, sellerPastelIDticket))
        throw std::runtime_error(strprintf(
                "The PastelID [%s] from sell ticket with this txid [%s] is not in the blockchain or is invalid",
                sellerPastelID, sellTnxId));
    
    CAmount nPriceAmount = artSellTicket->askedPrice * COIN;
    CAmount nRoyaltyAmount = 0;
    CAmount nGreenNFTAmount = 0;
    
    auto artTicket = FindArtRegTicket();
    auto artRegTicket = dynamic_cast<CArtRegTicket*>(artTicket.get());
    if (!artRegTicket)
    {
        throw std::runtime_error(strprintf(
                "Can't find Art Registration ticket for this Trade ticket [txid=%s]",
                GetTxId()));
    }
    
    std::string strRoyaltyAddress;
    if (artRegTicket->nRoyalty > 0) {
      strRoyaltyAddress = artRegTicket->GetRoyaltyPayeeAddress();
      if (strRoyaltyAddress.empty()) {
        throw std::runtime_error(strprintf(
          "The Artist PastelID [%s] from Art Registration ticket with this txid [%s] is not in the blockchain or is invalid",
          artRegTicket->pastelIDs[CArtRegTicket::artistsign], artRegTicket->GetTxId()));
      }
      nRoyaltyAmount = nPriceAmount * artRegTicket->nRoyalty / 100;
    }
    
    if (!artRegTicket->strGreenAddress.empty()) {
      unsigned int chainHeight = 0; {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
      }
      nGreenNFTAmount = nPriceAmount * artRegTicket->GreenPercent(chainHeight) / 100;
    }

    nPriceAmount -= (nRoyaltyAmount + nGreenNFTAmount);

    KeyIO keyIO(Params());
    const auto addOutput = [&](const std::string& strAddress, const CAmount nAmount) -> bool
    {
        const auto dest = keyIO.DecodeDestination(strAddress);
        if (!IsValidDestination(dest))
            return false;
        
        CScript scriptPubKey = GetScriptForDestination(dest);
        CTxOut out(nAmount, scriptPubKey);
        outputs.push_back(out);
        return true;
    };
    
    if (!addOutput(sellerPastelIDticket.address, nPriceAmount)) {
        throw std::runtime_error(
                strprintf("The PastelID [%s] from sell ticket with this txid [%s] has invalid address",
                          sellerPastelID, sellTnxId));
    }
    
    if (!strRoyaltyAddress.empty() && !addOutput(sellerPastelIDticket.address, nRoyaltyAmount)) {
        throw std::runtime_error(
                strprintf("The PastelID [%s] from sell ticket with this txid [%s] has invalid address",
                          sellerPastelID, sellTnxId));
    }
    
    if (!artRegTicket->strGreenAddress.empty() && !addOutput(artRegTicket->strGreenAddress, nGreenNFTAmount)) {
        throw std::runtime_error(
                strprintf("The PastelID [%s] from sell ticket with this txid [%s] has invalid address",
                          sellerPastelID, sellTnxId));
    }
    
    return nPriceAmount + nRoyaltyAmount + nGreenNFTAmount;
}

std::string CArtTradeTicket::ToJSON() const noexcept
{
    json jsonObj;
    jsonObj = {
            {"txid", m_txid},
            {"height", m_nBlock},
            {"ticket", {
                {"type", GetTicketName()},
                {"version", GetStoredVersion()},
                {"pastelID", pastelID},
                {"sell_txid", sellTnxId},
                {"buy_txid", buyTnxId},
                {"art_txid", artTnxId},
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }}
    };
    return jsonObj.dump(4);
}

bool CArtTradeTicket::FindTicketInDb(const std::string& key, CArtTradeTicket& ticket)
{
    ticket.sellTnxId = key;
    ticket.buyTnxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket);
}

std::vector<CArtTradeTicket> CArtTradeTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtTradeTicket>(pastelID);
}

std::vector<CArtTradeTicket> CArtTradeTicket::FindAllTicketByArtTnxID(const std::string& artTnxID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtTradeTicket>(artTnxID);
}

bool CArtTradeTicket::CheckTradeTicketExistBySellTicket(const std::string& _sellTnxId)
{
    CArtTradeTicket _ticket;
    _ticket.sellTnxId = _sellTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket);
}

bool CArtTradeTicket::CheckTradeTicketExistByBuyTicket(const std::string& _buyTnxId)
{
    CArtTradeTicket _ticket;
    _ticket.buyTnxId = _buyTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(_ticket);
}

bool CArtTradeTicket::GetTradeTicketBySellTicket(const std::string& _sellTnxId, CArtTradeTicket& ticket)
{
    ticket.sellTnxId = _sellTnxId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CArtTradeTicket::GetTradeTicketByBuyTicket(const std::string& _buyTnxId, CArtTradeTicket& ticket)
{
    ticket.buyTnxId = _buyTnxId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

std::unique_ptr<CPastelTicket> CArtTradeTicket::FindArtRegTicket() const
{
    std::vector< std::unique_ptr<CPastelTicket> > chain;
    std::string errRet;
    if (!CPastelTicketProcessor::WalkBackTradingChain(artTnxId, chain, true, errRet))
    {
        throw std::runtime_error(errRet);
    }
    
    auto artRegTicket = dynamic_cast<CArtRegTicket*>(chain.front().get());
    if (!artRegTicket)
    {
        throw std::runtime_error(
                strprintf("This is not an Art Registration ticket [txid=%s]",
                          chain.front()->GetTxId()));
    }
    
    return std::move(chain.front());
}

// CArtRoyaltyTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CArtRoyaltyTicket CArtRoyaltyTicket::Create(
    std::string _pastelID, std::string _newPastelID,
    std::string _artTnxId, const SecureString& strKeyPass) {
  CArtRoyaltyTicket ticket(std::move(_pastelID), std::move(_newPastelID));

  ticket.artTnxId = std::move(_artTnxId);

  ticket.GenerateTimestamp();

  std::string strTicket = ticket.ToStr();
  ticket.signature = CPastelID::Sign(reinterpret_cast<const unsigned char*>(strTicket.c_str()), strTicket.size(), ticket.pastelID, strKeyPass);

  return ticket;
}

std::string CArtRoyaltyTicket::ToStr() const noexcept {
  std::stringstream ss;
  ss << pastelID;
  ss << newPastelID;
  ss << artTnxId;
  ss << m_nTimestamp;
  return ss.str();
}

bool CArtRoyaltyTicket::IsValid(bool preReg, int depth) const {
  unsigned int chainHeight = 0; {
    LOCK(cs_main);
    chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
  }

  // 0. Common validations
  std::unique_ptr<CPastelTicket> pastelTicket;
  if (!common_validation(*this, preReg, artTnxId, pastelTicket,
      [](const TicketID tid) { return (tid != TicketID::Art); },
      "Royalty", "art", depth, TicketPrice(chainHeight))) {
    throw std::runtime_error(strprintf("The Change Royalty ticket with art txid [%s] is not validated", artTnxId));
  }

  // Check the Royalty change ticket for that Art is already in the database
  // (ticket transaction replay attack protection)
  CArtRoyaltyTicket _ticket;
  if (FindTicketInDb(KeyOne(), _ticket) &&
     (preReg ||  // if pre reg - this is probably repeating call, so signatures can be the same
      _ticket.signature != signature || !_ticket.IsBlock(m_nBlock) || _ticket.m_txid != m_txid)) {
    throw std::runtime_error(strprintf(
      "The Change Royalty ticket is already registered in blockchain [pastelID = %s; new_pastelID = %s]"
      "[this ticket block = %u txid = %s; found ticket block = %u txid = %s] with art txid [%s]",
      pastelID, newPastelID, m_nBlock, m_txid, _ticket.GetBlock(), _ticket.m_txid, artTnxId));
  }

  if (newPastelID.empty()) {
    throw std::runtime_error("The Change Royalty ticket new_pastelID is empty");
  }

  CPastelIDRegTicket newPastelIDticket;
  if (!CPastelIDRegTicket::FindTicketInDb(newPastelID, newPastelIDticket)) {
    throw std::runtime_error(strprintf(
      "The new_pastelID [%s] for Change Royalty ticket with art txid [%s] is not in the blockchain or is invalid",
      newPastelID, artTnxId));
  }

  int index{0};
  int foundIndex{-1};
  unsigned int highBlock{0};
  const auto tickets = CArtRoyaltyTicket::FindAllTicketByArtTnxId(artTnxId);
  for (const auto& royaltyTicket: tickets) {
    if (royaltyTicket.signature == signature) {
      continue;
    }
    if (royaltyTicket.m_nBlock == 0) {
      throw std::runtime_error(strprintf(
        "The old Change Royalty ticket is registered in blockchain [pastelID = %s; new_pastelID = %s]"
        "with [ticket block = %d txid = %s] is invalid",
        royaltyTicket.pastelID, royaltyTicket.newPastelID, royaltyTicket.GetBlock(), royaltyTicket.m_txid));
    }
    if (royaltyTicket.m_nBlock > highBlock) {
      highBlock = royaltyTicket.m_nBlock;
      foundIndex = index;
    }
    ++index;
  }
  if (foundIndex >= 0) {
    // 1. check PastelID in Royalty ticket matches PastelID from this ticket
    if (tickets.at(foundIndex).newPastelID != pastelID) {
      throw std::runtime_error(strprintf(
        "The PastelID [%s] is not matching the PastelID [%s] in the Change Royalty ticket with art txid [%s]",
        pastelID, tickets.at(foundIndex).newPastelID, artTnxId));
    }
  } else {
    auto artTicket = dynamic_cast<const CArtRegTicket*>(pastelTicket.get());
    if (!artTicket) {
      throw std::runtime_error(strprintf(
        "The art Reg ticket with this txid [%s] is not in the blockchain or is invalid", artTnxId));
    }

    // 1. check Artist PastelID in ArtReg ticket matches PastelID from this ticket
    const std::string& artistPastelID = artTicket->pastelIDs[CArtRegTicket::artistsign];
    if (artistPastelID != pastelID) {
      throw std::runtime_error(strprintf(
        "The PastelID [%s] is not matching the Artist's PastelID [%s] in the Art Reg ticket with this txid [%s]",
        pastelID, artistPastelID, artTnxId));
    }
  }

  return true;
}

std::string CArtRoyaltyTicket::ToJSON() const noexcept {
  const json jsonObj {
    {"txid", m_txid},
    {"height", m_nBlock},
    {"ticket", {
      {"type", GetTicketName()},
      {"version", GetStoredVersion()},
      {"pastelID", pastelID},
      {"new_pastelID", newPastelID},
      {"art_txid", artTnxId},
      {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
    }}
  };
  return jsonObj.dump(4);
}

bool CArtRoyaltyTicket::FindTicketInDb(const std::string& key, CArtRoyaltyTicket& ticket) {
  ticket.signature = {key.cbegin(), key.cend()};
  return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

std::vector<CArtRoyaltyTicket> CArtRoyaltyTicket::FindAllTicketByPastelID(const std::string& pastelID) {
  return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtRoyaltyTicket>(pastelID);
}

std::vector<CArtRoyaltyTicket> CArtRoyaltyTicket::FindAllTicketByArtTnxId(const std::string& artTnxId) {
  return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CArtRoyaltyTicket>(artTnxId);
}

// CTakeDownTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CTakeDownTicket::FindTicketInDb(const std::string& key, CTakeDownTicket& ticket)
{
    return false;
}
