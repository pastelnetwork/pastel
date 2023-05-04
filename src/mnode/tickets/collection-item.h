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
    bool IsCollectionItem() const noexcept { return !m_sCollectionActTxid.empty(); }
    // count number of items in the collection
    virtual uint32_t CountItemsInCollection() const = 0;
    // retrieve referred collection activate ticket
    virtual std::unique_ptr<CPastelTicket> RetrieveCollectionActivateTicket(std::string& error, bool& bInvalidTxId) const noexcept;

protected:
    std::string m_sCollectionActTxid; // txid of the collection activation ticket - can be empty for the simple item
    std::string m_sCreatorPastelID;   // Pastel ID of the collection item creator

    // validate referred collection
    ticket_validation_t IsValidCollection(const bool bPreReg) const noexcept;
};
