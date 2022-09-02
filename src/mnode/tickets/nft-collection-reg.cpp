// Copyright (c) 2022 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <str_utils.h>
#include <init.h>
#include <key_io.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/mnode-controller.h>
#include <mnode/ticket-processor.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/nft-collection-reg.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

//  ---- NFT Collection Registration Ticket ---- ////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Create NFT Collection ticket.
 * 
 * Current nft_collection_ticket !!!
 * {
 *    "nft_collection_ticket_version": integer     // 1
 *    "creator": bytes,           // PastelID of the NFT collection creator
 *    "permitted_users": [  // list of Pastel IDs that are permitted to register an NFT as part of the collection
 *        "xxxx",
 *        "xxxx",
 *        ...
 *    ]
 *   "blocknum": integer,         // block number when the ticket was created - this is to map the ticket to the MNs that should process it
 *   "block_hash": bytes          // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
 *   "closing_height": integer,   // a "closing" block height after which no new NFTs would be allowed to be added to the collection
 *   "nft_max_count": integer,    // max number of NFTs allowed in this collection
 *   "nft_copy_count": integer,   // default number of copies for all NFTs in a collection
 *   "royalty": float,            // royalty fee, how much creators should get on all future resales (common for all NFTs in a collection)
 *   "green": boolean,            // is there Green NFT payment or not (common for all NFTs in a collection)
 *   "app_ticket": bytes          // cNode parses app_ticket only for search
 *       as base64: { ... }
 * }
 * 
 * \param nft_collection_ticket - NFT collection ticket json base64-encoded
 * \param signatures - signatures json
 * \param sPastelID - NFT collection creator's Pastel ID
 * \param strKeyPass - passphrase for creator's secure container
 * \param label - search key #2
 * \param storageFee - ticket storage fee
 * \return - NFT collection ticket object
 */
CNFTCollectionRegTicket CNFTCollectionRegTicket::Create(
    string &&nft_collection_ticket,
    const string& signatures,
    string &&sPastelID,
    SecureString&& strKeyPass,
    string &&label,
    const CAmount storageFee)
{
    CNFTCollectionRegTicket ticket(move(nft_collection_ticket));
    ticket.parse_nft_collection_ticket();

    // parse and set principal's and MN2/3's signatures
    ticket.set_signatures(signatures);
    ticket.m_label = move(label);
    ticket.m_storageFee = storageFee;
    ticket.GenerateKeyOne();
    ticket.GenerateTimestamp();

    ticket.m_vPastelID[SIGN_MAIN] = move(sPastelID);
    // sign the ticket hash using principal PastelID, ed448 algorithm
    string_to_vector(CPastelID::Sign(ticket.m_sNFTCollectionTicket, ticket.m_vPastelID[SIGN_MAIN], move(strKeyPass)), ticket.m_vTicketSignature[SIGN_MAIN]);
    return ticket;
}

// tuple: 
//    - nft collection ticket property enum
//    - property required (true) or optional (false)
using NFTCollTicketProp = tuple<NFTCOLL_TKT_PROP, bool>;
// nft_collection_ticket info
using NFTCollTicketInfo = struct
{
    // NFT Collection ticket version
    uint32_t nVersion;
    // map of supported properties (property name)->(property tuple)
    unordered_map<string, NFTCollTicketProp> propMap;
};

static const std::array<NFTCollTicketInfo, 1> NFTCOLL_TICKET_INFO =
{{
    { 1, //version
        {
            { "nft_collection_ticket_version",  make_tuple(NFTCOLL_TKT_PROP::version, true) },
            { "nft_collection_name",            make_tuple(NFTCOLL_TKT_PROP::name, true) },
            { "creator",                        make_tuple(NFTCOLL_TKT_PROP::creator, true) },
            { "permitted_users",                make_tuple(NFTCOLL_TKT_PROP::permitted_users, true) },
            { "blocknum",                       make_tuple(NFTCOLL_TKT_PROP::blocknum, true) },
            { "block_hash",                     make_tuple(NFTCOLL_TKT_PROP::block_hash, true) },
            { "closing_height",                 make_tuple(NFTCOLL_TKT_PROP::closing_height, true) },
            { "nft_max_count",                  make_tuple(NFTCOLL_TKT_PROP::nft_max_count, true)},
            { "nft_copy_count",                 make_tuple(NFTCOLL_TKT_PROP::nft_copy_count, true)},
            { "royalty",                        make_tuple(NFTCOLL_TKT_PROP::royalty, true) },
            { "green",                          make_tuple(NFTCOLL_TKT_PROP::green, true) },
            { "app_ticket",                     make_tuple(NFTCOLL_TKT_PROP::app_ticket, true) }
        }
    }
}};

/**
* Parses base64-encoded nft_ticket in json format.
* Throws runtime_error exception in case nft_ticket has invalid format
* 
*/
void CNFTCollectionRegTicket::parse_nft_collection_ticket()
{
    // parse NFT Collection Registration Ticket json
    json jsonTicketObj;
    try
    {
        unordered_set<NFTCOLL_TKT_PROP> props; // set of properties in the nft_collection_ticket 

        const auto jsonTicketObj = json::parse(ed_crypto::Base64_Decode(m_sNFTCollectionTicket));
        // check nft_collection_ticket version
        const int nTicketVersion = jsonTicketObj.at("nft_collection_ticket_version");
        if (nTicketVersion < 1 || nTicketVersion > static_cast<int>(NFTCOLL_TICKET_INFO.size()))
            throw runtime_error(strprintf("'%s' ticket json version '%d' cannot be greater than '%zu'", 
                GetTicketDescription(), nTicketVersion, NFTCOLL_TICKET_INFO.size()));
        const auto& tktInfo = NFTCOLL_TICKET_INFO[nTicketVersion - 1];
        // validate all nft_collection_ticket properties and get values
        const auto& propMap = tktInfo.propMap;
        for (const auto& [sPropName, value] : jsonTicketObj.items())
        {
            const auto itProp = propMap.find(sPropName);
            if (itProp == propMap.cend())
                throw runtime_error(strprintf(
                    "Found unsupported property '%s' in '%s' ticket json v%d",
                    sPropName, GetTicketDescription(), nTicketVersion));
            const auto& prop = get<0>(itProp->second);
            props.insert(prop);
            // process properties
            switch (prop)
            {
                case NFTCOLL_TKT_PROP::name:
                    value.get_to(m_sNFTCollectionName);
                    break;

                case NFTCOLL_TKT_PROP::creator:
                    value.get_to(m_sCreatorPastelID);
                    break;

                case NFTCOLL_TKT_PROP::blocknum:
                    value.get_to(m_nCreatorHeight);
                    break;

                case NFTCOLL_TKT_PROP::block_hash:
                    value.get_to(m_sTopBlockHash);
                    break;

                case NFTCOLL_TKT_PROP::permitted_users:
                    value.get_to(m_PermittedUsers);
                    break;

                case NFTCOLL_TKT_PROP::closing_height:
                    value.get_to(m_nClosingHeight);
                    break;

                case NFTCOLL_TKT_PROP::nft_max_count:
                    value.get_to(m_nMaxNFTCount);
                    break;

                case NFTCOLL_TKT_PROP::nft_copy_count:
                    value.get_to(m_nNFTCopyCount);
                    break;

                case NFTCOLL_TKT_PROP::royalty:
                    value.get_to(m_nRoyalty);
                    break;

                case NFTCOLL_TKT_PROP::green:
                {
                    bool bHasGreen = false;
                    value.get_to(bHasGreen);
                    if (bHasGreen)
                        m_sGreenAddress = GreenAddress(GetActiveChainHeight());
                } break;
            } //switch
        } // for

          // check for missing required properties
        string sMissingProps;
        for (const auto& [sPropName, propInfo] : propMap)
        {
            const auto& [prop, bRequired] = propInfo;
            if (bRequired && !props.count(prop))
                str_append_field(sMissingProps, sPropName.c_str(), ",");
        }
        if (!sMissingProps.empty())
            throw runtime_error(strprintf(
                "Missing required properties '%s' in '%s' ticket json v%d",
                sMissingProps, GetTicketDescription(), nTicketVersion));
    } catch (const json::exception& ex)
    {
        throw runtime_error(
            strprintf("Failed to parse '%s' ticket json. %s", 
                GetTicketDescription(), SAFE_SZ(ex.what())));
    }
}

/**
* Validate NFT collection ticket.
* 
* \param bPreReg - if true: called from ticket pre-registration
* \param nCallDepth - function call depth
* \return true if the ticket is valid
*/
ticket_validation_t CNFTCollectionRegTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const auto chainHeight = GetActiveChainHeight();
    ticket_validation_t tv;
    do
    {
        if (bPreReg)
        {
            // Something to check ONLY before the ticket made into transaction.
            // Only done after Create

            // check if NFT collection ticket is already in the blockchain
            if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this))
            {
                tv.errorMsg = strprintf(
                    "This NFT collection '%s' is already registered in blockchain [key=%s; label=%s]", 
                    m_sNFTCollectionName, m_keyOne, m_label);
                break;
            }

            // validate that address has coins to pay for registration - 10PSL (default fee)
            const auto fullTicketPrice = TicketPricePSL(chainHeight); //10% of storage fee is paid by the 'creator' and this ticket is created by MN
            if (pwalletMain->GetBalance() < fullTicketPrice * COIN)
            {
                tv.errorMsg = strprintf(
                    "Not enough coins to cover price [%" PRId64 " PSL]", 
                    fullTicketPrice);
                break;
            }
        }

        // check collection name
        if (m_sNFTCollectionName.empty())
        {
            tv.errorMsg = "NFT Collection name is not defined";
            break;
        }

        // validate max nft count
        if (!m_nMaxNFTCount || m_nMaxNFTCount > MAX_NFT_COLLECTION_SIZE)
        {
            tv.errorMsg = strprintf(
                "Maximum number of NFTs in the collection '%u' should be within range (0..%u)", 
                m_nMaxNFTCount, MAX_NFT_COLLECTION_SIZE);
            break;
        }

        // validate closing height
        if (m_nClosingHeight <= m_nCreatorHeight)
        {
            tv.errorMsg = strprintf(
                "Closing height %u should not be less than or equal ticket height %u", 
                m_nClosingHeight, m_nCreatorHeight);
            break;
        }

        // (ticket transaction replay attack protection)
        CNFTCollectionRegTicket ticket;
        if (FindTicketInDb(m_keyOne, ticket) && (!ticket.IsBlock(m_nBlock) || !ticket.IsTxId(m_txid)))
        {
            tv.errorMsg = strprintf(
                "This NFT collection '%s' is already registered in blockchain [key=%s; label=%s] [%sfound ticket block=%u, txid=%s]",
                m_sNFTCollectionName, m_keyOne, m_label, 
                bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", m_nBlock, m_txid),
                ticket.GetBlock(), ticket.m_txid);
            break;
        }

        // B. Something to validate always
        const ticket_validation_t sigTv = validate_signatures(nCallDepth, m_nCreatorHeight, m_sNFTCollectionTicket);
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

/**
 * Check if this user is in the permitted list.
 * 
 * \param sPastelID - Pastel ID of the user
 * \return true if user is in the permitted list
 */
bool CNFTCollectionRegTicket::IsUserPermitted(const std::string& sPastelID) const noexcept
{
    return m_PermittedUsers.count(sPastelID) > 0;
}

void CNFTCollectionRegTicket::Clear() noexcept
{
    CTicketSignedWithExtraFees::Clear();
    m_sNFTCollectionTicket.clear();
    m_sNFTCollectionName.clear();
    m_nClosingHeight = 0;
    m_nMaxNFTCount = 0;
    m_nNFTCopyCount = 0;
    m_PermittedUsers.clear();
}

/**
* Get json string representation of the ticket.
* 
* \return json string
*/
string CNFTCollectionRegTicket::ToJSON() const noexcept
{
    const json jsonObj
    {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", 
            {
                {"type", GetTicketName()},
                {"nft_collection_ticket", m_sNFTCollectionTicket},
                {"version", GetStoredVersion()},
                get_signatures_json(),
                {"permitted_users", m_PermittedUsers},
                {"key", m_keyOne},
                {"label", m_label},
                {"creator_height", m_nCreatorHeight},
                {"closing_height", m_nClosingHeight},
                {"nft_max_count", m_nMaxNFTCount},
                {"nft_copy_count", m_nNFTCopyCount},
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
bool CNFTCollectionRegTicket::FindTicketInDb(const string& key, CNFTCollectionRegTicket& ticket)
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
bool CNFTCollectionRegTicket::CheckIfTicketInDb(const string& key)
{
    CNFTCollectionRegTicket ticket;
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

NFTCollectionRegTickets_t CNFTCollectionRegTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTCollectionRegTicket>(pastelID);
}
