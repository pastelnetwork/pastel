// Copyright (c) 2018-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mnode/mnode-consts.h>
#include <mnode/ticket-processor.h>

#include <test_mnode/mock_p2fms_txbuilder.h>

using namespace std;

CMutableTransaction CreateTestTransaction()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_11;
    tx.vout.resize(1);
    tx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx.vout[0].nValue = 100000LL;
    return tx;
}

MockP2FMS_TxBuilder::MockP2FMS_TxBuilder() :
    CP2FMS_TX_Builder(m_DataStream, 0),
    m_DataStream(SER_NETWORK, DATASTREAM_VERSION)
{}

CMutableTransaction MockP2FMS_TxBuilder::CreateTicketTransaction(const TicketID ticket_id, const function<void(CPastelTicket& tkt)>& fnSetTicketData)
{
    CMutableTransaction txTicket;
    auto pTicket = CPastelTicketProcessor::CreateTicket(ticket_id);
    if (!pTicket)
        return txTicket;
    fnSetTicketData(*pTicket);
    m_DataStream.clear();
    m_DataStream << to_integral_type<TicketID>(ticket_id);
    m_DataStream << *pTicket;

    size_t nInputDataSize = CreateP2FMSScripts();
    txTicket.vout.resize(m_vOutScripts.size() + 1);
    for (int i = 0; i < m_vOutScripts.size(); i++)
    {
        txTicket.vout[i].scriptPubKey = m_vOutScripts[i];
        txTicket.vout[i].nValue = 10000LL;
    }
    txTicket.vout[m_vOutScripts.size()].nValue = 0; // no change
    return txTicket;
}
