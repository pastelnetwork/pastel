#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <mnode/tickets/ticket.h>

/**
 * Class to calculate MN fees for ticket.
 */
class CPastelTicketMNFee : public CPastelTicket
{
public:
    // structure to define MN fees percentage
    typedef struct _mn_fees_t
    {
        uint8_t all; // all MN fees percentage (from storage fee)
        uint8_t principalShare; // principal MN fee share percentage (from all MN fees)
        uint8_t otherShare; // other MNs fee share percentage (from all MN fees)
    } mn_fees_t;

    virtual ~CPastelTicketMNFee() = default;

    // get MN fees
    // with C++20 this can be rewritten as constexpr virtual and calculated at compile-time
    virtual mn_fees_t getMNFees() const noexcept = 0;

    // get all MN fees in PSL
    double getAllMNFeesPSL() const noexcept
    {
        return (static_cast<double>(GetStorageFee()) * getMNFees().all) / 100;
    }
    CAmount getAllMNFees() const noexcept { return static_cast<CAmount>(getAllMNFeesPSL() * COIN); }
    // get principal MN fee in PSL
    double getPrincipalMNFeePSL() const noexcept
    {
        return (getAllMNFeesPSL() * getMNFees().principalShare) / 100;
    }
    CAmount getPrincipalMNFee() const noexcept { return static_cast<CAmount>(getPrincipalMNFeePSL() * COIN); }

    // get other MNs fee in PSL
    double getOtherMNFeePSL() const noexcept
    {
        return getAllMNFeesPSL() * (static_cast<double>(getMNFees().otherShare) / 100);
    }
    CAmount getOtherMNFee() const noexcept { return static_cast<CAmount>(getOtherMNFeePSL() * COIN); }
};
