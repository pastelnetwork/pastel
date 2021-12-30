#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <vector>
#include <amount.h>

enum class TrimmeanErrorNumber {
    ENOERROR = 0,
    EBADPCNT,
    EBADINPUT
};
// calculate the mean of the interior of a data set
double TRIMMEAN(const std::vector<CAmount>&, const double percent, TrimmeanErrorNumber* pErrNo = nullptr);

