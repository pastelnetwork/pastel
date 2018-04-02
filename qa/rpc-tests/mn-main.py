#!/usr/bin/env python2
# Copyright (c) 2018 The Anime developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
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
        print("Mining blocks on node 1...")
        self.nodes[1].generate(100)
        self.sync_all()

        self.nodes[1].generate(100)
        self.sync_all()

        assert_equal(self.nodes[1].getbalance(), self._reward*100)    # node_1 has 100 blocks over maturity

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

        print("Wating 60 seconds...")
        time.sleep(60)

        print("Checking sync status of node 2...")
        assert_equal(self.nodes[2].mnsync("status")["IsBlockchainSynced"], True)
        assert_equal(self.nodes[2].mnsync("status")["IsMasternodeListSynced"], True)
        assert_equal(self.nodes[2].mnsync("status")["IsSynced"], True)
        assert_equal(self.nodes[2].mnsync("status")["IsFailed"], False)

        print("Enabling MN...")
        res = self.nodes[2].masternode("start-alias", "mn1")
        print(res)
        assert_equal(res["alias"], "mn1")
        assert_equal(res["result"], "successful")

        print("Wating 10 seconds...")
        time.sleep(10)

        assert_equal(self.nodes[0].masternode("list")[mnId], "PRE_ENABLED")
        assert_equal(self.nodes[1].masternode("list")[mnId], "PRE_ENABLED")
        assert_equal(self.nodes[2].masternode("list")[mnId], "PRE_ENABLED")

        print("Wating 120 seconds...")
        time.sleep(120)

        assert_equal(self.nodes[0].masternode("list")[mnId], "ENABLED")
        assert_equal(self.nodes[1].masternode("list")[mnId], "ENABLED")
        assert_equal(self.nodes[2].masternode("list")[mnId], "ENABLED")

        #print("Test sync after crash")
        # 1. kill (not gracefully) node0 (masternode)
        # 2. start node0 again 
        # 3. Check all nodes
        # assert_equal(self.nodes[0].masternode("list")[mnId], "ENABLED")
        # assert_equal(self.nodes[1].masternode("list")[mnId], "ENABLED")
        # assert_equal(self.nodes[2].masternode("list")[mnId], "ENABLED")

        print("Test disk cache")
        print("Stopping node 1...")
        stop_node(self.nodes[1],1)
        print("Starting node 1...")
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug=masternode"]))
        print("Wating 10 seconds...")
        time.sleep(10)
        assert_equal(self.nodes[0].masternode("list")[mnId], "ENABLED")
        assert_equal(self.nodes[1].masternode("list")[mnId], "ENABLED")
        assert_equal(self.nodes[2].masternode("list")[mnId], "ENABLED")

        #Test disk cache 2
        print("Stopping node 0 - Masternode...")
        stop_node(self.nodes[0],0)
        print("Starting node 0 as Masternode...")
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=masternode", "-masternode", "-txindex=1", "-reindex", "-masternodeprivkey=91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe"]))
        print("Wating 20 seconds...")
        time.sleep(20)
        assert_equal(self.nodes[0].masternode("list")[mnId], "ENABLED")
        assert_equal(self.nodes[1].masternode("list")[mnId], "ENABLED")
        assert_equal(self.nodes[2].masternode("list")[mnId], "ENABLED")

        print("Test new node sync")
        print("Starting node 3...")
        self.nodes.append(start_node(3, self.options.tmpdir, ["-debug=masternode"]))
        print("Wating 20 seconds...")
        time.sleep(20)
        assert_equal(self.nodes[0].masternode("list")[mnId], "ENABLED")
        assert_equal(self.nodes[1].masternode("list")[mnId], "ENABLED")
        assert_equal(self.nodes[2].masternode("list")[mnId], "ENABLED")
        assert_equal(self.nodes[3].masternode("list")[mnId], "ENABLED")
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

if __name__ == '__main__':
    MasterNodeMainTest ().main ()
