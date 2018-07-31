#!/usr/bin/env python2
# Copyright (c) 2018 The Anime developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from __future__ import print_function

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, initialize_chain_clean, \
    initialize_datadir, start_nodes, start_node, connect_nodes_bi, \
    bitcoind_processes, wait_and_assert_operationid_status, p2p_port, \
    stop_node

import os
import sys
import time

from decimal import Decimal, getcontext
getcontext().prec = 16

class MasterNodeMainTest (BitcoinTestFramework):
    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def setup_network(self, split=False):
        self.nodes = []
        self.is_network_split = False
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=masternode", "-masternode", "-txindex=1", "-reindex", "-masternodeprivkey=91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe"]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug=masternode"]))
        self.nodes.append(start_node(2, self.options.tmpdir, ["-debug=masternode"]))
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)

    def run_test (self):
        tests = ['cache', 'sync', 'ping', 'restart', 'spent']
        num_of_nodes = 3

        print("=== Test MN basics ===")
        print("Mining blocks on node 1...")
        self.nodes[1].generate(100)
        self.sync_all()

        self.nodes[1].generate(100)
        self.sync_all()

        assert_equal(self.nodes[1].getbalance(), self._reward*100)    # node_1 has 100 blocks over maturity

        print("=== Test MN activation ===")
        print("Sending 1000 coins to node 2...")
        collateraladdr = self.nodes[2].getnewaddress()
        collateraltxid = self.nodes[1].sendtoaddress(collateraladdr, 1000, "", "", False)
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()

        assert_equal(self.nodes[2].getbalance(), 1000)    # node_1 has 100 blocks over maturity

        print("node 2 collateral outputs")
        print(self.nodes[2].masternode("outputs"))

        collateralvin = self.nodes[2].masternode("outputs")[collateraltxid]

        print("Stopping node 2...")
        stop_node(self.nodes[2],2)

        mnId = str(collateraltxid) + "-" + str(collateralvin)

        print("Creating masternode.conf for node 2...")
        create_masternode_conf(2, self.options.tmpdir, collateraltxid, collateralvin, "91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe", p2p_port(0))

        print("Starting node 2...")
        self.nodes[2]=start_node(2, self.options.tmpdir, ["-debug=masternode", "-txindex=1", "-reindex"], timewait=900)
        connect_nodes_bi(self.nodes,2,0)
        connect_nodes_bi(self.nodes,2,1)

        print("Wating 90 seconds...")
        time.sleep(90)

        print("Checking sync status of node 2...")
        # print(self.nodes[2].mnsync("status"))
        assert_equal(self.nodes[2].mnsync("status")["IsBlockchainSynced"], True)
        assert_equal(self.nodes[2].mnsync("status")["IsMasternodeListSynced"], True)
        assert_equal(self.nodes[2].mnsync("status")["IsSynced"], True)
        assert_equal(self.nodes[2].mnsync("status")["IsFailed"], False)

        print("Enabling MN...")
        res = self.nodes[2].masternode("start-alias", "mn1")
        print(res)
        assert_equal(res["alias"], "mn1")
        assert_equal(res["result"], "successful")

        wait_for_it(30, 10, "PRE_ENABLED", self.nodes[0:num_of_nodes], mnId)
        wait_for_it(120, 20, "ENABLED", self.nodes[0:num_of_nodes], mnId)

        #print("Test sync after crash")
        # 1. kill (not gracefully) node0 (masternode)
        # 2. start node0 again 
        # 3. Check all nodes
        # wait_for_it(120, 20, "ENABLED", self.nodes[0:num_of_nodes], mnId)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent']
        if 'cache' in tests:
            print("=== Test cache save/load ===")
            print("Stopping node 1...")
            stop_node(self.nodes[1],1)
            print("Starting node 1...")
            self.nodes.append(start_node(1, self.options.tmpdir, ["-debug=masternode"]))
            connect_nodes_bi(self.nodes,1,0)
            connect_nodes_bi(self.nodes,1,2)

            wait_for_it(10, 10, "ENABLED", self.nodes[0:num_of_nodes], mnId)

            #Test disk cache 2
            print("Stopping node 0 - Masternode...")
            stop_node(self.nodes[0],0)
            print("Starting node 0 as Masternode...")
            self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=masternode", "-masternode", "-txindex=1", "-reindex", "-masternodeprivkey=91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe"]))
            connect_nodes_bi(self.nodes,0,1)
            connect_nodes_bi(self.nodes,0,2)

            wait_for_it(20, 10, "ENABLED", self.nodes[0:num_of_nodes], mnId)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent']
        if 'sync' in tests:
            print("=== Test MN list sync ===")
            print("Test new node sync")
            print("Starting node 3...")
            self.nodes.append(start_node(3, self.options.tmpdir, ["-debug=masternode"]))
            connect_nodes_bi(self.nodes,3,0)
            connect_nodes_bi(self.nodes,3,1)
            connect_nodes_bi(self.nodes,3,2)
            num_of_nodes = 4

            wait_for_it(20, 10, "ENABLED", self.nodes[0:num_of_nodes], mnId)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent']
        if 'ping' in tests:
            print("=== Test Ping ===")
            print("Stopping node 0 - Masternode...")
            stop_node(self.nodes[0],0)

            wait_for_it(150, 50, "EXPIRED", self.nodes[1:num_of_nodes], mnId)

            print("Starting node 0 as Masternode again...")
            self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=masternode", "-masternode", "-txindex=1", "-reindex", "-masternodeprivkey=91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe"]))
            connect_nodes_bi(self.nodes,0,1)
            connect_nodes_bi(self.nodes,0,2)
            if num_of_nodes > 3:
                connect_nodes_bi(self.nodes,0,3)
            
            wait_for_it(120, 20, "ENABLED", self.nodes[0:num_of_nodes], mnId)
        
        # tests = ['cache', 'sync', 'ping', 'restart', 'spent']
        if 'restart' in tests:
            print("=== Test 'restart required' ===")
            print("Stopping node 0 - Masternode...")
            stop_node(self.nodes[0],0)

            wait_for_it(150, 50, "EXPIRED", self.nodes[1:num_of_nodes], mnId)
            wait_for_it(360, 30, "NEW_START_REQUIRED", self.nodes[1:num_of_nodes], mnId)

            print("Starting node 0 as Masternode again...")
            self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=masternode", "-masternode", "-txindex=1", "-reindex", "-masternodeprivkey=91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe"]))
            connect_nodes_bi(self.nodes,0,1)
            connect_nodes_bi(self.nodes,0,2)
            if num_of_nodes > 3:
                connect_nodes_bi(self.nodes,0,3)

            print("Enabling node 0 as MN again (start-alias from node 2)...")
            res = self.nodes[2].masternode("start-alias", "mn1")
            print(res)
            assert_equal(res["alias"], "mn1")
            assert_equal(res["result"], "successful")

            # wait_for_it(30, 10, "PRE_ENABLED", self.nodes[0:num_of_nodes], mnId, 6)
            wait_for_it(120, 20, "ENABLED", self.nodes[0:num_of_nodes], mnId, 3)

        # tests = ['cache', 'sync', 'ping', 'restart', 'spent']
        if 'spent' in tests:
            print("=== Test MN Spent ===")

            assert_equal(self.nodes[2].getbalance(), 1000)
            usp = self.nodes[2].listlockunspent()
            print("{0}-{1}".format(usp[0]['txid'],usp[0]['vout']))
            assert_equal(usp[0]['txid'], collateraltxid)
            assert_equal(Decimal(usp[0]['vout']), Decimal(collateralvin))

            print("Unlocking locked output...")
            locked = [{"txid":usp[0]['txid'], "vout":usp[0]['vout']}]
            assert_equal(self.nodes[2].lockunspent(True, locked), True)

            print("Sending 100 coins from node 2 to node 1...")
            newaddr = self.nodes[1].getnewaddress()
            newtxid = self.nodes[2].sendtoaddress(newaddr, 100, "", "", False)
            self.sync_all()
            self.nodes[1].generate(1)
            self.sync_all()

            newbal = self.nodes[2].getbalance()
            print(newbal)
            assert_greater_than(Decimal("1000"), Decimal(newbal))

            print(self.nodes[0].masternode("status")["status"])
            wait_for_it(10, 10, "OUTPOINT_SPENT", self.nodes[0:num_of_nodes], mnId, 3)

            for _ in range(10):
                result = self.nodes[0].masternode("status")["status"]
                if result != "Not capable masternode: Masternode not in masternode list":
                    print(result)
                    print('Wating 20 seconds...')
                    time.sleep(20)
                else: break

            print(self.nodes[0].masternode("status")["status"])
            assert_equal(self.nodes[0].masternode("status")["status"], "Not capable masternode: Masternode not in masternode list")

        print("All set...")

def create_masternode_conf(n, dirname, txid, vin, privKey, mnPort):
    datadir = os.path.join(dirname, "node"+str(n))
    if not os.path.isdir(datadir):
        os.makedirs(datadir)    
    regtestdir = os.path.join(datadir, "regtest")
    if not os.path.isdir(regtestdir):
        os.makedirs(regtestdir)    
    with open(os.path.join(regtestdir, "masternode.conf"), 'w') as f:
        f.write("mn1 127.0.0.1:" + str(mnPort) + " " + str(privKey) + " " + str(txid) + " " + str(vin))
    return datadir

def wait_for_it(init_wait, more_wait, wait_for, node_list, mnId, repeatMore=1):
    debug = False
    print('Wating {0:d} seconds...'.format(init_wait))
    time.sleep(init_wait)

    for _ in range(repeatMore):
        result = all(node.masternode("list")[mnId] == wait_for for node in node_list)
        if not result:
            if debug:
                [print(node.masternode("list")) for node in node_list]
            print('Wating {0:d} seconds more...'.format(more_wait))
            time.sleep(more_wait)
        else:
            break

    if debug:
        [print(node.masternode("list")) for node in node_list]

    [assert_equal(node.masternode("list")[mnId], wait_for) for node in node_list]

if __name__ == '__main__':
    MasterNodeMainTest ().main ()
