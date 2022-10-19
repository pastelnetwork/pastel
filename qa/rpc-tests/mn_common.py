#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
from pathlib import Path
import time
import itertools
import json
import random
from decimal import getcontext

from pastel_test_framework import PastelTestFramework
from test_framework.util import (
    assert_true,
    assert_equal,
    start_node,
    connect_nodes_bi,
    p2p_port,
    Timer
)
getcontext().prec = 16

class MasterNodeCommon (PastelTestFramework):

    """ Class to represent MasterNode.
    """
    class MasterNode:
        index: int = None            # masternode index
        passphrase: str = None       # passphrase to access secure container data
        mnid: str = None             # generated Pastel ID
        lrKey: str = None            # generated LegRoast key
        privKey: str = None          # generated private key
        port: int = None
        collateral_address: str = None
        collateral_txid: str = None
        collateral_index: int = None
        mnid_reg_address: str = None # address for mnid registration
        mnid_reg_txid: str = None    # txid for mnid registration   

        def __init__(self, index, passphrase):
            self.index = index
            self.passphrase = passphrase
            self.port = p2p_port(index)


        @property
        def collateral_id(self) -> str:
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


        def create_masternode_conf(self, dirname: str, node_index: int):
            """create masternode.conf for this masternode

            Args:
                dirname (str): node base directory

            Returns:
                str: node data directory
            """
            datadir = Path(dirname, f"node{node_index}")
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
            config[name]["extKey"] = self.mnid if self.mnid else ""
            config[name]["extCfg"] = {}
            config[name]["extCfg"]["param1"] = str(random.randint(0, 9))
            config[name]["extCfg"]["param2"] = str(random.randint(0, 9))

            print(f"Creating masternode.conf for node {name}...")
            with cfg_file.open('w') as f:
                json.dump(config, f, indent=4)
            return str(datadir)


    def __init__(self):
        super().__init__()

        # list of master nodes (MasterNode)
        self.mn_nodes = []
        self.collateral = int(1000)
        # flag to use new "masternode init" API
        self.use_masternode_init = False


    def setup_masternodes_network(self, mn_count=1, non_mn_count: int = 2,
                                  mining_node_num=1, hot_node_num=2, cold_node_count=1,
                                  debug_flags: str = ""):
        timer = Timer()
        timer.start()
        # create list of mns
        # start only non-mn nodes
        for index in range(mn_count + non_mn_count):
            if index < mn_count:
                mn = self.MasterNode(index, self.passphrase)
                self.mn_nodes.append(mn)
                self.nodes.append(None) # add dummy node for now
            else:
                print(f"starting non-mn{index} node")
                self.nodes.append(start_node(index, self.options.tmpdir, [f"-debug={debug_flags}"]))

        # connect non-mn nodes
        for pair in itertools.combinations(range(mn_count, mn_count + non_mn_count), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])

        # mining enough coins for collateral for mn_count nodes
        self.mining_enough(mining_node_num, mn_count)

        # hot node will keep all collateral amounts in its wallet to activate all master nodes
        for mn in self.mn_nodes:
            # create collateral address for each MN
            # number of nodes to activate is defined by cold_node_count
            if mn.index >= cold_node_count:
                continue
            mn.collateral_address = self.nodes[hot_node_num].getnewaddress()
            # send coins to the collateral addresses on hot node
            print(f"{mn.index}: Sending {self.collateral} coins to node{hot_node_num}; collateral address {mn.collateral_address} ...")
            mn.collateral_txid = self.nodes[mining_node_num].sendtoaddress(mn.collateral_address, self.collateral, "", "", False)

        self.generate_and_sync_inc(1, mining_node_num)
        assert_equal(self.nodes[hot_node_num].getbalance(), self.collateral * cold_node_count)

        # prepare parameters and create masternode.conf for all masternodes
        outputs = self.nodes[hot_node_num].masternode("outputs")
        print(f"hot node{hot_node_num} collateral outputs\n{outputs}")
        for mn in self.mn_nodes:
            # get the collateral outpoint indexes
            if mn.index < cold_node_count:
                mn.collateral_index = int(outputs[mn.collateral_txid])
            if self.use_masternode_init:
                # generate info for masternodes (masternode init)
                params = self.nodes[hot_node_num].masternode("init", mn.passphrase, mn.collateral_txid, mn.collateral_index)
                mn.mnid = params["mnid"]
                mn.lrKey = params["legRoastKey"]
                mn.privKey = params["privKey"]
                print(f"mn{index} id: {mn.mnid}")
                mn.create_masternode_conf(self.options.tmpdir, hot_node_num)
            else:
                mn.privKey = self.nodes[hot_node_num].masternode("genkey")
                assert_true(mn.privKey, "Failed to generate private key for Master Node")
        if self.use_masternode_init:
            self.generate_and_sync_inc(1, mining_node_num)

        # starting all master nodes
        for mn in self.mn_nodes:
            print(f"starting masternode {mn.alias}")
            self.nodes[mn.index] = start_node(mn.index, self.options.tmpdir, [f"-debug={debug_flags}", "-masternode", "-txindex=1", "-reindex", f"-masternodeprivkey={mn.privKey}"])

        # connect nodes (non-mn nodes already interconnected)
        for pair in itertools.combinations(range(mn_count + non_mn_count), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])

        self.sync_all()
        # wait for sync status for up to 2 mins
        wait_counter = 24
        while not all(self.nodes[mn.index].mnsync("status")["IsSynced"] for mn in self.mn_nodes):
            time.sleep(5)
            wait_counter -= 1
            assert_true(wait_counter, "Timeout period elapsed waiting for the MN synced status")

        # for old MN initialization
        if not self.use_masternode_init:
            # create & register mnid
            for mn in self.mn_nodes:
                mn.mnid, mn.lrKey = self.create_pastelid(mn.index)
                if mn.index >= cold_node_count:
                    continue
                # get new address and send some coins for mnid registration
                mn.mnid_reg_address = self.nodes[mn.index].getnewaddress()
                self.nodes[mining_node_num].sendtoaddress(mn.mnid_reg_address, 100, "", "", False)
                mn.create_masternode_conf(self.options.tmpdir, hot_node_num)
            self.generate_and_sync_inc(1, mining_node_num)

            for mn in self.mn_nodes:
                if mn.index >= cold_node_count:
                    continue
                print(f"Enabling master node: {mn.alias}...")
                res = self.nodes[hot_node_num].masternode("start-alias", mn.alias)
                print(res)
                assert_equal(res["alias"], mn.alias)
                assert_equal(res["result"], "successful")

            print("Waiting for PRE_ENABLED status...")
            initial_wait = 10
            for mn in self.mn_nodes:
                if mn.index >= cold_node_count:
                    continue
                self.wait_for_mn_state(initial_wait, 10, "PRE_ENABLED", mn.index, 3)
                initial_wait = 0
                
            # register mnids
            for mn in self.mn_nodes:
                if mn.index >= cold_node_count:
                    continue
                result = self.nodes[mn.index].tickets("register", "mnid", mn.mnid,
                    self.passphrase, mn.mnid_reg_address)
                mn.mnid_reg_txid = result["txid"]
                self.generate_and_sync_inc(1, mining_node_num)
            # wait for ticket transactions
            time.sleep(10)
            for _ in range(5):
                self.generate_and_sync_inc(1, mining_node_num)
                self.sync_all(10, 3)
            self.sync_all(10, 30)


            print("Waiting for ENABLED status...")
            initial_wait = 15
            for mn in self.mn_nodes:
                if mn.index >= cold_node_count:
                    continue
                self.wait_for_mn_state(initial_wait, 10, "ENABLED", mn.index, 10)
                initial_wait = 0
     
        timer.stop()
        print(f"<<<< MasterNode network INITIALIZED in {timer.elapsed_time} secs >>>>")


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


    def reconnect_nodes(self, fromindex: int, toindex: int):
        for pair in itertools.combinations(range(fromindex, toindex), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])

    def reconnect_all_nodes(self):
        """ Reconnect all nodes.
        """
        self.reconnect_nodes(0, len(self.nodes))

    def reconnect_node(self, index: int):
        """ Reconnect the node with the given index.

        Args:
            index (int): Node to reconnect to all other nodes
        """
        for i in range(len(self.nodes)):
            if i != index:
                connect_nodes_bi(self.nodes, index, i)
    
    def wait_for_mn_state(self, init_wait: int, more_wait: int, wait_for_state: str, mn_index: int, repeat_count: int = 1, node_list = None):
        """Wait for the specific MN state.

        Args:
            init_wait (int): initial wait in secs
            more_wait (int): additional wait in secs
            wait_for_state (str): MN state to wait for
            mn_index (int): MN index in self.mn_nodes to wait for the target state
            repeat_count (int, optional): times to repeat wait in case state is not yet obtained. Defaults to 1.
            node_list (list of nodes, optional): list of nodes to check MN's state on, if not defined - all nodes are checked
        """
        debug = False
        timer = Timer()
        timer.start()
        
        print(f'Waiting {init_wait} seconds...')
        time.sleep(init_wait)

        if node_list is None:
            node_list = self.nodes
        mn = self.mn_nodes[mn_index]
        collateral_id = mn.collateral_id
        for no in range(repeat_count):
            result = all(node.masternode("list").get(collateral_id, "") == wait_for_state for node in node_list)
            if not result:
                if debug:
                    [print(node.masternode("list")) for node in node_list]
                print(f'Waiting {more_wait} seconds more [{no+1}/{repeat_count}]...')
                time.sleep(more_wait)
            else:
                print(f"Nodes [{','.join(str(node.index) for node in node_list)}] achieved {mn.alias} state '{wait_for_state}' in {int(timer.elapsed_time)} secs")
                break
        if debug:
            [print(node.masternode("list")) for node in node_list]
        [assert_equal(node.masternode("list").get(collateral_id, ""), wait_for_state) for node in node_list]
