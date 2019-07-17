#!/usr/bin/env python2
# Copyright (c) 2018 The Pastel developers
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
import itertools
import json
import random

from decimal import Decimal, getcontext
getcontext().prec = 16

class MasterNodeCommon (BitcoinTestFramework):
    def setup_masternodes_network(self, private_keys_list, number_of_non_mn_to_start=0, debug_flags="masternode,mnpayments,governance"):
        for index, key in enumerate(private_keys_list):
            print("start MN {0}".format(index))
            self.nodes.append(start_node(index, self.options.tmpdir, ["-debug={0}".format(debug_flags), "-masternode", "-txindex=1", "-reindex", "-masternodeprivkey={0}".format(key)]))
        
        for index2 in range (index+1, index+number_of_non_mn_to_start+1):
            print("start non-MN {0}".format(index2))
            self.nodes.append(start_node(index2, self.options.tmpdir, ["-debug={0}".format(debug_flags)]))

        for pair in itertools.combinations(range(index2+1), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])

    def mining_enough(self, mining_node_num, nodes_to_start):
        
        min_blocks_to_mine = nodes_to_start*1000/self._reward
        blocks_to_mine = max(min_blocks_to_mine, 100)

        print("Mining {0} blocks on node {1}...".format(blocks_to_mine, mining_node_num))
        
        self.nodes[mining_node_num].generate(blocks_to_mine)
        self.sync_all()
        self.nodes[mining_node_num].generate(100)
        self.sync_all()

        assert_equal(self.nodes[mining_node_num].getbalance(), self._reward*blocks_to_mine)

    def start_mn(self, mining_node_num, hot_node_num, cold_nodes, num_of_nodes):
        
        mn_ids = dict()
        mn_aliases = dict()
        mn_collateral_addresses = dict()

        #1
        for ind, num in enumerate(cold_nodes):
            collateral_address = self.nodes[hot_node_num].getnewaddress()
            print("{0}: Sending 1000 coins to node {1}; collateral address {2} ...".format(ind, hot_node_num, collateral_address))
            collateral_txid = self.nodes[mining_node_num].sendtoaddress(collateral_address, 1000, "", "", False)

            self.sync_all()
            self.nodes[mining_node_num].generate(1)
            self.sync_all()
            
            assert_equal(self.nodes[hot_node_num].getbalance(), 1000*(ind+1))
            
            # print("node {0} collateral outputs".format(hot_node_num))
            # print(self.nodes[hot_node_num].masternode("outputs"))
            
            collateralvin = self.nodes[hot_node_num].masternode("outputs")[collateral_txid]
            mn_ids[num] = str(collateral_txid) + "-" + str(collateralvin)
            mn_collateral_addresses[num] = collateral_address

        #2
        print("Stopping node {0}...".format(hot_node_num))
        stop_node(self.nodes[hot_node_num], hot_node_num)

        #3
        print("Creating masternode.conf for node {0}...".format(hot_node_num))
        for num, key in cold_nodes.iteritems():
            mn_alias = "mn{0}".format(num)
            c_txid, c_vin = mn_ids[num].split('-')
            print("{0}  127.0.0.1:{1} {2} {3} {4}".format(mn_alias, p2p_port(num), key, c_txid, c_vin))
            create_masternode_conf(mn_alias, hot_node_num, self.options.tmpdir, c_txid, c_vin, key, p2p_port(num))
            mn_aliases[num] = mn_alias

        #4
        print("Starting node {0}...".format(hot_node_num))
        self.nodes[hot_node_num]=start_node(hot_node_num, self.options.tmpdir, ["-debug=masternode", "-txindex=1", "-reindex"], timewait=900)
        for i in range(num_of_nodes):
            if i != hot_node_num:
                connect_nodes_bi(self.nodes, hot_node_num, i)
        
        print("Waiting 90 seconds...")
        time.sleep(90)
        print("Checking sync status of node {0}...".format(hot_node_num))
        assert_equal(self.nodes[hot_node_num].mnsync("status")["IsSynced"], True)
        assert_equal(self.nodes[hot_node_num].mnsync("status")["IsFailed"], False)

        #5
        for _, mn_alias in mn_aliases.iteritems():
            print("Enabling MN {0}...".format(mn_alias))
            res = self.nodes[hot_node_num].masternode("start-alias", mn_alias)
            print(res)        
            assert_equal(res["alias"], mn_alias)
            assert_equal(res["result"], "successful")
            time.sleep(1)
        
        print("Waiting for PRE_ENABLED...")
        for ind, num in enumerate(mn_ids):
            wait = 30 if ind == 0 else 0
            wait_for_it(wait, 10, "PRE_ENABLED", self.nodes[0:num_of_nodes], mn_ids[num])

        print("Waiting for ENABLED...")
        for ind, num in enumerate(mn_ids):
            wait = 120 if ind == 0 else 0
            wait_for_it(wait, 20, "ENABLED", self.nodes[0:num_of_nodes], mn_ids[num])

        return mn_ids, mn_aliases, mn_collateral_addresses

    def reconnect_nodes(self, fromindex, toindex):
        for pair in itertools.combinations(range(fromindex, toindex), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])        

def create_masternode_conf(name, n, dirname, txid, vin, private_key, mn_port):
    datadir = os.path.join(dirname, "node"+str(n))
    if not os.path.isdir(datadir):
        os.makedirs(datadir)    
    regtestdir = os.path.join(datadir, "regtest")
    if not os.path.isdir(regtestdir):
        os.makedirs(regtestdir)    
    
    cfg_file = os.path.join(regtestdir, "masternode.conf")
    
    config = {}
    if os.path.isfile(cfg_file):
        with open(cfg_file) as json_file:  
            config = json.load(json_file)

    config[name] = {}
    config[name]["mnAddress"] = "127.0.0.1:" + str(mn_port)
    config[name]["mnPrivKey"] = str(private_key)
    config[name]["txid"] = str(txid)
    config[name]["outIndex"] = str(vin)
    config[name]["extAddress"] = "127.0.0.1:" + str(random.randint(2000, 5000))
    config[name]["extKey"] = ''.join(random.sample(private_key,len(private_key)))
    config[name]["extCfg"] = {}
    config[name]["extCfg"]["param1"] = str(random.randint(0, 9))
    config[name]["extCfg"]["param2"] = str(random.randint(0, 9))

    with open(cfg_file, 'w') as f:
        json.dump(config, f, indent=4)
    return datadir

def wait_for_it(init_wait, more_wait, wait_for, node_list, mnId, repeatMore=1):
    debug = False
    print('Waiting {0:d} seconds...'.format(init_wait))
    time.sleep(init_wait)

    for _ in range(repeatMore):
        result = all(node.masternode("list")[mnId] == wait_for for node in node_list)
        if not result:
            if debug:
                [print(node.masternode("list")) for node in node_list]
            print('Waiting {0:d} seconds more...'.format(more_wait))
            time.sleep(more_wait)
        else:
            break

    if debug:
        [print(node.masternode("list")) for node in node_list]

    [assert_equal(node.masternode("list")[mnId], wait_for) for node in node_list]
