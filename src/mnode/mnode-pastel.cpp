// Copyright (c) 2018 The PASTELCoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "key_io.h"
#include "init.h"
#include "str_utils.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

#include "mnode/mnode-controller.h"
#include "mnode/mnode-pastel.h"
#include "mnode/mnode-msgsigner.h"
#include "mnode/mnode-badwords.h"
#include "mnode/ticket-processor.h"

#include "pastelid/pastel_key.h"
#include "pastelid/ed.h"
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
        ss << vector_to_string(ticket.mn_signature);
    }
    const std::string fullTicket = ss.str();
    string_to_vector(CPastelID::Sign(fullTicket, ticket.pastelID, strKeyPass), ticket.pslid_signature);
    
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
        ss << vector_to_string(mn_signature);
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
    ss << vector_to_string(mn_signature);
    std::string fullTicket = ss.str();
    if (!CPastelID::Verify(fullTicket, vector_to_string(pslid_signature), pastelID))
      throw std::runtime_error(strprintf("Ticket's PastelID signature is invalid. PastelID - [%s]", pastelID));
     
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

// CNFTRegTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Current nft_ticket - 8 Items!!!!
{
  "nft_ticket_version": integer     // 1
  "author": bytes,                  // PastelID of the author (creator)
  "blocknum": integer,              // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes               // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "copies": integer,                // number of copies
  "royalty": short,                 // how much creator should get on all future resales (not yet supported by cNode)
  "green_address": string,          // address for Green NFT payment (not yet supported by cNode)
  "app_ticket": ...
}
*/
CNFTRegTicket CNFTRegTicket::Create(
        std::string _ticket, const std::string& signatures,
        std::string _pastelID, const SecureString& strKeyPass,
        std::string _keyOne, std::string _keyTwo,
        CAmount _storageFee)
{
    CNFTRegTicket ticket(std::move(_ticket));
    
    //NFT Ticket
    auto jsonTicketObj = json::parse(ed_crypto::Base64_Decode(ticket.NFTTicket));
    if (jsonTicketObj.size() != 8){
        throw std::runtime_error("NFT ticket json is incorrect");
    }
    if (jsonTicketObj["nft_ticket_version"] != 1) {
        throw std::runtime_error("Only accept version 1 of NFT ticket json");
    }
    ticket.creatorHeight = jsonTicketObj["blocknum"];
    ticket.totalCopies = jsonTicketObj["copies"];
    ticket.nRoyalty = jsonTicketObj["royalty"];
    bool bHasGreen = jsonTicketObj["green"];
    if (bHasGreen) {
        unsigned int chainHeight = 0; {
            LOCK(cs_main);
            chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
        }
        ticket.strGreenAddress = CNFTRegTicket::GreenAddress(chainHeight);
    }
    
    //creator's and MN2/3's signatures
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
        if (el.key() == "creator"){
            ticket.pastelIDs[creatorsign] = std::move(pastelID);
            ticket.ticketSignatures[creatorsign] = ed_crypto::Base64_Decode(signature);
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
    string_to_vector(CPastelID::Sign(ticket.NFTTicket, ticket.pastelIDs[mainmnsign], strKeyPass), ticket.ticketSignatures[mainmnsign]);
    return ticket;
}

std::string CNFTRegTicket::ToStr() const noexcept
{
    return NFTTicket;
}

bool CNFTRegTicket::IsValid(bool preReg, int depth) const
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
        
        // A.1 check that NFT ticket already in the blockchain
        if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this))
        {
          throw std::runtime_error(strprintf(
            "This NFT is already registered in blockchain [Key1 = %s; Key2 = %s]", keyOne, keyTwo));
        }
    
        // A.2 validate that address has coins to pay for registration - 10PSL
        const auto fullTicketPrice = TicketPrice(chainHeight); //10% of storage fee is paid by the 'creator' and this ticket is created by MN
        if (pwalletMain->GetBalance() < fullTicketPrice*COIN)
        {
          throw std::runtime_error(strprintf("Not enough coins to cover price [%" PRId64 "]", fullTicketPrice));
        }
    }
    
    // (ticket transaction replay attack protection)
    CNFTRegTicket _ticket;
    if ((FindTicketInDb(keyOne, _ticket) || (FindTicketInDb(keyTwo, _ticket))) &&
        (!_ticket.IsBlock(m_nBlock) || _ticket.m_txid != m_txid))
    {
      throw std::runtime_error(strprintf(
        "This NFT is already registered in blockchain [Key1 = %s; Key2 = %s]"
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
        // and this will mark NFTReg tnx from being valid!!!
        CPastelIDRegTicket pastelIdRegTicket;
        if (!CPastelIDRegTicket::FindTicketInDb(pastelIDs[mnIndex], pastelIdRegTicket)){
            if (mnIndex == creatorsign) {
              throw std::runtime_error(strprintf("Creator PastelID is not registered [%s]", pastelIDs[mnIndex]));
            } else {
              throw std::runtime_error(strprintf("MN%d PastelID is not registered [%s]", mnIndex, pastelIDs[mnIndex]));
            }
        }
        //2. PastelIDs are valid
        try {
          pastelIdRegTicket.IsValid(false, ++depth);
        }
        catch (const std::exception& ex) {
          if (mnIndex == creatorsign) {
             throw std::runtime_error(strprintf("Creator PastelID is invalid [%s] - %s", pastelIDs[mnIndex], ex.what()));
          } else {
            throw std::runtime_error(strprintf("MN%d PastelID is invalid [%s] - %s", mnIndex, pastelIDs[mnIndex], ex.what()));
          }
        }
        catch (...) {
          if (mnIndex == creatorsign) {
            throw std::runtime_error(strprintf("Creator PastelID is invalid [%s] - Unknown exception", pastelIDs[mnIndex]));
          } else {
            throw std::runtime_error(strprintf("MN%d PastelID is invalid [%s] - Unknown exception", mnIndex, pastelIDs[mnIndex]));
          }
        }
        //3. creator PastelID is personal PastelID and MNs PastelIDs are not personal
        if (mnIndex == creatorsign)
        {
            if (!pastelIdRegTicket.outpoint.IsNull())
            {
              throw std::runtime_error(strprintf("Creator PastelID is NOT personal PastelID [%s]", pastelIDs[mnIndex]));
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
            { //NFT ticket needs synced MNs
                auto topBlockMNs = masterNodeCtrl.masternodeManager.GetTopMNsForBlock(creatorHeight, true);
                auto found = find_if(topBlockMNs.begin(), topBlockMNs.end(),
                                     [&pastelIdRegTicket](CMasternode const &mn) {
                                         return mn.vin.prevout == pastelIdRegTicket.outpoint;
                                     });
    
                if (found == topBlockMNs.end())
                { //not found
                  throw std::runtime_error(strprintf(
                    "MN%d was NOT in the top masternodes list for block %d", mnIndex, creatorHeight));
                }
            }
        }
    }
    //5. Signatures matches included PastelIDs (signature verification is slower - hence separate loop)
    for (int mnIndex=0; mnIndex < allsigns; mnIndex++)
    {
        if (!CPastelID::Verify(NFTTicket, vector_to_string(ticketSignatures[mnIndex]), pastelIDs[mnIndex]))
        {
            if (mnIndex == creatorsign)
              throw std::runtime_error("Creator signature is invalid");
            throw std::runtime_error(strprintf("MN%d signature is invalid", mnIndex));
        }
    }
    
    if (nRoyalty < 0 || nRoyalty > 0.2)
    {
      throw std::runtime_error(strprintf("Royalty can't be %hu per cent, Min is 0 and Max is 20 per cent", nRoyalty*100));
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

std::string CNFTRegTicket::ToJSON() const noexcept {
  const json jsonObj {
    {"txid", m_txid},
    {"height", m_nBlock},
    {"ticket", {
      {"type", GetTicketName()},
      {"nft_ticket", NFTTicket},
      {"version", GetStoredVersion()},
      {"signatures", {
        {"creator", {
          {pastelIDs[creatorsign], ed_crypto::Base64_Encode(ticketSignatures[creatorsign].data(), ticketSignatures[creatorsign].size())}
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
      {"creator_height", creatorHeight},
      {"total_copies", totalCopies},
      {"royalty", nRoyalty},
      {"royalty_address", GetRoyaltyPayeeAddress()},
      {"green", !strGreenAddress.empty()},
      //      {"green_address", strGreenAddress},
      {"storage_fee", storageFee},
    }}
  };
    
  return jsonObj.dump(4);
}

std::string CNFTRegTicket::GetRoyaltyPayeePastelID() const {
  if (!nRoyalty) { return {}; }

  int index{0};
  int foundIndex{-1};
  unsigned int highBlock{0};
  const auto tickets = CNFTRoyaltyTicket::FindAllTicketByNFTTnxId(m_txid);
  for (const auto& ticket: tickets) {
    if (ticket.GetBlock() > highBlock) {
      highBlock = ticket.GetBlock();
      foundIndex = index;
    }
    ++index;
  }
  return foundIndex >= 0 ? tickets.at(foundIndex).newPastelID : pastelIDs[CNFTRegTicket::creatorsign];
}

std::string CNFTRegTicket::GetRoyaltyPayeeAddress() const {
  const std::string pastelID = GetRoyaltyPayeePastelID();
  if (!pastelID.empty()) {
    CPastelIDRegTicket ticket;
    if (CPastelIDRegTicket::FindTicketInDb(pastelID, ticket)) {
      return ticket.address;
    }
  }
  return {};
}

bool CNFTRegTicket::FindTicketInDb(const std::string& key, CNFTRegTicket& _ticket)
{
    _ticket.keyOne = key;
    _ticket.keyTwo = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(_ticket) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(_ticket);
}

bool CNFTRegTicket::CheckIfTicketInDb(const std::string& key)
{
    CNFTRegTicket _ticket;
    _ticket.keyOne = key;
    _ticket.keyTwo = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket) ||
           masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(_ticket);
}

std::vector<CNFTRegTicket> CNFTRegTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTRegTicket>(pastelID);
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
            throw std::runtime_error(strprintf("Not enough coins to cover price [%" PRId64 "]", ticketPrice));
        }
    }
    
    // C. Something to always validate
    
    // C.1 Check there are ticket referred from that new ticket with this tnxId
    uint256 txid;
    txid.SetHex(strTnxId);
    //  Get ticket pointed by NFTTnxId. This is either Activation or Trade tickets (Sell, Buy, Trade)
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
    // We will check that it is the correct PastelID and the one that belongs to the owner of the NFT in the following steps
    std::string strThisTicket = ticket.ToStr();
    if (!CPastelID::Verify(strThisTicket, vector_to_string(ticket.signature), ticket.pastelID))
    {
      throw std::runtime_error(strprintf("%s ticket's signature is invalid. PastelID - [%s]", thisTicket, ticket.pastelID));
    }
    
    // C.3 check the referred ticket is valid
    // (IsValid of the referred ticket validates signatures as well!)
    if (depth > 0)
        return true;
    
    if (!pastelTicket->IsValid(false, ++depth)) {
      throw std::runtime_error(strprintf("The %s ticket with this txid [%s] is invalid", prevTicket, strTnxId));
    }
    
    return true;
}

/**
 * Checks either still exist available copies to sell or generates exception otherwise
 * @param nftTnxId is the NFT txid with either 1) NFT activation ticket or 2) trade ticket in it
 * @param signature is the signature of current CNFTTradeTicket that is checked
 */
void trade_copy_validation(const std::string& nftTnxId, const std::vector<unsigned char>& signature) {
//  if (!masterNodeCtrl.masternodeSync.IsSynced()) {
//    throw std::runtime_error("Can not validate trade ticket as master node is not synced");
//  }

  size_t totalCopies{0};

  uint256 txid;
  txid.SetHex(nftTnxId);
  auto nftTicket = CPastelTicketProcessor::GetTicket(txid);
  if (!nftTicket) {
    throw std::runtime_error(strprintf(
      "The NFT ticket with txid [%s] referred by this trade ticket is not in the blockchain", nftTnxId));
  }
  if (nftTicket->ID() == TicketID::Activate) {
    auto actTicket = dynamic_cast<const CNFTActivateTicket*>(nftTicket.get());
    if (!actTicket) {
      throw std::runtime_error(strprintf(
        "The activation ticket with txid [%s] referred by this trade ticket is invalid", nftTnxId));
    }

    auto pNFTTicket = CPastelTicketProcessor::GetTicket(actTicket->regTicketTnxId, TicketID::NFT);
    if (!pNFTTicket) {
      throw std::runtime_error(strprintf(
        "The registration ticket with txid [%s] referred by activation ticket is invalid",
        actTicket->regTicketTnxId));
    }

    auto NFTTicket = dynamic_cast<const CNFTRegTicket*>(pNFTTicket.get());
    if (!NFTTicket) {
      throw std::runtime_error(strprintf(
        "The registration ticket with txid [%s] referred by activation ticket is invalid",
        actTicket->regTicketTnxId));
    }

    totalCopies = NFTTicket->totalCopies;
  } else if (nftTicket->ID() == TicketID::Trade) {
    auto tradeTicket = dynamic_cast<const CNFTTradeTicket*>(nftTicket.get());
    if (!tradeTicket) {
      throw std::runtime_error(strprintf(
        "The trade ticket with txid [%s] referred by this trade ticket is invalid", nftTnxId));
    }

    totalCopies = 1;
  } else {
    throw std::runtime_error(strprintf(
      "Unknown ticket with txid [%s] referred by this trade ticket is invalid", nftTnxId));
  }

  size_t soldCopies{0};
  const auto existingTradeTickets = CNFTTradeTicket::FindAllTicketByNFTTnxID(nftTnxId);
  for (const auto& t: existingTradeTickets) {
    if (t.signature != signature) {
      ++soldCopies;
    }
  }

  if (soldCopies >= totalCopies) {
    throw std::runtime_error(strprintf(
      "Invalid trade ticket - cannot exceed the total number of available copies [%zu] with sold [%zu] copies",
      totalCopies, soldCopies));
  }
}

// CNFTActivateTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTActivateTicket CNFTActivateTicket::Create(std::string _regTicketTxId, int _creatorHeight, int _storageFee, std::string _pastelID, const SecureString& strKeyPass)
{
    CNFTActivateTicket ticket(std::move(_pastelID));
    
    ticket.regTicketTnxId = std::move(_regTicketTxId);
    ticket.creatorHeight = _creatorHeight;
    ticket.storageFee = _storageFee;
    
    ticket.GenerateTimestamp();
    
    std::string strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, strKeyPass), ticket.signature);
    
    return ticket;
}

std::string CNFTActivateTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << regTicketTnxId;
    ss << creatorHeight;
    ss << storageFee;
    ss << m_nTimestamp;
    return ss.str();
}

bool CNFTActivateTicket::IsValid(bool preReg, int depth) const
{
    unsigned int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
    }
    
    // 0. Common validations
    std::unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(*this, preReg, regTicketTnxId, pastelTicket,
        [](const TicketID tid) { return (tid != TicketID::NFT); },
        "Activation", "NFT", depth,
        TicketPrice(chainHeight) + (storageFee * 9 / 10))) { //fee for ticket + 90% of storage fee
      throw std::runtime_error(strprintf(
        "The Activation ticket for the Registration ticket with txid [%s] is not validated [block = %u txid = %s]",
        regTicketTnxId, m_nBlock, m_txid));
    }

    // Check the Activation ticket for that Registration ticket is already in the database
    // (ticket transaction replay attack protection)
    CNFTActivateTicket existingTicket;
    if (CNFTActivateTicket::FindTicketInDb(regTicketTnxId, existingTicket))
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
    
    auto NFTTicket = dynamic_cast<CNFTRegTicket*>(pastelTicket.get());
    if (!NFTTicket)
    {
      throw std::runtime_error(strprintf(
        "The NFT ticket with this txid [%s] is not in the blockchain or is invalid", regTicketTnxId));
    }
    
    // 1. check creator PastelID in NFTReg ticket matches PastelID from this ticket
    std::string& creatorPastelID = NFTTicket->pastelIDs[CNFTRegTicket::creatorsign];
    if (creatorPastelID != pastelID)
    {
      throw std::runtime_error(strprintf(
        "The PastelID [%s] is not matching the Creator's PastelID [%s] in the NFT Reg ticket with this txid [%s]",
        pastelID, creatorPastelID, regTicketTnxId));
    }
    
    // 2. check NFTReg ticket is at the assumed height
    if (NFTTicket->creatorHeight != creatorHeight)
    {
      throw std::runtime_error(strprintf(
        "The CreatorHeight [%d] is not matching the CreatorHeight [%d] in the NFT Reg ticket with this txid [%s]",
        creatorHeight, NFTTicket->creatorHeight, regTicketTnxId));
    }
    
    // 3. check NFTReg ticket fee is same as storageFee
    if (NFTTicket->storageFee != storageFee)
    {
      throw std::runtime_error(strprintf(
        "The storage fee [%d] is not matching the storage fee [%d] in the NFT Reg ticket with this txid [%s]",
        storageFee, NFTTicket->storageFee, regTicketTnxId));
    }
    
    return true;
}

CAmount CNFTActivateTicket::GetExtraOutputs(std::vector<CTxOut>& outputs) const
{
    auto ticket = CPastelTicketProcessor::GetTicket(regTicketTnxId, TicketID::NFT);
    auto NFTTicket = dynamic_cast<CNFTRegTicket*>(ticket.get());
    if (!NFTTicket)
        return 0;
    
    CAmount nAllAmount = 0;
    CAmount nAllMNFee = storageFee * COIN * 9 / 10; //90%
    CAmount nMainMNFee = nAllMNFee * 3 / 5; //60% of 90%
    CAmount nOtherMNFee = nAllMNFee / 5;    //20% of 90%
    
    KeyIO keyIO(Params());
    for (int mn = CNFTRegTicket::mainmnsign; mn<CNFTRegTicket::allsigns; mn++)
    {
        auto mnPastelID = NFTTicket->pastelIDs[mn];
        CPastelIDRegTicket mnPastelIDticket;
        if (!CPastelIDRegTicket::FindTicketInDb(mnPastelID, mnPastelIDticket))
            throw std::runtime_error(strprintf(
                    "The PastelID [%s] from NFT ticket with this txid [%s] is not in the blockchain or is invalid",
                    mnPastelID, regTicketTnxId));
    
        const auto dest = keyIO.DecodeDestination(mnPastelIDticket.address);
        if (!IsValidDestination(dest))
            throw std::runtime_error(
                    strprintf("The PastelID [%s] from NFT ticket with this txid [%s] has invalid MN's address",
                              mnPastelID, regTicketTnxId));
    
        CScript scriptPubKey = GetScriptForDestination(dest);
        CAmount nAmount = (mn == CNFTRegTicket::mainmnsign? nMainMNFee: nOtherMNFee);
        nAllAmount += nAmount;
    
        CTxOut out(nAmount, scriptPubKey);
        outputs.push_back(out);
    }
    
    return nAllAmount;
}

std::string CNFTActivateTicket::ToJSON() const noexcept
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
				{"creator_height", creatorHeight},
                {"storage_fee", storageFee},
				{"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
		 }}
	};
	
	return jsonObj.dump(4);
}

bool CNFTActivateTicket::FindTicketInDb(const std::string& key, CNFTActivateTicket& ticket)
{
    ticket.regTicketTnxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CNFTActivateTicket::CheckTicketExistByNFTTicketID(const std::string& regTicketTnxId)
{
    CNFTActivateTicket ticket;
    ticket.regTicketTnxId = regTicketTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

std::vector<CNFTActivateTicket> CNFTActivateTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTActivateTicket>(pastelID);
}

std::vector<CNFTActivateTicket> CNFTActivateTicket::FindAllTicketByCreatorHeight(int height)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTActivateTicket>(std::to_string(height));
}

// NFT Trade Tickets ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CNFTSellTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTSellTicket CNFTSellTicket::Create(std::string _NFTTnxId, int _askedPrice, int _validAfter, int _validBefore, int _copy_number, std::string _pastelID, const SecureString& strKeyPass)
{
    CNFTSellTicket ticket(std::move(_pastelID));
    
    ticket.NFTTnxId = std::move(_NFTTnxId);
    ticket.askedPrice = _askedPrice;
    ticket.activeBefore = _validBefore;
    ticket.activeAfter = _validAfter;
    
    ticket.GenerateTimestamp();
    
    //NOTE: Sell ticket for Trade ticket will always has copyNumber = 1
    ticket.copyNumber = _copy_number > 0 ? 
        _copy_number : static_cast<decltype(ticket.copyNumber)>(CNFTSellTicket::FindAllTicketByNFTTnxID(ticket.NFTTnxId).size()) + 1;
    ticket.key = ticket.NFTTnxId + ":" + to_string(ticket.copyNumber);
    
    std::string strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, strKeyPass), ticket.signature);
    
    return ticket;
}

std::string CNFTSellTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << NFTTnxId;
    ss << askedPrice;
    ss << copyNumber;
    ss << activeBefore;
    ss << activeAfter;
    ss << m_nTimestamp;
    return ss.str();
}

bool CNFTSellTicket::IsValid(bool preReg, int depth) const
{
    unsigned int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
    }
    
    // 0. Common validations
    std::unique_ptr<CPastelTicket> pastelTicket;
    if (!common_validation(*this, preReg, NFTTnxId, pastelTicket,
        [](const TicketID tid) { return (tid != TicketID::Activate && tid != TicketID::Trade); },
        "Sell", "activation or trade", depth, TicketPrice(chainHeight))) {
      throw std::runtime_error(strprintf("The Sell ticket with this txid [%s] is not validated", NFTTnxId));
    }

    if (!askedPrice) {
      throw std::runtime_error(strprintf("The asked price for Sell ticket with NFT txid [%s] should be not 0", NFTTnxId));
    }

    bool ticketFound = false;
    CNFTSellTicket existingTicket;
    if (CNFTSellTicket::FindTicketInDb(KeyOne(), existingTicket))
    {
        if (existingTicket.signature == signature &&
            existingTicket.IsBlock(m_nBlock) &&
            existingTicket.m_txid == m_txid) // if this ticket is already in the DB
            ticketFound = true;
    }

    // Check PastelID in this ticket matches PastelID in the referred ticket (Activation or Trade)
    size_t totalCopies{0};
    // Verify the NFT is not already sold or gifted
    const auto verifyAvailableCopies = [this](const std::string& strTicket, const size_t totalCopies) {
      const auto existingTradeTickets = CNFTTradeTicket::FindAllTicketByNFTTnxID(NFTTnxId);
      size_t soldCopies = existingTradeTickets.size();

      if (soldCopies >= totalCopies) {
        throw std::runtime_error(strprintf(
          "The NFT you are trying to sell - from %s ticket [%s] - is already sold - "
          "there are already [%zu] sold copies, but only [%zu] copies were available",
          strTicket, NFTTnxId, soldCopies, totalCopies));
      }
    };
    if (pastelTicket->ID() == TicketID::Activate)
    {
        // 1.a
        auto actTicket = dynamic_cast<const CNFTActivateTicket*>(pastelTicket.get());
        if (!actTicket)
        {
          throw std::runtime_error(strprintf(
            "The activation ticket with this txid [%s] referred by this sell ticket is invalid", NFTTnxId));
        }
        const std::string& creatorPastelID = actTicket->pastelID;
        if (creatorPastelID != pastelID)
        {
          throw std::runtime_error(strprintf(
            "The PastelID [%s] in this ticket is not matching the Creator's PastelID [%s] in the NFT Activation ticket with this txid [%s]",
            pastelID, creatorPastelID, NFTTnxId));
        }
        //  Get ticket pointed by NFTTnxId. Here, this is an Activation ticket
        auto pNFTTicket = CPastelTicketProcessor::GetTicket(actTicket->regTicketTnxId, TicketID::NFT);
        if (!pNFTTicket)
        {
          throw std::runtime_error(strprintf(
            "The NFT Registration ticket with this txid [%s] referred by this NFT Activation ticket is invalid",
            actTicket->regTicketTnxId));
        }
        auto NFTTicket = dynamic_cast<const CNFTRegTicket*>(pNFTTicket.get());
        if (!NFTTicket)
        {
          throw std::runtime_error(strprintf(
            "The NFT Registration ticket with this txid [%s] referred by this NFT Activation ticket is invalid",
            actTicket->regTicketTnxId));
        }
        totalCopies = NFTTicket->totalCopies;
    
        if (preReg || !ticketFound)
        { //else if this is already confirmed ticket - skip this check, otherwise it will failed
          verifyAvailableCopies("registration", totalCopies);
        }
    }
    else if (pastelTicket->ID() == TicketID::Trade)
    {
        // 1.b
        auto tradeTicket = dynamic_cast<const CNFTTradeTicket*>(pastelTicket.get());
        if (!tradeTicket)
        {
          throw std::runtime_error(strprintf(
            "The trade ticket with this txid [%s] referred by this sell ticket is invalid", NFTTnxId));
        }
        const std::string& ownersPastelID = tradeTicket->pastelID;
        if (ownersPastelID != pastelID)
        {
          throw std::runtime_error(strprintf(
            "The PastelID [%s] in this ticket is not matching the PastelID [%s] in the Trade ticket with this txid [%s]",
            pastelID, ownersPastelID, NFTTnxId));
        }
        totalCopies = 1;

        // 3.b Verify there is no already trade ticket referring to that trade ticket
        if (preReg || !ticketFound)
        {  //else if this is already confirmed ticket - skip this check, otherwise it will failed
          verifyAvailableCopies("trade", totalCopies);
        }
    }
    
    if (copyNumber > totalCopies || copyNumber == 0)
    {
      throw std::runtime_error(strprintf(
        "Invalid Sell ticket - copy number [%d] cannot exceed the total number of available copies [%d] or be 0",
        copyNumber, totalCopies));
    }
    
    //4. If this is replacement - verify that it is allowed (original ticket is not sold)
    // (ticket transaction replay attack protection)
    // If found similar ticket, replacement is possible if allowed
    // Can be a few Sell tickets
    const auto existingSellTickets = CNFTSellTicket::FindAllTicketByNFTTnxID(NFTTnxId);
    for (const auto& t: existingSellTickets) {
      if (t.IsBlock(m_nBlock) || t.m_txid == m_txid || t.copyNumber != copyNumber) {
        continue;
      }

      if (CNFTTradeTicket::CheckTradeTicketExistBySellTicket(t.m_txid)) {
        throw std::runtime_error(strprintf(
          "Cannot replace Sell ticket - it has been already sold. "
          "txid - [%s] copyNumber [%d].", t.m_txid, copyNumber));
      }

      // find if it is the old ticket
      if (m_nBlock > 0 && t.m_nBlock > m_nBlock) {
        throw std::runtime_error(strprintf(
          "This Sell ticket has been replaced with another ticket. "
          "txid - [%s] copyNumber [%d].", t.m_txid, copyNumber));
      }

      // Validate only if both blockchain and MNs are synced
      if (!masterNodeCtrl.masternodeSync.IsSynced()) {
        throw std::runtime_error(strprintf(
          "Can not replace the Sell ticket as master node not is not synced. "
          "txid - [%s] copyNumber [%d].", t.m_txid, copyNumber));
      }
      {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
      }
      if (t.m_nBlock + 2880 > chainHeight) {
        // 1 block per 2.5; 4 blocks per 10 min; 24 blocks per 1h; 576 blocks per 24 h;
        throw std::runtime_error(strprintf(
          "Can only replace Sell ticket after 5 days. txid - [%s] copyNumber [%d].", t.m_txid, copyNumber));
      }
    }
    
    return true;
}

std::string CNFTSellTicket::ToJSON() const noexcept
{
    const json jsonObj {
            {"txid", m_txid},
            {"height", m_nBlock},
            {"ticket", {
                {"type", GetTicketName()},
                {"version", GetStoredVersion()},
                {"pastelID", pastelID},
                {"nft_txid", NFTTnxId},
                {"copy_number", copyNumber},
                {"asked_price", askedPrice},
                {"valid_after", activeAfter},
                {"valid_before", activeBefore},
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }}
    };
    return jsonObj.dump(4);
}

bool CNFTSellTicket::FindTicketInDb(const std::string& key, CNFTSellTicket& ticket)
{
    ticket.key = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

std::vector<CNFTSellTicket> CNFTSellTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTSellTicket>(pastelID);
}

std::vector<CNFTSellTicket> CNFTSellTicket::FindAllTicketByNFTTnxID(const std::string& NFTTnxId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTSellTicket>(NFTTnxId);
}

// CNFTBuyTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTBuyTicket CNFTBuyTicket::Create(std::string _sellTnxId, int _price, std::string _pastelID, const SecureString& strKeyPass)
{
    CNFTBuyTicket ticket(std::move(_pastelID));
    
    ticket.sellTnxId = std::move(_sellTnxId);
    ticket.price = _price;
    
    ticket.GenerateTimestamp();
    
    string strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, strKeyPass), ticket.signature);
    
    return ticket;
}

std::string CNFTBuyTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << sellTnxId;
    ss << price;
    ss << m_nTimestamp;
    return ss.str();
}

bool CNFTBuyTicket::IsValid(bool preReg, int depth) const
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
    CNFTBuyTicket existingBuyTicket;
    if (CNFTBuyTicket::FindTicketInDb(sellTnxId, existingBuyTicket))
    {
        // fixed: new buy ticket is not created due to the next condition
        //if (preReg)
        //{  // if pre reg - this is probably repeating call, so signatures can be the same
        //  throw std::runtime_error(strprintf(
        //    "Buy ticket [%s] already exists for this sell ticket [%s]", existingBuyTicket.m_txid, sellTnxId));
        //}
    
        // (ticket transaction replay attack protection)
        // though the similar transaction will be allowed if existing Buy ticket has expired
        if (existingBuyTicket.signature != signature ||
            !existingBuyTicket.IsBlock(m_nBlock) ||
            existingBuyTicket.m_txid != m_txid)
        {
            //check trade ticket
            if (CNFTTradeTicket::CheckTradeTicketExistByBuyTicket(existingBuyTicket.m_txid))
            {
              throw std::runtime_error(strprintf(
                "The sell ticket you are trying to buy [%s] is already sold", sellTnxId));
            }

            // find if it is the old ticket
            if (m_nBlock > 0 && existingBuyTicket.m_nBlock > m_nBlock) {
              throw std::runtime_error(strprintf(
                "This Buy ticket has been replaced with another ticket. txid - [%s].", existingBuyTicket.m_txid));
            }

            //check age
            if (existingBuyTicket.m_nBlock + masterNodeCtrl.MaxBuyTicketAge > chainHeight)
            {
              throw std::runtime_error(strprintf(
                "Buy ticket [%s] already exists and is not yet 1h old for this sell ticket [%s]"
                "[this ticket block = %u txid = %s; found ticket block = %u txid = %s]",
                existingBuyTicket.m_txid, sellTnxId, m_nBlock, m_txid, existingBuyTicket.m_nBlock, existingBuyTicket.m_txid));
            }
        }
    }
    
    auto sellTicket = dynamic_cast<const CNFTSellTicket*>(pastelTicket.get());
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
    if (sellTicket->activeBefore > 0 && height > sellTicket->activeBefore)
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

std::string CNFTBuyTicket::ToJSON() const noexcept
{
    const json jsonObj {
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

bool CNFTBuyTicket::FindTicketInDb(const std::string& key, CNFTBuyTicket& ticket)
{
    ticket.sellTnxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CNFTBuyTicket::CheckBuyTicketExistBySellTicket(const std::string& _sellTnxId)
{
    CNFTBuyTicket _ticket;
    _ticket.sellTnxId = _sellTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket);
}

std::vector<CNFTBuyTicket> CNFTBuyTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTBuyTicket>(pastelID);
}

// CNFTTradeTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTTradeTicket CNFTTradeTicket::Create(std::string _sellTnxId, std::string _buyTnxId, std::string _pastelID, const SecureString& strKeyPass)
{
    CNFTTradeTicket ticket(std::move(_pastelID));
    
    ticket.sellTnxId = std::move(_sellTnxId);
    ticket.buyTnxId = std::move(_buyTnxId);
    
    auto pSellTicket = CPastelTicketProcessor::GetTicket(ticket.sellTnxId, TicketID::Sell);
    auto sellTicket = dynamic_cast<CNFTSellTicket*>(pSellTicket.get());
    if (!sellTicket)
        throw std::runtime_error(tfm::format("The NFT Sell ticket [txid=%s] referred by this NFT Buy ticket is not in the blockchain. [txid=%s]",
                                             ticket.sellTnxId, ticket.buyTnxId));

    ticket.NFTTnxId = sellTicket->NFTTnxId;
    ticket.price = sellTicket->askedPrice;

    ticket.GenerateTimestamp();
    
    // In case it is nested it means that we have the NFTTnxId of the sell ticket
    // available within the trade tickets.
    // [0]: original registration ticket's txid
    // [1]: copy number for a given NFT
    std::vector<std::string> NFTRegTicket_TxId_Serial = CNFTTradeTicket::GetNFTRegTxIDAndSerialIfResoldNft(sellTicket->NFTTnxId);
    if(NFTRegTicket_TxId_Serial[0].compare("") == 0)
    {  
      auto NFTTicket = ticket.FindNFTRegTicket();
      if (!NFTTicket)
      {
        throw std::runtime_error("NFT Reg ticket not found");
      }

      //Original TxId
      ticket.SetNFTRegTicketTxid(NFTTicket->GetTxId());
      //Copy nr.
      ticket.SetCopySerialNr(std::to_string(sellTicket->copyNumber));
    }
    else
    {
      //This is the re-sold case
      ticket.SetNFTRegTicketTxid(NFTRegTicket_TxId_Serial[0]);
      ticket.SetCopySerialNr(NFTRegTicket_TxId_Serial[1]);
    }
    std::string strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, strKeyPass), ticket.signature);
    
    return ticket;
}

std::vector<std::string> CNFTTradeTicket::GetNFTRegTxIDAndSerialIfResoldNft(const std::string& _txid)
{

    std::vector<std::string> vRetVal = {"",""};

    try
    {
      //Possible conversion to trade ticket - if any
      auto pNestedTicket = CPastelTicketProcessor::GetTicket(_txid, TicketID::Trade);
      if(pNestedTicket != nullptr)
      {
        auto tradeTicket = dynamic_cast<const CNFTTradeTicket*>(pNestedTicket.get());
        if (tradeTicket)
        {
          vRetVal[0] = tradeTicket->GetNFTRegTicketTxid();
          vRetVal[1] = tradeTicket->GetCopySerialNr();
        }
      }
    }
    catch(const runtime_error& error)
    {
      //Intentionally not throw exception!
      LogPrintf("DebugPrint: NFT with this txid is not resold: %s", _txid);
    }
    
    return vRetVal;
    
}

std::string CNFTTradeTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << sellTnxId;
    ss << buyTnxId;
    ss << NFTTnxId;
    ss << m_nTimestamp;
    ss << nftRegTnxId;
    ss << nftCopySerialNr;
    return ss.str();
}

bool CNFTTradeTicket::IsValid(bool preReg, int depth) const
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
    CNFTTradeTicket _tradeTicket;
    if (CNFTTradeTicket::GetTradeTicketBySellTicket(sellTnxId, _tradeTicket))
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
    if (CNFTTradeTicket::GetTradeTicketByBuyTicket(buyTnxId, _tradeTicket))
    {
        //Compare signatures to skip if the same ticket
        if (signature != _tradeTicket.signature || m_txid != _tradeTicket.m_txid || !_tradeTicket.IsBlock(m_nBlock))
        {
          throw std::runtime_error(strprintf(
            "There is already exist trade ticket for the buy ticket with this txid [%s]", buyTnxId));
        }
    }

    // Verify asked price
    auto sellTicketReal = dynamic_cast<const CNFTSellTicket*>(sellTicket.get());
    if (!sellTicketReal) {
      throw std::runtime_error(strprintf(
        "The sell ticket with txid [%s] referred by this trade ticket is invalid", sellTnxId));
    }
    if (!sellTicketReal->askedPrice) {
      throw std::runtime_error(strprintf("The NFT Sell ticket with txid [%s] asked price should be not 0", sellTnxId));
    }

    // 2. Verify Trade ticket PastelID is the same as in Buy Ticket
    auto buyTicketReal = dynamic_cast<CNFTBuyTicket*>(buyTicket.get());
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

    trade_copy_validation(NFTTnxId, signature);

    return true;
}

CAmount CNFTTradeTicket::GetExtraOutputs(std::vector<CTxOut>& outputs) const
{
    auto pNFTSellTicket = CPastelTicketProcessor::GetTicket(sellTnxId, TicketID::Sell);
    if (!pNFTSellTicket) {
      throw std::runtime_error(strprintf("The NFT Sell ticket with this txid [%s] is not in the blockchain", sellTnxId));
    }

    auto NFTSellTicket = dynamic_cast<const CNFTSellTicket*>(pNFTSellTicket.get());
    if (!NFTSellTicket)
        throw std::runtime_error(strprintf("The NFT Sell ticket with this txid [%s] is not in the blockchain", sellTnxId));

    auto sellerPastelID = NFTSellTicket->pastelID;
    CPastelIDRegTicket sellerPastelIDticket;
    if (!CPastelIDRegTicket::FindTicketInDb(sellerPastelID, sellerPastelIDticket))
        throw std::runtime_error(strprintf(
                "The PastelID [%s] from sell ticket with this txid [%s] is not in the blockchain or is invalid",
                sellerPastelID, sellTnxId));

    if (!NFTSellTicket->askedPrice) {
      throw std::runtime_error(strprintf("The NFT Sell ticket with txid [%s] asked price should be not 0", sellTnxId));
    }

    CAmount nPriceAmount = NFTSellTicket->askedPrice * COIN;
    CAmount nRoyaltyAmount = 0;
    CAmount nGreenNFTAmount = 0;
    
    auto NFTTicket = FindNFTRegTicket();
    auto NFTRegTicket = dynamic_cast<CNFTRegTicket*>(NFTTicket.get());
    if (!NFTRegTicket)
    {
        throw std::runtime_error(strprintf(
                "Can't find NFT Registration ticket for this Trade ticket [txid=%s]",
                GetTxId()));
    }
    
    std::string strRoyaltyAddress;
    if (NFTRegTicket->nRoyalty > 0) {
      strRoyaltyAddress = NFTRegTicket->GetRoyaltyPayeeAddress();
      if (strRoyaltyAddress.empty()) {
        throw std::runtime_error(strprintf(
          "The Creator PastelID [%s] from NFT Registration ticket with this txid [%s] is not in the blockchain or is invalid",
          NFTRegTicket->pastelIDs[CNFTRegTicket::creatorsign], NFTRegTicket->GetTxId()));
      }
      nRoyaltyAmount = nPriceAmount * NFTRegTicket->nRoyalty;
    }
    
    if (!NFTRegTicket->strGreenAddress.empty()) {
      unsigned int chainHeight = 0; {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
      }
      nGreenNFTAmount = nPriceAmount * CNFTRegTicket::GreenPercent(chainHeight) / 100;
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
    
    if (!strRoyaltyAddress.empty() && !addOutput(strRoyaltyAddress, nRoyaltyAmount)) {
        throw std::runtime_error(
                strprintf("The PastelID [%s] from sell ticket with this txid [%s] has invalid address",
                          sellerPastelID, sellTnxId));
    }
    
    if (!NFTRegTicket->strGreenAddress.empty() && !addOutput(NFTRegTicket->strGreenAddress, nGreenNFTAmount)) {
        throw std::runtime_error(
                strprintf("The PastelID [%s] from sell ticket with this txid [%s] has invalid address",
                          sellerPastelID, sellTnxId));
    }
    
    return nPriceAmount + nRoyaltyAmount + nGreenNFTAmount;
}

std::string CNFTTradeTicket::ToJSON() const noexcept
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
                {"nft_txid", NFTTnxId},
                {"registration_txid",nftRegTnxId},
                {"copy_serial_nr", nftCopySerialNr},
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }}
    };
    return jsonObj.dump(4);
}

bool CNFTTradeTicket::FindTicketInDb(const std::string& key, CNFTTradeTicket& ticket)
{
    ticket.sellTnxId = key;
    ticket.buyTnxId = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket) ||
           masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket);
}

std::vector<CNFTTradeTicket> CNFTTradeTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTTradeTicket>(pastelID);
}

std::vector<CNFTTradeTicket> CNFTTradeTicket::FindAllTicketByNFTTnxID(const std::string& NFTTnxID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTTradeTicket>(NFTTnxID);
}

std::vector<CNFTTradeTicket> CNFTTradeTicket::FindAllTicketByRegTnxID(const std::string& nftRegTnxId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTTradeTicket>(nftRegTnxId);
}

std::map<std::string, std::string> CNFTTradeTicket::GetPastelIdAndTxIdWithTopHeightPerCopy(const std::vector<CNFTTradeTicket> & filteredTickets)
{
  //The list is already sorted by height (from beginning to end)

  //This will hold all the owner / copies serial number where serial number is the key 
  std::map<std::string, std::string> ownerPastelIDs_and_txids;

  //Copy number and winning index (within the vector)
  //std::map<std::string, int> copyOwner_Idxs;
  std::map<std::string, std::pair<unsigned int, int>> copyOwner_Idxs;
  int winning_idx = 0;

  for (const auto & element : filteredTickets) {

    const std::string& serial = element.GetCopySerialNr();
    if(copyOwner_Idxs.find(serial) != copyOwner_Idxs.end())
    {
      //We do have it in our copyOwner_Idxs
      if(element.GetBlock() >= copyOwner_Idxs[serial].first)
      {
        copyOwner_Idxs[serial] = std::make_pair(element.GetBlock(), winning_idx);
      }
    }
    else
    {
      copyOwner_Idxs.insert({ serial, std::make_pair(element.GetBlock(), winning_idx) });
    }
    winning_idx++;
  }

  // Now we do have the winning IDXs
  // we need to extract owners pastelId and TxnIds
  for (const auto& winners: copyOwner_Idxs)
  {
    ownerPastelIDs_and_txids.insert({ filteredTickets[winners.second.second].pastelID, filteredTickets[winners.second.second].GetTxId() });
  }

  return ownerPastelIDs_and_txids;
}

bool CNFTTradeTicket::CheckTradeTicketExistBySellTicket(const std::string& _sellTnxId)
{
    CNFTTradeTicket _ticket;
    _ticket.sellTnxId = _sellTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(_ticket);
}

bool CNFTTradeTicket::CheckTradeTicketExistByBuyTicket(const std::string& _buyTnxId)
{
    CNFTTradeTicket _ticket;
    _ticket.buyTnxId = _buyTnxId;
    return masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(_ticket);
}

bool CNFTTradeTicket::GetTradeTicketBySellTicket(const std::string& _sellTnxId, CNFTTradeTicket& ticket)
{
    ticket.sellTnxId = _sellTnxId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CNFTTradeTicket::GetTradeTicketByBuyTicket(const std::string& _buyTnxId, CNFTTradeTicket& ticket)
{
    ticket.buyTnxId = _buyTnxId;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

std::unique_ptr<CPastelTicket> CNFTTradeTicket::FindNFTRegTicket() const
{
    std::vector< std::unique_ptr<CPastelTicket> > chain;
    std::string errRet;
    if (!CPastelTicketProcessor::WalkBackTradingChain(NFTTnxId, chain, true, errRet))
    {
        throw std::runtime_error(errRet);
    }
    
    auto NFTRegTicket = dynamic_cast<CNFTRegTicket*>(chain.front().get());
    if (!NFTRegTicket)
    {
        throw std::runtime_error(
                strprintf("This is not an NFT Registration ticket [txid=%s]",
                          chain.front()->GetTxId()));
    }
    
    return std::move(chain.front());
}

void CNFTTradeTicket::SetNFTRegTicketTxid(const std::string& _NftRegTxid)
{
   nftRegTnxId =_NftRegTxid;
}

const std::string CNFTTradeTicket::GetNFTRegTicketTxid() const
{
  return nftRegTnxId;
}

void CNFTTradeTicket::SetCopySerialNr(const std::string& _nftCopySerialNr)
{
  nftCopySerialNr = std::move(_nftCopySerialNr);
}

const std::string& CNFTTradeTicket::GetCopySerialNr() const
{
  return nftCopySerialNr;
}

// CNFTRoyaltyTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTRoyaltyTicket CNFTRoyaltyTicket::Create(
    std::string _NFTTnxId, std::string _newPastelID,
    std::string _pastelID, const SecureString& strKeyPass) {
  CNFTRoyaltyTicket ticket(std::move(_pastelID), std::move(_newPastelID));

  ticket.NFTTnxId = std::move(_NFTTnxId);

  ticket.GenerateTimestamp();

  std::string strTicket = ticket.ToStr();
  string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, strKeyPass), ticket.signature);

  return ticket;
}

std::string CNFTRoyaltyTicket::ToStr() const noexcept {
  std::stringstream ss;
  ss << pastelID;
  ss << newPastelID;
  ss << NFTTnxId;
  ss << m_nTimestamp;
  return ss.str();
}

bool CNFTRoyaltyTicket::IsValid(bool preReg, int depth) const {
  unsigned int chainHeight = 0; {
    LOCK(cs_main);
    chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
  }

  if (newPastelID.empty()) {
    throw std::runtime_error("The Change Royalty ticket new_pastelID is empty");
  }

  if (pastelID == newPastelID) {
    throw std::runtime_error("The Change Royalty ticket new_pastelID is equal to current pastelID");
  }

  // 0. Common validations
  std::unique_ptr<CPastelTicket> pastelTicket;
  if (!common_validation(*this, preReg, NFTTnxId, pastelTicket,
      [](const TicketID tid) { return (tid != TicketID::NFT); },
      "Royalty", "NFT", depth, TicketPrice(chainHeight))) {
    throw std::runtime_error(strprintf("The Change Royalty ticket with NFT txid [%s] is not validated", NFTTnxId));
  }

  auto NFTTicket = dynamic_cast<const CNFTRegTicket*>(pastelTicket.get());
  if (!NFTTicket) {
    throw std::runtime_error(strprintf(
      "The NFT Reg ticket with txid [%s] is not in the blockchain or is invalid", NFTTnxId));
  }

  if (NFTTicket->nRoyalty == 0) {
    throw std::runtime_error(strprintf(
      "The NFT Reg ticket with txid [%s] has no royalty", NFTTnxId));
  }

  // Check the Royalty change ticket for that NFT is already in the database
  // (ticket transaction replay attack protection)
  CNFTRoyaltyTicket _ticket;
  if (FindTicketInDb(KeyOne(), _ticket) &&
     (preReg ||  // if pre reg - this is probably repeating call, so signatures can be the same
      _ticket.signature != signature || !_ticket.IsBlock(m_nBlock) || _ticket.m_txid != m_txid)) {
    throw std::runtime_error(strprintf(
      "The Change Royalty ticket is already registered in blockchain [pastelID = %s; new_pastelID = %s]"
      "[this ticket block = %u txid = %s; found ticket block = %u txid = %s] with NFT txid [%s]",
      pastelID, newPastelID, m_nBlock, m_txid, _ticket.GetBlock(), _ticket.m_txid, NFTTnxId));
  }

  CPastelIDRegTicket newPastelIDticket;
  if (!CPastelIDRegTicket::FindTicketInDb(newPastelID, newPastelIDticket)) {
    throw std::runtime_error(strprintf(
      "The new_pastelID [%s] for Change Royalty ticket with NFT txid [%s] is not in the blockchain or is invalid",
      newPastelID, NFTTnxId));
  }

  int index{0};
  int foundIndex{-1};
  unsigned int highBlock{0};
  const auto tickets = CNFTRoyaltyTicket::FindAllTicketByNFTTnxId(NFTTnxId);
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
        "The PastelID [%s] is not matching the PastelID [%s] in the Change Royalty ticket with NFT txid [%s]",
        pastelID, tickets.at(foundIndex).newPastelID, NFTTnxId));
    }
  } else {
    // 1. check creator PastelID in NFTReg ticket matches PastelID from this ticket
    const std::string& creatorPastelID = NFTTicket->pastelIDs[CNFTRegTicket::creatorsign];
    if (creatorPastelID != pastelID) {
      throw std::runtime_error(strprintf(
        "The PastelID [%s] is not matching the Creator's PastelID [%s] in the NFT Reg ticket with this txid [%s]",
        pastelID, creatorPastelID, NFTTnxId));
    }
  }

  return true;
}

std::string CNFTRoyaltyTicket::ToJSON() const noexcept {
  const json jsonObj {
    {"txid", m_txid},
    {"height", m_nBlock},
    {"ticket", {
      {"type", GetTicketName()},
      {"version", GetStoredVersion()},
      {"pastelID", pastelID},
      {"new_pastelID", newPastelID},
      {"nft_txid", NFTTnxId},
      {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
    }}
  };
  return jsonObj.dump(4);
}

bool CNFTRoyaltyTicket::FindTicketInDb(const std::string& key, CNFTRoyaltyTicket& ticket) {
  ticket.signature = {key.cbegin(), key.cend()};
  return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

std::vector<CNFTRoyaltyTicket> CNFTRoyaltyTicket::FindAllTicketByPastelID(const std::string& pastelID) {
  return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTRoyaltyTicket>(pastelID);
}

std::vector<CNFTRoyaltyTicket> CNFTRoyaltyTicket::FindAllTicketByNFTTnxId(const std::string& NFTTnxId) {
  return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTRoyaltyTicket>(NFTTnxId);
}

// CTakeDownTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CTakeDownTicket::FindTicketInDb(const std::string& key, CTakeDownTicket& ticket)
{
    return false;
}


// CChangeUsernameTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::string CChangeUsernameTicket::ToJSON() const noexcept
{
    json jsonObj;
    jsonObj = {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", {
            {"type", GetTicketName()}, 
            {"pastelID", pastelID},
            {"username", username}, 
            {"fee", fee}, 
            {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}}}};

    return jsonObj.dump(4);
}

std::string CChangeUsernameTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << username;
    ss << fee;
    ss << m_nTimestamp;
    return ss.str();
}

bool CChangeUsernameTicket::IsValid(bool preReg, int depth) const
{
    unsigned int chainHeight = 0;
    {
        LOCK(cs_main);
        chainHeight = static_cast<unsigned int>(chainActive.Height()) + 1;
    }

    CChangeUsernameTicket existingTicket;
    bool ticketExists = FindTicketInDb(username, existingTicket);
    // A. Something to check ONLY before ticket made into transaction
    if (preReg) 
    {
        // A1. Check if the username is already registered on the blockchain.
        if (ticketExists && masterNodeCtrl.masternodeTickets.getValueBySecondaryKey(existingTicket) == username) {

            throw std::runtime_error(strprintf("This Username is already registered in blockchain [Username = %s]", username));
        }
        // A2. Check if address has coins to pay for Username Change Ticket
        const auto fullTicketPrice = TicketPrice(chainHeight);

        if (pwalletMain->GetBalance() < fullTicketPrice * COIN) {
            throw std::runtime_error(strprintf("Not enough coins to cover price [%" PRId64 "]", fullTicketPrice));
        }
    }

    // Check if username is a bad username. For now check if it is empty only.
    std::string badUsernameError;
    if (isUsernameBad(username, badUsernameError)) {
        throw std::runtime_error(badUsernameError.c_str());
    }

    // B Verify signature
    // We will check that it is the correct PastelID
    std::string strThisTicket = ToStr();
    if (!CPastelID::Verify(strThisTicket, vector_to_string(signature), pastelID))
    {
        throw std::runtime_error(strprintf("%s ticket's signature is invalid. PastelID - [%s]", GetTicketDescription(TicketID::Username), pastelID));
    }
    // C (ticket transaction replay attack protection)
    if ((ticketExists) &&
        (!existingTicket.IsBlock(m_nBlock) || existingTicket.m_txid != m_txid) &&
        masterNodeCtrl.masternodeTickets.getValueBySecondaryKey(existingTicket) == username)
    {
        throw std::runtime_error(strprintf("This Username Change Request is already registered in blockchain [Username = %s]"
                           "[this ticket block = %u txid = %s; found ticket block  = %u txid = %s]",
                           username, existingTicket.GetBlock(), existingTicket.m_txid, m_nBlock, m_txid));
    }

    // D. Check if this PastelID hasn't changed Username in last 24 hours.
    CChangeUsernameTicket _ticket;
    _ticket.pastelID = pastelID;
    bool foundTicketBySecondaryKey = masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(_ticket);
    if (foundTicketBySecondaryKey) {
        const unsigned int height = (preReg || IsBlock(0)) ? chainHeight : m_nBlock;
        if (height <= _ticket.m_nBlock + 24 * 24) { // For testing purpose, the value 24 * 24 can be lower to decrease the waiting time
            // D.2 IF PastelID has changed Username in last 24 hours (~24*24 blocks), do not allow them to change
            throw std::runtime_error(strprintf("%s ticket is invalid. Already changed in last 24 hours. PastelID - [%s]", GetTicketDescription(TicketID::Username), pastelID));
        }
    }

    // E. Check if ticket fee is valid
    if (!foundTicketBySecondaryKey) {
        if (fee != masterNodeCtrl.MasternodeUsernameFirstChangeFee) {
            throw std::runtime_error(strprintf("%s ticket's fee is invalid. PastelID - [%s], invalid fee - [%" PRId64 "], expected fee - [%" PRId64 "]",
                                               GetTicketDescription(TicketID::Username), pastelID, fee, masterNodeCtrl.MasternodeUsernameFirstChangeFee));
        }
    } else {
        if (fee != masterNodeCtrl.MasternodeUsernameChangeAgainFee) {
            throw std::runtime_error(strprintf("%s ticket's fee is invalid. PastelID - [%s], invalid fee - [%" PRId64 "], expected fee - [%" PRId64 "]",
                                               GetTicketDescription(TicketID::Username), pastelID, fee, masterNodeCtrl.MasternodeUsernameChangeAgainFee));
        }
    }
    
    return true;
}

CChangeUsernameTicket CChangeUsernameTicket::Create(std::string _pastelID, std::string _username, const SecureString& strKeyPass)
{
    CChangeUsernameTicket ticket(std::move(_pastelID), std::move(_username));

    // Check if PastelID already have a username on the blockchain. 
    if (!masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(ticket)) {
        // IF PastelID has no Username yet, the fee is 100 PSL
        ticket.fee = masterNodeCtrl.MasternodeUsernameFirstChangeFee;
    } else {
        // IF PastelID changed Username before, fee should be 5000
        ticket.fee = masterNodeCtrl.MasternodeUsernameChangeAgainFee;
    }

    ticket.GenerateTimestamp();

    std::string strTicket = ticket.ToStr();
    ticket.signature = string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, strKeyPass));

    return ticket;
}

bool CChangeUsernameTicket::FindTicketInDb(const std::string& key, CChangeUsernameTicket& ticket)
{
    ticket.username = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CChangeUsernameTicket::isUsernameBad(const std::string& username, std::string& error)
{
    // Check if has only <4, or has more than 12 characters
    if ((username.size() < 4) || (username.size() > 12)) {
        error = "Invalid size of username, the size should have at least 4 characters, and at most 12 characters";
        return true;
    }

    // Check if doesn't start with letters.
    if ( !isalphaex(username.front()) ) {
        error = "Invalid username, should start with a letter A-Z or a-z only";
        return true;
    }
    // Check if contains characters that is different than upper and lowercase Latin characters and numbers
    if (!std::all_of(username.begin(), username.end(), [&](unsigned char c) 
        { 
          return (isalphaex(c) || isdigitex(c)); 
        }
      )) {
        error = "Invalid username, should contains letters A-Z a-z, or digits 0-9 only";
        return true;
    }
    // Check if contains bad words (swear, racist,...)
    std::string lowercaseUsername = username;
    lowercase(lowercaseUsername);
    for (const auto& elem : UsernameBadWords::Singleton().wordSet) {
      if (lowercaseUsername.find(elem) != std::string::npos) {
          error = "Invalid username, should NOT contains swear, racist... words";
          return true;
      }
    }

    return false;
}

// NFT Auction Tickets ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CNFTSellTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CNFTAuctionTicket CNFTAuctionTicket::Create(std::string _NFTTnxId, int _askedPrice, int _validAfter, int _validBefore, int _copy_number, int _duration, int _minimumReputationScore, std::string _pastelID, const SecureString& strKeyPass)
{
    CNFTAuctionTicket ticket(std::move(_pastelID));
    
    ticket.NFTTnxId = std::move(_NFTTnxId);
    ticket.askedPrice = _askedPrice;
    ticket.activeBefore = _validBefore;
    ticket.activeAfter = _validAfter;
    
    ticket.GenerateTimestamp();
    
    //NOTE: Sell ticket for Trade ticket will always has copyNumber = 1
    ticket.copyNumber = _copy_number > 0 ? 
        _copy_number : static_cast<decltype(ticket.copyNumber)>(CNFTAuctionTicket::FindAllTicketByNFTTnxID(ticket.NFTTnxId).size()) + 1;
    
    ticket.duration = _duration;
    ticket.minimumReputationScore = _minimumReputationScore;
    
    ticket.key = ticket.NFTTnxId + ":" + to_string(ticket.copyNumber);
    
    std::string strTicket = ticket.ToStr();
    string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, strKeyPass), ticket.signature);
    
    return ticket;
}

std::string CNFTAuctionTicket::ToStr() const noexcept
{
    std::stringstream ss;
    ss << pastelID;
    ss << NFTTnxId;
    ss << askedPrice;
    ss << copyNumber;
    ss << duration;
    ss << minimumReputationScore;
    ss << activeBefore;
    ss << activeAfter;
    ss << m_nTimestamp;
    return ss.str();
}

std::string CNFTAuctionTicket::ToJSON() const noexcept
{
    const json jsonObj {
            {"txid", m_txid},
            {"height", m_nBlock},
            {"ticket", {
                {"type", GetTicketName()},
                {"version", GetStoredVersion()},
                {"pastelID", pastelID},
                {"NFT_txid", NFTTnxId},
                {"copy_number", copyNumber},
                {"asked_price", askedPrice},
                {"valid_after", activeAfter},
                {"valid_before", activeBefore},
                {"duration", duration},
                {"minimumReputationScore", minimumReputationScore},
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }}
    };
    return jsonObj.dump(4);
}

bool CNFTAuctionTicket::FindTicketInDb(const std::string& key, CNFTAuctionTicket& ticket)
{
    ticket.key = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

std::vector<CNFTAuctionTicket> CNFTAuctionTicket::FindAllTicketByPastelID(const std::string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTAuctionTicket>(pastelID);
}

std::vector<CNFTAuctionTicket> CNFTAuctionTicket::FindAllTicketByNFTTnxID(const std::string& NFTTnxId)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTAuctionTicket>(NFTTnxId);
}