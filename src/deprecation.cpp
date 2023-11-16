// Copyright (c) 2017 The Zcash developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <utils/util.h>
#include <deprecation.h>
#include <alert.h>
#include <clientversion.h>
#include <init.h>
#include <ui_interface.h>
#include <chainparams.h>

using namespace std;

static const string CLIENT_VERSION_STR = FormatVersion(CLIENT_VERSION);

void EnforceNodeDeprecation(int nHeight, bool forceLogging, bool fThread)
{

    // Do not enforce deprecation in regtest or on testnet
    const auto &params = Params();
    if (!params.IsMainNet())
        return;

    const int blocksToDeprecation = DEPRECATION_HEIGHT - nHeight;
    if (blocksToDeprecation <= 0)
    {
        // In order to ensure we only log once per process when deprecation is
        // disabled (to avoid log spam), we only need to log in two cases:
        // - The deprecating block just arrived
        //   - This can be triggered more than once if a block chain reorg
        //     occurs, but that's an irregular event that won't cause spam.
        // - The node is starting
        if (blocksToDeprecation == 0 || forceLogging) {
            auto msg = strprintf(translate("This version has been deprecated as of block height %d."),
                                 DEPRECATION_HEIGHT) + " " +
                       translate("You should upgrade to the latest version of Pastel.");
            LogPrintf("*** %s\n", msg);
            CAlert::Notify(msg, fThread);
            uiInterface.ThreadSafeMessageBox(msg, "", CClientUIInterface::MSG_ERROR);
        }
        StartShutdown();
    } else if (blocksToDeprecation == DEPRECATION_WARN_LIMIT ||
               (blocksToDeprecation < DEPRECATION_WARN_LIMIT && forceLogging)) {
        string msg = strprintf(translate("This version will be deprecated at block height %d, and will automatically shut down."),
                            DEPRECATION_HEIGHT) + " " +
                  translate("You should upgrade to the latest version of Pastel.");
        LogPrintf("*** %s\n", msg);
        CAlert::Notify(msg, fThread);
        uiInterface.ThreadSafeMessageBox(msg, "", CClientUIInterface::MSG_WARNING);
    }
}
