#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
from pathlib import Path
import os
import time
import itertools
import json
import random
from decimal import getcontext
from pastel_test_framework import PastelTestFramework
from test_framework.util import (
    assert_equal,
    start_node,
    connect_nodes_bi,
    p2p_port,
    stop_node
)
getcontext().prec = 16

class MasterNodeCommon (PastelTestFramework):
    collateral = int(1000)

    class MasterNode:
        index = None            # masternode index
        passphrase = None       # passphrase to access secure container data
        mnid = None             # generated Pastel ID
        lrKey = None            # generated LegRoast key
        privKey = None          # generated private key
        port = None
        collateral_address = None
        collateral_txid = None
        collateral_index = None

        def __init__(self, index, passphrase):
            self.index = index
            self.passphrase = passphrase
            self.port = p2p_port(index)

        @property
        def id(self) -> str:
            """ masternode collateral id property

            Returns:
                str: masternode collateral id (txid-index)
            """
            return str(self.collateral_txid) + "-" + str(self.collateral_index)

        @property
        def alias(self) -> str:
            """masternode alias property

            Returns:
                str: masternode alias
            """
            return f'mn{self.index}'

        def create_masternode_conf(self, dirname):
            """create masternode.conf for this masternode

            Args:
                dirname (str): node base directory

            Returns:
                str: node data directory
            """
            datadir = Path(dirname, f"node{self.index}")
            if not datadir.is_dir():
                datadir.mkdir()
            regtestdir = datadir / "regtest"
            if not regtestdir.is_dir():
                regtestdir.mkdir()
            cfg_file = regtestdir / "masternode.conf"
        
            config = {}
            if cfg_file.is_file():
                with cfg_file.open() as json_file:  
                    config = json.load(json_file)

            name = self.alias
            config[name] = {}
            config[name]["mnAddress"] = f"127.0.0.1:{self.port}"
            config[name]["mnPrivKey"] = self.privKey
            config[name]["txid"] = self.collateral_txid
            config[name]["outIndex"] = str(self.collateral_index)
            config[name]["extAddress"] = f"127.0.0.1:{random.randint(2000, 5000)}"
            config[name]["extP2P"] = f"127.0.0.1:{random.randint(22000, 25000)}"
            config[name]["extKey"] = self.mnid
            config[name]["extCfg"] = {}
            config[name]["extCfg"]["param1"] = str(random.randint(0, 9))
            config[name]["extCfg"]["param2"] = str(random.randint(0, 9))

            print(f"Creating masternode.conf for node {name}...")
            with cfg_file.open('w') as f:
                json.dump(config, f, indent=4)
            return str(datadir)


    mnNodes = list()

    def setup_masternodes_network(self, private_keys_list, number_of_non_mn_to_start=0, debug_flags=""):
        for index, key in enumerate(private_keys_list):
            print(f"start MN {index}")
            self.nodes.append(start_node(index, self.options.tmpdir, [f"-debug={debug_flags}", "-masternode", "-txindex=1", "-reindex", f"-masternodeprivkey={key}"]))
        
        for index2 in range (index+1, index+number_of_non_mn_to_start+1):
            print(f"start non-MN {index2}")
            self.nodes.append(start_node(index2, self.options.tmpdir, [f"-debug={debug_flags}"]))

        for pair in itertools.combinations(range(index2+1), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])

    def setup_masternodes_network_new(self, mn_count=1, non_mn_count=2, mining_node_num=1, hot_node_num=2, debug_flags=""):
        # create list of mns
        # start only non-mn nodes
        for index in range(mn_count + non_mn_count):
            if index < mn_count:
                mn = self.MasterNode(index, self.passphrase)
                self.mnNodes.append(mn)
                self.nodes.append(None) # add dummy node for now
            else:
                print(f"starting non-mn{index} node")
                self.nodes.append(start_node(index, self.options.tmpdir, [f"-debug={debug_flags}"]))

        # connect non-mn nodes
        for pair in itertools.combinations(range(mn_count, mn_count + non_mn_count), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])

        # mining enough coins for collateral for mn_count nodes
        self.mining_enough(mining_node_num, mn_count)

        # send coins to the collateral address on hot node
        for index, mn in enumerate(self.mnNodes):
            mn.collateral_address = self.nodes[hot_node_num].getnewaddress()
            print(f"{index}: Sending {self.collateral} coins to node {hot_node_num}; collateral address {mn.collateral_address} ...")
            mn.collateral_txid = self.nodes[mining_node_num].sendtoaddress(mn.collateral_address, self.collateral, "", "", False)
            
        self.generate_and_sync_inc(1, mining_node_num)
        assert_equal(self.nodes[hot_node_num].getbalance(), self.collateral * len(self.mnNodes))
        
        # prepare parameters and create masternode.conf for all masternodes
        for index, mn in enumerate(self.mnNodes):
            # get the collateral outpoint indexes
            outputs = self.nodes[hot_node_num].masternode("outputs")
            mn.collateral_index = int(outputs[mn.collateral_txid])
            print(f"node{hot_node_num} collateral outputs: {outputs}")
            # generate info for masternodes (masternode init)
            params = self.nodes[hot_node_num].masternode("init", mn.passphrase, mn.collateral_txid, mn.collateral_index)
            mn.mnid = params["mnid"]
            mn.lrKey = params["legRoastKey"]
            mn.privKey = params["privKey"]
            print(f"mn{index} id: {mn.mnid}")
            mn.create_masternode_conf(self.options.tmpdir)
        self.generate_and_sync_inc(1, mining_node_num)

        # starting masternodes
        for index, mn in enumerate(self.mnNodes):
            print(f"starting {mn.alias} masternode")
            self.nodes[index] = start_node(index, self.options.tmpdir, [f"-debug={debug_flags}", "-masternode", "-txindex=1", "-reindex", f"-masternodeprivkey={mn.privKey}"])
        
        # connect nodes (non-mn nodes already interconnected)
        for pair in itertools.combinations(range(mn_count + non_mn_count), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])

        self.sync_all()

    def mining_enough(self, mining_node_num, nodes_to_start):
        min_blocks_to_mine = nodes_to_start*self.collateral/self._reward
        blocks_to_mine = int(max(min_blocks_to_mine, 100))

        print(f"Mining total of {blocks_to_mine} blocks on node {mining_node_num}...")

        while blocks_to_mine > 0:
            blocks_to_mine_part = int(min(blocks_to_mine, 100))
            blocks_to_mine -= blocks_to_mine_part
            print(f"Mining {blocks_to_mine_part} blocks on node {mining_node_num}...")
            self.generate_and_sync_inc(blocks_to_mine_part, mining_node_num)
        self.generate_and_sync_inc(100, mining_node_num)

        blocks_to_mine = int(max(min_blocks_to_mine, 100))
        assert_equal(self.nodes[mining_node_num].getbalance(), self._reward*blocks_to_mine)


    def start_mn(self, mining_node_num, hot_node_num, cold_nodes, num_of_nodes):
        
        mn_ids = dict()
        mn_aliases = dict()
        mn_collateral_addresses = dict()

        #1
        for ind, num in enumerate(cold_nodes):
            collateral_address = self.nodes[hot_node_num].getnewaddress()
            print(f"{ind}: Sending {self.collateral} coins to node {hot_node_num}; collateral address {collateral_address} ...")
            collateral_txid = self.nodes[mining_node_num].sendtoaddress(collateral_address, self.collateral, "", "", False)

            self.generate_and_sync_inc(1, mining_node_num)
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
        
        print("Waiting for PRE_ENABLED...")
        for ind, num in enumerate(mn_ids):
            wait = 30 if ind == 0 else 0
            self.wait_for_mn_state(wait, 10, "PRE_ENABLED", self.nodes[0:num_of_nodes], mn_ids[num])

        print("Waiting for ENABLED...")
        for ind, num in enumerate(mn_ids):
            wait = 120 if ind == 0 else 0
            self.wait_for_mn_state(wait, 20, "ENABLED", self.nodes[0:num_of_nodes], mn_ids[num])

        return mn_ids, mn_aliases, mn_collateral_addresses


    def reconnect_nodes(self, fromindex, toindex):
        for pair in itertools.combinations(range(fromindex, toindex), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])        


    def wait_for_mn_state(self, init_wait, more_wait, wait_for, node_list, mnId, repeatMore=1):
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
