// Copyright (c) 2018-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/tickets/ticket-key.h>

void CTicketWithKey::Clear() noexcept
{
    CPastelTicket::Clear();
    m_keyOne.clear();
    m_label.clear();
}

/**
 * Generate unique random key #1.
 */
void CTicketWithKey::GenerateKeyOne()
{
    m_keyOne = generateRandomBase32Str(RANDOM_KEY_BASE_LENGTH);
}

