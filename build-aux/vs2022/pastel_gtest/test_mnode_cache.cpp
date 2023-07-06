// Copyright (c) 2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <mnode/mnode-manager.h>
#include <mnode/mnode-payments.h>
#include <mnode/mnode-db.h>

#include <pastel_gtest_main.h>

using namespace std;

TEST(mnode_cache, load)
{
    //SelectParams(ChainNetwork::TESTNET);
    //gl_pPastelTestEnv->SetTempDataDir("c:\\temp\\");

    //CMasternodeMan masternodeManager;
    //string strErrors;
    // CFlatDB<CMasternodeMan> flatDB1(MNCACHE_FILENAME, "magicMasternodeCache");
    // EXPECT_TRUE(flatDB1.Load(masternodeManager)) << _("Failed to load masternode cache from") + "\n" + flatDB1.getFilePath();

    //CMasternodePayments mnPayments;
    //CFlatDB<CMasternodePayments> flatDB(MNPAYMENTS_CACHE_FILENAME, MNPAYMENTS_CACHE_MAGIC_STR);
    //EXPECT_TRUE(flatDB.Load(mnPayments)) << _("Failed to load masternode payments cache from") + "\n" + flatDB.getFilePath();
}

