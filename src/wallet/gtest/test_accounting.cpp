// Copyright (c) 2012-2014 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdint.h>

#include <gtest/gtest.h>

#include "wallet/db.h"
#include "wallet/wallet.h"

using namespace std;
using namespace testing;


static void
GetResults(CWallet* wallet, CWalletDB& walletdb, map<CAmount, CAccountingEntry>& results)
{
    list<CAccountingEntry> aes;

    results.clear();
    EXPECT_EQ(walletdb.ReorderTransactions(wallet) , DB_LOAD_OK);
    walletdb.ListAccountCreditDebit("", aes);
    for (const auto& ae : aes)
        results[ae.nOrderPos] = ae;
}

class TestAccounting : public Test
{
public:
    void SetUp() override
    {
        pathTemp = fs::temp_directory_path() / fs::unique_path();
        EXPECT_TRUE(fs::create_directories(pathTemp));

        m_sSavedDataDir = mapArgs["-datadir"];
    }

    void TearDown() 
    {   
        fs::remove_all(pathTemp);

        mapArgs["-datadir"] = m_sSavedDataDir;
    }
    fs::path pathTemp;
    string m_sSavedDataDir;
};

TEST_F(TestAccounting, acc_orderupgrade)
{
    SelectParams(CBaseChainParams::Network::TESTNET);
    mapArgs["-datadir"] = pathTemp.string();
    bool fFirstRun;
    CWallet wallet("wallet_crypted_sapling.dat");
    EXPECT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));
    // RegisterValidationInterface(&wallet);

    CWalletDB walletdb(wallet.strWalletFile);
    vector<CWalletTx*> vpwtx;
    CWalletTx wtx;
    CAccountingEntry ae;
    map<CAmount, CAccountingEntry> results;

    LOCK(wallet.cs_wallet);

    ae.strAccount = "";
    ae.nCreditDebit = 1;
    ae.nTime = 1333333333;
    ae.strOtherAccount = "b";
    ae.strComment = "";
    walletdb.WriteAccountingEntry(ae);

    wtx.mapValue["comment"] = "z";
    wallet.AddToWallet(wtx, false, &walletdb);
    vpwtx.push_back(&wallet.mapWallet[wtx.GetHash()]);
    vpwtx[0]->nTimeReceived = (unsigned int)1333333335;
    vpwtx[0]->nOrderPos = -1;

    ae.nTime = 1333333336;
    ae.strOtherAccount = "c";
    walletdb.WriteAccountingEntry(ae);

    GetResults(&wallet, walletdb, results);

    EXPECT_EQ(wallet.nOrderPosNext , 3);
    EXPECT_EQ(2u , results.size());
    EXPECT_EQ(results[0].nTime , 1333333333);
    EXPECT_TRUE(results[0].strComment.empty());
    EXPECT_EQ(1u , vpwtx[0]->nOrderPos);
    EXPECT_EQ(results[2].nTime , 1333333336u);
    EXPECT_EQ(results[2].strOtherAccount , "c");


    ae.nTime = 1333333330;
    ae.strOtherAccount = "d";
    ae.nOrderPos = wallet.IncOrderPosNext();
    walletdb.WriteAccountingEntry(ae);

    GetResults(&wallet, walletdb, results);

    EXPECT_EQ(results.size() , 3u);
    EXPECT_EQ(wallet.nOrderPosNext , 4u);
    EXPECT_EQ(results[0].nTime , 1333333333u);
    EXPECT_EQ(1u , vpwtx[0]->nOrderPos);
    EXPECT_EQ(results[2].nTime , 1333333336u);
    EXPECT_EQ(results[3].nTime , 1333333330u);
    EXPECT_TRUE(results[3].strComment.empty());


    wtx.mapValue["comment"] = "y";
    {
        CMutableTransaction tx(wtx);
        --tx.nLockTime;  // Just to change the hash :)
        *static_cast<CTransaction*>(&wtx) = CTransaction(tx);
    }
    wallet.AddToWallet(wtx, false, &walletdb);
    vpwtx.push_back(&wallet.mapWallet[wtx.GetHash()]);
    vpwtx[1]->nTimeReceived = (unsigned int)1333333336;

    wtx.mapValue["comment"] = "x";
    {
        CMutableTransaction tx(wtx);
        --tx.nLockTime;  // Just to change the hash :)
        *static_cast<CTransaction*>(&wtx) = CTransaction(tx);
    }
    wallet.AddToWallet(wtx, false, &walletdb);
    vpwtx.push_back(&wallet.mapWallet[wtx.GetHash()]);
    vpwtx[2]->nTimeReceived = (unsigned int)1333333329;
    vpwtx[2]->nOrderPos = -1;

    GetResults(&wallet, walletdb, results);

    EXPECT_EQ(results.size() , 3u);
    EXPECT_EQ(wallet.nOrderPosNext , 6u);
    EXPECT_EQ(0u , vpwtx[2]->nOrderPos);
    EXPECT_EQ(results[1].nTime , 1333333333u);
    EXPECT_EQ(2u , vpwtx[0]->nOrderPos);
    EXPECT_EQ(results[3].nTime , 1333333336u);
    EXPECT_EQ(results[4].nTime , 1333333330u);
    EXPECT_TRUE(results[4].strComment.empty());
    EXPECT_EQ(5u , vpwtx[1]->nOrderPos);


    ae.nTime = 1333333334;
    ae.strOtherAccount = "e";
    ae.nOrderPos = -1;
    walletdb.WriteAccountingEntry(ae);

    GetResults(&wallet, walletdb, results);

    EXPECT_EQ(results.size() , 4u);
    EXPECT_EQ(wallet.nOrderPosNext , 7u);
    EXPECT_EQ(0u , vpwtx[2]->nOrderPos);
    EXPECT_EQ(results[1].nTime , 1333333333u);
    EXPECT_EQ(2u , vpwtx[0]->nOrderPos);
    EXPECT_EQ(results[3].nTime , 1333333336u);
    EXPECT_TRUE(results[3].strComment.empty());
    EXPECT_EQ(results[4].nTime , 1333333330u);
    EXPECT_TRUE(results[4].strComment.empty());
    EXPECT_EQ(results[5].nTime , 1333333334u);
    EXPECT_EQ(6u , vpwtx[1]->nOrderPos);

    //  UnregisterValidationInterface(&wallet);
}
