// Copyright (c) 2018-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <utils/str_utils.h>
#include <utils/utilstrencodings.h>
#include <init.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/action-reg.h>
#include <mnode/tickets/collection-act.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using json = nlohmann::json;
using namespace std;

// tuple:
//    - action ticket property enum
//    - property required (true) or optional (false)
using ActionTicketProp = tuple<ACTION_TKT_PROP, bool>;
// action_ticket info
using ActionTicketInfo = struct
{
    // Action ticket version
    uint32_t nVersion;
    // action ticket version that this action ticket is based on,
    // 0 if it is not based on any other action ticket version
    uint32_t nBasedOnVersion; 
    // map of supported properties (property name)->(property tuple)
    unordered_map<string, ActionTicketProp> propMap;
};

static const std::array<ActionTicketInfo, 2> ACTION_TICKET_INFO =
{{
    // version, based-on-version, map of supported properties
    { 1, 0,
       {
           {"action_ticket_version",        make_tuple(ACTION_TKT_PROP::version, true)},
           {"action_type",                  make_tuple(ACTION_TKT_PROP::action_type, true)},
           {"caller",                       make_tuple(ACTION_TKT_PROP::caller, true)},
           {"blocknum",                     make_tuple(ACTION_TKT_PROP::blocknum, true)},
           {"block_hash",                   make_tuple(ACTION_TKT_PROP::block_hash, true)},
           {ACTION_TICKET_APP_OBJ, make_tuple(ACTION_TKT_PROP::app_ticket, true)},
       }
    },
    { 2, 1,
       {
           {"collection_txid",              make_tuple(ACTION_TKT_PROP::collection_act_txid, false)},
       }
    }
}};

/**
 * Get Action Ticket type name.
 * 
 * \param actionTicketType - action ticket type
 * \return type name
 */
const char* GetActionTypeName(const ACTION_TICKET_TYPE actionTicketType) noexcept
{
    const char* szActionTypeName = nullptr;
    switch (actionTicketType)
    {
        case ACTION_TICKET_TYPE::SENSE:
            szActionTypeName = ACTION_TICKET_TYPE_SENSE;
            break;

        case ACTION_TICKET_TYPE::CASCADE:
            szActionTypeName = ACTION_TICKET_TYPE_CASCADE;
            break;

        default:
            break;
    }
    return szActionTypeName;
}

// ----Action Registration Ticket-- -- ////////////////////////////////////////////////////////////////////////////////////////////////////////
/* current action_ticket passed base64-encoded
{
  "action_ticket_version": integer // 1 or 2
  "caller": string,                // Pastel ID of the caller
  "blocknum": integer,             // block number when the ticket was created - this is to map the ticket to the MNs that should process it
  "block_hash": bytes              // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
  "action_type": string,           // action type (sense, cascade)
  "app_ticket": bytes              // as ascii85(app_ticket), actual structure of app_ticket is different for different API and is not parsed by pasteld !!!!
}
*/

/**
 * Create action ticket.
 * 
 * \param action_ticket - base64-encoded action ticket in json format
 * \param signatures - json with (principal, mn2, mn3) signatures
 * \param sPastelID - Pastel ID of the action caller
 * \param strKeyPass - passphrase to access secure container for action caller (principal signer)
 * \param label - key #2 (search label)
 * \param storageFee - ticket fee
 * \return created action ticket
 */
CActionRegTicket CActionRegTicket::Create(string&& action_ticket, const string& signatures, 
    string&& sPastelID, SecureString&& strKeyPass, string &&label, const CAmount storageFee)
{
    CActionRegTicket ticket(std::move(action_ticket));
    ticket.parse_action_ticket();

    // parse and set principal's and MN2/3's signatures
    ticket.set_signatures(signatures);
    ticket.m_label = std::move(label);
    ticket.m_storageFee = storageFee;
    ticket.GenerateKeyOne();
    ticket.GenerateTimestamp();

    ticket.m_vPastelID[SIGN_MAIN] = std::move(sPastelID);
    // sign the ticket hash using principal PastelID, ed448 algorithm
    string_to_vector(CPastelID::Sign(ticket.m_sActionTicket, ticket.m_vPastelID[SIGN_MAIN], 
        std::move(strKeyPass)), ticket.m_vTicketSignature[SIGN_MAIN]);
    return ticket;
}

/**
 * Parses base64-encoded action_ticket to json.
 * 
 * \return action ticket object in json format
 * \throw runtime_error in case action_ticket has invalid base64 encoding
 */
json CActionRegTicket::get_action_ticket_json() const
{
    bool bInvalidBase64Encoding = false;
    string sDecodedActionTicket = DecodeBase64(m_sActionTicket, &bInvalidBase64Encoding);
    if (bInvalidBase64Encoding)
        throw runtime_error("Invalid base64 encoding found in Action ticket");
    return json::parse(sDecodedActionTicket);
}

/**
 * Parses base64-encoded action_ticket in json format.
 * Throws runtime_error exception in case action_ticket has invalid format
 */
void CActionRegTicket::parse_action_ticket()
{
    try
    {
        const json jsonTicketObj = get_action_ticket_json();
        // check action_ticket version
        const int nTicketVersion = jsonTicketObj.at("action_ticket_version");
        if (nTicketVersion < 1 || nTicketVersion > static_cast<int>(ACTION_TICKET_INFO.size()))
            throw runtime_error(strprintf("'%s' ticket json version '%d' cannot be greater than '%zu'",
                                          GetTicketDescription(), m_nActionTicketVersion, ACTION_TICKET_INFO.size()));
        m_nActionTicketVersion = static_cast<uint16_t>(nTicketVersion);

        const auto& tktInfo = ACTION_TICKET_INFO[m_nActionTicketVersion - 1];
        const unordered_map<string, ActionTicketProp>* pBasePropMap = nullptr;
        if (tktInfo.nBasedOnVersion > 0 && tktInfo.nBasedOnVersion < tktInfo.nVersion)
            pBasePropMap = &ACTION_TICKET_INFO[tktInfo.nBasedOnVersion - 1].propMap;

        // validate all action_ticket properties and get values
        const auto& propMap = tktInfo.propMap;
        for (const auto& [sPropName, value] : jsonTicketObj.items())
        {
            ACTION_TKT_PROP prop = ACTION_TKT_PROP::unknown;
            const auto itProp = propMap.find(sPropName);
            if (itProp == propMap.cend())
            {
                if (pBasePropMap)
                {
                    const auto itBaseProp = pBasePropMap->find(sPropName);
                    if (itBaseProp != pBasePropMap->end())
                        prop = get<0>(itBaseProp->second);
                }
                if (prop == ACTION_TKT_PROP::unknown)
                    throw runtime_error(strprintf(
                        "Found unsupported property '%s' in '%s' ticket json v%hu",
                        sPropName, GetTicketDescription(), m_nActionTicketVersion));
            } else
                prop = get<0>(itProp->second);
            m_props.insert(prop);
            // process properties
            switch (prop)
            {
                case ACTION_TKT_PROP::caller:
                    value.get_to(m_sCreatorPastelID);
                    break;

                case ACTION_TKT_PROP::action_type: {
                    string sActionType;
                    value.get_to(sActionType);
                    if (!setActionType(sActionType))
                        throw runtime_error(strprintf("Action type [%s] is not supported", sActionType));
                } break;

                case ACTION_TKT_PROP::blocknum:
                    value.get_to(m_nCalledAtHeight);
                    break;

                case ACTION_TKT_PROP::block_hash:
                    value.get_to(m_sTopBlockHash);
                    break;

                case ACTION_TKT_PROP::collection_act_txid:
                    value.get_to(m_sCollectionActTxid);
                    break;

                case ACTION_TKT_PROP::version:
                case ACTION_TKT_PROP::app_ticket:
                case ACTION_TKT_PROP::unknown:
                    break;
            }  // switch
        } // for

        // check for missing required properties
        string sMissingProps;
        for (const auto& [sPropName, propInfo] : propMap)
        {
            const auto& [prop, bRequired] = propInfo;
            if (bRequired && !m_props.count(prop))
                str_append_field(sMissingProps, sPropName.c_str(), ",");
        }
        if (pBasePropMap)
        {
            for (const auto& [sPropName, propInfo] : *pBasePropMap)
            {
                if (propMap.count(sPropName) > 0)
                    continue;
                const auto& [prop, bRequired] = propInfo;
                if (bRequired && !m_props.count(prop))
                    str_append_field(sMissingProps, sPropName.c_str(), ",");
            }
        }
        if (!sMissingProps.empty())
            throw runtime_error(strprintf(
                "Missing required properties '%s' in '%s' ticket json v%hu",
                sMissingProps, GetTicketDescription(), m_nActionTicketVersion));


    } catch (const json::exception& ex)
    {
        throw runtime_error(strprintf(
            "Failed to parse '%s' ticket json. %s",
            GetTicketDescription(), SAFE_SZ(ex.what())));
    }
}

// Clear action registration ticket.
void CActionRegTicket::Clear() noexcept
{
    CollectionItem::Clear();
    m_nActionTicketVersion = 0;
    m_sActionTicket.clear();
    setActionType("");
    m_nCalledAtHeight = 0;
    m_sTopBlockHash.clear();
    clear_signatures();
    m_props.clear();
    m_storageFee = 0;
}

/**
 * Set action type.
 * 
 * \param sActionType - sense or cascade
 * \return true if action type was set successfully (known action type)
 */
bool CActionRegTicket::setActionType(const string& sActionType) noexcept
{
    m_ActionType = ACTION_TICKET_TYPE::UNKNOWN;
    m_sActionType = lowercase(sActionType);
    if (m_sActionType == ACTION_TICKET_TYPE_SENSE)
        m_ActionType = ACTION_TICKET_TYPE::SENSE;
    else if (m_sActionType == ACTION_TICKET_TYPE_CASCADE)
        m_ActionType = ACTION_TICKET_TYPE::CASCADE;
    return (m_ActionType != ACTION_TICKET_TYPE::UNKNOWN);
}

uint32_t CActionRegTicket::CountItemsInCollection(const CBlockIndex *pindexPrev) const
{
    if (m_ActionType == ACTION_TICKET_TYPE::SENSE)
        return CollectionActivateTicket::CountItemsInCollection(m_sCollectionActTxid, COLLECTION_ITEM_TYPE::SENSE, true);
    return 0;
}

/**
 * Get json representation of the ticket.
 * 
 * \param bDecodeProperties - if true, then decode api_ticket and its properties
 * \return json object
 */
json CActionRegTicket::getJSON(const bool bDecodeProperties) const noexcept
{
    json action_ticket_json;
    if (bDecodeProperties)
    {
        try
        {
            action_ticket_json = get_action_ticket_json();
            if (action_ticket_json.contains(ACTION_TICKET_APP_OBJ))
            {
                // try to decode ascii85-encoded app_ticket
                bool bInvalidEncoding = false;
                string sDecodedAppTicket = DecodeAscii85(action_ticket_json[ACTION_TICKET_APP_OBJ], &bInvalidEncoding);
                if (!bInvalidEncoding)
                    action_ticket_json[ACTION_TICKET_APP_OBJ] = std::move(json::parse(sDecodedAppTicket));
                else
                {
                    // this can be base64-encoded app_ticket as well
                    sDecodedAppTicket = DecodeBase64(action_ticket_json[ACTION_TICKET_APP_OBJ], &bInvalidEncoding);
                    if (!bInvalidEncoding)
						action_ticket_json[ACTION_TICKET_APP_OBJ] = std::move(json::parse(sDecodedAppTicket));
                }
            }
        } catch(...) {}
    }
    if (action_ticket_json.empty())
        action_ticket_json = m_sActionTicket;
    
    const json jsonObj
    {
        { "txid", m_txid },
        { "height", static_cast<int32_t>(m_nBlock)},
        { "tx_info", get_txinfo_json() },
        { "ticket",
            {
               { "type", GetTicketName() },
               { "version", GetStoredVersion() },
               { "action_ticket", action_ticket_json },
               { "action_type", m_sActionType },
               get_signatures_json(),
               { "key", m_keyOne },
               { "label", m_label },
               { "called_at", m_nCalledAtHeight },
               { "storage_fee", m_storageFee }
            }
        }
    };
    return jsonObj;
}

/**
 * Get json string representation of the ticket.
 * 
 * \param bDecodeProperties - if true, then decode action_ticket and its properties
 * \return json string
 */
string CActionRegTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    return getJSON(bDecodeProperties).dump(4);
}

/**
 * Validate Action Registration ticket.
 * 
 * \param txOrigin - ticket transaction origin (used to determine pre-registration mode)
 * \param nCallDepth - function call depth
 * \param pindexPrev - previous block index
 * \return ticket validation state and error message if any
 */
ticket_validation_t CActionRegTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept
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

            // A.1 check that the ActionReg ticket is already in the blockchain
            if (masterNodeCtrl.masternodeTickets.CheckTicketExist(*this, pindexPrev))
            {
                tv.errorMsg = strprintf(
                    "This Action is already registered in blockchain [key=%s; label=%s]", 
                    m_keyOne, m_label);
                break;
            }

#ifdef ENABLE_WALLET
            if (isLocalPreReg(txOrigin))
            {
                // A.2 validate that address has coins to pay for registration - 10PSL
                const auto fullTicketPrice = TicketPricePSL(nActiveChainHeight); //10% of storage fee is paid by the 'caller' and this ticket is created by MN
                if (pwalletMain->GetBalance() < fullTicketPrice * COIN)
                {
                    tv.errorMsg = strprintf(
                        "Not enough coins to cover price [%" PRId64 " PSL]",
                        fullTicketPrice);
                    break;
                }
            }
#endif // ENABLE_WALLET

            // A.3 check that called_at_height is not in the future
            if (m_nCalledAtHeight > nActiveChainHeight)
            {
                tv.state = TICKET_VALIDATION_STATE::MISSING_INPUTS;
                tv.errorMsg = strprintf(
					"This Action called_at height is in the future [called_at=%u, active chain height=%u]", 
					m_nCalledAtHeight, nActiveChainHeight);
                break;
            }
        }

        // (ticket transaction replay attack protection)
        CActionRegTicket existingTicket;
        if (FindTicketInDb(m_keyOne, existingTicket, pindexPrev) &&
            (!existingTicket.IsBlock(m_nBlock) ||
             !existingTicket.IsTxId(m_txid)))
        {
            string message = strprintf("This Action is already registered in blockchain [key=%s; label=%s]", m_keyOne, KeyTwo());
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
        const ticket_validation_t sigTv = validate_signatures(txOrigin, nCallDepth, m_nCalledAtHeight, m_sActionTicket, pindexPrev);
        if (sigTv.IsNotValid())
        {
            tv.state = sigTv.state;
            tv.errorMsg = strprintf(
                "%s ticket signature validation failed. %s", 
                GetTicketDescription(), sigTv.errorMsg);
            break;
        }

        tv.setValid();
    } while (false);
    return tv;
}

/**
 * Find ticket in a DB by primary key.
 * 
 * \param key - lookup key, used in a search by primary key
 * \param ticket - returns ticket if found
 * \param pindexPrev - previous block index
 * \return true if ticket was found
 */
bool CActionRegTicket::FindTicketInDb(const string& key, CActionRegTicket& ticket, const CBlockIndex *pindexPrev)
{
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev);
}

/**
 * Check if ticket exists in a DB by primary key.
 * 
 * \param key - lookup key, used in a search by primary key
 * \param pindexPrev - previous block index
 * \return true if ticket exists in a DB
 */
bool CActionRegTicket::CheckIfTicketInDb(const string& key, const CBlockIndex *pindexPrev)
{
    CActionRegTicket ticket;
    ticket.m_keyOne = key;
    return masterNodeCtrl.masternodeTickets.CheckTicketExist(ticket, pindexPrev);
}

ActionRegTickets_t CActionRegTicket::FindAllTicketByMVKey(const string& sMVKey, const CBlockIndex *pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CActionRegTicket>(sMVKey, pindexPrev);
}

/**
 * Get action fees based on data size in PSL.
 * 
 * \param nDataSizeInMB - data size in MB
 * \param nChainHeight - chain height to get action fees for
 * \param bIncludeTicketFee - include action ticket blockchain storage fee in the result, default is true
 * \param bUseAdjustmentMultiplier - use global adjustment multiplier and fee deflator factor, default is true
 * 
 * \return map of <actionTicketType> -> <fee_in_psl>
 */
action_fee_map_t CActionRegTicket::GetActionFees(const size_t nDataSizeInMB, const uint32_t nChainHeight,
    const bool bIncludeTicketFee, const bool bUseAdjustmentMultiplier) noexcept
{
    action_fee_map_t feeMap;
    const CAmount nStorageFeePerMB = masterNodeCtrl.GetNetworkMedianMNFee(MN_FEE::StorageFeePerMB);
    const CAmount nSenseFeePerMB = masterNodeCtrl.GetActionTicketFeePerMB(ACTION_TICKET_TYPE::SENSE);
    const CAmount nSenseComputeFee = masterNodeCtrl.GetNetworkMedianMNFee(MN_FEE::SenseComputeFee);

    // calculate sense fee
    CAmount nSenseFee = 
        nStorageFeePerMB * AVERAGE_SENSE_DUPE_DATA_SIZE_MB +
        nDataSizeInMB * nSenseFeePerMB +
        nSenseComputeFee;

    // calculate cascade fee
    CAmount nCascadeFee = nStorageFeePerMB * nDataSizeInMB;

    if (bIncludeTicketFee)
    {
        const CAmount nTicketFeePerKB = masterNodeCtrl.GetNetworkMedianMNFee(MN_FEE::TicketChainStorageFeePerKB);

        nSenseFee += nTicketFeePerKB * ACTION_SENSE_TICKET_SIZE_KB;
        nCascadeFee += nTicketFeePerKB * ACTION_CASCADE_TICKET_SIZE_KB;
    }

    if (bUseAdjustmentMultiplier)
    {
        const auto& consensusParams = Params().GetConsensus();
        const auto nGlobalFeeAdjustmentMultiplier = consensusParams.nGlobalFeeAdjustmentMultiplier;
        const double nFeeAdjustmentMultiplier = nGlobalFeeAdjustmentMultiplier * masterNodeCtrl.GetChainDeflatorFactor(nChainHeight);

        nSenseFee = static_cast<CAmount>(nSenseFee * nFeeAdjustmentMultiplier);
        nCascadeFee = static_cast<CAmount>(nCascadeFee * nFeeAdjustmentMultiplier);
    }

    feeMap.emplace(ACTION_TICKET_TYPE::SENSE, nSenseFee);
    feeMap.emplace(ACTION_TICKET_TYPE::CASCADE, nCascadeFee);

    return feeMap;
}
