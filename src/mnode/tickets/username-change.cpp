// Copyright (c) 2018-2024 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <utils/str_utils.h>
#include <init.h>
#include <pastelid/common.h>
#include <mnode/tickets/username-change.h>
#include <mnode/ticket-processor.h>
#include <mnode/ticket-mempool-processor.h>
#include <mnode/mnode-controller.h>
#include <mnode/mnode-badwords.h>
#include <wallet/wallet.h>

using json = nlohmann::json;
using namespace std;

/**
 * Create UserName-Change ticket.
 * 
 * \param sPastelID - Pastel ID the user should be associated with
 * \param sUserName - User name
 * \param strKeyPass - secure passphrase to access secure container associated with the Pastel ID
 * 
 * \return created UserName-Change ticket
 */
CChangeUsernameTicket CChangeUsernameTicket::Create(string &&sPastelID, string &&sUserName, SecureString&& strKeyPass)
{
    CChangeUsernameTicket ticket(std::move(sPastelID), std::move(sUserName));

    // Check if Pastel ID already have a username on the blockchain.
    if (!masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(ticket)) {
        // if Pastel ID has no Username yet, the fee is 100 PSL
        ticket.m_fee = masterNodeCtrl.MasternodeUsernameFirstChangeFee;
    } else {
        // if Pastel ID changed Username before, fee should be 5000 PSL
        ticket.m_fee = masterNodeCtrl.MasternodeUsernameChangeAgainFee;
    }

    ticket.GenerateTimestamp();

    const auto strTicket = ticket.ToStr();
    ticket.m_signature = string_to_vector(CPastelID::Sign(strTicket, ticket.m_sPastelID, std::move(strKeyPass)));

    return ticket;
}

/**
 * Set username-change ticket signature.
 * 
 * \param sSignature - signature in a string format
 */
void CChangeUsernameTicket::set_signature(const std::string& sSignature)
{
    m_signature = string_to_vector(sSignature);
}

// CChangeUsernameTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Get json representation of the ticket.
 * 
 * \param bDecodeProperties - not used in this class
 * \return json object
 */
json CChangeUsernameTicket::getJSON(const bool bDecodeProperties) const noexcept
{
    const json jsonObj
    {
        { "txid", m_txid },
        { "height", static_cast<int32_t>(m_nBlock) },
        { "tx_info", get_txinfo_json() },
        { "ticket", 
            {
                { "type", GetTicketName() }, 
                { "version", GetStoredVersion() },
                { "pastelID", m_sPastelID },
                { "username", m_sUserName },
                { "fee", m_fee },
                { "signature", ed_crypto::Hex_Encode(m_signature.data(), m_signature.size()) }
            }
        }
    };
    return jsonObj;
}

/**
 * Get json string representation of the ticket.
 * 
 * \param bDecodeProperties - not used in this class
 * \return json string
 */
string CChangeUsernameTicket::ToJSON(const bool bDecodeProperties) const noexcept
{
    return getJSON(bDecodeProperties).dump(4);
}

string CChangeUsernameTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << m_sPastelID;
    ss << m_sUserName;
    ss << m_fee;
    ss << m_nTimestamp;
    return ss.str();
}

/**
 * Check if username change ticket is valid.
 * Possible call stack:
 *    "tickets register username" RPC -> SendTicket (bPreReg=true)
 *    ProcessNewBlock->AcceptBlock-+
 *                                 |
 *    TestBlockValidity------------+--->ContextualCheckBlock->ContextualCheckTransaction->ValidateIfTicketTransaction
 * 
 * \param bPreReg - ticket pre-registration
 * \param nCallDepth - current function call depth
 * \param pindexPrev - previous block index
 * \return true if ticket is valid
 */
ticket_validation_t CChangeUsernameTicket::IsValid(const TxOrigin txOrigin, const uint32_t nCallDepth, const CBlockIndex *pindexPrev) const noexcept
{
    using namespace chrono;
    using namespace literals::chrono_literals;

    // username-change ticket keys:
    // 1) username
    // 2) pastelid
    const auto nActiveChainHeight = gl_nChainHeight + 1;

    // initialize Pastel Ticket mempool processor for username-change tickets
    // retrieve mempool transactions with TicketID::Username tickets
    CPastelTicketMemPoolProcessor TktMemPool(ID());
    TktMemPool.Initialize(mempool);

    CChangeUsernameTicket tktDB, tktMP;
    const bool bTicketExistsInDB = FindTicketInDb(m_sUserName, tktDB, pindexPrev);
    // true if the username-change ticket found in the mempool with the same PastelId as an existing DB ticket: tktDB
    // if ticket exists - it means that user already has changed username
    const bool bDBUserChangedName_MemPool = bTicketExistsInDB && TktMemPool.TicketExistsBySecondaryKey(tktDB.m_sPastelID);

    ticket_validation_t tv;
    do
    {
        const bool bPreReg = isPreReg(txOrigin);
        // These checks executed ONLY before ticket made into transaction
        if (bPreReg)
        {
            // search for username-change tickets in the mempool by username
            tktMP.Clear();
            tktMP.m_sUserName = m_sUserName;
            const bool bFoundTicketByUserName_MemPool = TktMemPool.FindTicket(tktMP);
            // Check if the username-change ticket for the same username is in mempool.
            if (bFoundTicketByUserName_MemPool)
            {
                tv.errorMsg = strprintf(
                    "Found '%s' ticket transaction for the same username in the memory pool. [Username=%s, txid=%s]",
                    TICKET_NAME_USERNAME_CHANGE, m_sUserName, tktMP.GetTxId());
                break;
            }

            // search for username-change tickets in the mempool by PastelId
            tktMP.Clear();
            tktMP.m_sPastelID = m_sPastelID;
            const bool bFoundTicketByPastelID_MemPool = TktMemPool.FindTicketBySecondaryKey(tktMP);
            // do not allow multiple username-change tickets with the same Pastel ID in the mempool
            if (bFoundTicketByPastelID_MemPool)
            {
                tv.errorMsg = strprintf(
                    "Found '%s' ticket transaction with the same Pastel ID in the memory pool. [Username=%s, txid=%s]",
                    TICKET_NAME_USERNAME_CHANGE, m_sUserName, tktMP.GetTxId());
                break;
            }

            // Check if the username is already registered in the blockchain.
            if (bTicketExistsInDB && masterNodeCtrl.masternodeTickets.getValueBySecondaryKey(tktDB) == m_sUserName)
            {
                // do not throw an error if the user with tktDB.pastelid has already changed username
                // username-change transaction was found in the mempool
                if (!bDBUserChangedName_MemPool)
                {
                    tv.errorMsg = strprintf(
                        "This Username is already registered in blockchain [Username=%s, txid=%s]", 
                        m_sUserName, tktDB.GetTxId());
                    break;
                }
            }
#ifdef ENABLE_WALLET
            if (isLocalPreReg(txOrigin))
            {
                // Check if address has coins to pay for Username Change Ticket
                const auto fullTicketPrice = TicketPricePSL(nActiveChainHeight);
                if (pwalletMain->GetBalance() < fullTicketPrice * COIN)
                {
                    tv.errorMsg = strprintf(
                        "Not enough coins to cover price [%" PRId64 " PSL]",
                        fullTicketPrice);
                    break;
                }
            }
#endif // ENABLE_WALLET
        }

        // Check if username is a bad username. For now check if it is empty only.
        string badUsernameError;
        if (isUsernameBad(m_sUserName, badUsernameError))
        {
            tv.errorMsg = std::move(badUsernameError);
            break;
        }

        // Verify signature
        // We will check that it is the correct Pastel ID
        const string strThisTicket = ToStr();
        if (!CPastelID::Verify(strThisTicket, vector_to_string(m_signature), m_sPastelID))
        {
            tv.errorMsg = strprintf(
                "%s ticket's signature is invalid. Pastel ID - [%s]", 
                GetTicketDescription(), m_sPastelID);
            break;
        }

        // ticket transaction replay attack protection
        if (bTicketExistsInDB && !bDBUserChangedName_MemPool && 
            (!tktDB.IsBlock(m_nBlock) || !tktDB.IsTxId(m_txid)) &&
            masterNodeCtrl.masternodeTickets.getValueBySecondaryKey(tktDB) == m_sUserName)
        {
            tv.errorMsg = strprintf(
                "This Username Change Request is already registered in blockchain [Username = %s] [%sfound ticket block=%u, txid=%s]",
                m_sUserName, 
                bPreReg ? "" : strprintf("this ticket block=%u, txid=%s; ", m_nBlock, m_txid),
                tktDB.GetBlock(), tktDB.GetTxId());
            break;
        }

        // Check whether this Pastel ID has changed Username in the last N blocks.
        tktDB.Clear();
        tktDB.m_sPastelID = m_sPastelID;
        // find username-change ticket in DB by Pastel ID
        const bool bFoundTicketByPastelID_DB = masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(tktDB);
        if (bFoundTicketByPastelID_DB)
        {
            const unsigned int height = (bPreReg || IsBlock(0)) ? nActiveChainHeight : m_nBlock;
            if (height <= tktDB.GetBlock() + CChangeUsernameTicket::GetDisablePeriodInBlocks())
            {
                string sTimeDiff;
                // If Pastel ID has changed Username in last 24 hours (~24*24 blocks), do not allow them to change (for mainnet & testnet)
                // For regtest - number of blocks is 10
                const seconds diffTime(time(nullptr) - tktDB.GetTimestamp());
                if (diffTime > 1h)
                    sTimeDiff = strprintf("%d hours", ceil<hours>(diffTime).count());
                else
                    sTimeDiff = strprintf("%d minutes", ceil<minutes>(diffTime).count());
                tv.errorMsg = strprintf(
                    "%s ticket is invalid. Already changed in last %s. Transaction with txid=%s is in blockchain. Pastel ID - [%s]",
                    GetTicketDescription(), sTimeDiff, tktDB.GetTxId(), m_sPastelID);
                break;
            }
        }

        // Check if ticket fee is valid
        const auto expectedFee = bFoundTicketByPastelID_DB ? masterNodeCtrl.MasternodeUsernameChangeAgainFee : masterNodeCtrl.MasternodeUsernameFirstChangeFee;
        if (m_fee != expectedFee)
        {
            tv.errorMsg = strprintf(
                "%s ticket's fee is invalid. Pastel ID - [%s], invalid fee - [%" PRId64 "], expected fee - [%" PRId64 "]",
                GetTicketDescription(), m_sPastelID, m_fee, expectedFee);
            break;
        }

        tv.setValid();
    } while (false);
    return tv;
}

/**
 * Find ChangeUserName ticket in DB.
 * 
 * \param key - username
 * \param ticket - found ticket
 * \param pindexPrev - previous block index
 * \return true if ticket is found
 */
bool CChangeUsernameTicket::FindTicketInDb(const string& key, CChangeUsernameTicket& ticket, const CBlockIndex *pindexPrev)
{
    ticket.m_sUserName = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket, pindexPrev);
}

ChangeUsernameTickets_t CChangeUsernameTicket::FindAllTicketByMVKey(const string& sMVKey, const CBlockIndex *pindexPrev)
{
    return masterNodeCtrl.masternodeTickets.FindTicketsByMVKey<CChangeUsernameTicket>(sMVKey, pindexPrev);
}

bool CChangeUsernameTicket::isUsernameBad(const string& username, string& error)
{
    // Check if has only <4, or has more than 12 characters
    if ((username.size() < 4) || (username.size() > 12))
    {
        error = "Invalid size of username, the size should have at least 4 characters, and at most 12 characters";
        return true;
    }

    // Check if doesn't start with letters.
    if (!isalphaex(username.front())) {
        error = "Invalid username, should start with a letter A-Z or a-z only";
        return true;
    }
    // Check if contains characters that is different than upper and lowercase Latin characters and numbers
    if (!all_of(username.begin(), username.end(), [&](unsigned char c)
        {
            return (isalphaex(c) || isdigitex(c));
        })) {
        error = "Invalid username, should contains letters A-Z a-z, or digits 0-9 only";
        return true;
    }
    // Check if contains bad words (swear, racist,...)
    string lowercaseUsername = username;
    lowercase(lowercaseUsername);
    for (const auto& elem : UsernameBadWords::Singleton().wordSet)
    {
        if (lowercaseUsername.find(elem) != string::npos)
        {
            error = "Invalid username, should NOT contains swear, racist... words";
            return true;
        }
    }

    return false;
}
