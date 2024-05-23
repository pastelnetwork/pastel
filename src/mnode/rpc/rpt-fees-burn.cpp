// Copyright (c) 2018-2023 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <unordered_set>
#include <unordered_map>
#include <numeric>

#include <key_io.h>
#include <script/standard.h>
#include <mnode/rpc/rpt-fees-burn.h>
#include <mnode/tickets/ticket-types.h>
#include <mnode/tickets/tickets-all.h>
#include <mnode/ticket-processor.h>
#include <mnode/mnode-controller.h>

using namespace std;

typedef struct _pastel_ticket_data_t
{
	uint256 txid;
	uint32_t nHeight;

	_pastel_ticket_data_t(const uint256& _txid, uint32_t nHeightIn) noexcept :
		txid(_txid),
		nHeight(nHeightIn)
	{}
} pastel_ticket_data_t;
using pastel_ticket_data_vector_t = std::vector<pastel_ticket_data_t>;
using pastel_ticket_data_map_t = std::unordered_map<uint256, uint32_t>;

constexpr size_t TICKET_DATA_VECTOR_RESERVE_SIZE = 1000;
constexpr size_t FEE_PAYING_TICKETS_RESERVE_SIZE = 400;
constexpr size_t TICKET_DATA_VECTOR_RESERVE_INCREMENT = 500;
constexpr size_t FEE_PAYING_TICKETS_RESERVE_INCREMENT = 200;

void collect_all_pastel_ticket_data(
	pastel_ticket_data_vector_t& vAllTicketData,
	pastel_ticket_data_map_t& vFeePayingTicketDataMap)
{
	static unordered_set<TicketID> FEE_PAYING_TICKETS =
		{ TicketID::Activate, TicketID::ActionActivate, TicketID::CollectionAct };

	vAllTicketData.clear();
	vFeePayingTicketDataMap.clear();
	vAllTicketData.reserve(TICKET_DATA_VECTOR_RESERVE_SIZE);
	vFeePayingTicketDataMap.reserve(FEE_PAYING_TICKETS_RESERVE_SIZE);

	for (const auto& ticketInfo : TICKET_INFO)
	{
		masterNodeCtrl.masternodeTickets.ProcessAllTickets(ticketInfo.id,
			[&](string &&sKey, const PastelTicketPtr &ticket)
			{
				if (vAllTicketData.size() >= vAllTicketData.capacity())
					vAllTicketData.reserve(vAllTicketData.capacity() + TICKET_DATA_VECTOR_RESERVE_INCREMENT);

				const auto nHeight = ticket->GetBlock();
				uint256 txid = uint256S(ticket->GetTxId());

				if (FEE_PAYING_TICKETS.count(ticketInfo.id))
					vFeePayingTicketDataMap.emplace(txid, nHeight);
				vAllTicketData.emplace_back(move(txid), nHeight);
				return true;
			});
	}
}

using sendaddr_map_t = unordered_map<string, CAmount>;

void get_send_addresses(const CChainParams &chainparams, const CTransaction& tx, sendaddr_map_t &sendAddressesMap)
{
	sendAddressesMap.clear();

	const auto& consensusParams = chainparams.GetConsensus();
	for (const auto& txIn : tx.vin)
	{
		if (txIn.prevout.IsNull())
			throw runtime_error("Bad ticket transaction input  - prevout is null");

		CTransaction txPrev;
		uint256 hashBlock;
		if (!GetTransaction(txIn.prevout.hash, txPrev, consensusParams, hashBlock, true))
			throw runtime_error(strprintf("Can't find input transaction by txid '%s'", 
				txIn.prevout.hash.GetHex()));

		if (txPrev.vout.size() <= txIn.prevout.n)
			throw runtime_error(strprintf("Output index %u is out of bounds for transaction with txid '%s' which has only %u outputs", 
                                  txIn.prevout.n, txIn.prevout.hash.GetHex(), txPrev.vout.size()));

        txnouttype type;
        txdest_vector_t vDest;
        int nRequired;
		const auto& txOut = txPrev.vout[txIn.prevout.n];
		if (!ExtractDestinations(txOut.scriptPubKey, type, vDest, nRequired))
			continue;

		KeyIO keyIO(chainparams);
        for (auto &dest : vDest)
		{
			if (!IsValidDestination(dest))
				continue;
			string sAddress = keyIO.EncodeDestination(dest);
			if (sendAddressesMap.count(sAddress))
				sendAddressesMap[sAddress] += txOut.nValue;
			else
				sendAddressesMap.emplace(sAddress, txOut.nValue);
		}
	}
}

UniValue generate_report_fees_and_burn(const UniValue& vParams)
{
	LogFnPrintf("Executing fees and burn report");

	// all ticket txids and heights
	pastel_ticket_data_vector_t vAllTicketData;
	// fee-paying ticket txids and heights
	pastel_ticket_data_map_t vFeePayingTicketDataMap;
	// all addresses that sent coins to the ticket
	sendaddr_map_t sendAddressesMap;
	// total burn amount in patoshis per address
	unordered_map<string, CAmount> addressTotalCoinsBurnedMap;
	// SN collateral address statistics:
	//     - total fees received in patoshis
	//     - total number of fee-paying transactions
	unordered_map<string, tuple<CAmount, uint32_t>> snFeeReceivedMap;

	collect_all_pastel_ticket_data(vAllTicketData, vFeePayingTicketDataMap);
	LogFnPrintf("Collected %zu ticket txids, %zu fee-paying ticket txids",
		vAllTicketData.size(), vFeePayingTicketDataMap.size());

	CAmount nTotalBurnedInDustTransactionsPat = 0;
	CAmount nTotalFeesPaidToSNsPat = 0;
	const auto &chainparams = Params();
	const auto& consensusParams = chainparams.GetConsensus();
	KeyIO keyIO(chainparams);

	for (const auto& [txid, nHeight]: vAllTicketData)
	{
		try
		{
			const bool bIsFeePayingTicket = vFeePayingTicketDataMap.count(txid) > 0;
			CAmount nTxIdTotalBurnAmountPat = 0;
			CAmount nTxIdTotalFeesPaidToSNsPat = 0;
			uint256 hashBlock;
			uint32_t nTxHeight;
			CTransaction tx;
			if (!GetTransaction(txid, tx, consensusParams, hashBlock, true, &nTxHeight))
				continue;

			get_send_addresses(chainparams, tx, sendAddressesMap);
			for (const auto& [addr, nValue] : sendAddressesMap)
			{
				if (addressTotalCoinsBurnedMap.count(addr))
					addressTotalCoinsBurnedMap[addr] += nValue;
				else
					addressTotalCoinsBurnedMap.emplace(addr, nValue);
			}

			const auto& vTxOut = tx.vout;
			txdest_vector_t vTxDestAddresses;
			size_t nVoutCounter = 0;
			size_t nVoutSize = vTxOut.size();
			for (const auto& txOut : vTxOut)
			{
				txnouttype type;
				int nRequired;
				const bool bHaveDests = ExtractDestinations(txOut.scriptPubKey, type, vTxDestAddresses, nRequired);
				if (type == TX_MULTISIG)
					nTxIdTotalBurnAmountPat += txOut.nValue;
				else if (type == TX_PUBKEYHASH && bIsFeePayingTicket)
				{
					// last 3 outputs are payments to SNs for fee-paying tickets
					if (nVoutCounter + 3 >= nVoutSize)
					{
						if (bHaveDests && !vTxDestAddresses.empty())
						{
							const auto& dest = vTxDestAddresses[0];
							if (!IsValidDestination(dest))
							{
								++nVoutCounter;
								continue;
							}
							nTxIdTotalFeesPaidToSNsPat += txOut.nValue;
							string sReceivingSnCollateralAddress = keyIO.EncodeDestination(dest);
							if (snFeeReceivedMap.count(sReceivingSnCollateralAddress))
							{
								auto& [nTotalFeesReceived, nNumberOfFeePayingTransactions] = snFeeReceivedMap[sReceivingSnCollateralAddress];
								nTotalFeesReceived += txOut.nValue;
								nNumberOfFeePayingTransactions += 1;
							}
							else
								snFeeReceivedMap.emplace(move(sReceivingSnCollateralAddress), make_tuple(txOut.nValue, 1));
						}
					}
				}
				++nVoutCounter;
			}
			nTotalBurnedInDustTransactionsPat += nTxIdTotalBurnAmountPat;
			if (bIsFeePayingTicket)
				nTotalFeesPaidToSNsPat += nTxIdTotalFeesPaidToSNsPat;
		} catch (const std::exception& e)
		{
			LogPrintf("Could not process ticket with txid '%s'. %s\n", txid.GetHex(), e.what());
		}
	}

	UniValue summaryObj(UniValue::VOBJ);
	summaryObj.pushKV("totalBurnedInDustTransactionsPat", nTotalBurnedInDustTransactionsPat);
	summaryObj.pushKV("totalBurnedInDustTransactions", GetTruncatedPSLAmount(nTotalBurnedInDustTransactionsPat));
	summaryObj.pushKV("totalFeesPaidToSNsPat", nTotalFeesPaidToSNsPat);
	summaryObj.pushKV("totalFeesPaidToSNs", GetTruncatedPSLAmount(nTotalFeesPaidToSNsPat));

    vector<pair<string, tuple<CAmount, uint32_t>>> sortedSNFeeReceived(
        snFeeReceivedMap.cbegin(), snFeeReceivedMap.cend());

    std::sort(sortedSNFeeReceived.begin(), sortedSNFeeReceived.end(),
        [](const auto& a, const auto& b)
		{
            return get<0>(a.second) > get<0>(b.second); // Descending order by total fees
        });
	UniValue snStat(UniValue::VARR);
	snStat.reserve(snFeeReceivedMap.size());
	for (const auto& [addr, t] : sortedSNFeeReceived)
	{
		const auto& [nTotalFeesReceived, nNumberOfFeePayingTransactions] = t;
		UniValue snStatObj(UniValue::VOBJ);
		snStatObj.pushKV("address", addr);
		snStatObj.pushKV("totalFeesReceivedPat", nTotalFeesReceived);
		snStatObj.pushKV("totalFeesReceived", GetTruncatedPSLAmount(nTotalFeesReceived));
		snStatObj.pushKV("feePayingTransactionCount", static_cast<uint64_t>(nNumberOfFeePayingTransactions));
		snStat.push_back(move(snStatObj));
	}

    vector<pair<string, CAmount>> sortedAddressTotalCoinsBurned(
        addressTotalCoinsBurnedMap.cbegin(), addressTotalCoinsBurnedMap.cend());

    std::sort(sortedAddressTotalCoinsBurned.begin(), sortedAddressTotalCoinsBurned.end(), 
        [](const auto& a, const auto& b)
		{
            return a.second > b.second; // Descending order of total coins burned
        });

	UniValue addrStatObj(UniValue::VOBJ);
	addrStatObj.reserve(sortedAddressTotalCoinsBurned.size());
	for (const auto& [addr, nTotalCoinsBurned] : sortedAddressTotalCoinsBurned)
		addrStatObj.pushKV(addr, GetTruncatedPSLAmount(nTotalCoinsBurned));

	// generate report
	UniValue rptObj(UniValue::VOBJ);
	rptObj.pushKV("summary", move(summaryObj));
	rptObj.pushKV("snStatistics", move(snStat));
	rptObj.pushKV("addressCoinBurn", move(addrStatObj));
	return rptObj;
}
