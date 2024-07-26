// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <boost/bind/bind.hpp>

#include <validationinterface.h>

using namespace boost::placeholders;
using namespace std;

static CMainSignals g_signals;

CMainSignals& GetMainSignals()
{
    return g_signals;
}

void RegisterValidationInterface(CValidationInterface* pwalletIn)
{
    g_signals.AcceptedBlockHeader.connect(boost::bind(&CValidationInterface::AcceptedBlockHeader, pwalletIn, _1));
    g_signals.NotifyHeaderTip.connect(boost::bind(&CValidationInterface::NotifyHeaderTip, pwalletIn, _1, _2));
    g_signals.UpdatedBlockTip.connect(boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, _1, _2));
    g_signals.SyncTransaction.connect(boost::bind(&CValidationInterface::SyncTransaction, pwalletIn, _1, _2));
    g_signals.EraseTransaction.connect(boost::bind(&CValidationInterface::EraseFromWallet, pwalletIn, _1));
    g_signals.UpdatedTransaction.connect(boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, _1));
    g_signals.ChainTip.connect(boost::bind(&CValidationInterface::ChainTip, pwalletIn, _1, _2, _3, _4));
    g_signals.SetBestChain.connect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, _1));
    g_signals.Inventory.connect(boost::bind(&CValidationInterface::Inventory, pwalletIn, _1));
    g_signals.Broadcast.connect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn, _1));
    g_signals.BlockChecked.connect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, _1, _2));
}

void UnregisterValidationInterface(CValidationInterface* pwalletIn)
{
    g_signals.BlockChecked.disconnect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, _1, _2));
    g_signals.Broadcast.disconnect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn, _1));
    g_signals.Inventory.disconnect(boost::bind(&CValidationInterface::Inventory, pwalletIn, _1));
    g_signals.ChainTip.disconnect(boost::bind(&CValidationInterface::ChainTip, pwalletIn, _1, _2, _3, _4));
    g_signals.SetBestChain.disconnect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, _1));
    g_signals.UpdatedTransaction.disconnect(boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, _1));
    g_signals.EraseTransaction.disconnect(boost::bind(&CValidationInterface::EraseFromWallet, pwalletIn, _1));
    g_signals.SyncTransaction.disconnect(boost::bind(&CValidationInterface::SyncTransaction, pwalletIn, _1, _2));
    g_signals.UpdatedBlockTip.disconnect(boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, _1, _2));
    g_signals.NotifyHeaderTip.disconnect(boost::bind(&CValidationInterface::NotifyHeaderTip, pwalletIn, _1, _2));
    g_signals.AcceptedBlockHeader.disconnect(boost::bind(&CValidationInterface::AcceptedBlockHeader, pwalletIn, _1));
}

void UnregisterAllValidationInterfaces()
{
    g_signals.BlockChecked.disconnect_all_slots();
    g_signals.Broadcast.disconnect_all_slots();
    g_signals.Inventory.disconnect_all_slots();
    g_signals.ChainTip.disconnect_all_slots();
    g_signals.SetBestChain.disconnect_all_slots();
    g_signals.UpdatedTransaction.disconnect_all_slots();
    g_signals.EraseTransaction.disconnect_all_slots();
    g_signals.SyncTransaction.disconnect_all_slots();
    g_signals.UpdatedBlockTip.disconnect_all_slots();
    g_signals.NotifyHeaderTip.disconnect_all_slots();
    g_signals.AcceptedBlockHeader.disconnect_all_slots();
}

void SyncWithWallets(const CTransaction &tx, const CBlock *pblock)
{
    g_signals.SyncTransaction(tx, pblock);
}
