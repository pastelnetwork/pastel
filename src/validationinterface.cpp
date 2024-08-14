// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>

#include <validationinterface.h>

using namespace std;

enum class MainSignalType
{
	AcceptedBlockHeader,
	NotifyHeaderTip,
	UpdatedBlockTip,
	SyncTransaction,
	EraseTransaction,
	UpdatedTransaction,
	ChainTip,
	SetBestChain,
	Inventory,
	Broadcast,
	BlockChecked
};

using signal_connection_map_t = std::unordered_map<MainSignalType, boost::signals2::connection>;

static mutex g_signal_connections_mutex;
static unordered_map<CValidationInterface*, signal_connection_map_t> g_signal_connections;
static CMainSignals g_signals;

CMainSignals& GetMainSignals()
{
    return g_signals;
}

void RegisterValidationInterface(CValidationInterface* pwalletIn)
{
	if (!pwalletIn)
		throw runtime_error("RegisterValidationInterface: null wallet");

	unique_lock lock(g_signal_connections_mutex);
	auto pConn = g_signals.AcceptedBlockHeader.connect([pwalletIn](const CBlockIndex* pindexNew)
    {
        pwalletIn->AcceptedBlockHeader(pindexNew);
	});
	g_signal_connections[pwalletIn][MainSignalType::AcceptedBlockHeader] = std::move(pConn);

    pConn = g_signals.NotifyHeaderTip.connect([pwalletIn](const CBlockIndex* pindexNew, bool fInitialDownload)
	{
		pwalletIn->NotifyHeaderTip(pindexNew, fInitialDownload);
	});
	g_signal_connections[pwalletIn][MainSignalType::NotifyHeaderTip] = std::move(pConn);

    pConn = g_signals.UpdatedBlockTip.connect([pwalletIn](const CBlockIndex* pindex, bool fInitialDownload)
    {
        pwalletIn->UpdatedBlockTip(pindex, fInitialDownload);
	});
	g_signal_connections[pwalletIn][MainSignalType::UpdatedBlockTip] = std::move(pConn);

    pConn = g_signals.SyncTransaction.connect([pwalletIn](const CTransaction& tx, const CBlock* pblock)
	{
		pwalletIn->SyncTransaction(tx, pblock);
	});
	g_signal_connections[pwalletIn][MainSignalType::SyncTransaction] = std::move(pConn);

    pConn = g_signals.EraseTransaction.connect([pwalletIn](const uint256& hash)
    {
        pwalletIn->EraseFromWallet(hash);
	});
	g_signal_connections[pwalletIn][MainSignalType::EraseTransaction] = std::move(pConn);

    pConn = g_signals.UpdatedTransaction.connect([pwalletIn](const uint256& hash)
	{
		pwalletIn->UpdatedTransaction(hash);
    });
	g_signal_connections[pwalletIn][MainSignalType::UpdatedTransaction] = std::move(pConn);

    pConn = g_signals.ChainTip.connect([pwalletIn](const CBlockIndex* pindex, const CBlock* pblock, SaplingMerkleTree saplingTree, bool bAdded)
	{
		pwalletIn->ChainTip(pindex, pblock, saplingTree, bAdded);
	});
	g_signal_connections[pwalletIn][MainSignalType::ChainTip] = std::move(pConn);

    pConn = g_signals.SetBestChain.connect([pwalletIn](const CBlockLocator& locator)
	{
		pwalletIn->SetBestChain(locator);
	});
	g_signal_connections[pwalletIn][MainSignalType::SetBestChain] = std::move(pConn);

    pConn = g_signals.Inventory.connect([pwalletIn](const uint256& hash)
	{
		pwalletIn->Inventory(hash);
	});
	g_signal_connections[pwalletIn][MainSignalType::Inventory] = std::move(pConn);

    pConn = g_signals.Broadcast.connect([pwalletIn](int64_t nBestBlockTime)
	{
		pwalletIn->ResendWalletTransactions(nBestBlockTime);
	});
	g_signal_connections[pwalletIn][MainSignalType::Broadcast] = std::move(pConn);

    pConn = g_signals.BlockChecked.connect([pwalletIn](const CBlock& block, const CValidationState& state)
	{
		pwalletIn->BlockChecked(block, state);
	});
	g_signal_connections[pwalletIn][MainSignalType::BlockChecked] = std::move(pConn);
}

void UnregisterValidationInterface(CValidationInterface* pwalletIn)
{
	if (!pwalletIn)
		throw runtime_error("UnregisterValidationInterface: null wallet");

	unique_lock lock(g_signal_connections_mutex);

	auto it = g_signal_connections.find(pwalletIn);
	if (it != g_signal_connections.end())
	{
		// Disconnect all signals associated with this validation interface
		for (auto& [signalType, connection] : it->second)
		{
			connection.disconnect();
		}

		g_signal_connections.erase(it);
	}
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
