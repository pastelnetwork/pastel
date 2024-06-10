// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include <experimental_features.h>
#include <utils/util.h>
#include <main.h>

using namespace std;

optional<string> InitExperimentalFeatures()
{
    // Set this early so that experimental features are correctly enabled/disabled
    fExperimentalMode = GetBoolArg("-experimentalfeatures", false);

    // Fail early if user has set experimental options without the global flag
    if (!fExperimentalMode)
    {
        if (mapArgs.count("-developerencryptwallet"))
        {
            return translate("Wallet encryption requires -experimentalfeatures.");
        }
        else if (mapArgs.count("-paymentdisclosure"))
        {
            return translate("Payment disclosure requires -experimentalfeatures.");
        }
        else if (mapArgs.count("-zmergetoaddress"))
        {
            return translate("RPC method z_mergetoaddress requires -experimentalfeatures.");
        }
        else if (mapArgs.count("-savesproutr1cs"))
        {
            return translate("Saving the Sprout R1CS requires -experimentalfeatures.");
        }
    }
    return nullopt;
}
