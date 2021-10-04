#pragma once
// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "primitives/transaction.h"

/** Coin Control Features. */
class CCoinControl
{
public:
    CTxDestination destChange;
    //! If false, allows unselected inputs, but requires all selected inputs be used
    bool fAllowOtherInputs;

    CCoinControl()
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        destChange = CNoDestination();
        fAllowOtherInputs = false;
        setSelected.clear();
    }

    bool HasSelected() const noexcept
    {
        return !setSelected.empty();
    }

    bool IsSelected(const uint256& hash, unsigned int n) const noexcept
    {
        COutPoint outpt(hash, n);
        return (setSelected.count(outpt) > 0);
    }

    void Select(const COutPoint& output)
    {
        setSelected.insert(output);
    }

    void UnSelect(const COutPoint& output)
    {
        setSelected.erase(output);
    }

    void UnSelectAll()
    {
        setSelected.clear();
    }

    void ListSelected(std::vector<COutPoint>& vOutpoints) const
    {
        vOutpoints.assign(setSelected.begin(), setSelected.end());
    }

private:
    std::set<COutPoint> setSelected;
};
