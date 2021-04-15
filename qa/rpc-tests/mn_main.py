#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, initialize_chain_clean, \
    initialize_datadir, start_nodes, start_node, connect_nodes_bi, \
    pasteld_processes, wait_and_assert_operationid_status, p2p_port, \
    stop_node

from mn_common import MasterNodeCommon, wait_for_it

import os
import sys
import time

from decimal import Decimal, getcontext
getcontext().prec = 16

private_keys_list = ["91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe"]

class MasterNodeMainTest (MasterNodeCommon):
    number_of_master_nodes = len(private_keys_list)
    number_of_simple_nodes = 2
    total_number_of_nodes = number_of_master_nodes+number_of_simple_nodes
    cold_node_num = 0
    mining_node_num = 1
    hot_node_num = 2

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes+1) # three now, 1 later

    def setup_network(self, split=False):
        self.nodes = []
        self.is_network_split = False
        self.setup_masternodes_network(private_keys_list, self.number_of_simple_nodes)

    def run_test (self):
        tests = ['cache', 'sync', 'ping', 'restart', 'spent', 'fee']

        print("=== Test MN basics ===")
        self.mining_enough(1, 2)

        print("=== Test MN activation ===")
        cold_nodes = {k: v for k, v in enumerate(private_keys_list)}
        mn_ids, mn_aliases, _ = self.start_mn(self.mining_node_num, self.hot_node_num, cold_nodes, self.total_number_of_nodes)
        mn_id = mn_ids[self.cold_node_num]
        mn_alias = mn_aliases[self.cold_node_num]

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'fee' in tests:
            print("=== Test MN Fee ===")
            nfee_mn0 = self.nodes[0].storagefee("getnetworkfee")["networkfee"]
            nfee_mn1 = self.nodes[1].storagefee("getnetworkfee")["networkfee"]
            nfee_mn2 = self.nodes[2].storagefee("getnetworkfee")["networkfee"]
            assert_equal(nfee_mn0, 100)
            assert_equal(nfee_mn1, 100)
            assert_equal(nfee_mn2, 100)
            print("Network fee is ", nfee_mn0)

            lfee_mn0 = self.nodes[0].storagefee("getlocalfee")["localfee"]
            assert_equal(lfee_mn0, 100)
            print("Local fee of MN0 is ", lfee_mn0)

        #print("Test sync after crash")
        # 1. kill (not gracefully) node0 (masternode)
        # 2. start node0 again 
        # 3. Check all nodes
        # wait_for_it(120, 20, "ENABLED", self.nodes[0:self.total_number_of_nodes], mn_id)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'cache' in tests:
            print("=== Test cache save/load ===")
            print("Stopping node 1...")
            stop_node(self.nodes[1],1)
            print("Starting node 1...")
            self.nodes.append(start_node(1, self.options.tmpdir, ["-debug=masternode"]))
            connect_nodes_bi(self.nodes,1,0)
            connect_nodes_bi(self.nodes,1,2)
            self.sync_all()

            wait_for_it(10, 10, "ENABLED", self.nodes[0:self.total_number_of_nodes], mn_id)

            #Test disk cache 2
            print("Stopping node 0 - Masternode...")
            stop_node(self.nodes[0],0)
            print("Starting node 0 as Masternode...")
            self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=masternode", "-masternode", "-txindex=1", "-reindex", "-masternodeprivkey=91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe"]))
            connect_nodes_bi(self.nodes,0,1)
            connect_nodes_bi(self.nodes,0,2)
            self.sync_all()

            wait_for_it(20, 10, "ENABLED", self.nodes[0:self.total_number_of_nodes], mn_id)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'sync' in tests:
            print("=== Test MN list sync ===")
            print("Test new node sync")
            print("Starting node 3...")
            self.nodes.append(start_node(3, self.options.tmpdir, ["-debug=masternode"]))
            connect_nodes_bi(self.nodes,3,0)
            connect_nodes_bi(self.nodes,3,1)
            connect_nodes_bi(self.nodes,3,2)
            self.total_number_of_nodes = 4
            self.sync_all()

            wait_for_it(20, 10, "ENABLED", self.nodes[0:self.total_number_of_nodes], mn_id)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'ping' in tests:
            print("=== Test Ping ===")
            print("Stopping node 0 - Masternode...")
            stop_node(self.nodes[0],0)

            wait_for_it(150, 50, "EXPIRED", self.nodes[1:self.total_number_of_nodes], mn_id)

            print("Starting node 0 as Masternode again...")
            self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=masternode", "-masternode", "-txindex=1", "-reindex", "-masternodeprivkey=91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe"]))
            connect_nodes_bi(self.nodes,0,1)
            connect_nodes_bi(self.nodes,0,2)
            if self.total_number_of_nodes > 3:
                connect_nodes_bi(self.nodes,0,3)
            self.sync_all()
            
            wait_for_it(120, 20, "ENABLED", self.nodes[0:self.total_number_of_nodes], mn_id)
        
        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'restart' in tests:
            print("=== Test 'restart required' ===")
            print("Stopping node 0 - Masternode...")
            stop_node(self.nodes[0],0)

            wait_for_it(150, 50, "EXPIRED", self.nodes[1:self.total_number_of_nodes], mn_id)
            wait_for_it(360, 30, "NEW_START_REQUIRED", self.nodes[1:self.total_number_of_nodes], mn_id)

            print("Starting node 0 as Masternode again...")
            self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=masternode", "-masternode", "-txindex=1", "-reindex", "-masternodeprivkey=91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe"]))
            connect_nodes_bi(self.nodes,0,1)
            connect_nodes_bi(self.nodes,0,2)
            if self.total_number_of_nodes > 3:
                connect_nodes_bi(self.nodes,0,3)
            self.sync_all()

            print("Enabling node 0 as MN again (start-alias from node 2)...")
            res = self.nodes[2].masternode("start-alias", mn_alias)
            print(res)
            assert_equal(res["alias"], mn_alias)
            assert_equal(res["result"], "successful")

            # wait_for_it(30, 10, "PRE_ENABLED", self.nodes[0:self.total_number_of_nodes], mn_id, 6)
            wait_for_it(120, 20, "ENABLED", self.nodes[0:self.total_number_of_nodes], mn_id, 5)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent', "fee"]
        if 'spent' in tests:
            print("=== Test MN Spent ===")

            assert_equal(self.nodes[2].getbalance(), self.collateral)
            usp = self.nodes[2].listlockunspent()
            print("{0}-{1}".format(usp[0]['txid'],usp[0]['vout']))
            callateral_outpoint = mn_id.split('-')
            assert_equal(usp[0]['txid'], callateral_outpoint[0])
            assert_equal(Decimal(usp[0]['vout']), Decimal(callateral_outpoint[1]))

            print("Unlocking locked output...")
            locked = [{"txid":usp[0]['txid'], "vout":usp[0]['vout']}]
            assert_equal(self.nodes[2].lockunspent(True, locked), True)

            print("Sending 100 coins from node 2 to node 1...")
            newaddr = self.nodes[1].getnewaddress()
            self.nodes[2].sendtoaddress(newaddr, 100, "", "", False)
            self.sync_all()
            self.nodes[1].generate(1)
            self.sync_all()

            balance = self.nodes[2].getbalance()
            print(balance)
            assert_greater_than(Decimal(str(self.collateral)), Decimal(balance))

            print(self.nodes[0].masternode("status")["status"])
            # wait_for_it(10, 10, "OUTPOINT_SPENT", self.nodes[0:self.total_number_of_nodes], mn_id, 3)

            for _ in range(10):
                result = self.nodes[0].masternode("status")["status"]
                if result != "Not capable masternode: Masternode not in masternode list":
                    print(result)
                    print('Waiting 20 seconds...')
                    time.sleep(20)
                else: break

            print(self.nodes[0].masternode("status")["status"])
            assert_equal(self.nodes[0].masternode("status")["status"], "Not capable masternode: Masternode not in masternode list")

        print("All set...")

if __name__ == '__main__':
    MasterNodeMainTest ().main ()
