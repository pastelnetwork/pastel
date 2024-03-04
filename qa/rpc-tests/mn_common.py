#!/usr/bin/env python3
# Copyright (c) 2018-2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
from typing import Dict
from pathlib import Path
from enum import Enum
import time
import itertools
import json
import random
import string
from decimal import getcontext

from test_framework.util import (
    assert_true,
    assert_equal,
    assert_raises_rpc,
    assert_greater_than,
    start_node,
    connect_nodes_bi,
    p2p_port,
    str_to_b64str,
    Timer
)
from pastel_test_framework import (
    PastelTestFramework,
    TicketType
)
import test_framework.rpc_consts as rpc

getcontext().prec = 16

MIN_TICKET_CONFIRMATIONS = 5
NFT_DISCOUNT_MULTIPLIER = 0.45
GLOBAL_FEE_ADJUSTMENT_MULTIPLIER = 1.0
MIN_TX_RELAY_FEE = 0.001 # PSL, 100 patoshis
AMOUNT_TOLERANCE: float = 1e-5

class MnFeeType(Enum):
    """
    Masternode fees.
    Fee name                   | ID | fee in PSL | get rpc command name  | fee option name (network median) | local fee option name    |   setfee rpc command
    """
    STORAGE_FEE_PER_MB              = 1, 5000, "getstoragefee",         "storageFeePerMb",            "localStorageFeePerMb",            "storage"
    TICKET_CHAIN_STORAGE_FEE_PER_KB = 2,  200, "getticketfee",          "ticketChainStorageFeePerKb", "localTicketChainStorageFeePerKb", "ticket"
    SENSE_COMPUTE_FEE               = 3, 5000, "getsensecomputefee",    "senseComputeFee",            "localSenseComputeFee",            "sense-compute"
    SENSE_PROCESSING_FEE_PER_MB     = 4,   50, "getsenseprocessingfee", "senseProcessingFeePerMb",    "localSenseProcessingFeePerMb",    "sense-processing"
    
    def __init__(self, _: str, fee: int, getfee_rpc_command: str, option_name: str, local_option_name: str, setfee_rpc_command: str):
        self._fee = fee
        self._getfee_rpc_command = getfee_rpc_command
        self._option_name = option_name
        self._local_option_name = local_option_name
        self._setfee_rpc_command = setfee_rpc_command
    
    def __new__(cls, *args, **kwds):
        obj = object.__new__(cls)
        obj._value_ = args[0]
        return obj
    
    def __str__(self):
        return self.name

    @property
    def fee(self) -> int:
        return self._fee
    
    @property
    def getfee_rpc_command(self) -> str:
        return self._getfee_rpc_command
    
    @property
    def option_name(self) -> str:
        return self._option_name
    
    @property
    def local_option_name(self) -> str:
        return self._local_option_name

    @property
    def setfee_rpc_command(self) -> str:
        return self._setfee_rpc_command
    
    
class TicketData:
    def __init__(self):
        self.reg_ticket = None                  # Registration ticket json (not encoded)
        self.reg_ticket_base64_encoded = None   # Registration ticket json base64-encoded
        self.reg_txid: str = None               # Registration ticket txid
        self.reg_height: int = None             # Registration ticket block height
        self.reg_node_id: int = None            # Node where ticket was registered
        self.reg_pastelid: str = None           # Pastel ID of the Registration ticket NFT Creator/Action Caller, etc..
        self.pastelid_node_id: int = None       # Node where reg_pastelid is created

        self.act_txid: str = None               # Activation ticket txid
        self.act_height: int = None             # Activation ticket block height

        self.offer_txid: str = None             # Offer ticket txid
        self.accept_txid: str = None            # Accept ticket txid
        self.transfer_txid: str = None          # Transfer ticket txid

        self.label: str = None                  # unique label
        self.item_price: int = 0                # item price
        self.ticket_price: int = 10             # ticket price
        self.royalty_address: str = None        # NFT Royalty address
        self.address = None                     # address that can be used in a ticket

    def set_reg_ticket(self, reg_ticket: str):
        self.reg_ticket = reg_ticket
        self.reg_ticket_base64_encoded = str_to_b64str(reg_ticket)


class TopMN:
    def __init__(self, index: int, pastelid: str = None):
        self._index_ = index
        self._pastelid_ = pastelid
        self._signature_ = None

    @property
    def index(self) -> int:
        return self._index_

    @property
    def pastelid(self) -> str:
        return self._pastelid_

    @property
    def signature(self) -> str:
        return self._signature_

    @signature.setter
    def signature(self, value):
        self._signature_ = value

    def __repr__(self) -> str:
        return f"[{self.index}, {self.pastelid}]"


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


        def add_mnid_conf(self, dirname: str):
            """ add mnid configuration for mining to pastel.conf
            
            Args:
                dirname (str): node base directory
            """
            datadir = Path(dirname, f"node{self.index}")
            if not datadir.is_dir():
                datadir.mkdir()
            conf_file = Path(datadir, "pastel.conf")
            with conf_file.open('a') as f:
                f.write(f"genpastelid={self.mnid}\n")
                f.write(f"genpassphrase={self.passphrase}\n")
                f.write("genenablemnmining=1\n")
        

        def create_masternode_conf(self, dirname: str, node_index: int):
            """create masternode.conf for this masternode

            Args:
                dirname (str): node base directory
                node_index (int): node index in self.nodes

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
            config[name]["extCfg"] = {}
            config[name]["extCfg"]["param1"] = str(random.randint(0, 9))
            config[name]["extCfg"]["param2"] = str(random.randint(0, 9))

            print(f"Creating masternode.conf for node {name}...")
            with cfg_file.open('w') as f:
                json.dump(config, f, indent=4)
            return str(datadir)


    def __init__(self):
        super().__init__()

        # this should be redefined in a child class
        self.number_of_master_nodes = 1
        self.number_of_cold_nodes = self.number_of_master_nodes
        self.number_of_simple_nodes = 2
        self.mining_node_num = 1
        self.hot_node_num = 2

        # list of master nodes (MasterNode)
        self.mn_nodes = []
        self.collateral = int(1000)
        # flag to use new "masternode init" API
        self.use_masternode_init = False
        
        # dict for fast search of the MN by outpoint (txid-index)
        self.mn_outpoints = {}
        
        # list of 3 TopMNs
        self.top_mns = [TopMN(i) for i in range(3)]
        self.non_top_mns = []
        
        self.signatures_dict = None
        self.same_mns_signatures_dict = None
        self.not_top_mns_signatures_dict = None
        # dict of all principal signatures for validation: 'principal Pastel ID' -> 'signature'
        self.principal_signatures_dict = {}
        
        self.royalty = 0.075                        # default royalty fee 7.5%
        self.is_green = True                        # is green fee payment?
        self.green_address = "tPj5BfCrLfLpuviSJrD3B1yyWp3XkgtFjb6"

        # storage for all ticket types used in tests
        self.tickets: Dict[TicketType, TicketData] = {}
        for _, member in TicketType.__members__.items():
            self.tickets[member] = TicketData()
            self.tickets[member].ticket_price = member.ticket_price
            print(f"{member.description} ticket price: {member.ticket_price}")


    @property
    def total_number_of_nodes(self) -> int:
        """ Returns total number of nodes: masternodes + simple nodes

        Returns:
            int: total number of nodes
        """
        return self.number_of_master_nodes + self.number_of_simple_nodes
    

    def get_mnid(self, mn_no: int) -> str:
        """ Get mnid (Pastel ID of the MasterNode).

        Args:
            mn_no (int): mn index in self.mn_nodes

        Returns:
            str: mnid if mn found by index, None - otherwise
        """
        if mn_no >= len(self.mn_nodes):
            return None
        mn = self.mn_nodes[mn_no]
        return mn.mnid


    def setup_masternodes_network(self, debug_flags: str = ""):
        """ Setup MasterNode network using hot/cold method.
            Hot node keeps all collaterals for all MNs (cold nodes).
            Network initialization steps:
            - simple nodes are started and connected with each other
            - required amount is mined on miner node to be able to initialize the given number of Masternodes
            - collateral amounts (self.collateral) sent to all collateral addresses
            - configuration for all MNs is generated on hot node.
            - all MNs are started and connected with each other and simple nodes
            - Pastel IDs are created on all MNs
            - coins required for mnid registration sent to all MNs
            - Hot node calls "masternode start-alias" to start all MNs
            - wait for PRE_ENABLED status for all MNs
            - register mnids on all MNs
            - wait for ENABLED status for all MNs

        Args:
            mn_count (int, optional): number of MasterNodes. Defaults to 1.
            non_mn_count (int, optional): Number of simple nodes. Defaults to 2.
            mining_node_num (int, optional): Mining node number. Defaults to 1.
            hot_node_num (int, optional): Hot node number. Defaults to 2.
            cold_node_count (int, optional): Cold node number. Defaults to None.
            debug_flags (str, optional): Additional debug flags for nodes. Defaults to "".
        """
        timer = Timer()
        timer.start()
        
        # create list of mns
        # start only non-mn nodes
        for index in range(self.total_number_of_nodes):
            if index < self.number_of_master_nodes:
                mn = self.MasterNode(index, self.passphrase)
                self.mn_nodes.append(mn)
                self.nodes.append(None) # add dummy node for now
            else:
                print(f"starting non-mn{index} node")
                self.nodes.append(start_node(index, self.options.tmpdir, [f"-debug={debug_flags}"]))
       
        # connect non-mn nodes
        for pair in itertools.combinations(range(self.number_of_master_nodes, self.total_number_of_nodes), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])

        # mining enough coins for collateral for mn_count nodes
        self.mining_enough(self.mining_node_num, self.number_of_master_nodes)

        # hot node will keep all collateral amounts in its wallet to activate all master nodes
        for mn in self.mn_nodes:
            # create collateral address for each MN
            # number of nodes to activate is defined by cold_node_count
            if mn.index >= self.number_of_cold_nodes:
                continue
            mn.collateral_address = self.nodes[self.hot_node_num].getnewaddress()
            # send coins to the collateral addresses on hot node
            print(f"{mn.index}: Sending {self.collateral} coins to node{self.hot_node_num}; collateral address {mn.collateral_address} ...")
            mn.collateral_txid = self.nodes[self.mining_node_num].sendtoaddress(mn.collateral_address, self.collateral, "", "", False)

        self.generate_and_sync_inc(1, self.mining_node_num)
        assert_equal(self.nodes[self.hot_node_num].getbalance(), self.collateral * self.number_of_cold_nodes)

        # prepare parameters and create masternode.conf for all masternodes
        outputs = self.nodes[self.hot_node_num].masternode("outputs")
        print(f"hot node{self.hot_node_num} collateral outputs\n{json.dumps(outputs, indent=4)}")
        for mn in self.mn_nodes:
            # get the collateral outpoint indexes
            if mn.index < self.number_of_cold_nodes:
                mn.collateral_index = int(outputs[mn.collateral_txid])
            if self.use_masternode_init:
                # generate info for masternodes (masternode init)
                params = self.nodes[self.hot_node_num].masternode("init", mn.passphrase, mn.collateral_txid, mn.collateral_index)
                mn.mnid = params["mnid"]
                mn.lrKey = params["legRoastKey"]
                mn.privKey = params["privKey"]
                print(f"mn{index} id: {mn.mnid}")
                mn.create_masternode_conf(self.options.tmpdir, self.hot_node_num)
            else:
                mn.privKey = self.nodes[self.hot_node_num].masternode("genkey")
                assert_true(mn.privKey, "Failed to generate private key for Master Node")
        if self.use_masternode_init:
            self.generate_and_sync_inc(1, self.mining_node_num)

        # starting all master nodes
        for mn in self.mn_nodes:
            print(f"starting masternode {mn.alias}")
            self.nodes[mn.index] = start_node(mn.index, self.options.tmpdir, [f"-debug={debug_flags}", "-masternode", "-txindex=1", "-reindex", f"-masternodeprivkey={mn.privKey}"])

        # connect nodes (non-mn nodes already interconnected)
        for pair in itertools.combinations(range(self.total_number_of_nodes), 2):
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
                if mn.index >= self.number_of_cold_nodes:
                    continue
                # get new address and send some coins for mnid registration
                mn.mnid_reg_address = self.nodes[mn.index].getnewaddress()
                self.nodes[self.mining_node_num].sendtoaddress(mn.mnid_reg_address, 100, "", "", False)
                mn.create_masternode_conf(self.options.tmpdir, self.hot_node_num)
                mn.add_mnid_conf(self.options.tmpdir)
            self.generate_and_sync_inc(1, self.mining_node_num)

            # send "masternode start-alias <alias>" for all cold nodes
            for mn in self.mn_nodes:
                if mn.index >= self.number_of_cold_nodes:
                    continue
                print(f"Enabling master node: {mn.alias}...")
                res = self.nodes[self.hot_node_num].masternode("start-alias", mn.alias)
                print(res)
                assert_equal(res["alias"], mn.alias)
                assert_equal(res["result"], "successful")

            print("Waiting for PRE_ENABLED status...")
            initial_wait = 10
            for mn in self.mn_nodes:
                if mn.index >= self.number_of_cold_nodes:
                    continue
                self.wait_for_mn_state(initial_wait, 10, "PRE_ENABLED", mn.index, 3)
                initial_wait = 0
                
            # register mnids
            for mn in self.mn_nodes:
                if mn.index >= self.number_of_cold_nodes:
                    continue
                result = self.nodes[mn.index].tickets("register", TicketType.MNID.type_name, mn.mnid,
                    self.passphrase, mn.mnid_reg_address)
                # only for mn0 - duplicate mnid registration should not be accepted to mempool
                if mn.index == 0:
                    assert_raises_rpc(rpc.RPC_MISC_ERROR, "is already in the mempool",
                        self.nodes[mn.index].tickets, "register", TicketType.MNID.type_name, mn.mnid,
                        self.passphrase, mn.mnid_reg_address)
                mn.mnid_reg_txid = result["txid"]
                self.generate_and_sync_inc(1, self.mining_node_num)
            # wait for ticket transactions
            time.sleep(10)
            for _ in range(MIN_TICKET_CONFIRMATIONS):
                self.generate_and_sync_inc(1, self.mining_node_num)
                self.sync_all(10, 3)
            self.sync_all(10, 30)

            print("Waiting for ENABLED status...")
            initial_wait = 15
            for mn in self.mn_nodes:
                if mn.index >= self.number_of_cold_nodes:
                    continue
                self.wait_for_mn_state(initial_wait, 10, "ENABLED", mn.index, 10)
                initial_wait = 0


        # create dict for fast search of the MN by outpoint (txid-index)
        for mn in self.mn_nodes:
            self.mn_outpoints[mn.collateral_id] = mn.index

        self.reconnect_all_nodes()
        self.sync_all()
        for mn in self.mn_nodes:
            if mn.index >= self.number_of_cold_nodes:
                continue
            mnid = self.nodes[mn.index].refreshminingmnidinfo()
            assert_true(mnid, "Failed to refresh mining mnid info")
        timer.stop()
        print(f"<<<< MasterNode network INITIALIZED in {timer.elapsed_time} secs >>>>")
        self.list_masternode_info()


    def list_masternode_info(self):
        """List info for all masternodes.
        """
        print(f'MasterNodes [{len(self.mn_nodes)}], (outpoint, address, mnid)):')
        for mn in self.mn_nodes:
            print(f"  {mn.index}) {mn.collateral_id}, {mn.mnid_reg_address}, {mn.mnid}")
        

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


    def update_mn_indexes(self, node_num: int = 0, height: int = -1, number_of_top_mns: int = 3):
        """Get historical information about top MNs at given height.

        Args:
            nodeNum (int, optional): Use this node to get info. Defaults to 0 (mn0).
            height (int, optional): Get historical info at this height. Defaults to -1 (current blockchain height).
            number_of_top_mns (int, optional): Number of top mns to return. Defaults to 3.

        Returns:
            list(): list of top mn indexes
        """
        # Get current top MNs on given node
        if height == -1:
            creator_height = self.nodes[node_num].getblockcount()
        else:
            creator_height = height
        top_masternodes = self.nodes[node_num].masternode("top", creator_height)[str(creator_height)]
        print(f"top masternodes for height {creator_height}:\n{json.dumps(top_masternodes, indent=4)}")
        assert_greater_than(len(top_masternodes), number_of_top_mns)

        top_mns_indexes = []
        for mn in top_masternodes:
            index = self.mn_outpoints[mn["outpoint"]]
            top_mns_indexes.append(index)

        self.top_mns = []
        for i in range(number_of_top_mns):
            idx = top_mns_indexes[i]
            top_mn = TopMN(idx, self.get_mnid(idx))
            self.top_mns.append(top_mn)
            print(f"TopMN[{i}]: {top_mn!r}")

        return top_mns_indexes


    # ===============================================================================================================
    def create_signatures(self, item_type: TicketType, principal_node_num: int, make_bad_signatures_dicts = False):
        """Create ticket signatures

        Args:
            item_type (TicketType): ticket type
            principal_node_num (int): node# for principal signer
            make_bad_signatures_dicts (bool): if True - create invalid signatures
        """
        ticket = self.tickets[item_type]
        principal_pastelid = ticket.reg_pastelid

        mn_ticket_signatures = {}
        # for now, only collection ticket will be signed not base64-encoded
        # later on, this will be changed for all tickets
        sign_decoded_ticket: bool = item_type == TicketType.COLLECTION
        principal_signature = self.nodes[principal_node_num].pastelid(
            "sign-base64-encoded" if sign_decoded_ticket else "sign", ticket.reg_ticket_base64_encoded,
            principal_pastelid, self.passphrase)["signature"]
        assert_true(principal_signature, f"Principal signer {principal_pastelid} failed to sign ticket")
        # save this principal signature for validation
        self.principal_signatures_dict[principal_pastelid] = principal_signature

        for n in range(self.number_of_cold_nodes):
            mnid = self.get_mnid(n)
            mn_ticket_signatures[n] = self.nodes[n].pastelid(
                "sign-base64-encoded" if sign_decoded_ticket else "sign", ticket.reg_ticket_base64_encoded,
                mnid, self.passphrase)["signature"]
            assert_true(mn_ticket_signatures[n], f"MN{n} signer {mnid} failed to sign ticket")
        print(f"principal ticket signer - {principal_signature}")
        print(f"mn_ticket_signatures - {mn_ticket_signatures}")

        # update top master nodes used for signing
        top_mns_indexes = self.update_mn_indexes()

        # update top mn signatures
        for i in range(3):
            top_mn = self.top_mns[i]
            idx = top_mn.index
            top_mn.signature = mn_ticket_signatures[idx]

        self.signatures_dict = dict(
            {
                "principal": {principal_pastelid: principal_signature},
                "mn2": {self.top_mns[1].pastelid: self.top_mns[1].signature},
                "mn3": {self.top_mns[2].pastelid: self.top_mns[2].signature},
            }
        )
        print(f"signatures_dict:\n{json.dumps(self.signatures_dict, indent=4)}")

        if make_bad_signatures_dicts:
            self.same_mns_signatures_dict = dict(
                {
                    "principal": {principal_pastelid: principal_signature},
                    "mn2": {self.top_mns[0].pastelid: self.top_mns[0].signature},
                    "mn3": {self.top_mns[0].pastelid: self.top_mns[0].signature},
                }
            )
            print(f"same_mns_signatures_dict:\n{json.dumps(self.same_mns_signatures_dict, indent=4)}")

            not_top_mns_indexes = set(self.mn_outpoints.values()) ^ set(top_mns_indexes)
            print(not_top_mns_indexes)

            not_top_mns_index1 = list(not_top_mns_indexes)[0]
            not_top_mns_index2 = list(not_top_mns_indexes)[1]
            not_top_mn_pastelid1 = self.get_mnid(not_top_mns_index1)
            not_top_mn_pastelid2 = self.get_mnid(not_top_mns_index2)
            not_top_mn_ticket_signature1 = mn_ticket_signatures[not_top_mns_index1]
            not_top_mn_ticket_signature2 = mn_ticket_signatures[not_top_mns_index2]
            self.not_top_mns_signatures_dict = dict(
                {
                    "principal": {principal_pastelid: principal_signature},
                    "mn2": {not_top_mn_pastelid1: not_top_mn_ticket_signature1},
                    "mn3": {not_top_mn_pastelid2: not_top_mn_ticket_signature2},
                }
            )
            print(f"not_top_mns_signatures_dict:\n{json.dumps(self.not_top_mns_signatures_dict, indent=4)}")


    # ===============================================================================================================
    def generate_nft_app_ticket_details(self):
        # app_ticket structure
        # {
        #     "creator_name": string,
        #     "nft_title": string,
        #     "nft_series_name": string,
        #     "nft_keyword_set": string,
        #     "creator_website": string,
        #     "creator_written_statement": string,
        #     "nft_creation_video_youtube_url": string,
        #
        #     "preview_hash": bytes,        //hash of the preview thumbnail !!!!SHA3-256!!!!
        #     "thumbnail1_hash": bytes,     //hash of the thumbnail !!!!SHA3-256!!!!
        #     "thumbnail2_hash": bytes,     //hash of the thumbnail !!!!SHA3-256!!!!
        #     "data_hash": bytes,           //hash of the image that this ticket represents !!!!SHA3-256!!!!
        #
        #     "fingerprints_hash": bytes,       //hash of the fingerprint !!!!SHA3-256!!!!
        #     "fingerprints_signature": bytes,  //signature on raw image fingerprint
        #
        #     "rq_ids": [list of strings],      //raptorq symbol identifiers -  !!!!SHA3-256 of symbol block!!!!
        #     "rq_oti": [array of 12 bytes],    //raptorq CommonOTI and SchemeSpecificOTI
        #
        #     "dupe_detection_system_version": string,  //
        #     "pastel_rareness_score": float,            // 0 to 1
        #
        #     "internet_rareness_score": 0,
        #     "matches_found_on_first_page": integer,
        #     "number_of_pages_of_results": integer,
        #     "url_of_first_match_in_page": string,
        #
        #     "open_nsfw_score": float,                     // 0 to 1
        #     "alternate_nsfw_scores": {
        #           "drawing": float,                       // 0 to 1
        #           "hentai": float,                        // 0 to 1
        #           "neutral": float,                       // 0 to 1
        #           "porn": float,                          // 0 to 1
        #           "sexy": float,                          // 0 to 1
        #     },
        #
        #     "image_hashes": {
        #         "pdq_hash": bytes,
        #         "perceptual_hash": bytes,
        #         "average_hash": bytes,
        #         "difference_hash": bytes
        #     },
        # }
        # Data for nft-ticket generation
        creator_first_names=('John', 'Andy', 'Joe', 'Jennifer', 'August', 'Dave', 'Blanca', 'Diana', 'Tia', 'Michael')
        creator_last_names=('Johnson', 'Smith', 'Williams', 'Ecclestone', 'Schumacher', 'Faye', 'Counts', 'Wesley')
        letters = string.ascii_letters

        rq_ids = []
        for _ in range(5):
            rq_ids.insert(1, self.get_random_mock_hash())

        nft_ticket_json = {
            "creator_name": "".join(random.choice(creator_first_names)+" "+random.choice(creator_last_names)),
            "nft_title": self.get_rand_testdata(letters, 10),
            "nft_series_name": self.get_rand_testdata(letters, 10),
            "nft_keyword_set": self.get_rand_testdata(letters, 10),
            "creator_website": self.get_rand_testdata(letters, 10),
            "creator_written_statement": self.get_rand_testdata(letters, 10),
            "nft_creation_video_youtube_url": self.get_rand_testdata(letters, 10),

            "preview_hash": self.get_random_mock_hash(),
            "thumbnail_hash": self.get_random_mock_hash(),
            "thumbnail1_hash": self.get_random_mock_hash(),
            "thumbnail2_hash": self.get_random_mock_hash(),
            "data_hash": self.get_random_mock_hash(),

            "fingerprints_hash": self.get_random_mock_hash(),
            "fingerprints_signature": self.get_rand_testdata(letters, 20),

            "rq_ids": rq_ids,
            "rq_oti": self.get_rand_testdata(letters, 12),

            "dupe_detection_system_version": "1",
            "pastel_rareness_score": round(random.random(), 2),

            "rareness_score": random.randint(0, 1000),
            "internet_rareness_score": round(random.random(), 2),
            "matches_found_on_first_page": random.randint(1, 5),
            "number_of_pages_of_results": random.randint(1, 50),
            "url_of_first_match_in_page": self.get_rand_testdata(letters, 10),

            "open_nsfw_score": round(random.random(), 2),
            "nsfw_score": random.randint(0, 1000),
            "alternate_nsfw_scores": {
                  "drawing": round(random.random(), 2),
                  "hentai": round(random.random(), 2),
                  "neutral": round(random.random(), 2),
            },

            "image_hashes": {
                "pdq_hash": self.get_random_mock_hash(),
                "perceptual_hash": self.get_random_mock_hash(),
                "average_hash": self.get_random_mock_hash(),
                "difference_hash": self.get_random_mock_hash()
            },
        }

        return nft_ticket_json


    # ===============================================================================================================
    def create_nft_ticket_v1(self, creator_node_num: int, total_copies: int,
        royalty: float, green: bool, make_bad_signatures_dicts = False):
        """Create NFT ticket v1 and signatures

        Args:
            creator_node_num (int): node that creates NFT ticket and signatures
            total_copies (int): [number of copies]
            royalty (float): [royalty fee, how much creator should get on all future resales]
            green (bool): [is there Green NFT payment or not]
            make_bad_signatures_dicts (bool): [create bad signatures]
        """
        # Get current height
        nft_ticket = self.tickets[TicketType.NFT]
        nft_ticket.reg_height = self.nodes[0].getblockcount()
        nft_ticket.reg_pastelid = self.creator_pastelid1
        nft_ticket.pastelid_node_id = self.non_mn3
        print(f"creator_ticket_height - {nft_ticket.reg_height}")

        # nft_ticket - v1
        # {
        #   "nft_ticket_version": integer  // 1
        #   "author": bytes,               // Pastel ID of the author (creator)
        #   "blocknum": integer,           // block number when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "block_hash": bytes            // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "copies": integer,             // number of copies
        #   "royalty": float,              // how much creator should get on all future resales
        #   "green": bool,                 // is green payment
        #   "app_ticket": ...
        # }

        block_hash = self.nodes[creator_node_num].getblock(str(nft_ticket.reg_height))["hash"]
        app_ticket_json = self.generate_nft_app_ticket_details()
        app_ticket = str_to_b64str(json.dumps(app_ticket_json))

        json_ticket = {
            "nft_ticket_version": 1,
            "author": nft_ticket.reg_pastelid,
            "blocknum": nft_ticket.reg_height,
            "block_hash": block_hash,
            "copies": total_copies,
            "royalty": royalty,
            "green": green,
            "app_ticket": app_ticket
        }
        nft_ticket.set_reg_ticket(json.dumps(json_ticket, indent=4))
        print(f"nft_ticket v1 - {nft_ticket.reg_ticket}")
        print(f"nft_ticket v1 (base64) - {nft_ticket.reg_ticket_base64_encoded}")

        self.create_signatures(TicketType.NFT, creator_node_num, make_bad_signatures_dicts)


    def wait_for_min_confirmations(self, node_no: int = -1):
        if node_no == -1:
            node_no = self.mining_node_num
        print(f"block count - {self.nodes[node_no].getblockcount()}")
        time.sleep(2)
        self.generate_and_sync_inc(MIN_TICKET_CONFIRMATIONS, self.mining_node_num)
        time.sleep(2)
        print(f"block count - {self.nodes[node_no].getblockcount()}")


    def wait_for_ticket_tnx(self, blocks_to_generate: int = 2):
        time.sleep(10)
        for _ in range(blocks_to_generate):
            self.nodes[self.mining_node_num].generate(1)
            self.sync_all(10, 3)
        self.sync_all(10, 30)


    def wait_for_sync_all(self, blocks: int):
        time.sleep(2)
        self.generate_and_sync_inc(blocks, self.mining_node_num)


    def wait_for_sync_all10(self):
        time.sleep(2)
        self.sync_all(10, 30)
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10, 30)
