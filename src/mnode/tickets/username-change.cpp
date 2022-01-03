// Copyright (c) 2018-2021 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <inttypes.h>
#include <json/json.hpp>

#include <str_utils.h>
#include <init.h>
#include <pastelid/common.h>
#include <mnode/tickets/username-change.h>
#include <mnode/ticket-processor.h>
#include <mnode/ticket-mempool-processor.h>
#include <mnode/mnode-controller.h>
#include <mnode/mnode-badwords.h>

using json = nlohmann::json;
using namespace std;

// CChangeUsernameTicket ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
string CChangeUsernameTicket::ToJSON() const noexcept
{
    const json jsonObj
    {
        {"txid", m_txid},
        {"height", m_nBlock},
        {"ticket", 
            {
                {"type", GetTicketName()}, 
                {"pastelID", pastelID},
                {"username", username},
                {"fee", fee},
                {"signature", ed_crypto::Hex_Encode(signature.data(), signature.size())}
            }
        }
    };

    return jsonObj.dump(4);
}

string CChangeUsernameTicket::ToStr() const noexcept
{
    stringstream ss;
    ss << pastelID;
    ss << username;
    ss << fee;
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
 * \param nDepth
 * \return true if ticket is valid
 */
bool CChangeUsernameTicket::IsValid(const bool bPreReg, const int nDepth) const
{
    using namespace chrono;
    using namespace literals::chrono_literals;

    // username-change ticket keys:
    // 1) username
    // 2) pastelid
    const unsigned int chainHeight = GetActiveChainHeight();

    // initialize Pastel Ticket mempool processor for username-change tickets
    // retrieve mempool transactions with TicketID::Username tickets
    CPastelTicketMemPoolProcessor TktMemPool(ID());
    TktMemPool.Initialize(mempool);

    CChangeUsernameTicket tktDB, tktMP;
    const bool bTicketExistsInDB = FindTicketInDb(username, tktDB);
    // true if the username-change ticket found in the mempool with the same PastelId as an existing DB ticket: tktDB
    // if ticket exists - it means that user already has changed username
    const bool bDBUserChangedName_MemPool = bTicketExistsInDB && TktMemPool.TicketExistsBySecondaryKey(tktDB.pastelID);

    // These checks executed ONLY before ticket made into transaction
    if (bPreReg)
    {
        // search for username-change tickets in the mempool by username
        tktMP.Clear();
        tktMP.username = username;
        const bool bFoundTicketByUserName_MemPool = TktMemPool.FindTicket(tktMP);
        // Check if the username-change ticket for the same username is in mempool.
        if (bFoundTicketByUserName_MemPool)
            throw runtime_error(strprintf("Found '%s' ticket transaction for the same username in the memory pool. [Username=%s, txid=%s]",
                                          TICKET_NAME_USERNAME_CHANGE, username, tktMP.GetTxId()));

        // search for username-change tickets in the mempool by PastelId
        tktMP.Clear();
        tktMP.pastelID = pastelID;
        const bool bFoundTicketByPastelID_MemPool = TktMemPool.FindTicketBySecondaryKey(tktMP);
        // do not allow multiple username-change tickets with the same PastelID in the mempool
        if (bFoundTicketByPastelID_MemPool)
            throw runtime_error(strprintf("Found '%s' ticket transaction with the same PastelID in the memory pool. [Username=%s, txid=%s]",
                                          TICKET_NAME_USERNAME_CHANGE, username, tktMP.GetTxId()));

        // Check if the username is already registered in the blockchain.
        if (bTicketExistsInDB && masterNodeCtrl.masternodeTickets.getValueBySecondaryKey(tktDB) == username)
        {
            // do not throw an error if the user with tktDB.pastelid has already changed username
            // username-change transaction was found in the mempool
            if (!bDBUserChangedName_MemPool)
                throw runtime_error(strprintf("This Username is already registered in blockchain [Username=%s, txid=%s]", username, tktDB.GetTxId()));
        }

        // Check if address has coins to pay for Username Change Ticket
        const auto fullTicketPrice = TicketPrice(chainHeight);

        if (pwalletMain->GetBalance() < fullTicketPrice * COIN)
            throw runtime_error(strprintf("Not enough coins to cover price [%" PRId64 "]", fullTicketPrice));
    }

    // Check if username is a bad username. For now check if it is empty only.
    string badUsernameError;
    if (isUsernameBad(username, badUsernameError))
        throw runtime_error(badUsernameError.c_str());

    // Verify signature
    // We will check that it is the correct PastelID
    string strThisTicket = ToStr();
    if (!CPastelID::Verify(strThisTicket, vector_to_string(signature), pastelID))
        throw runtime_error(strprintf("%s ticket's signature is invalid. PastelID - [%s]", GetTicketDescription(TicketID::Username), pastelID));

    // ticket transaction replay attack protection
    if (bTicketExistsInDB && !bDBUserChangedName_MemPool && (!tktDB.IsBlock(m_nBlock) || !tktDB.IsTxId(m_txid)) &&
        masterNodeCtrl.masternodeTickets.getValueBySecondaryKey(tktDB) == username) {
        throw runtime_error(strprintf("This Username Change Request is already registered in blockchain [Username = %s]"
                                      "[this ticket block = %u, txid = %s; found ticket block  = %u, txid = %s]",
                                      username, tktDB.GetBlock(), tktDB.GetTxId(), GetBlock(), GetTxId()));
    }

    // Check whether this PastelID has changed Username in the last N blocks.
    tktDB.Clear();
    tktDB.pastelID = pastelID;
    // find username-change ticket in DB by Pastel ID
    const bool bFoundTicketByPastelID_DB = masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(tktDB);
    if (bFoundTicketByPastelID_DB)
    {
        const unsigned int height = (bPreReg || IsBlock(0)) ? chainHeight : m_nBlock;
        if (height <= tktDB.GetBlock() + CChangeUsernameTicket::GetDisablePeriodInBlocks())
        {
            string sTimeDiff;
            // If PastelID has changed Username in last 24 hours (~24*24 blocks), do not allow them to change (for mainnet & testnet)
            // For regtest - number of blocks is 10
            const seconds diffTime(time(nullptr) - tktDB.m_nTimestamp);
            if (diffTime > 1h)
                sTimeDiff = strprintf("%d hours", ceil<hours>(diffTime).count());
            else
                sTimeDiff = strprintf("%d minutes", ceil<minutes>(diffTime).count());
            throw runtime_error(strprintf("%s ticket is invalid. Already changed in last %s. Transaction with txid=%s is in blockchain. PastelID - [%s]",
                                          GetTicketDescription(TicketID::Username), sTimeDiff, tktDB.GetTxId(), pastelID));
        }
    }

    // Check if ticket fee is valid
    const auto expectedFee = bFoundTicketByPastelID_DB ? masterNodeCtrl.MasternodeUsernameChangeAgainFee : masterNodeCtrl.MasternodeUsernameFirstChangeFee;
    if (fee != expectedFee)
        throw runtime_error(strprintf("%s ticket's fee is invalid. PastelID - [%s], invalid fee - [%" PRId64 "], expected fee - [%" PRId64 "]",
                                      GetTicketDescription(TicketID::Username), pastelID, fee, expectedFee));
    return true;
}

CChangeUsernameTicket CChangeUsernameTicket::Create(string _pastelID, string _username, SecureString&& strKeyPass)
{
    CChangeUsernameTicket ticket(move(_pastelID), move(_username));

    // Check if PastelID already have a username on the blockchain.
    if (!masterNodeCtrl.masternodeTickets.CheckTicketExistBySecondaryKey(ticket)) {
        // IF PastelID has no Username yet, the fee is 100 PSL
        ticket.fee = masterNodeCtrl.MasternodeUsernameFirstChangeFee;
    } else {
        // IF PastelID changed Username before, fee should be 5000
        ticket.fee = masterNodeCtrl.MasternodeUsernameChangeAgainFee;
    }

    ticket.GenerateTimestamp();

    const auto strTicket = ticket.ToStr();
    ticket.signature = string_to_vector(CPastelID::Sign(strTicket, ticket.pastelID, move(strKeyPass)));

    return ticket;
}

bool CChangeUsernameTicket::FindTicketInDb(const string& key, CChangeUsernameTicket& ticket)
{
    ticket.username = key;
    return masterNodeCtrl.masternodeTickets.FindTicket(ticket);
}

bool CChangeUsernameTicket::isUsernameBad(const string& username, string& error)
{
    // Check if has only <4, or has more than 12 characters
    if ((username.size() < 4) || (username.size() > 12)) {
        error = "Invalid size of username, the size should have at least 4 characters, and at most 12 characters";
        return true;
    }

    // Check if doesn't start with letters.
    if (!isalphaex(username.front())) {
        error = "Invalid username, should start with a letter A-Z or a-z only";
        return true;
    }
    // Check if contains characters that is different than upper and lowercase Latin characters and numbers
    if (!all_of(username.begin(), username.end(), [&](unsigned char c) {
            return (isalphaex(c) || isdigitex(c));
        })) {
        error = "Invalid username, should contains letters A-Z a-z, or digits 0-9 only";
        return true;
    }
    // Check if contains bad words (swear, racist,...)
    string lowercaseUsername = username;
    lowercase(lowercaseUsername);
    for (const auto& elem : UsernameBadWords::Singleton().wordSet) {
        if (lowercaseUsername.find(elem) != string::npos) {
            error = "Invalid username, should NOT contains swear, racist... words";
            return true;
        }
    }

    return false;
}
