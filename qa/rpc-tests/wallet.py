#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2018-2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import (
    assert_equal,
    assert_true,
    assert_raises_rpc,
    assert_shows_help,
    start_nodes,
    start_node,
    connect_nodes_bi,
    stop_nodes,
    sync_blocks,
    sync_mempools,
    wait_and_assert_operationid_status,
    wait_pastelds
)
import test_framework.rpc_consts as rpc

import math
from decimal import Decimal, getcontext
getcontext().prec = 16
AMOUNT_TOLERANCE: float = 1e-5

class WalletTest (BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.mining_node_num = 1


    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir)
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.is_network_split=False
        self.sync_all()

    def make_zero_balance(self, nodeNum: int):
            """Make zero balance on the given node

            Args:
                nodeNum (int): node number
            """
            balance = self.nodes[nodeNum].getbalance()
            print(f"node{nodeNum} balance: {balance}")
            if balance > 0:
                self.nodes[nodeNum].sendtoaddress(self.miner_address, balance, "make_zero_balance", "miner", True)
                self.generate_and_sync_inc(1, self.mining_node_num)


    def test_sendtoaddress(self):
        print("=== testing sendtoaddress ===")
        # create new t-addr on node0 and node1
        node0_taddr1 = self.nodes[0].getnewaddress()
        node0_taddr2 = self.nodes[0].getnewaddress()
        node1_taddr1 = self.nodes[1].getnewaddress()
        fixed_amount = 1000
        extra_amount = 200
        
        assert_shows_help(self.nodes[0].sendtoaddress)
        
        print('sendtoaddress by default should return change to original address')
        self.make_zero_balance(0)
        assert_equal(0, self.nodes[0].getbalance())
        self.nodes[self.mining_node_num].sendtoaddress(node0_taddr1, fixed_amount)
        self.nodes[self.mining_node_num].sendtoaddress(node0_taddr2, fixed_amount + extra_amount)
        self.generate_and_sync_inc(1)
        print(f'node0: taddr1={self.nodes[0].z_getbalance(node0_taddr1)}, taddr2={self.nodes[0].z_getbalance(node0_taddr2)}')
        assert_equal(fixed_amount, self.nodes[0].z_getbalance(node0_taddr1))
        assert_equal(fixed_amount + extra_amount, self.nodes[0].z_getbalance(node0_taddr2))
        # sendtoaddress by default should return change to original address
        txid = self.nodes[0].sendtoaddress(node1_taddr1, 2 * fixed_amount)
        assert_true(txid, "sendtoaddress with default change-address should return txid")
        self.generate_and_sync_inc(1)
        txfee = self.nodes[0].gettxfee(txid)["txFee"]
        expected_balance_after = extra_amount - txfee
        print(f"node0 sendtoaddress {2 * fixed_amount} to node1_taddr1, txid={txid}, txfee={txfee}, expected_balance_after={expected_balance_after}")
        assert_true(math.isclose(expected_balance_after, self.nodes[0].getbalance(), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(expected_balance_after, 
                                 self.nodes[0].z_getbalance(node0_taddr1) + self.nodes[0].z_getbalance(node0_taddr2), rel_tol=AMOUNT_TOLERANCE))

        print("sendtoaddress with change-address='original' should return change to original address")
        self.make_zero_balance(0)
        assert_equal(0, self.nodes[0].getbalance())
        self.nodes[self.mining_node_num].sendtoaddress(node0_taddr1, fixed_amount)
        self.generate_and_sync_inc(1)
        print(f'node0: taddr1={self.nodes[0].z_getbalance(node0_taddr1)}')
        assert_equal(fixed_amount, self.nodes[0].z_getbalance(node0_taddr1))
        txid = self.nodes[0].sendtoaddress(node1_taddr1, extra_amount, "test sendtoaddress with 'original' change-address", "node1_taddr1", False, "original")
        assert_true(txid, "sendtoaddress with change-address='original' should return txid")
        self.generate_and_sync_inc(1)
        txfee = self.nodes[0].gettxfee(txid)["txFee"]
        expected_balance_after = fixed_amount - extra_amount - txfee
        print(f"node0 sendtoaddress {extra_amount} to node1_taddr1, txid={txid}, txfee={txfee}, expected_balance_after={expected_balance_after}")
        assert_true(math.isclose(expected_balance_after, self.nodes[0].getbalance(), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(expected_balance_after, 
                                 self.nodes[0].z_getbalance(node0_taddr1), rel_tol=AMOUNT_TOLERANCE))

        print("sendtoaddress with change-address='new' should return change to new address")
        self.make_zero_balance(0)
        assert_equal(0, self.nodes[0].getbalance())
        addrlist_before = self.nodes[0].listaddressamounts()
        assert_equal(0, len(addrlist_before), "addrlist_before should be empty (no addresses with empty balance)")
        self.nodes[self.mining_node_num].sendtoaddress(node0_taddr1, fixed_amount)
        self.generate_and_sync_inc(1)
        print(f'node0 taddr1 balance: {self.nodes[0].z_getbalance(node0_taddr1)}')
        txid = self.nodes[0].sendtoaddress(node1_taddr1, fixed_amount - extra_amount, "test sendtoaddress with 'new' change-address", "node1_taddr1", False, "new")
        assert_true(txid, "sendtoaddress with 'new' change-address should return txid")
        self.generate_and_sync_inc(1)
        txfee = self.nodes[0].gettxfee(txid)["txFee"]
        addrlist_after = self.nodes[0].listaddressamounts()
        assert_equal(1, len(addrlist_after), "addrlist_after should have one new address")
        print(f"node0 addresses after sendtoaddress call with 'new' change-address: {addrlist_after}")
        expected_balance_after = extra_amount - txfee
        assert_true(math.isclose(expected_balance_after, self.nodes[0].z_getbalance(next(iter(addrlist_after))), rel_tol=AMOUNT_TOLERANCE))
        
        print("sendtoaddress with change-address='specific t-addr' should return change to that address")
        self.make_zero_balance(0)
        assert_equal(0, self.nodes[0].getbalance())
        node0_taddr3 = self.nodes[0].getnewaddress()
        print(f"Created new t-addr on node0: {node0_taddr3}")
        self.nodes[self.mining_node_num].sendtoaddress(node0_taddr1, fixed_amount)
        self.generate_and_sync_inc(1)
        print(f'node0 balance: {self.nodes[0].getbalance()}')
        txid = self.nodes[0].sendtoaddress(node1_taddr1, fixed_amount - extra_amount, "test sendtoaddress with 'specific t-addr' change-address", "node1_taddr1", False, node0_taddr3)
        assert_true(txid, "sendtoaddress with 'specific t-addr' change-address should return txid")
        self.generate_and_sync_inc(1)
        txfee = self.nodes[0].gettxfee(txid)["txFee"]
        expected_balance_after = extra_amount - txfee
        print(f"node0 addresses after sendtoaddress call with '{node0_taddr3}' change-address: {self.nodes[0].listaddressamounts()}")
        assert_true(math.isclose(expected_balance_after, self.nodes[0].z_getbalance(node0_taddr3), rel_tol=AMOUNT_TOLERANCE))
        
        assert_raises_rpc(rpc.RPC_INVALID_ADDRESS_OR_KEY, 
                          "Invalid Pastel address",
                          self.nodes[0].sendtoaddress, node1_taddr1, fixed_amount, "comment", "comment_to", False, "invalid_change_address")
        assert_raises_rpc(rpc.RPC_TYPE_ERROR, 
                          "Amount is not a number or string",
                          self.nodes[0].sendtoaddress, node1_taddr1, False)
        assert_raises_rpc(rpc.RPC_TYPE_ERROR, 
                          "Invalid amount for send",
                          self.nodes[0].sendtoaddress, node1_taddr1, 0)
        assert_raises_rpc(rpc.RPC_TYPE_ERROR, 
                          "Amount out of range",
                          self.nodes[0].sendtoaddress, node1_taddr1, -1)
        assert_raises_rpc(rpc.RPC_TYPE_ERROR, 
                          "Invalid amount",
                          self.nodes[0].sendtoaddress, node1_taddr1, 0.123456)


    def test_sendmany(self):
        print("=== testing sendmany ===")        
        node0_taddr1 = self.nodes[0].getnewaddress()
        node0_taddr2 = self.nodes[0].getnewaddress()
        
        fixed_amount = 1000
        send_amount = 500
        extra_amount = 200
        
        assert_shows_help(self.nodes[0].sendmany)
        
        print('sendmany by default should return change to original address')
        self.make_zero_balance(0)
        assert_equal(0, self.nodes[0].getbalance())
        self.nodes[self.mining_node_num].sendtoaddress(node0_taddr1, fixed_amount)
        self.nodes[self.mining_node_num].sendtoaddress(node0_taddr2, fixed_amount + extra_amount)
        self.generate_and_sync_inc(1)
        print(f'node0: taddr1={self.nodes[0].z_getbalance(node0_taddr1)}, taddr2={self.nodes[0].z_getbalance(node0_taddr2)}')
        assert_equal(fixed_amount, self.nodes[0].z_getbalance(node0_taddr1))
        assert_equal(fixed_amount + extra_amount, self.nodes[0].z_getbalance(node0_taddr2))
        # sendtomany by default should return change to original address
        node1_taddr1 = self.nodes[1].getnewaddress()
        node1_taddr2 = self.nodes[1].getnewaddress()
        node1_taddr3 = self.nodes[1].getnewaddress()
        send_to = {
                    node1_taddr1:send_amount,
                    node1_taddr2:send_amount + 1,
                    node1_taddr3:send_amount + 2
                  }
        txid = self.nodes[0].sendmany("", send_to, 1, "test sendmany with default change-address")
        assert_true(txid, "sendmany with default change-address should return txid")
        self.generate_and_sync_inc(1)
        txfee = self.nodes[0].gettxfee(txid)["txFee"]
        expected_balance_after = 2 * fixed_amount + extra_amount - 3*send_amount - 3 - txfee
        print(f"node0 sendmany [{send_amount}, {send_amount + 1}, {send_amount + 2}] to node1 addresses, txid={txid}, txfee={txfee}, expected_balance_after={expected_balance_after}")
        assert_true(math.isclose(expected_balance_after, self.nodes[0].getbalance(), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(expected_balance_after, 
                                 self.nodes[0].z_getbalance(node0_taddr1) + self.nodes[0].z_getbalance(node0_taddr2), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount, self.nodes[1].z_getbalance(node1_taddr1), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount + 1, self.nodes[1].z_getbalance(node1_taddr2), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount + 2, self.nodes[1].z_getbalance(node1_taddr3), rel_tol=AMOUNT_TOLERANCE))
        
        print("sendmany with change-address='original' should return change to original address")
        self.make_zero_balance(0)
        assert_equal(0, self.nodes[0].getbalance())
        self.nodes[self.mining_node_num].sendtoaddress(node0_taddr1, 2 * fixed_amount)
        self.generate_and_sync_inc(1)
        print(f'node0: taddr1={self.nodes[0].z_getbalance(node0_taddr1)}')
        assert_equal(2 * fixed_amount, self.nodes[0].z_getbalance(node0_taddr1))
        # need to create new addresses on node1
        node1_taddr1 = self.nodes[1].getnewaddress()
        node1_taddr2 = self.nodes[1].getnewaddress()
        node1_taddr3 = self.nodes[1].getnewaddress()
        send_to = {
                    node1_taddr1:send_amount,
                    node1_taddr2:send_amount + 1,
                    node1_taddr3:send_amount + 2
                  }
        txid = self.nodes[0].sendmany("", send_to, 1, "test sendmany with 'original' change-address", [], "original")
        assert_true(txid, "sendmany with change-address='original' should return txid")
        self.generate_and_sync_inc(1)
        txfee = self.nodes[0].gettxfee(txid)["txFee"]
        expected_balance_after = 2 * fixed_amount - 3 * send_amount - 3 - txfee
        print(f"node0 sendmany [{send_amount}, {send_amount + 1}, {send_amount + 2}] to node1 addresses, txid={txid}, txfee={txfee}, expected_balance_after={expected_balance_after}")
        assert_true(math.isclose(expected_balance_after, self.nodes[0].getbalance(), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(expected_balance_after, self.nodes[0].z_getbalance(node0_taddr1), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount, self.nodes[1].z_getbalance(node1_taddr1), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount + 1, self.nodes[1].z_getbalance(node1_taddr2), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount + 2, self.nodes[1].z_getbalance(node1_taddr3), rel_tol=AMOUNT_TOLERANCE))
        
        print("sendmany with change-address='new' should return change to new address")
        self.make_zero_balance(0)
        assert_equal(0, self.nodes[0].getbalance())
        addrlist_before = self.nodes[0].listaddressamounts()
        assert_equal(0, len(addrlist_before), "addrlist_before should be empty (no addresses with empty balance)")
        self.nodes[self.mining_node_num].sendtoaddress(node0_taddr1, 2 * fixed_amount)
        self.generate_and_sync_inc(1)
        print(f'node0 taddr1 balance: {self.nodes[0].z_getbalance(node0_taddr1)}')
        # need to create new addresses on node1
        node1_taddr1 = self.nodes[1].getnewaddress()
        node1_taddr2 = self.nodes[1].getnewaddress()
        node1_taddr3 = self.nodes[1].getnewaddress()
        send_to = {
                    node1_taddr1:send_amount,
                    node1_taddr2:send_amount + 1,
                    node1_taddr3:send_amount + 2
                  }
        txid = self.nodes[0].sendmany("", send_to, 1, "test sendmany with 'new' change-address", [], "new")
        assert_true(txid, "sendmany with 'new' change-address should return txid")
        self.generate_and_sync_inc(1)
        txfee = self.nodes[0].gettxfee(txid)["txFee"]
        addrlist_after = self.nodes[0].listaddressamounts()
        assert_equal(1, len(addrlist_after), "addrlist_after should have one new address")
        print(f"node0 addresses after sendmany call with 'new' change-address: {addrlist_after}")
        expected_balance_after = 2 * fixed_amount - 3 * send_amount - 3 - txfee
        assert_true(math.isclose(expected_balance_after, self.nodes[0].z_getbalance(next(iter(addrlist_after))), rel_tol=AMOUNT_TOLERANCE))
        assert_equal(0,  self.nodes[0].z_getbalance(node0_taddr1))
        assert_true(math.isclose(send_amount, self.nodes[1].z_getbalance(node1_taddr1), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount + 1, self.nodes[1].z_getbalance(node1_taddr2), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount + 2, self.nodes[1].z_getbalance(node1_taddr3), rel_tol=AMOUNT_TOLERANCE))
        
        print("sendmany with change-address='specific t-addr' should return change to that address")
        self.make_zero_balance(0)
        assert_equal(0, self.nodes[0].getbalance())
        node0_taddr3 = self.nodes[0].getnewaddress()
        print(f"Created new t-addr on node0: {node0_taddr3}")
        self.nodes[self.mining_node_num].sendtoaddress(node0_taddr1, 2 * fixed_amount)
        self.generate_and_sync_inc(1)
        print(f'node0 balance: {self.nodes[0].getbalance()}')
        # need to create new addresses on node1
        node1_taddr1 = self.nodes[1].getnewaddress()
        node1_taddr2 = self.nodes[1].getnewaddress()
        node1_taddr3 = self.nodes[1].getnewaddress()
        send_to = {
                    node1_taddr1:send_amount,
                    node1_taddr2:send_amount + 1,
                    node1_taddr3:send_amount + 2
                  }
        txid = self.nodes[0].sendmany("", send_to, 1, "test sendmany with 'new' change-address", [], node0_taddr3)
        assert_true(txid, "sendmany with 'specific t-addr' change-address should return txid")
        self.generate_and_sync_inc(1)
        txfee = self.nodes[0].gettxfee(txid)["txFee"]
        expected_balance_after = 2 * fixed_amount - 3 * send_amount - 3 - txfee
        print(f"node0 addresses after sendmany call with '{node0_taddr3}' change-address: {self.nodes[0].listaddressamounts()}")
        assert_true(math.isclose(expected_balance_after, self.nodes[0].z_getbalance(node0_taddr3), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount, self.nodes[1].z_getbalance(node1_taddr1), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount + 1, self.nodes[1].z_getbalance(node1_taddr2), rel_tol=AMOUNT_TOLERANCE))
        assert_true(math.isclose(send_amount + 2, self.nodes[1].z_getbalance(node1_taddr3), rel_tol=AMOUNT_TOLERANCE))
        
        assert_raises_rpc(rpc.RPC_INVALID_ADDRESS_OR_KEY, 
                          "Invalid Pastel address",
                          self.nodes[0].sendmany, "", send_to, 1, "comment", [], "invalid_change_address")
        
        
    def run_test (self):
        print("Mining blocks...")
        print(f"On REGTEST... \n\treward is {self._reward} per block\n\t100 blocks to maturity")

        self.generate_and_sync(4)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(walletinfo['immature_balance'], self._reward*4)
        assert_equal(walletinfo['balance'], 0)

        self.sync_all()
        self.generate_and_sync_inc(101, 1)

        assert_equal(self.nodes[0].getbalance(), self._reward*4)    # node_0 has 4 blocks over maturity
        assert_equal(self.nodes[1].getbalance(), self._reward)  # node_1 has 1 block over maturity
        assert_equal(self.nodes[2].getbalance(), 0)
        assert_equal(self.nodes[0].getbalance("*"), self._reward*4)
        assert_equal(self.nodes[1].getbalance("*"), self._reward)
        assert_equal(self.nodes[2].getbalance("*"), 0)
        
        self.miner_address = self.nodes[self.mining_node_num].getnewaddress()

        # Send 26 PASTEL from 0 to 2 using sendtoaddress call. Now node_0 has 24, node_2 has 26
        # Second transaction will be child of first, and will require a fee
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), self._reward+1)
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), self._reward)

        s = 2*self._reward+1

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(walletinfo['immature_balance'], 0)

        # Have node0 mine a block, thus it will collect its own fee.
        self.sync_all()
        self.generate_and_sync_inc(1)

        # Have node1 generate 100 blocks (so node0 can recover the fee)
        self.generate_and_sync_inc(100, 1)

        # node0 should end up with 62.5 in block rewards plus fees, but
        # minus the 26 plus fees sent to node2
        # node_2 has 26
        assert_equal(self.nodes[0].getbalance(), self._reward*5-s)
        assert_equal(self.nodes[2].getbalance(), s)
        assert_equal(self.nodes[0].getbalance("*"), self._reward*5-s)
        assert_equal(self.nodes[2].getbalance("*"), s)

        # Node0 should have three unspent outputs.
        # Create a couple of transactions to send them to node2, submit them through
        # node1, and make sure both node0 and node2 pick them up properly:
        node0utxos = self.nodes[0].listunspent(1)
        assert_equal(len(node0utxos), 3)

        # Check 'generated' field of listunspent
        # Node 0: has one coinbase utxo and two regular utxos
        assert_equal(sum(int(uxto["generated"] is True) for uxto in node0utxos), 1)
        # Node 1: has 101 coinbase utxos and no regular utxos
        node1utxos = self.nodes[1].listunspent(1)
        assert_equal(len(node1utxos), 101)
        assert_equal(sum(int(uxto["generated"] is True) for uxto in node1utxos), 101)
        # Node 2: has no coinbase utxos and two regular utxos
        node2utxos = self.nodes[2].listunspent(1)
        assert_equal(len(node2utxos), 2)
        assert_equal(sum(int(uxto["generated"] is True) for uxto in node2utxos), 0)

        # Catch an attempt to send a transaction with an absurdly high fee.
        # Send 1.0 from an utxo of value 6250.0 but don't specify a change output, so then
        # the change of 6249.0 becomes the fee, which is greater than estimated fee of 0.0019 (0.0021 for sapling).
        inputs = []
        outputs = {}
        for utxo in node2utxos:
            if utxo["amount"] == self._reward:
                break
        assert_equal(utxo["amount"], self._reward)
        inputs.append({ "txid" : utxo["txid"], "vout" : utxo["vout"]})
        outputs[self.nodes[2].getnewaddress("")] = Decimal("1.0")
        raw_tx = self.nodes[2].createrawtransaction(inputs, outputs)
        signed_tx = self.nodes[2].signrawtransaction(raw_tx)
        try:
            self.nodes[2].sendrawtransaction(signed_tx["hex"])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("absurdly high fees" in errorString)
        print(errorString)
        assert("624900000 > 60000" in errorString)

        # create both transactions
        txns_to_send = []
        for utxo in node0utxos:
            inputs = []
            outputs = {}
            inputs.append({ "txid" : utxo["txid"], "vout" : utxo["vout"]})
            outputs[self.nodes[2].getnewaddress("")] = utxo["amount"]
            raw_tx = self.nodes[0].createrawtransaction(inputs, outputs)
            txns_to_send.append(self.nodes[0].signrawtransaction(raw_tx))

        # Have node 1 (miner) send the transactions
        self.nodes[1].sendrawtransaction(txns_to_send[0]["hex"], True)
        self.nodes[1].sendrawtransaction(txns_to_send[1]["hex"], True)
        self.nodes[1].sendrawtransaction(txns_to_send[2]["hex"], True)

        # Have node1 mine a block to confirm transactions:
        self.sync_all()
        self.generate_and_sync_inc(1, 1)

        # node_0 now has 0; node_2 - has all 5 coins (62.5)
        assert_equal(self.nodes[0].getbalance(), 0)
        assert_equal(self.nodes[2].getbalance(), self._reward*5)
        assert_equal(self.nodes[0].getbalance("*"), 0)
        assert_equal(self.nodes[2].getbalance("*"), self._reward*5)

        # Send 1 coin from 2 to 0 with fee
        address = self.nodes[0].getnewaddress("")
        self.nodes[2].settxfee(self._fee)
        self.nodes[2].sendtoaddress(address, self._reward, "", "", False)
        self.sync_all()
        self.generate_and_sync_inc(1, 2)
        # node_0 now has 1 coin; node_2 - has 4 coins - fee (49.999)
        assert_equal(self.nodes[2].getbalance(), Decimal(self._reward*4)-self._fee)
        assert_equal(self.nodes[0].getbalance(), self._reward)
        assert_equal(self.nodes[2].getbalance("*"), Decimal(self._reward*4)-self._fee)
        assert_equal(self.nodes[0].getbalance("*"), self._reward)

        # Send 1 coin with subtract fee from amount
        self.nodes[2].sendtoaddress(address, self._reward, "", "", True)
        self.sync_all()
        self.generate_and_sync_inc(1, 2)
        # node_0 now has 2 coin - fee (24.999); node_2 - has 3 coins - fee (37.499)
        assert_equal(self.nodes[2].getbalance(), Decimal(self._reward*3)-self._fee)
        assert_equal(self.nodes[0].getbalance(), Decimal(self._reward*2)-self._fee)
        assert_equal(self.nodes[2].getbalance("*"), Decimal(self._reward*3)-self._fee)
        assert_equal(self.nodes[0].getbalance("*"), Decimal(self._reward*2)-self._fee)

        # Sendmany
        self.nodes[2].sendmany("", {address: self._reward}, 0, "", [])
        self.sync_all()
        self.generate_and_sync_inc(1, 2)
        # node_0 now has 3 coin - fee (37.499); node_2 - has 2 coins - 2*fee (25.998)
        assert_equal(self.nodes[2].getbalance(), Decimal(self._reward*2)-self._fee-self._fee)
        assert_equal(self.nodes[0].getbalance(), Decimal(self._reward*3)-self._fee)
        assert_equal(self.nodes[2].getbalance("*"), Decimal(self._reward*2)-self._fee-self._fee)
        assert_equal(self.nodes[0].getbalance("*"), Decimal(self._reward*3)-self._fee)

        # Sendmany with subtract fee from amount
        self.nodes[2].sendmany("", {address: self._reward}, 0, "", [address])
        self.sync_all()
        self.generate_and_sync_inc(1, 2)
        # node_0 now has 4 coin - 2*fee (49.998); node_2 - has 1 coins - 2*fee (12.498)
        assert_equal(self.nodes[2].getbalance(), Decimal(self._reward*1)-self._fee-self._fee)
        assert_equal(self.nodes[0].getbalance(), Decimal(self._reward*4)-self._fee-self._fee)
        assert_equal(self.nodes[2].getbalance("*"), Decimal(self._reward*1)-self._fee-self._fee)
        assert_equal(self.nodes[0].getbalance("*"), Decimal(self._reward*4)-self._fee-self._fee)

        # Test ResendWalletTransactions:
        # Create a couple of transactions, then start up a fourth
        # node (nodes[3]) and ask nodes[0] to rebroadcast.
        # EXPECT: nodes[3] should have those transactions in its mempool.
        txid1 = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        txid2 = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), 1)
        sync_mempools(self.nodes)

        self.nodes.append(start_node(3, self.options.tmpdir))
        connect_nodes_bi(self.nodes, 0, 3)
        sync_blocks(self.nodes)

        relayed = self.nodes[0].resendwallettransactions()
        assert_equal(set(relayed), set([txid1, txid2]))
        sync_mempools(self.nodes)

        assert(txid1 in self.nodes[3].getrawmempool())

        #check if we can list zero value tx as available coins
        #1. create rawtx
        #2. hex-changed one output to 0.0
        #3. sign and send
        #4. check if recipient (node0) can list the zero value tx
        usp = self.nodes[1].listunspent()
        inputs = [{"txid":usp[0]['txid'], "vout":usp[0]['vout']}]
        outputs = {self.nodes[1].getnewaddress(): Decimal(self._reward*1)-self._fee-self._fee, self.nodes[0].getnewaddress(): 11.11}

        print(inputs)
        print(outputs)

        rawTx = self.nodes[1].createrawtransaction(inputs, outputs).replace("d8f31", "00000") #replace 11.11 with 0.0 (int32)
        print(rawTx)
        decRawTx = self.nodes[1].decoderawtransaction(rawTx)
        signedRawTx = self.nodes[1].signrawtransaction(rawTx)
        decRawTx = self.nodes[1].decoderawtransaction(signedRawTx['hex'])
        zeroValueTxid= decRawTx['txid']
        self.nodes[1].sendrawtransaction(signedRawTx['hex'])

        self.sync_all()
        self.generate_and_sync_inc(1, 1) #mine a block

        unspentTxs = self.nodes[0].listunspent() #zero value tx must be in listunspents output
        found = False
        for uTx in unspentTxs:
            if uTx['txid'] == zeroValueTxid:
                found = True
                assert_equal(uTx['amount'], self._null);
        assert(found)

        #do some -walletbroadcast tests
        stop_nodes(self.nodes)
        wait_pastelds()
        self.nodes = start_nodes(3, self.options.tmpdir, [["-walletbroadcast=0"],["-walletbroadcast=0"],["-walletbroadcast=0"]])
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.sync_all()

        txIdNotBroadcasted  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 2)
        txObjNotBroadcasted = self.nodes[0].gettransaction(txIdNotBroadcasted)
        self.sync_all()
        self.generate_and_sync_inc(1, 1) #mine a block, tx should not be in there
        #should not be changed because tx was not broadcasted
        assert_equal(self.nodes[2].getbalance(), self._reward-self._fee-self._fee)
        assert_equal(self.nodes[2].getbalance("*"), self._reward-self._fee-self._fee)

        #now broadcast from another node, mine a block, sync, and check the balance
        self.nodes[1].sendrawtransaction(txObjNotBroadcasted['hex'])
        self.sync_all()
        self.generate_and_sync_inc(1, 1)
        txObjNotBroadcasted = self.nodes[0].gettransaction(txIdNotBroadcasted)
        assert_equal(self.nodes[2].getbalance(), self._reward-self._fee-self._fee+Decimal('2.000')) #should not be
        assert_equal(self.nodes[2].getbalance("*"), self._reward-self._fee-self._fee+Decimal('2.000')) #should not be

        #create another tx
        txIdNotBroadcasted  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 2)

        #restart the nodes with -walletbroadcast=1
        stop_nodes(self.nodes)
        wait_pastelds()
        self.nodes = start_nodes(3, self.options.tmpdir)
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        sync_blocks(self.nodes)

        self.nodes[0].generate(1)
        sync_blocks(self.nodes)

        #tx should be added to balance because after restarting the nodes tx should be broadcasted
        assert_equal(self.nodes[2].getbalance(), self._reward-self._fee-self._fee+Decimal('2.000')+Decimal('2.000')) #should not be
        assert_equal(self.nodes[2].getbalance("*"), self._reward-self._fee-self._fee+Decimal('2.000')+Decimal('2.000')) #should not be

        # send from node 0 to node 2 taddr
        mytaddr = self.nodes[2].getnewaddress()
        mytxid = self.nodes[0].sendtoaddress(mytaddr, self._reward)
        self.sync_all()
        self.generate_and_sync_inc(1)

        mybalance = self.nodes[2].z_getbalance(mytaddr)
        assert_equal(mybalance, self._reward)

        self.test_sendtoaddress()
        self.test_sendmany()
        
        # z_sendmany is expected to fail if tx size breaks limit
        myzaddr = self.nodes[0].z_getnewaddress()

        recipients = []
        num_t_recipients = 3000
        amount_per_recipient = self._patoshi
        errorString = ''
        for i in range(0,num_t_recipients):
            newtaddr = self.nodes[2].getnewaddress()
            recipients.append({"address":newtaddr, "amount":amount_per_recipient})

        # Issue #2759 Workaround START
        # HTTP connection to node 0 may fall into a state, during the few minutes it takes to process
        # loop above to create new addresses, that when z_sendmany is called with a large amount of
        # rpc data in recipients, the connection fails with a 'broken pipe' error.  Making a RPC call
        # to node 0 before calling z_sendmany appears to fix this issue, perhaps putting the HTTP
        # connection into a good state to handle a large amount of data in recipients.
        self.nodes[0].getinfo()
        # Issue #2759 Workaround END

        # in Sapling mode this transaction is possible, but should return insufficient funds error
        opid = self.nodes[0].z_sendmany(myzaddr, recipients)
        wait_and_assert_operationid_status(self.nodes[0], opid, "failed", "Insufficient shielded funds, have 0.00, need 0.13")

        recipients = []
        num_t_recipients = 2000
        num_z_recipients = 50
        amount_per_recipient = self._patoshi
        errorString = ''
        for i in range(0,num_t_recipients):
            newtaddr = self.nodes[2].getnewaddress()
            recipients.append({"address":newtaddr, "amount":amount_per_recipient})
        for i in range(0,num_z_recipients):
            newzaddr = self.nodes[2].z_getnewaddress()
            recipients.append({"address":newzaddr, "amount":amount_per_recipient})

        # Issue #2759 Workaround START
        self.nodes[0].getinfo()
        # Issue #2759 Workaround END

        opid = self.nodes[0].z_sendmany(myzaddr, recipients)
        wait_and_assert_operationid_status(self.nodes[0], opid, "failed", "Insufficient shielded funds, have 0.00, need 0.1205")

        recipients = []
        num_z_recipients = 100
        amount_per_recipient = self._patoshi
        errorString = ''
        for i in range(0,num_z_recipients):
            newzaddr = self.nodes[2].z_getnewaddress()
            recipients.append({"address":newzaddr, "amount":amount_per_recipient})
        opid = self.nodes[0].z_sendmany(myzaddr, recipients)
        wait_and_assert_operationid_status(self.nodes[0], opid, "failed", "Insufficient shielded funds, have 0.00, need 0.101")

        # add zaddr to node 2
        myzaddr = self.nodes[2].z_getnewaddress()

        # send node 2 taddr to zaddr
        recipients = []
        recipients.append({"address":myzaddr, "amount":7})

        mytxid = wait_and_assert_operationid_status(self.nodes[2], self.nodes[2].z_sendmany(mytaddr, recipients))

        self.sync_all()
        self.generate_and_sync_inc(1, 2)

        # check balances
        zsendmanynotevalue = Decimal('7.0')
        zsendmanyfee = self._fee
        cur_balance = self._reward-self._fee-self._fee+Decimal('2.000')+Decimal('2.000')+Decimal(self._reward*1) #28.998
        node2utxobalance = cur_balance - zsendmanynotevalue - zsendmanyfee

        assert_equal(self.nodes[2].getbalance(), node2utxobalance)
        assert_equal(self.nodes[2].getbalance("*"), node2utxobalance)

        # check zaddr balance
        assert_equal(self.nodes[2].z_getbalance(myzaddr), zsendmanynotevalue)

        # check via z_gettotalbalance
        resp = self.nodes[2].z_gettotalbalance()
        assert_equal(Decimal(resp["transparent"]), node2utxobalance)
        assert_equal(Decimal(resp["private"]), zsendmanynotevalue)
        assert_equal(Decimal(resp["total"]), node2utxobalance + zsendmanynotevalue)

        # send from private note to node 0 and node 2
        node0balance = self.nodes[0].getbalance() # 
        node2balance = self.nodes[2].getbalance() # 

        recipients = []
        recipients.append({"address":self.nodes[0].getnewaddress(), "amount":1})
        recipients.append({"address":self.nodes[2].getnewaddress(), "amount":1.0})
        
        wait_and_assert_operationid_status(self.nodes[2], self.nodes[2].z_sendmany(myzaddr, recipients))

        self.sync_all()
        self.generate_and_sync_inc(1, 2)

        node0balance += Decimal('1.0')
        node2balance += Decimal('1.0')
        assert_equal(Decimal(self.nodes[0].getbalance()), node0balance)
        assert_equal(Decimal(self.nodes[0].getbalance("*")), node0balance)
        assert_equal(Decimal(self.nodes[2].getbalance()), node2balance)
        assert_equal(Decimal(self.nodes[2].getbalance("*")), node2balance)

        #send a tx with value in a string (PR#6380 +)
        txId  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), "2")
        txObj = self.nodes[0].gettransaction(txId)
        assert_equal(txObj['amount'], Decimal('-2.00000'))

        txId  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), "0.01")
        txObj = self.nodes[0].gettransaction(txId)
        assert_equal(txObj['amount'], Decimal('-0.01'))

        #check if JSON parser can handle scientific notation in strings
        txId  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), "1e-2")
        txObj = self.nodes[0].gettransaction(txId)
        assert_equal(txObj['amount'], Decimal('-0.01'))

        #this should fail
        errorString = ""
        try:
            txId  = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), "1f-4")
        except JSONRPCException as e:
            errorString = e.error['message']

        assert_equal("Invalid amount" in errorString, True)

        errorString = ""
        try:
            self.nodes[0].generate("2") #use a string to as block amount parameter must fail because it's not interpreted as amount
        except JSONRPCException as e:
            errorString = e.error['message']

        assert_equal("not an integer" in errorString, True)

        myzaddr     = self.nodes[0].z_getnewaddress()
        recipients  = [ {"address": myzaddr, "amount": Decimal('0.0') } ]
        errorString = ''

        # Make sure that amount=0 transactions can use the default fee
        # without triggering "absurd fee" errors
        try:
            myopid = self.nodes[0].z_sendmany(myzaddr, recipients)
            assert(myopid)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
            assert(False)

        # This fee is larger than the default fee and since amount=0
        # it should trigger error
        fee         = self._fee*1000
        recipients  = [ {"address": myzaddr, "amount": Decimal('0.0') } ]
        minconf     = 1
        errorString = ''

        try:
            myopid = self.nodes[0].z_sendmany(myzaddr, recipients, minconf, fee)
        except JSONRPCException as e:
            errorString = e.error['message']
        print(errorString)
        assert('Small transaction amount' in errorString)

        # This fee is less than default and greater than amount, but still valid
        fee         = self._fee/1000 # 0.0001
        recipients  = [ {"address": myzaddr, "amount": self._fee/10000 } ] # 0.0001
        minconf     = 1
        errorString = ''

        try:
            myopid = self.nodes[0].z_sendmany(myzaddr, recipients, minconf, fee)
            assert(myopid)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
            assert(False)

        # Make sure amount=0, fee=0 transaction are valid to add to mempool
        # though miners decide whether to add to a block
        fee         = Decimal('0.0')
        minconf     = 1
        recipients  = [ {"address": myzaddr, "amount": Decimal('0.0') } ]
        errorString = ''

        try:
            myopid = self.nodes[0].z_sendmany(myzaddr, recipients, minconf, fee)
            assert(myopid)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
            assert(False)


if __name__ == '__main__':
    WalletTest ().main ()
