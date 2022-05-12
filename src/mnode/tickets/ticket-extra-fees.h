#pragma once
// Copyright (c) 2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <mnode/tickets/ticket.h>
#include <mnode/tickets/ticket_signing.h>

class CTicketSignedWithExtraFees :
    public CPastelTicket,
    public CTicketSigning
{
public:
    CTicketSignedWithExtraFees() = default;

    void Clear() noexcept override;

    std::string KeyOne() const noexcept override { return m_keyOne; }
    void SetKeyOne(std::string &&sValue) override { m_keyOne = std::move(sValue); }
    void GenerateKeyOne() override;

    static CAmount GreenPercent(const unsigned int nHeight) { return GREEN_FEE_PERCENT; }
    static std::string GreenAddress(const unsigned int nHeight);

    // getters for ticket fields
    float getRoyalty() const noexcept { return m_nRoyalty; }
    CAmount getStorageFee() const noexcept { return m_storageFee; }
    std::string getGreenAddress() const noexcept { return m_sGreenAddress; }
    bool hasGreenFee() const noexcept { return !m_sGreenAddress.empty(); }
    uint32_t getCreatorHeight() const noexcept { return m_nCreatorHeight; }

    // get Pastel ID to pay royalty fee
    std::string GetRoyaltyPayeePastelID() const;
    // get royalty payee address
    std::string GetRoyaltyPayeeAddress() const;
    // check that fees are valid
    bool ValidateFees(std::string& error) const noexcept;

protected:
    uint32_t m_nCreatorHeight{}; // blocknum when the ticket was created by the wallet

    std::string m_keyOne;        // key #1 (primary)
    std::string m_label;         // label
    CAmount m_storageFee{};      // ticket storage fee in PSL

    float m_nRoyalty{0.0f};      // how much creator(s) should get on all future resales
    std::string m_sGreenAddress; // if not empty - Green NFT payment address
};