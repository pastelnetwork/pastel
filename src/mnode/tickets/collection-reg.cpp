// Copyright (c) 2022-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <str_utils.h>
#include <init.h>
#include <key_io.h>
#include <utilstrencodings.h>
#include <pastelid/common.h>
#include <pastelid/pastel_key.h>
#include <mnode/mnode-controller.h>
#include <mnode/ticket-processor.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/collection-reg.h>
#include <mnode/tickets/action-reg.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

//  ---- Collection Registration Ticket ---- ////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Create Collection registration ticket.
 * 
 * \param collection_ticket_base64_encoded - base64-encoded json object with collection ticket
 * \param signatures - signatures json
 * \param sPastelID - Collection creator's Pastel ID
 * \param strKeyPass - passphrase for creator's secure container
 * \param label - search key #2
 * \param storageFee - ticket storage fee
 * \return - collection ticket object
 */
CollectionRegTicket CollectionRegTicket::Create(
    string &&collection_ticket_base64_encoded,
    const string& signatures,
    string &&sPastelID,
    SecureString&& strKeyPass,
    string &&label,
    const CAmount storageFee)
{
    bool bInvalidBase64Encoding = false;
    string sDecodedCollectionTicket = DecodeBase64(move(collection_ticket_base64_encoded), &bInvalidBase64Encoding);
    if (bInvalidBase64Encoding)
        throw runtime_error("Invalid base64 encoding found in collection ticket");

    CollectionRegTicket ticket(move(sDecodedCollectionTicket));
    ticket.parse_collection_ticket();

    // parse and set principal's and MN2/3's signatures
    ticket.set_signatures(signatures);
    ticket.m_label = move(label);
    ticket.m_storageFee = storageFee;
    ticket.GenerateKeyOne();
    ticket.GenerateTimestamp();

    ticket.m_vPastelID[SIGN_MAIN] = move(sPastelID);
    // sign the ticket hash using principal Pastel ID with ed448 algorithm
    string_to_vector(CPastelID::Sign(ticket.m_sCollectionTicket, ticket.m_vPastelID[SIGN_MAIN], move(strKeyPass)), ticket.m_vTicketSignature[SIGN_MAIN]);
    return ticket;
}

// tuple: 
//    - collection ticket property enum
//    - property required (true) or optional (false)
using CollTicketProp = tuple<COLL_TKT_PROP, bool>;
// collection_ticket info
using CollTicketInfo = struct
{
    // Collection ticket version
    uint32_t nVersion;
    // map of supported properties (property name)->(property tuple)
    unordered_map<string, CollTicketProp> propMap;
};

static const std::array<CollTicketInfo, 1> COLL_TICKET_INFO =
{{
    { 1, //version
        {   // property name -> property tuple (property enum, is_required_property)
            { "collection_ticket_version",                      make_tuple(COLL_TKT_PROP::version, true) },
            { "collection_name",                                make_tuple(COLL_TKT_PROP::name, true) },
            { "item_type",                                      make_tuple(COLL_TKT_PROP::item_type, true) },
            { "creator",                                        make_tuple(COLL_TKT_PROP::creator, true) },
            { "list_of_pastelids_of_authorized_contributors",   make_tuple(COLL_TKT_PROP::list_of_pastelids_of_authorized_contributors, true) },
            { "blocknum",                                       make_tuple(COLL_TKT_PROP::blocknum, true) },
            { "block_hash",                                     make_tuple(COLL_TKT_PROP::block_hash, true) },
            { "collection_final_allowed_block_height",          make_tuple(COLL_TKT_PROP::collection_final_allowed_block_height, false) },
            { "max_collection_entries",                         make_tuple(COLL_TKT_PROP::max_collection_entries, true)},
            { "collection_item_copy_count",                     make_tuple(COLL_TKT_PROP::collection_item_copy_count, false)},
            { "royalty",                                        make_tuple(COLL_TKT_PROP::royalty, false) },
            { "green",                                          make_tuple(COLL_TKT_PROP::green, false) },
            { COLL_TICKET_APP_OBJ,                              make_tuple(COLL_TKT_PROP::app_ticket, true) }
        }
    }
}};

/**
* Parses collection_ticket in json format.
* Throws runtime_error exception in case collection_ticket has invalid format
*/
void CollectionRegTicket::parse_collection_ticket()
{
    // parse Collection Registration Ticket json
    try
    {
        unordered_set<COLL_TKT_PROP> props; // set of properties in the collection_ticket 

        const auto jsonTicketObj = json::parse(m_sCollectionTicket);
        // check collection_ticket version
        const int nTicketVersion = jsonTicketObj.at("collection_ticket_version");
        if (nTicketVersion < 1 || nTicketVersion > static_cast<int>(COLL_TICKET_INFO.size()))
            throw runtime_error(strprintf("'%s' ticket json version '%d' cannot be greater than '%zu'", 
                GetTicketDescription(), nTicketVersion, COLL_TICKET_INFO.size()));
        const auto& tktInfo = COLL_TICKET_INFO[nTicketVersion - 1];
        // validate all collection_ticket properties and get values
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
                case COLL_TKT_PROP::name:
                    value.get_to(m_sCollectionName);
                    break;

                case COLL_TKT_PROP::item_type:
					value.get_to(m_sItemType);
					break;

                case COLL_TKT_PROP::creator:
                    value.get_to(m_sCreatorPastelID);
                    break;

                case COLL_TKT_PROP::blocknum:
                    value.get_to(m_nCreatorHeight);
                    break;

                case COLL_TKT_PROP::block_hash:
                    value.get_to(m_sTopBlockHash);
                    break;

                case COLL_TKT_PROP::list_of_pastelids_of_authorized_contributors:
                    value.get_to(m_AuthorizedContributors);
                    break;

                case COLL_TKT_PROP::collection_final_allowed_block_height:
                    value.get_to(m_nCollectionFinalAllowedBlockHeight);
                    break;

                case COLL_TKT_PROP::max_collection_entries:
                    value.get_to(m_nMaxCollectionEntries);
                    break;

                case COLL_TKT_PROP::collection_item_copy_count:
                    value.get_to(m_nItemCopyCount);
                    break;

                case COLL_TKT_PROP::royalty:
                    value.get_to(m_nRoyalty);
                    break;

                case COLL_TKT_PROP::green:
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
        setItemType(m_sItemType); 
        // set default closing height if not set
        if (m_nCollectionFinalAllowedBlockHeight == 0)
            m_nCollectionFinalAllowedBlockHeight = m_nCreatorHeight + masterNodeCtrl.getMaxInProcessCollectionTicketAge();
    } catch (const json::exception& ex)
    {
        throw runtime_error(
            strprintf("Failed to parse '%s' ticket json. %s", 
                GetTicketDescription(), SAFE_SZ(ex.what())));
    }
}

/**
 * Get description for the collection item type to be used in logs and error messages.
 * 
 * \return collection item type description
 */
string CollectionRegTicket::getCollectionItemDesc() const noexcept
{
    if (m_ItemType == COLLECTION_ITEM_TYPE::NFT)
        return "NFT";
    if (m_ItemType == COLLECTION_ITEM_TYPE::SENSE)
        return "Sense";
    return "Unknown";
}

bool CollectionRegTicket::CanAcceptTicket(const CPastelTicket &ticket) const noexcept
{
    const auto ticketId = ticket.ID();
	if (ticketId == TicketID::NFT && m_ItemType == COLLECTION_ITEM_TYPE::NFT)
        return true;
    if (ticketId == TicketID::ActionReg)
    {
        if (m_ItemType != COLLECTION_ITEM_TYPE::SENSE)
            return false;
        const auto& actionTicket = dynamic_cast<const CActionRegTicket&>(ticket);
        return actionTicket.getActionType() == ACTION_TICKET_TYPE::SENSE;
    }
    return false;
}

/**
* Validate collection ticket.
* 
* \param bPreReg - if true: called from ticket pre-registration
* \param nCallDepth - function call depth
* \return true if the ticket is valid
*/
ticket_validation_t CollectionRegTicket::IsValid(const bool bPreReg, const uint32_t nCallDepth) const noexcept
{
    const auto chainHeight = GetActiveChainHeight();
    ticket_validation_t tv;
    do
    {
        // check collection name
        if (m_sCollectionName.empty())
        {
            tv.errorMsg = "Collection name is not defined";
            break;
        }

        // check collection item type
        if (m_ItemType == COLLECTION_ITEM_TYPE::UNKNOWN)
        {
			tv.errorMsg = "Collection item type is not defined";
			break;
		}

        if (bPreReg)
        {
            // Something to check ONLY before the ticket made into transaction.
            // Only done after Create

            // check if collection ticket is already in the blockchain:
            // - search by key
            // - search by collection name
            if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this) ||
                masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(*this))
            {
                tv.errorMsg = strprintf(
                    "This %s collection '%s' is already registered in blockchain [key=%s; label=%s]", 
                    getCollectionItemDesc(), m_sCollectionName, m_keyOne, m_label);
                break;
            }

            // validate that address has coins to pay for registration - 10 PSL (default fee)
            const auto fullTicketPricePSL = TicketPricePSL(chainHeight); //10% of storage fee is paid by the 'creator' and this ticket is created by MN
            if (pwalletMain->GetBalance() < fullTicketPricePSL * COIN)
            {
                tv.errorMsg = strprintf(
                    "Not enough coins to cover price [%" PRId64 " PSL]", 
                    fullTicketPricePSL);
                break;
            }
        }

        // validate max collection entries
        if (!m_nMaxCollectionEntries || m_nMaxCollectionEntries > MAX_ALLOWED_COLLECTION_ENTRIES)
        {
            tv.errorMsg = strprintf(
                "Maximum number of items in the %s collection '%u' should be within range (0..%u)", 
                getCollectionItemDesc(), m_nMaxCollectionEntries, MAX_ALLOWED_COLLECTION_ENTRIES);
            break;
        }

        // validate closing height
        if (m_nCollectionFinalAllowedBlockHeight <= m_nCreatorHeight)
        {
            tv.errorMsg = strprintf(
                "Closing height %u for %s collection should not be less than or equal ticket height %u", 
                m_nCollectionFinalAllowedBlockHeight, getCollectionItemDesc(), m_nCreatorHeight);
            break;
        }
        if (m_nCollectionFinalAllowedBlockHeight > m_nCreatorHeight + masterNodeCtrl.getMaxInProcessCollectionTicketAge())
        {
            tv.errorMsg = strprintf(
				"Closing height %u for %s collection should not be more than %u blocks from the creator height %u",
				m_nCollectionFinalAllowedBlockHeight, getCollectionItemDesc(), masterNodeCtrl.getMaxInProcessCollectionTicketAge(),
                m_nCreatorHeight);
			break;
		}

        // (ticket transaction replay attack protection)
        CollectionRegTicket ticket;
        if (FindTicketInDb(m_keyOne, ticket) && (!ticket.IsBlock(m_nBlock) || !ticket.IsTxId(m_txid)))
        {
            tv.errorMsg = strprintf(
                "This %s collection '%s' is already registered in blockchain [key=%s; label=%s] [%sfound ticket block=%u, txid=%s]",
                getCollectionItemDesc(), m_sCollectionName, m_keyOne, m_label, 
                bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", m_nBlock, m_txid),
                ticket.GetBlock(), ticket.m_txid);
            break;
        }

        // B. Something to validate always
        const ticket_validation_t sigTv = validate_signatures(nCallDepth, m_nCreatorHeight, m_sCollectionTicket);
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
bool CollectionRegTicket::IsAuthorizedContributor(const std::string& sPastelID) const noexcept
{
    return m_AuthorizedContributors.count(sPastelID) > 0;
}

/**
 * Set collection item type.
 * 
 * \param sItemType - item type
 * \return true if item type is valid
 */
bool CollectionRegTicket::setItemType(const std::string sItemType) noexcept
{
    m_ItemType = COLLECTION_ITEM_TYPE::UNKNOWN;
    m_sItemType = lowercase(sItemType);
    if (m_sItemType == COLLECTION_ITEM_TYPE_NFT)
		m_ItemType = COLLECTION_ITEM_TYPE::NFT;
	else if (m_sItemType == COLLECTION_ITEM_TYPE_SENSE)
		m_ItemType = COLLECTION_ITEM_TYPE::SENSE; 
    return (m_ItemType != COLLECTION_ITEM_TYPE::UNKNOWN);
}

void CollectionRegTicket::Clear() noexcept
{
    CTicketWithKey::Clear();
    CTicketSignedWithExtraFees::clear_extra_fees();
    m_sCollectionTicket.clear();
    m_sCollectionName.clear();
    m_ItemType = COLLECTION_ITEM_TYPE::UNKNOWN;
    m_sItemType.clear();
    m_sTopBlockHash.clear();
    m_nCollectionFinalAllowedBlockHeight = 0;
    m_nMaxCollectionEntries = 0;
    m_nItemCopyCount = 0;
    m_AuthorizedContributors.clear();
}

/**
* Get json string representation of the ticket.
* 
* \param bDecodeProperties - if true, then decode collection_ticket and its properties
* \return json string
*/
string CollectionRegTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    const json collection_ticket_json = json::parse(m_sCollectionTicket);
    const json jsonObj
    {
        { "txid", m_txid },
        { "height", m_nBlock },
        { "tx_info", get_txinfo_json() },
        { "ticket",
            {
                { "type", GetTicketName() },
                { "version", GetStoredVersion() },
                { "collection_ticket", collection_ticket_json },
                get_signatures_json(),
                { "key", m_keyOne },
                { "label", m_label },
                { "creator_height", m_nCreatorHeight },
                { "royalty_address", GetRoyaltyPayeeAddress(GetTxId()) },
                { "storage_fee", m_storageFee }
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
bool CollectionRegTicket::FindTicketInDb(const string& key, CollectionRegTicket& ticket)
{
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

/**
 * Find ticket in DB by collection name (secondary key).
 * 
 * \param sCollectionName - collection name
 * \param ticket - returns ticket if found
 * \return  
 */
bool CollectionRegTicket::FindTicketInDbByCollectionName(const std::string& sCollectionName, CollectionRegTicket& ticket)
{
    ticket.setCollectionName(sCollectionName);
    return masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(ticket);
}

/**
* Check if ticket exists in a DB by primary key.
* 
* \param key - lookup key, used in a search by primary key
* \return true if ticket exists in a DB
*/
bool CollectionRegTicket::CheckIfTicketInDb(const string& key)
{
    CollectionRegTicket ticket;
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket);
}

CollectionRegTickets_t CollectionRegTicket::FindAllTicketByPastelID(const string& pastelID)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CollectionRegTicket>(pastelID);
}
