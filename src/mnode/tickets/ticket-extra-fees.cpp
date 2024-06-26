// Copyright (c) 2022-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <mnode/tickets/nft-royalty.h>
#include <mnode/tickets/pastelid-reg.h>
#include <mnode/tickets/ticket-extra-fees.h>
#include <mnode/mnode-controller.h>

using namespace std;

/**
* Get Pastel ID to pay royalty fee.
* 
* \param txid - ticket transaction id (txid)
* \return Pastel ID
*/
string CTicketSignedWithExtraFees::GetRoyaltyPayeePastelID(const string &txid) const
{
    if (!m_nRoyalty)
        return {};

    // get royalty tickets by txid 
    const auto vTickets = CNFTRoyaltyTicket::FindAllTicketByNFTTxID(txid);
    // find the ticket with max height
    const auto it = max_element(vTickets.cbegin(), vTickets.cend(),
        [&](const auto& ticketL, const auto& ticketR) -> bool
        { return ticketL.GetBlock() < ticketR.GetBlock(); });

    return it != vTickets.cend() ? it->getNewPastelID() : m_vPastelID[SIGN_PRINCIPAL];
}

/**
* Get royalty payee address.
* 
* \param txid - ticket transaction id (txid)
* \param pindexPrev - previous block index
* \return royalty payee address if royalty fee is defined or empty string
*/
string CTicketSignedWithExtraFees::GetRoyaltyPayeeAddress(const string &txid, const CBlockIndex *pindexPrev) const
{
    const string pastelID = GetRoyaltyPayeePastelID(txid);
    if (!pastelID.empty())
    {
        CPastelIDRegTicket ticket;
        if (CPastelIDRegTicket::FindTicketInDb(pastelID, ticket, pindexPrev))
            return ticket.getFundingAddress();
    }
    return {};
}

string CTicketSignedWithExtraFees::GreenAddress(const unsigned int nHeight)
{ 
    return masterNodeCtrl.TicketGreenAddress;
}

/**
 * Check that royalty and green fees are valid.
 * 
 * \param error - return error message in case fees are invalid
 * \return true if fees are valid, false - otherwise
 */
bool CTicketSignedWithExtraFees::ValidateFees(string& error) const noexcept
{
    bool bRet = false;
    do
    {
        if (m_nRoyalty < 0 || m_nRoyalty > MAX_ROYALTY)
        {
            error = strprintf(
                "Royalty can't be %hu percent, Min is 0 and Max is %hu percent",
                m_nRoyalty * 100, MAX_ROYALTY_PERCENT);
            break;
        }

        if (hasGreenFee())
        {
            KeyIO keyIO(Params());
            const auto dest = keyIO.DecodeDestination(m_sGreenAddress);
            if (!IsValidDestination(dest))
            {
                error = strprintf(
                    "The Green NFT address [%s] is invalid",
                    m_sGreenAddress);
                break;
            }
        }
        bRet = true;
    } while (false);

    return bRet;
}

void CTicketSignedWithExtraFees::clear_extra_fees() noexcept
{
    clear_signatures();
    m_storageFee = 0;
    m_nCreatorHeight = 0;
    m_nRoyalty = 0.0f;
    m_sGreenAddress.clear();
}
