#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

import time
from decimal import Decimal, getcontext

from test_framework.util import (
    assert_equal,
    assert_true,
    assert_greater_than,
    assert_shows_help,
    assert_raises_rpc,
    initialize_chain_clean,
    start_node,
    stop_node
)
from mn_common import MasterNodeCommon
import test_framework.rpc_consts as rpc

getcontext().prec = 16

class MasterNodeMainTest (MasterNodeCommon):

    def __init__(self):
        super().__init__()

        self.number_of_master_nodes = 1
        self.number_of_simple_nodes = 2
        self.number_of_cold_nodes = self.number_of_master_nodes
        self.cold_node_num = 0       # master node
        self.mining_node_num = 1     # mining node
        self.hot_node_num = 2        # keeps all collateral for MNs

    def setup_chain(self):
        print(f"Initializing test directory {self.options.tmpdir}")
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes+1) # three now, 1 later

    def setup_network(self, split=False):
        self.nodes = []
        self.is_network_split = False
        self.setup_masternodes_network(self.mining_node_num, self.hot_node_num)


    def storagefee_tests (self):
        print("=== Test MN Fees ===")
        assert_shows_help(self.nodes[0].storagefee)

        nfee_mn0 = self.nodes[0].storagefee("getnetworkfee")["networkfee"]
        nfee_mn1 = self.nodes[1].storagefee("getnetworkfee")["networkfee"]
        nfee_mn2 = self.nodes[2].storagefee("getnetworkfee")["networkfee"]
        assert_equal(nfee_mn0, 50)
        assert_equal(nfee_mn1, 50)
        assert_equal(nfee_mn2, 50)
        print("Network fee is ", nfee_mn0)

        lfee_mn0 = self.nodes[0].storagefee("getlocalfee")["localfee"]
        assert_equal(lfee_mn0, 50)
        print("Local fee of MN0 is ", lfee_mn0)

        assert_raises_rpc(rpc.RPC_INVALID_PARAMETER, "storagefee getactionfees",
            self.nodes[0].storagefee, "getactionfees")
        assert_raises_rpc(rpc.RPC_INVALID_PARAMETER, "negative",
            self.nodes[0].storagefee, "getactionfees", "-10")
        actionfees = self.nodes[0].storagefee("getactionfees", "10")
        assert_equal(10, actionfees["datasize"])
        sense_fee = actionfees["sensefee"]
        cascade_fee = actionfees["cascadefee"]
        print(f"action fee [sense]: {sense_fee}")
        print(f"action fee [cascade]: {cascade_fee}")
        assert_greater_than(sense_fee, 0)
        assert_greater_than(cascade_fee, 0)

    def run_test (self):
        tests = ['cache', 'sync', 'ping', 'restart', 'spent', 'fee']

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'fee' in tests:
            self.storagefee_tests()

        #print("Test sync after crash")
        # 1. kill (not gracefully) node0 (masternode)
        # 2. start node0 again 
        # 3. Check all nodes
        # self.wait_for_mn_state(120, 20, "ENABLED", self.nodes[0:self.total_number_of_nodes], mn_id)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'cache' in tests:
            print("=== Test cache save/load ===")
            print(f"Stopping node {self.mining_node_num}...")
            stop_node(self.nodes[self.mining_node_num])
            print(f"Starting node {self.mining_node_num}...")
            self.nodes[self.mining_node_num] = start_node(self.mining_node_num, self.options.tmpdir, ["-debug=masternode"])
            self.reconnect_node(self.mining_node_num)
            self.sync_all()

            mn_id = 0
            mn = self.mn_nodes[mn_id]
            print(f"Waiting for {mn.alias} ENABLED state...")
            self.wait_for_mn_state(10, 10, "ENABLED", mn_id)

            mn_params = ["-debug=masternode", "-masternode", "-txindex=1", "-reindex", f"-masternodeprivkey={self.mn_nodes[mn_id].privKey}"]
            #Test disk cache 2
            print(f"Stopping node {self.cold_node_num}...")
            stop_node(self.nodes[self.cold_node_num])
            print(f"Starting node {self.cold_node_num} as Masternode...")
            self.nodes[self.cold_node_num] = start_node(self.cold_node_num, self.options.tmpdir, mn_params)
            self.reconnect_node(self.cold_node_num)
            self.sync_all()

            print(f"Waiting for {mn.alias} ENABLED state...")
            self.wait_for_mn_state(20, 10, "ENABLED", mn_id)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'sync' in tests:
            print("=== Test MN list sync ===")
            print("Test new node sync")
            new_node_no = len(self.nodes)
            print(f"Starting node {new_node_no}...")
            self.nodes.append(start_node(new_node_no, self.options.tmpdir, ["-debug=masternode"]))
            self.reconnect_node(new_node_no)
            self.sync_all()

            print(f"Waiting for {mn.alias} ENABLED state...")
            self.wait_for_mn_state(20, 10, "ENABLED", mn_id)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'ping' in tests:
            print("=== Test Ping ===")
            print(f"Stopping Masternode {self.cold_node_num}...")
            stop_node(self.nodes[self.cold_node_num])

            print(f"Waiting for {mn.alias} EXPIRED state on all other nodes...")
            self.wait_for_mn_state(30, 20, "EXPIRED", mn_id, 8, self.nodes[1:])

            print(f"Starting node {self.cold_node_num} as Masternode again...")
            self.nodes[self.cold_node_num] = start_node(self.cold_node_num, self.options.tmpdir, mn_params)
            self.reconnect_node(self.cold_node_num)
            self.sync_all()

            print(f"Waiting for {mn.alias} ENABLED state...")
            self.wait_for_mn_state(30, 20, "ENABLED", mn_id, 8)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'restart' in tests:
            print("=== Test 'restart required' ===")
            print(f"Stopping Masternode {self.cold_node_num}...")
            stop_node(self.nodes[self.cold_node_num])

            print(f"Waiting for {mn.alias} EXPIRED state on all other nodes...")
            self.wait_for_mn_state(30, 20, "EXPIRED", mn_id, 15, self.nodes[1:])
            
            print(f"Waiting for {mn.alias} NEW_START_REQUIRED state on all other nodes...")
            self.wait_for_mn_state(60, 30, "NEW_START_REQUIRED", mn_id, 15, self.nodes[1:])
            
            # regtest, the NEW_START_REQUIRED masternode is longer than 10 minutes will not be shown.
            self.wait_for_mn_state(60, 30, "", mn_id, 15, self.nodes[1:])

            print(f"Starting node {self.cold_node_num} as Masternode again...")
            self.nodes[self.cold_node_num] = start_node(self.cold_node_num, self.options.tmpdir, mn_params)
            self.reconnect_node(self.cold_node_num)
            self.sync_all()

            print("Enabling node 0 as MN again (start-alias from node 2)...")
            res = self.nodes[self.hot_node_num].masternode("start-alias", mn.alias)
            print(res)
            assert_equal(res["alias"], mn.alias)
            assert_equal(res["result"], "successful")

            # self.wait_for_mn_state(30, 10, "PRE_ENABLED", self.nodes[0:self.total_number_of_nodes], mn_id, 6)
            print(f"Waiting for {mn.alias} ENABLED state...")
            self.wait_for_mn_state(30, 20, "ENABLED", mn_id, 15)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'spent' in tests:
            print("=== Test MN Spent ===")

            assert_equal(self.nodes[self.hot_node_num].getbalance(), self.collateral)
            usp = self.nodes[self.hot_node_num].listlockunspent()
            assert_true(usp, "No locked MN outpoints found")
            print(f"{usp[0]['txid']}-{usp[0]['vout']}")
            assert_equal(usp[0]['txid'], mn.collateral_txid)
            assert_equal(Decimal(usp[0]['vout']), Decimal(mn.collateral_index))

            print("Unlocking locked output...")
            locked = [{"txid":usp[0]['txid'], "vout":usp[0]['vout']}]
            assert_equal(self.nodes[self.hot_node_num].lockunspent(True, locked), True)

            print(f"Sending 100 coins from node {self.hot_node_num} to node {self.mining_node_num}...")
            newaddr = self.nodes[self.mining_node_num].getnewaddress()
            self.nodes[self.hot_node_num].sendtoaddress(newaddr, 100, "", "", False)
            self.generate_and_sync_inc(1)

            balance = self.nodes[self.hot_node_num].getbalance()
            print(balance)
            assert_greater_than(Decimal(str(self.collateral)), Decimal(balance))

            print(self.nodes[self.cold_node_num].masternode("status")["status"])
            # self.wait_for_mn_state(10, 10, "OUTPOINT_SPENT", mn_id, 3)

            for _ in range(10):
                result = self.nodes[self.cold_node_num].masternode("status")["status"]
                if result != "Not capable masternode: Masternode not in masternode list":
                    print(result)
                    print('Waiting 20 seconds...')
                    time.sleep(20)
                else: break

            print(self.nodes[self.cold_node_num].masternode("status")["status"])
            assert_equal(self.nodes[self.cold_node_num].masternode("status")["status"],
                         "Not capable masternode: Masternode not in masternode list")

        print("All set...")

if __name__ == '__main__':
    MasterNodeMainTest ().main ()
