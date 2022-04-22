#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pastel_test_framework import PastelTestFramework
from test_framework.util import (
    assert_equal, 
    start_node, 
    connect_nodes_bi, 
    p2p_port,
    stop_node
)
from test_framework.authproxy import JSONRPCException

import os
import time
import itertools
import json
import random

from decimal import Decimal, getcontext
getcontext().prec = 16

class MasterNodeCommon (PastelTestFramework):
    collateral = int(1000)

    def setup_masternodes_network(self, private_keys_list, number_of_non_mn_to_start=0, debug_flags="masternode,mnpayments,governance"):
        for index, key in enumerate(private_keys_list):
            print(f"start MN {index}")
            self.nodes.append(start_node(index, self.options.tmpdir, [f"-debug={debug_flags}", "-masternode", "-txindex=1", "-reindex", f"-masternodeprivkey={key}"]))
  
        for index2 in range (index+1, index+number_of_non_mn_to_start+1):
            print(f"start non-MN {index2}")
            self.nodes.append(start_node(index2, self.options.tmpdir, [f"-debug={debug_flags}"]))

        for pair in itertools.combinations(range(index2+1), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])


    def mining_enough(self, mining_node_num, nodes_to_start):
        min_blocks_to_mine = nodes_to_start*self.collateral/self._reward
        blocks_to_mine = int(max(min_blocks_to_mine, 100))

        print(f"Mining total of {blocks_to_mine} blocks on node {mining_node_num}...")

        while blocks_to_mine > 0:
            blocks_to_mine_part = int(min(blocks_to_mine, 100))
            blocks_to_mine -= blocks_to_mine_part
            print(f"Mining {blocks_to_mine_part} blocks on node {mining_node_num}...")
            self.nodes[mining_node_num].generate(blocks_to_mine_part)
        self.sync_all()
        self.nodes[mining_node_num].generate(100)
        self.sync_all()

        blocks_to_mine = int(max(min_blocks_to_mine, 100))
        assert_equal(self.nodes[mining_node_num].getbalance(), self._reward*blocks_to_mine)

    def start_mn(self, mining_node_num, hot_node_num, cold_nodes, num_of_nodes, 
        mock_time = None, preenabled_test_functor = None, postenabled_test_functor = None):

        mn_ids = dict()
        mn_aliases = dict()
        mn_collateral_addresses = dict()

        #1
        for ind, num in enumerate(cold_nodes):
            collateral_address = self.nodes[hot_node_num].getnewaddress()
            print(f"{ind}: Sending {self.collateral} coins to node {hot_node_num}; collateral address {collateral_address} ...")
            collateral_txid = self.nodes[mining_node_num].sendtoaddress(collateral_address, self.collateral, "", "", False)

            self.sync_all()
            self.nodes[mining_node_num].generate(1)
            self.sync_all()
            
            assert_equal(self.nodes[hot_node_num].getbalance(), self.collateral*(ind+1))
            
            # print("node {0} collateral outputs".format(hot_node_num))
            # print(self.nodes[hot_node_num].masternode("outputs"))
            
            collateralvin = self.nodes[hot_node_num].masternode("outputs")[collateral_txid]
            mn_ids[num] = str(collateral_txid) + "-" + str(collateralvin)
            mn_collateral_addresses[num] = collateral_address

        #2
        print(f"Stopping node {hot_node_num}...")
        stop_node(self.nodes[hot_node_num], hot_node_num)

        #3
        print(f"Creating masternode.conf for node {hot_node_num}...")
        for num, key in cold_nodes.items():
            mn_alias = f"mn{num}"
            c_txid, c_vin = mn_ids[num].split('-')
            print(f"{mn_alias}  127.0.0.1:{p2p_port(num)} {key} {c_txid} {c_vin}")
            self.create_masternode_conf(mn_alias, hot_node_num, self.options.tmpdir, c_txid, c_vin, key, p2p_port(num))
            mn_aliases[num] = mn_alias

        #4
        print(f"Starting node {hot_node_num}...")
        self.nodes[hot_node_num]=start_node(hot_node_num, self.options.tmpdir, ["-debug=masternode", "-txindex=1", "-reindex"], timewait=900)
        for i in range(num_of_nodes):
            if i != hot_node_num:
                connect_nodes_bi(self.nodes, hot_node_num, i)
        
        print("Waiting 90 seconds...")
        time.sleep(90)
        print(f"Checking sync status of node {hot_node_num}...")
        assert_equal(self.nodes[hot_node_num].mnsync("status")["IsSynced"], True)
        assert_equal(self.nodes[hot_node_num].mnsync("status")["IsFailed"], False)

        #5
        for mn_alias in mn_aliases.values():
            print(f"Enabling MN {mn_alias}...")
            res = self.nodes[hot_node_num].masternode("start-alias", mn_alias)
            print(res)        
            assert_equal(res["alias"], mn_alias)
            assert_equal(res["result"], "successful")
            time.sleep(1)

        if preenabled_test_functor is not None:
            preenabled_test_functor()
        
        print("Waiting for PRE_ENABLED...")
        for ind, num in enumerate(mn_ids):
            wait = 30 if ind == 0 else 0
            self.wait_for_mn_state(wait, 10, "PRE_ENABLED", self.nodes[0:num_of_nodes], mn_ids[num])

        def wait_for_enabled(byPassAssert):
            print("Waiting for ENABLED...")
            for ind, num in enumerate(mn_ids):
                wait = 120 if ind == 0 else 0
                self.wait_for_mn_state(wait, 20, "ENABLED", self.nodes[0:num_of_nodes], mn_ids[num], 1, byPassAssert)
        wait_for_enabled(False)

        if postenabled_test_functor is not None:
            postenabled_test_functor()

        if mock_time is not None:
            for node in self.nodes:
                node.setmocktime(mock_time)
            # For some reason the masternodes become disabled after setting the mock time, 
            # need to wait until the masternodes are enabled again.
            wait_for_enabled(True)

        return mn_ids, mn_aliases, mn_collateral_addresses


    def reconnect_nodes(self, fromindex, toindex):
        for pair in itertools.combinations(range(fromindex, toindex), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])        


    def wait_for_mn_state(self, init_wait, more_wait, wait_for, node_list, mnId, repeatMore=1, byPassAssert=False):
        debug = False
        print(f'Waiting {init_wait} seconds...')
        time.sleep(init_wait)

        for _ in range(repeatMore):
            result = all(node.masternode("list").get(mnId, "") == wait_for for node in node_list)
            if not result:
                if debug:
                    [print(node.masternode("list")) for node in node_list]
                print(f'Waiting {more_wait} seconds more...')
                time.sleep(more_wait)
            else:
                break
        if debug:
            [print(node.masternode("list")) for node in node_list]
        if not byPassAssert:
            [assert_equal(node.masternode("list").get(mnId, ""), wait_for) for node in node_list]

    def create_masternode_conf(self, name, n, dirname, txid, vin, private_key, mn_port):
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
        config[name]["extP2P"] = "127.0.0.1:" + str(random.randint(22000, 25000))
        config[name]["extKey"] = ''.join(random.sample(private_key,len(private_key)))
        config[name]["extCfg"] = {}
        config[name]["extCfg"]["param1"] = str(random.randint(0, 9))
        config[name]["extCfg"]["param2"] = str(random.randint(0, 9))

        with open(cfg_file, 'w') as f:
            json.dump(config, f, indent=4)
        return datadir
