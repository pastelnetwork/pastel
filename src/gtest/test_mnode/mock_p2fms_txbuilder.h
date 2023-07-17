#pragma once
// Copyright (c) 2018-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <functional>

#include <gmock/gmock.h>

#include <streams.h>
#include <primitives/transaction.h>
#include <mnode/tickets/ticket.h>
#include <mnode/p2fms-txbuilder.h>

class MockP2FMS_TxBuilder : public CP2FMS_TX_Builder
{
public:
	MockP2FMS_TxBuilder();

	MOCK_METHOD(size_t, CreateP2FMSScripts, (), (override));
	MOCK_METHOD(bool, PreprocessAndValidate, (), (override));
	MOCK_METHOD(bool, BuildTransaction, (CMutableTransaction& tx_out), (override));
	MOCK_METHOD(bool, SignTransaction, (CMutableTransaction& tx_out), (override));

	size_t Call_CreateP2FMSScripts() { return CP2FMS_TX_Builder::CreateP2FMSScripts(); }
	bool Call_PreprocessAndValidate() { return CP2FMS_TX_Builder::PreprocessAndValidate(); }
	bool Call_BuildTransaction(CMutableTransaction& tx_out) { return CP2FMS_TX_Builder::BuildTransaction(tx_out); }
	bool Call_SignTransaction(CMutableTransaction& tx_out) { return CP2FMS_TX_Builder::SignTransaction(tx_out); }

	CMutableTransaction CreateTicketTransaction(const TicketID ticket_id, const std::function<void(CPastelTicket& tkt)>& fnSetTicketData);

protected:
	CDataStream m_DataStream;
};
CMutableTransaction CreateTestTransaction();