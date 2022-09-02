// Copyright (c) 2018-2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>
#include <tuple>

#include <str_utils.h>
#include <init.h>
#include <key_io.h>
#include <pastelid/common.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/nft-royalty.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/nft-collection-reg.h>
#include <mnode/mnode-controller.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

//  ---- NFT Registration Ticket ---- ////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Current nft_ticket
{
    "nft_ticket_version": integer     // 1 or 2
    "author": bytes,                  // PastelID of the NFT creator
    "blocknum": integer,              // block number when the ticket was created - this is to map the ticket to the MNs that should process it
    "block_hash": bytes               // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
    "copies": integer,                // number of copies
    "royalty": float,                 // how much creator should get on all future resales (optional in v2, if not defined - used the value from NFT collection)
    "green": boolean,                 // whether Green NFT payment should be made (optional in v2, if not defined - used the value from NFT collection)
    "nft_collection_txid": bytes,     // transaction id of the NFT collection that NFT belongs to (supported in v2 only, optional - can have empty value)
    "app_ticket": ...
}
*/
CNFTRegTicket CNFTRegTicket::Create(
    string &&nft_ticket,
    const string& signatures,
    string &&sPastelID,
    SecureString&& strKeyPass,
    string &&label,
    const CAmount storageFee)
{
    CNFTRegTicket ticket(move(nft_ticket));
    ticket.parse_nft_ticket();
    ticket.set_collection_properties();

    // parse and set principal's and MN2/3's signatures
    ticket.set_signatures(signatures);
    ticket.m_label = move(label);
    ticket.m_storageFee = storageFee;
    ticket.GenerateKeyOne();
    ticket.GenerateTimestamp();

    ticket.m_vPastelID[SIGN_MAIN] = move(sPastelID);
    // sign the ticket hash using principal PastelID, ed448 algorithm
    string_to_vector(CPastelID::Sign(ticket.m_sNFTTicket, ticket.m_vPastelID[SIGN_MAIN], move(strKeyPass)), ticket.m_vTicketSignature[SIGN_MAIN]);
    return ticket;
}

// tuple: 
//    - nft ticket property enum
//    - property required (true) or optional (false)
using NFTTicketProp = tuple<NFT_TKT_PROP, bool>;
// nft_ticket info
using NFTTicketInfo = struct
{
    // NFT ticket version
    uint32_t nVersion;
    // map of supported properties (property name)->(property tuple)
    unordered_map<string, NFTTicketProp> propMap;
};

static const std::array<NFTTicketInfo, 2> NFT_TICKET_INFO =
{{
        { 1, // version
            {
                { "nft_ticket_version",  make_tuple(NFT_TKT_PROP::version, true) },
                { "author",              make_tuple(NFT_TKT_PROP::creator, true) },
                { "blocknum",            make_tuple(NFT_TKT_PROP::blocknum, true) },
                { "block_hash",          make_tuple(NFT_TKT_PROP::block_hash, true) },
                { "copies",              make_tuple(NFT_TKT_PROP::copies ,true) },
                { "royalty",             make_tuple(NFT_TKT_PROP::royalty, true) },
                { "green",               make_tuple(NFT_TKT_PROP::green, true) },
                { "app_ticket",          make_tuple(NFT_TKT_PROP::app_ticket, true) }
            }
        },
        { 2,
            {
                { "nft_ticket_version",  make_tuple(NFT_TKT_PROP::version, true) },
                { "author",              make_tuple(NFT_TKT_PROP::creator, true) },
                { "blocknum",            make_tuple(NFT_TKT_PROP::blocknum, true) },
                { "block_hash",          make_tuple(NFT_TKT_PROP::block_hash, true) },
                { "nft_collection_txid", make_tuple(NFT_TKT_PROP::nft_collection_txid, false) },
                { "copies",              make_tuple(NFT_TKT_PROP::copies, false) },
                { "royalty",             make_tuple(NFT_TKT_PROP::royalty, false) },
                { "green",               make_tuple(NFT_TKT_PROP::green, false) },
                { "app_ticket",          make_tuple(NFT_TKT_PROP::app_ticket, true) }
            }
        }
}};

/**
 * Parses base64-encoded nft_ticket in json format.
 * Throws runtime_error exception in case nft_ticket has invalid format
 */
void CNFTRegTicket::parse_nft_ticket()
{
    // parse NFT Ticket (nft_ticket version can be different from serialized ticket version)
    json jsonTicketObj;
    try
    {
        const auto jsonTicketObj = json::parse(ed_crypto::Base64_Decode(m_sNFTTicket));
        // check nft_ticket version
        const int nTicketVersion = jsonTicketObj.at("nft_ticket_version");
        if (nTicketVersion < 1 || nTicketVersion > static_cast<int>(NFT_TICKET_INFO.size()))
            throw runtime_error(strprintf("'%s' ticket json version '%d' cannot be greater than '%zu'", 
                GetTicketDescription(), m_nNFTTicketVersion, NFT_TICKET_INFO.size()));
        m_nNFTTicketVersion = static_cast<uint16_t>(nTicketVersion);
        const auto& tktInfo = NFT_TICKET_INFO[m_nNFTTicketVersion - 1];
        // validate all nft_ticket properties and get values
        const auto& propMap = tktInfo.propMap;
        for (const auto& [sPropName, value] : jsonTicketObj.items())
        {
            const auto itProp = propMap.find(sPropName);
            if (itProp == propMap.cend())
                throw runtime_error(strprintf(
                    "Found unsupported property '%s' in '%s' ticket json v%hu",
                    sPropName, GetTicketDescription(), m_nNFTTicketVersion));
            const auto & prop = get<0>(itProp->second);
            m_props.insert(prop);
            // process properties
            switch (prop)
            {
                case NFT_TKT_PROP::creator:
                    value.get_to(m_sCreatorPastelID);
                    break;

                case NFT_TKT_PROP::blocknum:
                    value.get_to(m_nCreatorHeight);
                    break;

                case NFT_TKT_PROP::block_hash:
                    value.get_to(m_sTopBlockHash);
                    break;

                case NFT_TKT_PROP::copies:
                    value.get_to(m_nTotalCopies);
                    break;

                case NFT_TKT_PROP::nft_collection_txid:
                    value.get_to(m_sNFTCollectionTxid);
                    break;

                case NFT_TKT_PROP::royalty:
                    value.get_to(m_nRoyalty);
                    break;

                case NFT_TKT_PROP::green:
                {
                    bool bHasGreen = false;
                    value.get_to(bHasGreen);
                    if (bHasGreen)
                        m_sGreenAddress = GreenAddress(GetActiveChainHeight());
                } break;
            } // switch
        } // for 

        // check for missing required properties
        string sMissingProps;
        for (const auto& [sPropName, propInfo] : propMap)
        {
            const auto& [prop, bRequired] = propInfo;
            if (bRequired && !m_props.count(prop))
                str_append_field(sMissingProps, sPropName.c_str(), ",");
        }
        if (!sMissingProps.empty())
            throw runtime_error(strprintf(
                "Missing required properties '%s' in '%s' ticket json v%hu",
                sMissingProps, GetTicketDescription(), m_nNFTTicketVersion));
    } catch (const json::exception& ex)
    {
        throw runtime_error(strprintf(
            "Failed to parse '%s' ticket json. %s", 
            GetTicketDescription(), SAFE_SZ(ex.what())));
    }
}

/**
 * Get ticket by txid.
 * 
 * \param txid - ticket transaction id 
 * \return
 */
unique_ptr<CPastelTicket> CNFTRegTicket::GetTicket(const uint256 &txid) const
{
    return masterNodeCtrl.masternodeTickets.GetTicket(txid);
}

/**
 * Retrieve referred NFT collection.
 * 
 * \param error - return error if collection not found
 * \param bInvalidTxId - set to true if NFT collection txid is invalid
 * \return nullopt if NFT collection txid is invalid, false - if collection ticket not found
 */
unique_ptr<CPastelTicket> CNFTRegTicket::RetrieveCollectionTicket(string& error, bool &bInvalidTxId) const noexcept
{
    unique_ptr<CPastelTicket> collectionTicket;
    bInvalidTxId = false;
    do
    {
        uint256 collection_txid;
        // extract and validate NFT collection txid
        if (!parse_uint256(error, collection_txid, m_sNFTCollectionTxid, "NFT collection txid"))
        {
            bInvalidTxId = true;
            break;
        }

        // get the NFT collection registration ticket pointed by txid
        try
        {
            collectionTicket = GetTicket(collection_txid);
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
        }
    } while (false);
    return collectionTicket;
}

/**
 * Set missing properties from NFT collection.
 * Does not throw exception if collection not found - it will be raised in IsValid.
 */
void CNFTRegTicket::set_collection_properties() noexcept
{
    if (m_sNFTCollectionTxid.empty())
        return;
    string error;
    bool bInvalidTxId = false;
    const auto collectionTicket = RetrieveCollectionTicket(error, bInvalidTxId);
    const CNFTCollectionRegTicket *pNFTCollTicket = dynamic_cast<const CNFTCollectionRegTicket*>(collectionTicket.get());
    if (!pNFTCollTicket)
        return;
    // set royalty fee
    if (!m_props.count(NFT_TKT_PROP::royalty))
        m_nRoyalty = pNFTCollTicket->getRoyalty();
    // set green address
    if (!m_props.count(NFT_TKT_PROP::green))
        m_sGreenAddress = pNFTCollTicket->getGreenAddress();
    // set total copies
    if (!m_props.count(NFT_TKT_PROP::copies))
        m_nTotalCopies = pNFTCollTicket->getNFTCopyCount();
}

/**
 * Validate NFT Collection reference.
 * 
 * \param bPreReg - if true: called from ticket pre-registration
 * \return ticket validation result structure
 */
ticket_validation_t CNFTRegTicket::IsValidCollection(const bool bPreReg) const noexcept
{
    ticket_validation_t tv;
    // skip validation if NFT collection txid is not defined (v1 of the nft ticket)
    if (m_sNFTCollectionTxid.empty())
    {
        tv.setValid();
        return tv;
    }
    bool bRet = false;
    do
    {
        // retrieve NFT collection registration ticket
        string error;
        bool bInvalidTxId = false;
        const auto collectionTicket = RetrieveCollectionTicket(error, bInvalidTxId);
        if (bInvalidTxId)
        {
            tv.errorMsg = move(error);
            break;
        }
        // check that we've got NFT collection ticket
        if (!collectionTicket)
        {
            tv.errorMsg = strprintf(
                "The NFT collection registration ticket [txid=%s] referred by this NFT Reg ticket [txid=%s] is not in the blockchain. %s",
                m_sNFTCollectionTxid, GetTxId(), error);
            tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
            break;
        }

        const CNFTCollectionRegTicket *pNFTCollTicket = dynamic_cast<const CNFTCollectionRegTicket*>(collectionTicket.get());
        // check that ticket has valid type
        if (collectionTicket->ID() != TicketID::NFTCollectionReg || !pNFTCollTicket)
        {
            tv.errorMsg = strprintf(
                "The NFT collection registration ticket [txid=%s] referred by this NFT Reg ticket [txid=%s] has invalid type '%s'",
                m_sNFTCollectionTxid, GetTxId(), ::GetTicketDescription(collectionTicket->ID()));
            break;
        }

        // check that ticket has a valid height
        if (!bPreReg && (collectionTicket->GetBlock() > m_nBlock))
        {
            tv.errorMsg = strprintf(
                "The NFT Collection '%s' registration ticket [txid=%s] referred by this NFT Reg ticket [txid=%s] has invalid height (%u > %u)",
                pNFTCollTicket->getName(), m_sNFTCollectionTxid, GetTxId(), collectionTicket->GetBlock(), m_nBlock);
            break;
        }

        const auto chainHeight = GetActiveChainHeight();

        // check that NFT reg ticket height is less than closing height for the NFT Collection
        if (bPreReg && (chainHeight > pNFTCollTicket->getClosingHeight()))
        {
            // a "closing" block height after which no new NFTs would be allowed to be added to the collection
            tv.errorMsg = strprintf(
                "No new NFTs are allowed to be added to the NFT Collection '%s' after the closing height %u",
                pNFTCollTicket->getName(), pNFTCollTicket->getClosingHeight());
            break;
        }

        uint32_t nNFTCollectionItemCount = 0;
        // count all registered NFTs in a collection upto current height
        // don't count current reg ticket
        masterNodeCtrl.masternodeTickets.ProcessTicketsByMVKey<CNFTRegTicket>(m_sNFTCollectionTxid,
            [&](const CNFTRegTicket& nftRegTicket) -> bool
            {
                if ((nftRegTicket.GetBlock() <= chainHeight))
                    ++nNFTCollectionItemCount;
                return true;
            });

        // check if we have more than allowed number of NFTs in the collection
        if (nNFTCollectionItemCount + (bPreReg ? 1 : 0) > pNFTCollTicket->getMaxNFTCount())
        {
            tv.errorMsg = strprintf(
                "Max number of NFTs (%u) allowed in the collection '%s' has been exceeded",
                pNFTCollTicket->getMaxNFTCount(), pNFTCollTicket->getName());
            break;
        }

        // check if the NFT creator is in the permitted user list
        if (!pNFTCollTicket->IsUserPermitted(getCreatorPastelId()))
        {
            tv.errorMsg = strprintf(
                "User with Pastel ID '%s' is not allowed to add NFTs to the collection '%s' [txid=%s]",
                getCreatorPastelId(), pNFTCollTicket->getName(), m_sNFTCollectionTxid);
            break;
        }
        tv.setValid();
    } while (false);
    return tv;
}

/**
 * Validate NFT ticket.
 * 
 * \param bPreReg - if true: called from ticket pre-registration
 * \param nCallDepth - function call depth
 * \return true if the ticket is valid
 */
ticket_validation_t CNFTRegTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const auto chainHeight = GetActiveChainHeight();
    ticket_validation_t tv;
    do
    {
        if (bPreReg)
        {
            // A. Something to check ONLY before the ticket made into transaction.
            // Only done after Create

            // A.1 check that the NFT ticket is already in the blockchain
            if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this))
            {
                tv.errorMsg = strprintf(
                    "This NFT is already registered in blockchain [key=%s; label=%s]",
                    m_keyOne, m_label);
                break;
            }

            // A.2 validate that address has coins to pay for registration - 10PSL
            const auto fullTicketPrice = TicketPricePSL(chainHeight); //10% of storage fee is paid by the 'creator' and this ticket is created by MN
            if (pwalletMain->GetBalance() < fullTicketPrice * COIN)
            {
                tv.errorMsg = strprintf(
                    "Not enough coins to cover price [%" PRId64 " PSL]", 
                    fullTicketPrice);
                break;
            }
        }
        // (ticket transaction replay attack protection)
        CNFTRegTicket ticket;
        if (FindTicketInDb(m_keyOne, ticket) && (!ticket.IsBlock(m_nBlock) || !ticket.IsTxId(m_txid)))
        {
            tv.errorMsg = strprintf(
                "This NFT is already registered in blockchain [key=%s; label=%s] [%sfound ticket block=%u, txid=%s]",
                m_keyOne, m_label, 
                bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", m_nBlock, m_txid),
                ticket.GetBlock(), ticket.GetTxId());
            break;
        }

        // validate referenced NFT collection (for v2 only)
        ticket_validation_t collTv = IsValidCollection(bPreReg);
        if (collTv.IsNotValid())
        {
            tv = move(collTv);
            break;
        }

        // B. Something to validate always
        const ticket_validation_t sigTv = validate_signatures(nCallDepth, m_nCreatorHeight, m_sNFTTicket);
        if (sigTv.IsNotValid())
        {
            tv.state = sigTv.state;
            tv.errorMsg = strprintf(
                "%s ticket signature validation failed. %s",
                GetTicketDescription(), sigTv.errorMsg);
            break;
        }

        // C. Check that royalty and green fees are valid
        if (!ValidateFees(tv.errorMsg))
            break;

        tv.setValid();
    } while (false);
    return tv;
}

void CNFTRegTicket::Clear() noexcept
{
    CTicketSignedWithExtraFees::Clear();
    m_nNFTTicketVersion = 0;
    m_sNFTTicket.clear();
    m_nTotalCopies = 0;
}

/**
 * Get json string representation of the ticket.
 * 
 * \return json string
 */
string CNFTRegTicket::ToJSON() const noexcept
{
    const json jsonObj
    {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", 
            {
                {"type", GetTicketName()},
                {"nft_ticket", m_sNFTTicket},
                {"version", GetStoredVersion()},
                get_signatures_json(),
                {"key", m_keyOne},
                {"label", m_label},
                {"creator_height", m_nCreatorHeight},
                {"total_copies", m_nTotalCopies},
                {"royalty", m_nRoyalty},
                {"royalty_address", GetRoyaltyPayeeAddress()},
                {"green", !m_sGreenAddress.empty()},
                {"storage_fee", m_storageFee}
            }
        }
    };

    return jsonObj.dump(4);
}

/**
 * Find ticket in DB by primary key.
 * 
 * \param key - lookup key, used in a search by primary key
 * \param ticket - returns ticket if found
 * \return true if ticket was found
 */
bool CNFTRegTicket::FindTicketInDb(const string& key, CNFTRegTicket& ticket)
{
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

/**
 * Check if ticket exists in a DB by primary key.
 * 
 * \param key - lookup key, used in a search by primary key
 * \return true if ticket exists in a DB
 */
bool CNFTRegTicket::CheckIfTicketInDb(const string& key)
{
    CNFTRegTicket ticket;
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

NFTRegTickets_t CNFTRegTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTRegTicket>(pastelID);
}
