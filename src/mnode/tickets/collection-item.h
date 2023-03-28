#pragma once
// Copyright (c) 2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <mnode/tickets/ticket.h>
#include <mnode/tickets/ticket-key.h>

class CollectionItem : public CTicketWithKey
{
public:
    void Clear() noexcept override;

    // getters for ticket fields
    std::string getCollectionActTxId() const noexcept { return m_sCollectionActTxid; }
    std::string getCreatorPastelID_param() const noexcept { return m_sCreatorPastelID; }

protected:
    std::string m_sCollectionActTxid; // txid of the collection activation ticket - can be empty for the simple item
    std::string m_sCreatorPastelID;   // Pastel ID of the collection item creator

    // retrieve referred collection activate ticket
    virtual std::unique_ptr<CPastelTicket> RetrieveCollectionActivateTicket(std::string& error, bool& bInvalidTxId) const noexcept;
    // retrieve referred collection registration ticket
    static std::unique_ptr<CPastelTicket> RetrieveCollectionRegTicket(std::string& error, const std::string& sRegTxId, bool& bInvalidTxId) noexcept;
    // get collection ticket by txid
    static std::unique_ptr<CPastelTicket> GetCollectionTicket(const uint256& txid);
    // validate referred collection
    ticket_validation_t IsValidCollection(const bool bPreReg) const noexcept;
    // count number of items in the collection
    virtual uint32_t CountItemsInCollection(const uint32_t currentChainHeight) const = 0;
};
