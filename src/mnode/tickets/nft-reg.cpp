// Copyright (c) 2018-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>
#include <tuple>

#include <utils/str_utils.h>
#include <utils/utilstrencodings.h>
#include <init.h>
#include <key_io.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/nft-royalty.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/tickets/action-reg.h>
#include <mnode/tickets/collection-reg.h>
#include <mnode/tickets/collection-act.h>
#include <mnode/mnode-controller.h>
#include <wallet/wallet.h>

using json = nlohmann::json;
using namespace std;

//  ---- NFT Registration Ticket ---- ////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Current nft_ticket
{
    "nft_ticket_version": integer     // 1 or 2
    "author": bytes,                  // Pastel ID of the NFT creator
    "blocknum": integer,              // block number when the ticket was created - this is to map the ticket to the MNs that should process it
    "block_hash": bytes               // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
    "copies": integer,                // number of copies
    "royalty": float,                 // how much creator should get on all future resales (optional in v2, if not defined - used the value from collection)
    "green": boolean,                 // whether Green NFT payment should be made (optional in v2, if not defined - used the value from collection)
    "collection_txid": bytes,         // transaction id of the collection that NFT belongs to (supported in v2 only, optional - can have empty value)
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
    CNFTRegTicket ticket(std::move(nft_ticket));
    ticket.parse_nft_ticket();
    ticket.set_collection_properties();

    // parse and set principal's and MN2/3's signatures
    ticket.set_signatures(signatures);
    ticket.m_label = std::move(label);
    ticket.m_storageFee = storageFee;
    ticket.GenerateKeyOne();
    ticket.GenerateTimestamp();

    ticket.m_vPastelID[SIGN_MAIN] = std::move(sPastelID);
    // sign the ticket hash using principal PastelID, ed448 algorithm
    string_to_vector(CPastelID::Sign(ticket.m_sNFTTicket, ticket.m_vPastelID[SIGN_MAIN], std::move(strKeyPass)), ticket.m_vTicketSignature[SIGN_MAIN]);
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
                { NFT_TICKET_APP_OBJ,    make_tuple(NFT_TKT_PROP::app_ticket, true) }
            }
        },
        { 2,
            {
                { "nft_ticket_version",  make_tuple(NFT_TKT_PROP::version, true) },
                { "author",              make_tuple(NFT_TKT_PROP::creator, true) },
                { "blocknum",            make_tuple(NFT_TKT_PROP::blocknum, true) },
                { "block_hash",          make_tuple(NFT_TKT_PROP::block_hash, true) },
                { "collection_txid",     make_tuple(NFT_TKT_PROP::collection_act_txid, false) },
                { "copies",              make_tuple(NFT_TKT_PROP::copies, false) },
                { "royalty",             make_tuple(NFT_TKT_PROP::royalty, false) },
                { "green",               make_tuple(NFT_TKT_PROP::green, false) },
                { NFT_TICKET_APP_OBJ,    make_tuple(NFT_TKT_PROP::app_ticket, true) }
            }
        }
}};

/**
 * Parses base64-encoded nft_ticket to json.
 *
 * \return nft ticket object in json format
 * \throw runtime_error in case nft_ticket has invalid base64 encoding
 */
json CNFTRegTicket::get_nft_ticket_json() const
{
    bool bInvalidBase64Encoding = false;
    string sDecodedNFTTicket = DecodeBase64(m_sNFTTicket, &bInvalidBase64Encoding);
    if (bInvalidBase64Encoding)
        throw runtime_error("Invalid base64 encoding found in NFT ticket");
    return json::parse(sDecodedNFTTicket);
}

/**
 * Parses base64-encoded nft_ticket in json format.
 * Throws runtime_error exception in case nft_ticket has invalid format
 */
void CNFTRegTicket::parse_nft_ticket()
{
    // parse NFT Ticket (nft_ticket version can be different from serialized ticket version)
    try
    {
        const auto jsonTicketObj = get_nft_ticket_json();
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

                case NFT_TKT_PROP::collection_act_txid:
                    value.get_to(m_sCollectionActTxid);
                    break;

                case NFT_TKT_PROP::royalty:
                    value.get_to(m_nRoyalty);
                    break;

                case NFT_TKT_PROP::green:
                {
                    bool bHasGreen = false;
                    value.get_to(bHasGreen);
                    if (bHasGreen)
                        m_sGreenAddress = GreenAddress(gl_nChainHeight + 1);
                } break;

                case NFT_TKT_PROP::version:
                case NFT_TKT_PROP::app_ticket:
                case NFT_TKT_PROP::unknown:
                    break;
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
 * Set missing properties from collection.
 * Does not throw exception if collection not found - it will be raised in IsValid.
 */
void CNFTRegTicket::set_collection_properties() noexcept
{
    if (m_sCollectionActTxid.empty())
        return;
    string error;
    bool bInvalidTxId = false;
    const auto collectionTicket = RetrieveCollectionActivateTicket(error, bInvalidTxId, nullptr);
    const CollectionRegTicket *pCollTicket = dynamic_cast<const CollectionRegTicket*>(collectionTicket.get());
    if (!pCollTicket)
        return;
    // check that txid refers NFT collection
    if (!pCollTicket->CanAcceptTicket(*this))
		return;
    // set royalty fee
    if (!m_props.count(NFT_TKT_PROP::royalty))
        m_nRoyalty = pCollTicket->getRoyalty();
    // set green address
    if (!m_props.count(NFT_TKT_PROP::green))
        m_sGreenAddress = pCollTicket->getGreenAddress();
    // set total copies
    if (!m_props.count(NFT_TKT_PROP::copies))
        m_nTotalCopies = pCollTicket->getItemCopyCount();
}

/**
 * Validate NFT Registration ticket.
 * 
 * \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
 * \param nCallDepth - function call depth
 * \param pindexPrev - previous block index
 * \return true if the ticket is valid
 */
ticket_validation_t CNFTRegTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept
{
    const auto nActiveChainHeight = gl_nChainHeight + 1;
    ticket_validation_t tv;
    do
    {
        const bool bPreReg = isPreReg(txOrigin);
        if (bPreReg)
        {
            // A. Something to check ONLY before the ticket made into transaction.
            // Only done after Create

            // A.1 check that the NFT ticket is already in the blockchain
            if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this, pindexPrev))
            {
                tv.errorMsg = strprintf(
                    "This NFT is already registered in blockchain [key=%s; label=%s]",
                    m_keyOne, m_label);
                break;
            }

#ifdef ENABLE_WALLET
            if (isLocalPreReg(txOrigin))
            {
                // A.2 validate that address has coins to pay for registration - 10PSL
                const auto fullTicketPrice = TicketPricePSL(nActiveChainHeight); //10% of storage fee is paid by the 'creator' and this ticket is created by MN
                if (pwalletMain->GetBalance() < fullTicketPrice * COIN)
                {
                    tv.errorMsg = strprintf(
                        "Not enough coins to cover price [%" PRId64 " PSL]",
                        fullTicketPrice);
                    break;
                }
            }
#endif // ENABLE_WALLET
            // A.3 check that the NFT creator height is not in the future
            if (m_nCreatorHeight > nActiveChainHeight)
            {
                tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
                tv.errorMsg = strprintf(
					"This NFT creator height is in the future [creator_height=%u, active chain height=%u]", 
					m_nCreatorHeight, nActiveChainHeight);
                break;
            }
        }
        // (ticket transaction replay attack protection)
        CNFTRegTicket existingTicket;
        if (FindTicketInDb(m_keyOne, existingTicket, pindexPrev) &&
            (!existingTicket.IsBlock(m_nBlock) ||
            !existingTicket.IsTxId(m_txid)))
        {
            string message = strprintf("This NFT is already registered in blockchain [key=%s; label=%s]", m_keyOne, KeyTwo());
            const bool bTicketFound = masterNodeCtrl.masternodeTickets.FindAndValidateTicketTransaction(existingTicket, m_txid, m_nBlock, bPreReg, message);
            if (bTicketFound)
            {
                tv.errorMsg = message;
                break;
            }
        }

        // validate referenced collection (for v2 only)
        ticket_validation_t collTv = IsValidCollection(bPreReg);
        if (collTv.IsNotValid())
        {
            tv = std::move(collTv);
            break;
        }

        // B. Something to validate always
        const ticket_validation_t sigTv = validate_signatures(txOrigin, nCallDepth, m_nCreatorHeight, m_sNFTTicket, pindexPrev);
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
    CollectionItem::Clear();
    clear_extra_fees();
    m_nNFTTicketVersion = 0;
    m_sNFTTicket.clear();
    m_nTotalCopies = 0;
    m_props.clear();
}

uint32_t CNFTRegTicket::CountItemsInCollection(const CBlockIndex *pindexPrev) const
{
    return CollectionActivateTicket::CountItemsInCollection(m_sCollectionActTxid, COLLECTION_ITEM_TYPE::NFT, true, pindexPrev);
}

/**
 * Get json representation of the ticket.
 * 
 * \param bDecodeProperties - if true, then decode nft_ticket and its properties
 * \return json object
 */
json CNFTRegTicket::getJSON(const bool bDecodeProperties) const noexcept
{
    json nft_ticket_json;
    if (bDecodeProperties)
    {
        try {
            nft_ticket_json = get_nft_ticket_json();
            if (nft_ticket_json.contains(NFT_TICKET_APP_OBJ))
            {
                // try to decode ascii85-encoded app_ticket
                bool bInvalidEncoding = false;
                string sDecodedAppTicket = DecodeAscii85(nft_ticket_json[NFT_TICKET_APP_OBJ], &bInvalidEncoding);
                if (!bInvalidEncoding)
                        nft_ticket_json[NFT_TICKET_APP_OBJ] = std::move(json::parse(sDecodedAppTicket));
                else
                {
                    // this can be base64-encoded app_ticket as well
                    sDecodedAppTicket = DecodeBase64(nft_ticket_json[NFT_TICKET_APP_OBJ], &bInvalidEncoding);
                    if (!bInvalidEncoding)
						nft_ticket_json[NFT_TICKET_APP_OBJ] = std::move(json::parse(sDecodedAppTicket));
                }
            }
        } catch (...) {}
    }
    if (nft_ticket_json.empty())
        nft_ticket_json = m_sNFTTicket;

    const json jsonObj
    {
        { "txid", m_txid },
        { "height", static_cast<int32_t>(m_nBlock) },
        { "tx_info", get_txinfo_json() },
        { "ticket",
            {
                { "type", GetTicketName() },
                { "nft_ticket", nft_ticket_json },
                { "version", GetStoredVersion() },
                get_signatures_json(),
                { "key", m_keyOne },
                { "label", m_label },
                { "creator_height", m_nCreatorHeight },
                { "total_copies", m_nTotalCopies },
                { "royalty", m_nRoyalty },
                { "royalty_address", GetRoyaltyPayeeAddress(GetTxId()) },
                { "green", !m_sGreenAddress.empty() },
                { "storage_fee", m_storageFee }
            }
        }
    };
    return jsonObj;
}

/**
 * Get json string representation of the ticket.
 * 
 * \param bDecodeProperties - if true, then decode nft_ticket and its properties
 * \return json string
 */
string CNFTRegTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    return getJSON(bDecodeProperties).dump(4);
}

/**
 * Find ticket in DB by primary key.
 * 
 * \param key - lookup key, used in a search by primary key
 * \param ticket - returns ticket if found
 * \param pindexPrev - previous block index
 * \return true if ticket was found
 */
bool CNFTRegTicket::FindTicketInDb(const string& key, CNFTRegTicket& ticket, const CBlockIndex *pindexPrev)
{
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev);
}

/**
 * Check if ticket exists in a DB by primary key.
 * 
 * \param key - lookup key, used in a search by primary key
 * \return true if ticket exists in a DB
 */
bool CNFTRegTicket::CheckIfTicketInDb(const string& key, const CBlockIndex* pindexPrev)
{
    CNFTRegTicket ticket;
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket, pindexPrev);
}

NFTRegTickets_t CNFTRegTicket::FindAllTicketByMVKey(const string& sMVKey, const CBlockIndex *pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CNFTRegTicket>(sMVKey, pindexPrev);
}

CAmount CNFTRegTicket::GetNftFee(const size_t nImageDataSizeInMB, const size_t nTicketDataSizeInBytes, const uint32_t nChainHeight) noexcept
{
    const auto &consensusParams = Params().GetConsensus();
    const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
    const double nFeeAdjustmentMultiplier = nGlobalFeeAdjustmentMultiplier * masterNodeCtrl.GetChainDeflatorFactor(nChainHeight);

    const CAmount nStorageFeePerMB = masterNodeCtrl.GetNetworkMedianMNFee(MN_FEE::StorageFeePerMB);
    const CAmount nTicketChainStorageFeePerKB = masterNodeCtrl.GetNetworkMedianMNFee(MN_FEE::TicketChainStorageFeePerKB);

    // get sense and cascade fees not including ticket blockchain storage fee
    const action_fee_map_t action_fees = CActionRegTicket::GetActionFees(nImageDataSizeInMB, nChainHeight, false);

	const CAmount nNftFee =
        static_cast<CAmount>(
            (action_fees.at(ACTION_TICKET_TYPE::SENSE) + 
             action_fees.at(ACTION_TICKET_TYPE::CASCADE)) * NFT_DISCOUNT_MULTIPLIER) +
        static_cast<CAmount>(
            ceil(nTicketDataSizeInBytes * nTicketChainStorageFeePerKB / 1024) * nFeeAdjustmentMultiplier);
    return nNftFee;
}
