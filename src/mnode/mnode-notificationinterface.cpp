// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "chainparams.h"

#include "mnode/mnode-notificationinterface.h"
#include "mnode/mnode-controller.h"

void CACNotificationInterface::InitializeCurrentBlockTip()
{
    LOCK(cs_main);
    UpdatedBlockTip(chainActive.Tip(), IsInitialBlockDownload(Params().GetConsensus()));
}

void CACNotificationInterface::AcceptedBlockHeader(const CBlockIndex *pindexNew)
{
    masterNodeCtrl.masternodeSync.AcceptedBlockHeader(pindexNew);
}

void CACNotificationInterface::NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload)
{
    masterNodeCtrl.masternodeSync.NotifyHeaderTip(pindexNew, fInitialDownload);
}

void CACNotificationInterface::UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload)
{
    masterNodeCtrl.masternodeSync.UpdatedBlockTip(pindexNew, fInitialDownload);
	masterNodeCtrl.masternodeTickets.UpdatedBlockTip(pindexNew, fInitialDownload);
	
	if (fInitialDownload) {
		return;
	}
	
	masterNodeCtrl.masternodeManager.UpdatedBlockTip(pindexNew);
	masterNodeCtrl.masternodePayments.UpdatedBlockTip(pindexNew);
	masterNodeCtrl.masternodeGovernance.UpdatedBlockTip(pindexNew);
}
