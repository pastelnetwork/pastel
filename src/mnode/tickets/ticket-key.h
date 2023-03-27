#pragma once
// Copyright (c) 2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket.h>

class CTicketWithKey : public CPastelTicket
{
public:
    void Clear() noexcept override;

    std::string KeyOne() const noexcept override { return m_keyOne; }
    void SetKeyOne(std::string &&sValue) override { m_keyOne = std::move(sValue); }
    void GenerateKeyOne() override;

protected:
    std::string m_keyOne;        // key #1 (primary)
    std::string m_label;         // label
};
