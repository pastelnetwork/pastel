#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
import json
import time
import random
import string
import re
from decimal import Decimal, getcontext
from test_framework.util import (
    assert_equal,
    assert_equals,
    assert_raises_rpc,
    assert_greater_than,
    assert_shows_help,
    assert_true,
    initialize_chain_clean,
    str_to_b64str
)
from mn_common import (
    MasterNodeCommon,
    TicketData,
    TopMN
)
from pastel_test_framework import (
    TicketType,
    ActionType,
    get_action_type,
    get_activation_type
)
from test_framework.authproxy import JSONRPCException
import test_framework.rpc_consts as rpc
getcontext().prec = 16

TEST_COLLECTION_NAME = "My NFT Collection"

class MasterNodeTicketsTest(MasterNodeCommon):
    number_of_master_nodes = 13
    number_of_simple_nodes = 5
    total_number_of_nodes = number_of_master_nodes+number_of_simple_nodes

    non_active_mn = number_of_master_nodes-1

    non_mn1 = number_of_master_nodes        # mining node - will have coins #13
    non_mn2 = number_of_master_nodes+1      # hot node - will have collateral for all active MN #14
    non_mn3 = number_of_master_nodes+2      # will not have coins by default #15
    non_mn4 = number_of_master_nodes+3      # will not have coins by default #16
    non_mn5 = number_of_master_nodes+4

    mining_node_num = number_of_master_nodes    # same as non_mn1
    hot_node_num = number_of_master_nodes+1     # same as non_mn2

    def __init__(self):
        super().__init__()

        self.errorString = ""
        self.is_network_split = False
        self.nodes = []
        self.storage_fee = 100
        self.storage_fee90percent = self.storage_fee*9/10
        self.storage_fee80percent = self.storage_fee*8/10
        self.is_mn_pastel_ids_initialized = False
        self.nested_ownership_transfer_txid  = None
        self.single_offer_transfer_txids = []

        self.mn_addresses = {}
        self.mn_pastelids = {}
        self.mn_outpoints = {}
        # list of 3 TopMNs
        self.top_mns = [TopMN(i) for i in range(3)]

        self.signatures_dict = None
        self.same_mns_signatures_dict = None
        self.not_top_mns_signatures_dict = None

        self.mn0_pastelid1 = None
        self.mn0_id1_lrkey = None
        self.mn0_pastelid2 = None
        self.non_active_mn_pastelid1 = None

        self.nonmn1_pastelid1 = None
        self.nonmn1_pastelid2 = None
        self.nonmn3_pastelid1 = None
        self.nonmn3_id1_lrkey = None
        self.nonmn4_pastelid1 = None
        self.nonmn4_pastelid2 = None
        self.nonmn5_pastelid1 = None                # used for royalty
        self.action_caller_pastelid = None          # Action Caller Pastel ID on nonmn3

        self.miner_address = None # non_mn1
        self.nonmn3_address1 = None
        self.nonmn3_address_nobalance = None
        self.nonmn4_address1 = None
        self.nonmn4_address2 = None
        self.nonmn5_address1 = None                 # royalty address
        self.creator_pastelid1 = None               # non_mn3
        self.creator_pastelid2 = None               # non_mn3
        self.creator_pastelid3 = None               # non_mn3
        self.creator_nonregistered_pastelid1 = None
        self.mn2_nonregistered_pastelid1 = None     # not registered Pastel ID
        self.total_copies = None
        self.principal_signatures_dict = {}     # dict of all principal signatures for validation: 'principal Pastel ID' -> 'signature'
        self.nft_collection_name = None
        self.tickets = {}
        for name, member in TicketType.__members__.items():
            self.tickets[member] = TicketData()
            self.tickets[member].ticket_price = member.ticket_price
            print(f"{member.description} ticket price: {member.ticket_price}")
        self.nftcoll_closing_height_inc = 30        # closing block height increment for NFT collection

        self.royalty = 0.075                        # default royalty fee 7.5%
        self.is_green = True                        # is green fee payment?

        self.test_high_heights = False

        self.green_address = "tPj5BfCrLfLpuviSJrD3B1yyWp3XkgtFjb6"


    def setup_chain(self):
        print(f"Initializing test directory {self.options.tmpdir}")
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)

    def setup_network(self, split=False):
        self.setup_masternodes_network(self.number_of_master_nodes, self.number_of_simple_nodes,
            self.mining_node_num, self.hot_node_num, self.number_of_master_nodes - 1,
            "masternode,mnpayments,governance,compress")
        self.inc_ticket_counter(TicketType.MNID, self.number_of_master_nodes - 1)

    def run_test(self):
        self.miner_address = self.nodes[self.mining_node_num].getnewaddress()

        self.pastelid_tests()
        self.mn_pastelid_ticket_tests(False)
        self.personal_pastelid_ticket_tests(False)
        self.register_mn_pastelid()
        self.nft_intended_for_tests()
        self.action_intended_for_tests(ActionType.CASCADE)
        self.action_reg_ticket_tests(ActionType.SENSE, "sense-action-label")
        self.action_activate_ticket_tests(ActionType.SENSE, False)
        self.nft_collection_reg_ticket_tests(TEST_COLLECTION_NAME + " #1", "coll-label")
        self.nft_collection_tests("coll-label-v2")
        self.nft_collection_activate_ticket_tests(False)

        self.nft_reg_ticket_tests("nft-label")
        self.royalty_tests(2)
        self.nft_activate_ticket_tests(False)

        for item_type in [TicketType.SENSE_ACTION, TicketType.NFT]:
            self.offer_ticket_tests(item_type)
            self.accept_ticket_tests(item_type)
            self.transfer_ticket_tests(item_type)
            self.offer_accept_transfer_tests(item_type)
            self.list_and_validate_ticket_ownerships(item_type)

        self.takedown_ticket_tests()
        self.storage_fee_tests()
        self.tickets_list_filter_tests(0)

        if self.test_high_heights:
            self.tickets[TicketType.ID].ticket_price = 1000
            self.tickets[TicketType.MNID].ticket_price = 1000

            print("mining {} blocks".format(10000))
            for i in range(10):
                self.slow_mine(10, 100, 2, 0.01)
                print(f"mined {100*i} blocks")
                self.reconnect_nodes(0, self.number_of_master_nodes)
                self.sync_all()

            self.pastelid_tests()
            self.personal_pastelid_ticket_tests(True)
            self.action_reg_ticket_tests(ActionType.CASCADE, "cascade-action-label")
            self.action_activate_ticket_tests(ActionType.CASCADE, True)
            self.nft_reg_ticket_tests("nft-label2")
            self.nft_activate_ticket_tests(True)
            self.nft_collection_reg_ticket_tests(TEST_COLLECTION_NAME + " #2", "coll-label2")
            self.nft_collection_tests("coll-label2-v2")
            self.nft_collection_activate_ticket_tests(True)
            for item_type in [TicketType.NFT, TicketType.CASCADE_ACTION]:
                self.offer_ticket_tests(item_type)
                self.accept_ticket_tests(item_type)
                self.transfer_ticket_tests(item_type)
                self.offer_accept_transfer_tests(item_type)
                self.list_and_validate_ticket_ownerships(item_type)
            self.takedown_ticket_tests()
            self.storage_fee_tests()
            self.tickets_list_filter_tests(1)


    # ===============================================================================================================
    def nftroyalty_null_ticket_tests(self):
        print("== NFT royalty null tickets test ==")

        assert_equal(self.royalty, 0)

        ticket = self.tickets[TicketType.NFT]
        # not enough confirmations
        print(self.nodes[self.non_mn3].getblockcount())
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Royalty ticket can be created only after",
            self.nodes[self.non_mn3].tickets, "register", "royalty", ticket.reg_txid, "new_pastelid1", self.creator_pastelid1, self.passphrase)
        self.__wait_for_gen10_blocks()
        print(f"Current height: {self.nodes[self.non_mn3].getblockcount()}")

        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"The NFT Reg ticket with txid [{ticket.reg_txid}] has no royalty",
            self.nodes[self.non_mn3].tickets, "register", "royalty", ticket.reg_txid, "new_pastelid1", self.creator_pastelid1, self.passphrase)

        print("NFT royalty null tickets tested")


    # ===============================================================================================================
    def nftroyalty_ticket_tests(self, test_no: int, node_id: int, new_node_id: int):
        print(f"===== NFT royalty tickets test #{test_no} ====")

        nft_ticket = self.tickets[TicketType.NFT]
        royalty_ticket = self.tickets[TicketType.ROYALTY]
        id_ticket = self.tickets[TicketType.ID]
        ticket_type_name = "royalty"

        # save previous royalty Pastel ID
        old_royalty_pastelid = royalty_ticket.reg_pastelid

        # create personal royalty Pastel ID
        royalty_ticket.reg_pastelid = self.create_pastelid(new_node_id)[0]
        royalty_ticket.pastelid_node_id = new_node_id
        royalty_ticket.address = self.nodes[new_node_id].getnewaddress()
        print(f"Royalty Pastel ID: {royalty_ticket.reg_pastelid}")
        print(f"Royalty address: {royalty_ticket.address}")

        # another non-registered Pastel ID, used for testing, should be created on old node
        pastelid = self.create_pastelid(node_id)[0]

        # register new royalty Pastel ID from non MN with enough coins
        self.nodes[self.mining_node_num].sendtoaddress(royalty_ticket.address, 100 + id_ticket.ticket_price, "", "", False)
        self.generate_and_sync_inc(1)

        balance_before = self.nodes[new_node_id].getbalance()
        royalty_id_txid = self.nodes[new_node_id].tickets("register", TicketType.ID.type_name, royalty_ticket.reg_pastelid,
                                                               self.passphrase, royalty_ticket.address)["txid"]
        assert_true(royalty_id_txid, "No id registration ticket was created")
        print(f"Personal royalty Pastel ID [{royalty_ticket.reg_pastelid}] registered")
        self.inc_ticket_counter(TicketType.ID)
        self.generate_and_sync_inc(1)

        # check correct amount of change
        balance_after = self.nodes[new_node_id].getbalance()
        assert_equal(balance_after, balance_before - id_ticket.ticket_price)  # no fee yet
        print(f"Node{node_id} balance changes: {balance_before} -> {balance_after}, diff: {balance_after - balance_before}")

        # Royalty ticket registration tests
        # tickets register royalty "nft-txid" "new-pastelid" "old-pastelid" "passphrase" ["address"]
        # old node id should be used for royalty ticket registration

        # fail if wrong Pastel ID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[node_id].tickets, "register", ticket_type_name, nft_ticket.reg_txid,
            royalty_ticket.reg_pastelid, self.top_mns[1].pastelid, self.passphrase)

        # fail if wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_INVALID_PASS,
            self.nodes[node_id].tickets, "register", ticket_type_name, nft_ticket.reg_txid,
            royalty_ticket.reg_pastelid, old_royalty_pastelid, "wrong")

        # fail if there is no NFT Ticket with this txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not valid ticket type",
            self.nodes[node_id].tickets, "register", ticket_type_name, self.mn_nodes[0].mnid_reg_txid,
            royalty_ticket.reg_pastelid, old_royalty_pastelid, self.passphrase)

        # fail if there is no NFT Ticket with this txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not in the blockchain",
            self.nodes[node_id].tickets, "register", ticket_type_name, self.get_random_txid(),
            royalty_ticket.reg_pastelid, old_royalty_pastelid, self.passphrase)

        if test_no == 1:
            print(f"Current height: {self.nodes[node_id].getblockcount()}")
            # not enough confirmations
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "Royalty ticket can be created only after",
                self.nodes[node_id].tickets, "register", ticket_type_name, nft_ticket.reg_txid,
                royalty_ticket.reg_pastelid, old_royalty_pastelid, self.passphrase)
            self.generate_and_sync_inc(10)
        print(f"Current height: {self.nodes[node_id].getblockcount()}")

        if test_no > 1:
            # fail if is not matching current Pastel ID of the royalty payee
            msg = f"The Pastel ID [{pastelid}] is not matching the Pastel ID [{old_royalty_pastelid}] in the Change Royalty ticket with NFT txid [{nft_ticket.reg_txid}]"
        else:
            # fail if creator's Pastel ID is not matching creator's Pastel ID in the registration ticket
            msg = "is not matching the Creator's Pastel ID"
        assert_raises_rpc(rpc.RPC_MISC_ERROR, msg,
            self.nodes[node_id].tickets, "register", ticket_type_name, nft_ticket.reg_txid,
            royalty_ticket.reg_pastelid, pastelid, self.passphrase)

        # empty new Royalty Pastel ID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "The Change Royalty ticket new_pastelID is empty",
            self.nodes[node_id].tickets, "register", ticket_type_name, nft_ticket.reg_txid,
            "", old_royalty_pastelid, self.passphrase)

        assert_raises_rpc(rpc.RPC_MISC_ERROR, "The Change Royalty ticket new_pastelID is equal to current pastelID",
            self.nodes[node_id].tickets, "register", ticket_type_name, nft_ticket.reg_txid,
            old_royalty_pastelid, old_royalty_pastelid, self.passphrase)

        balance_before = self.nodes[node_id].getbalance()

        # successful royalty ticket registration
        royalty_ticket.reg_txid = self.nodes[node_id].tickets("register", ticket_type_name, nft_ticket.reg_txid,
            royalty_ticket.reg_pastelid, old_royalty_pastelid, self.passphrase)["txid"]
        royalty_ticket.reg_node_id = node_id
        assert_true(royalty_ticket.reg_txid, "No NFT royalty ticket was created")
        self.inc_ticket_counter(TicketType.ROYALTY)
        self.__wait_for_ticket_tnx()

        # fail if already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"The Pastel ID [{old_royalty_pastelid}] is not matching the Pastel ID [{royalty_ticket.reg_pastelid}] in the Change Royalty ticket with NFT txid [{nft_ticket.reg_txid}]",
            self.nodes[node_id].tickets, "register", ticket_type_name, nft_ticket.reg_txid,
            royalty_ticket.reg_pastelid, old_royalty_pastelid, self.passphrase)

        balance_after = self.nodes[node_id].getbalance()
        print(f"Node{node_id} balance changes: {balance_before} -> {balance_after}, diff: {balance_after - balance_before}")
        assert_equal(balance_before - balance_after, royalty_ticket.ticket_price)

        # from another node - get ticket transaction and check
        #   - amounts is totaling 10 PSL
        raw_tx = self.nodes[0].getrawtransaction(royalty_ticket.reg_txid)
        royalty_ticket_tx = self.nodes[0].decoderawtransaction(raw_tx)
        fee_amount = 0

        for v in royalty_ticket_tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                fee_amount += v["value"]
        assert_equal(fee_amount, 10)

        # find ticket by pastelID
        nft_ticket1_royalty_ticket_1 = self.nodes[self.non_mn1].tickets("find", ticket_type_name, old_royalty_pastelid)
        tkt1 = nft_ticket1_royalty_ticket_1[0]["ticket"]
        assert_equal(tkt1['type'], "nft-royalty")
        assert_equal(tkt1['pastelID'], old_royalty_pastelid)
        assert_equal(tkt1['new_pastelID'], royalty_ticket.reg_pastelid)
        assert_equal(tkt1['nft_txid'], nft_ticket.reg_txid)
        assert_equal(nft_ticket1_royalty_ticket_1[0]['txid'], royalty_ticket.reg_txid)

        # get the same ticket by txid and compare with ticket found by pastelID
        nft_ticket1_royalty_ticket_2 = self.nodes[self.non_mn1].tickets("get", royalty_ticket.reg_txid)
        tkt2 = nft_ticket1_royalty_ticket_2["ticket"]
        assert_equal(tkt2["signature"], tkt1["signature"])

        # list all NFT royalty tickets, check Pastel IDs
        royalty_tickets_list = self.nodes[0].tickets("list", ticket_type_name)
        f1 = False
        f2 = False
        for t in royalty_tickets_list:
            if royalty_ticket.reg_txid == t["txid"]:
                f1 = True
                f2 = (nft_ticket.reg_txid == t["ticket"]["nft_txid"])
        assert_true(f1)
        assert_true(f2)

        print(f"NFT royalty tickets {test_no} tested")


    def nft_change_royalty(self):
        if self.royalty == 0:
            return
        nft_ticket = self.tickets[TicketType.NFT]
        royalty_ticket = self.tickets[TicketType.ROYALTY]
        royalty_ticket.reg_pastelid = self.nonmn5_pastelid1
        royalty_ticket.pastelid_node_id = self.non_mn5
        royalty_ticket.address = self.nonmn5_address1
        ticket_type_name = "royalty"

        self.nodes[self.mining_node_num].sendtoaddress(royalty_ticket.address, 10 + royalty_ticket.ticket_price, "", "", False)
        self.generate_and_sync_inc(1)
        
        # royalty ticket registration
        # should be executed on the node where nft_ticket.reg_pastelid is stored
        royalty_ticket.reg_txid = self.nodes[nft_ticket.pastelid_node_id].tickets("register", ticket_type_name, nft_ticket.reg_txid,
            royalty_ticket.reg_pastelid, nft_ticket.reg_pastelid, self.passphrase)["txid"]
        royalty_ticket.reg_node_id = self.non_mn5
        assert_true(royalty_ticket.reg_txid, "No NFT royalty ticket was created")
        self.inc_ticket_counter(TicketType.ROYALTY)
        self.__wait_for_ticket_tnx()

        # update royalty address in NFT ticket
        nft_ticket.royalty_address = royalty_ticket.address


    # ===============================================================================================================
    def royalty_tests(self, royalty_change_count: int = 2):
        if self.royalty > 0:
            # set initial pastelid in royalty ticket
            # that should point to the NFT creator's Pastel ID
            nft_ticket = self.tickets[TicketType.NFT]
            royalty_ticket = self.tickets[TicketType.ROYALTY]
            royalty_ticket.reg_pastelid = nft_ticket.reg_pastelid
            royalty_ticket.pastelid_node_id = nft_ticket.pastelid_node_id
            # will use non_mn4 & non_mn5 nodes for royalty tests
            old_node_id = self.non_mn3 # current node with NFT Creator
            # use opposite node to register new royalty Pastel ID & address

            for test_no in range(royalty_change_count):
                new_node_id = self.non_mn4 if (old_node_id in [self.non_mn3, self.non_mn5]) else self.non_mn5
                self.nftroyalty_ticket_tests(test_no + 1, old_node_id, new_node_id)
                old_node_id = new_node_id
        else:
            self.nftroyalty_null_ticket_tests()


    # ===============================================================================================================
    def list_and_validate_ticket_ownerships(self, item_type: TicketType):
        print(f"== {item_type.description} Ownership validation tests ==")
        tickets_list = self.nodes[self.non_mn4].tickets("list", TicketType.TRANSFER.type_name, "all")
        print(f"{TicketType.TRANSFER.description} tickets:\n{tickets_list}")
        print(f"Original {item_type.description} owner: {self.tickets[item_type].reg_txid}")
        print(f"nonmn3_pastelid1: {self.nonmn3_pastelid1}")
        print(f"nonmn4_pastelid1: {self.nonmn4_pastelid1}")

        ticket = self.tickets[item_type]

        # tickets tools validateownership "txid" "pastelid" "passphrase"
        
        # Test not available Pastel ID
        assert_raises_rpc(rpc.RPC_INVALID_ADDRESS_OR_KEY, "Corresponding Pastel ID not found",
            self.nodes[self.non_mn4].tickets, "tools", "validateownership",
            ticket.reg_txid, "NOT_A_VALID_PASTELID", self.passphrase)

        # Test incorrect passphrase
        assert_raises_rpc(rpc.RPC_WALLET_PASSPHRASE_INCORRECT, "Failed to validate passphrase",
            self.nodes[self.non_mn3].tickets, "tools", "validateownership",
            ticket.reg_txid, ticket.reg_pastelid, self.new_passphrase)

        # Check if creator
        result = self.nodes[self.non_mn3].tickets("tools", "validateownership", ticket.reg_txid, ticket.reg_pastelid, self.passphrase)
        assert_equal(item_type.ticket_name, result["type"])
        assert_equal(ticket.reg_txid, result["txid"])
        assert_equal("", result['transfer'])
        assert_equal(True, result["owns"])

        if item_type == TicketType.NFT:
            # Test 'single offer' (without re-selling)
            result = self.nodes[self.non_mn4].tickets("tools", "validateownership", ticket.reg_txid, self.nonmn4_pastelid1, self.passphrase)
            assert_equal(item_type.ticket_name, result["type"])
            assert_equal(ticket.reg_txid, result["txid"])
            assert_equals(self.single_offer_transfer_txids, result['transfer'])
            assert_equal(True, result["owns"])

        # Test ownership with or re-sold NFT
        result = self.nodes[self.non_mn3].tickets("tools", "validateownership", ticket.reg_txid, self.nonmn3_pastelid1, self.passphrase)
        assert_equal(item_type.ticket_name, result["type"])
        assert_equal(ticket.reg_txid, result["txid"] )
        # nested_ownership_transfer_txid points to transfer_txid captured in T2 test
        assert_equal(self.nested_ownership_transfer_txid, result['transfer'])
        assert_equal(True, result["owns"])

        # Test no ownership
        result = self.nodes[self.non_mn1].tickets("tools", "validateownership", ticket.reg_txid, self.nonmn1_pastelid2, self.passphrase)
        assert_equal("unknown", result["type"])
        assert_equal("", result["txid"] )
        assert_equal("", result['transfer'])
        assert_equal(False, result["owns"])

        print(f"== {item_type.description} Ownership validation tested ==")


    # ===============================================================================================================
    def pastelid_tests(self):
        print("== Pastel ID tests ==")
	# most of the pastelid tests moved to the separate script secure_container.py

        # 1. pastelid tests
        # a. Generate new Pastel ID and associated keys (EdDSA448). Return Pastel ID and LegRoast pubkey base58-encoded
        # a.a - generate with no errors two keys at MN and non-MN
        mn = self.mn_nodes[0]
        self.mn0_pastelid1, self.mn0_id1_lrkey = mn.mnid, mn.lrKey
        self.mn0_pastelid2 = self.create_pastelid(0)[0]

        # for non active MN
        self.non_active_mn_pastelid1 = self.mn_nodes[self.non_active_mn].mnid
        self.nonmn1_pastelid1 = self.create_pastelid(self.non_mn1)[0]
        self.nonmn1_pastelid2 = self.create_pastelid(self.non_mn1)[0]
        # action caller Pastel ID (nonmn3)
        self.action_caller_pastelid = self.create_pastelid(self.non_mn3)[0]

        # for node without coins
        self.nonmn3_pastelid1, self.nonmn3_id1_lrkey = self.create_pastelid(self.non_mn3)
        # b. Import private "key" (EdDSA448) as PKCS8 encrypted string in PEM format. Return Pastel ID base58-encoded
        # NOT IMPLEMENTED
         # e. Sign "text" with the private "key" (EdDSA448) as PKCS8 encrypted string in PEM format
        # NOT IMPLEMENTED

    # ===============================================================================================================
    def mn_pastelid_ticket_tests(self, skip_low_coins_tests):
        print("== Masternode Pastel ID Tickets test ==")
        # 2. tickets tests
        # a. Pastel ID ticket
        #   a.a register MN Pastel ID
        #       a.a.1 fail if not MN
        ticket = self.tickets[TicketType.MNID]
        ticket_type_name = TicketType.MNID.type_name
        assert_raises_rpc(rpc.RPC_INTERNAL_ERROR, "This is not an active masternode",
            self.nodes[self.non_mn1].tickets, "register", ticket_type_name, self.nonmn1_pastelid2, self.passphrase)

        #       a.a.2 fail if not active MN
        assert_raises_rpc(rpc.RPC_INTERNAL_ERROR, "This is not an active masternode",
            self.nodes[self.non_active_mn].tickets, "register", ticket_type_name, self.non_active_mn_pastelid1, self.passphrase)

        #       a.a.3 fail if active MN, but wrong Pastel ID (not local)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"Pastel ID [{self.nonmn1_pastelid2}] should be generated and stored inside the local node",
            self.nodes[0].tickets, "register", ticket_type_name, self.nonmn1_pastelid2, self.passphrase)

        #       a.a.4 fail if active MN, but wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_INVALID_PASS,
            self.nodes[0].tickets, "register", ticket_type_name, self.mn0_pastelid1, "wrong")

        #       a.a.5 fail if active MN, but not enough coins - ~11PSL
        # if not skip_low_coins_tests:
        #     self.make_zero_balance(0)
        #     assert_raises_rpc(rpc.RPC_MISC_ERROR, "No unspent transaction found",
        #         self.nodes[0].tickets, "register", ticket_type_name, self.mn0_pastelid2, self.passphrase)

        #       a.a.6 register without errors from active MN with enough coins
        # mn0_address1 = self.nodes[0].getnewaddress()
        # self.nodes[self.mining_node_num].sendtoaddress(mn0_address1, 100, "", "", False)
        # self.__wait_for_sync_all(1)

        # coins_before = self.nodes[0].getbalance()
        # print(f"Coins before '{ticket_type_name}' registration: {coins_before}")

        # self.mn0_ticket1_txid = self.nodes[0].tickets("register", ticket_type_name, self.mn0_pastelid1, self.passphrase)["txid"]
        # assert_true(self.mn0_ticket1_txid, "No mnid ticket was created")
        # self.inc_ticket_counter(TicketType.MNID)
        # self.__wait_for_ticket_tnx()

        #       a.a.7 check correct amount of change
        # coins_after = self.nodes[0].getbalance()
        # print(f"Coins after '{ticket_type_name}' registration: {coins_after}")
        # assert_equal(coins_after, coins_before - ticket.ticket_price)  # no fee yet

        #       a.a.8 from another node - get ticket transaction and check
        #           - there are P2MS outputs with non-zero amounts
        #           - amounts is totaling ID ticket price
        mn = self.mn_nodes[0]
        mn0_ticket1_tx_hash = self.nodes[self.non_mn3].getrawtransaction(mn.mnid_reg_txid)
        mn0_ticket1_tx = self.nodes[self.non_mn3].decoderawtransaction(mn0_ticket1_tx_hash)
        amount = 0
        for v in mn0_ticket1_tx["vout"]:
            assert_greater_than(v["value"], 0)
            if v["scriptPubKey"]["type"] == "multisig":
                amount += v["value"]
        assert_equal(amount, ticket.ticket_price)

        #       a.a.9.1 fail if Pastel ID is already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "This Pastel ID is already registered in blockchain",
            self.nodes[0].tickets, "register", ticket_type_name, self.mn0_pastelid1, self.passphrase)

        #       a.a.9.2 fail if outpoint is already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Ticket (pastelid) is invalid. Masternode's outpoint",
            self.nodes[0].tickets, "register", ticket_type_name, self.mn0_pastelid2, self.passphrase)

        #   a.b find MN Pastel ID ticket
        #       a.b.1 by Pastel ID
        mn0_ticket1_1 = self.nodes[self.non_mn3].tickets("find", "id", self.mn0_pastelid1)
        assert_equal(mn0_ticket1_1["ticket"]["pastelID"], self.mn0_pastelid1)
        assert_equal(mn0_ticket1_1['ticket']['type'], "pastelid")
        assert_equal(mn0_ticket1_1['ticket']['id_type'], "masternode")

        #       a.b.2 by Collateral output, compare to ticket from a.b.1
        mn0_outpoint = self.nodes[0].masternode("status")["outpoint"]
        mn0_ticket1_2 = self.nodes[self.non_mn3].tickets("find", "id", mn0_outpoint)
        assert_equal(mn0_ticket1_1["ticket"]["signature"], mn0_ticket1_2["ticket"]["signature"])
        assert_equal(mn0_ticket1_1["ticket"]["outpoint"], mn0_outpoint)

        #   a.c get the same ticket by txid from a.a.3 and compare with ticket from a.b.1
        mn0_ticket1_3 = self.nodes[self.non_mn3].tickets("get", mn.mnid_reg_txid)
        assert_equal(mn0_ticket1_1["ticket"]["signature"], mn0_ticket1_3["ticket"]["signature"])

        #   a.d list all id tickets, check Pastel IDs
        # tickets_list = self.nodes[self.non_mn3].tickets("list", "id")
        # assert_equal(self.mn0_pastelid1, tickets_list[0]["ticket"]["pastelID"])
        # assert_equal(mn0_outpoint, tickets_list[0]["ticket"]["outpoint"])

        print("MN Pastel ID tickets tested")


    # ===============================================================================================================
    def personal_pastelid_ticket_tests(self, skip_low_coins_tests):
        print("== Personal Pastel ID Tickets test ==")
        # b. personal Pastel ID ticket
        self.nonmn3_address1 = self.nodes[self.non_mn3].getnewaddress()
        self.nonmn3_address_nobalance = self.nodes[self.non_mn3].getnewaddress()
        self.nonmn4_address1 = self.nodes[self.non_mn4].getnewaddress()
        self.nonmn4_address2 = self.nodes[self.non_mn4].getnewaddress()
        self.nonmn5_address1 = self.nodes[self.non_mn5].getnewaddress()

        #   b.a register personal Pastel ID
        #       b.a.1 fail if wrong Pastel ID
        ticket = self.tickets[TicketType.ID]
        ticket_type_name = TicketType.ID.type_name
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"Pastel ID [{self.nonmn1_pastelid2}] should be generated and stored inside the local node",
            self.nodes[self.non_mn3].tickets, "register", ticket_type_name, self.nonmn1_pastelid2, self.passphrase, self.nonmn3_address1)

        #       b.a.2 fail if wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_INVALID_PASS,
            self.nodes[self.non_mn3].tickets, "register", ticket_type_name, self.nonmn3_pastelid1, "wrong", self.nonmn3_address1)

        #       b.a.3 fail if not enough coins - ~11PSL
        if not skip_low_coins_tests:
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "No unspent transaction found",
                self.nodes[self.non_mn3].tickets, "register", ticket_type_name, self.nonmn3_pastelid1, self.passphrase, self.nonmn3_address1)

        #       b.a.4 register without errors from non MN with enough coins
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 100, "", "", False)
        self.__wait_for_sync_all(1)

        coins_before = self.nodes[self.non_mn3].getbalance()
        print(f"Coins before '{ticket_type_name}' registration: {coins_before}")

        nonmn3_ticket1_txid = self.nodes[self.non_mn3].tickets("register", ticket_type_name, self.nonmn3_pastelid1, self.passphrase,
                                                               self.nonmn3_address1)["txid"]
        assert_true(nonmn3_ticket1_txid, "No Pastel ID registration ticket was created")
        self.inc_ticket_counter(TicketType.ID)
        self.__wait_for_ticket_tnx()

        #       a.a.5 check correct amount of change
        coins_after = self.nodes[self.non_mn3].getbalance()
        print(f"Coins after '{ticket_type_name}' registration: {coins_after}")
        assert_equal(coins_after, coins_before - ticket.ticket_price)  # no fee yet

        #       b.a.6 from another node - get ticket transaction and check
        #           - there are P2MS outputs with non-zero amounts
        #           - amounts is totaling 10PSL
        nonmn3_ticket1_tx_hash = self.nodes[0].getrawtransaction(nonmn3_ticket1_txid)
        nonmn3_ticket1_tx = self.nodes[0].decoderawtransaction(nonmn3_ticket1_tx_hash)
        amount = 0
        for v in nonmn3_ticket1_tx["vout"]:
            assert_greater_than(v["value"], 0)
            if v["scriptPubKey"]["type"] == "multisig":
                amount += v["value"]
        assert_equal(amount, ticket.ticket_price)

        #       b.a.7 fail if already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "This Pastel ID is already registered in blockchain",
            self.nodes[self.non_mn3].tickets, "register", ticket_type_name, self.nonmn3_pastelid1, self.passphrase, self.nonmn3_address1)

        #   b.b find personal Pastel ID
        #       b.b.1 by Pastel ID
        nonmn3_ticket1_1 = self.nodes[0].tickets("find", ticket_type_name, self.nonmn3_pastelid1)
        assert_equal(nonmn3_ticket1_1["ticket"]["pastelID"], self.nonmn3_pastelid1)
        assert_equal(nonmn3_ticket1_1["ticket"]["pq_key"], self.nonmn3_id1_lrkey)
        assert_equal(nonmn3_ticket1_1['ticket']['type'], "pastelid")
        assert_equal(nonmn3_ticket1_1['ticket']['id_type'], "personal")

        #       b.b.2 by Address
        nonmn3_ticket1_2 = self.nodes[0].tickets("find", ticket_type_name, self.nonmn3_address1)
        assert_equal(nonmn3_ticket1_1["ticket"]["signature"], nonmn3_ticket1_2["ticket"]["signature"])

        #   b.c get the ticket by txid from b.a.3 and compare with ticket from b.b.1
        nonmn3_ticket1_3 = self.nodes[self.non_mn3].tickets("get", nonmn3_ticket1_txid)
        assert_equal(nonmn3_ticket1_1["ticket"]["signature"], nonmn3_ticket1_3["ticket"]["signature"])

        #   b.d list all id tickets, check Pastel IDs
        tickets_list = self.nodes[0].tickets("list", ticket_type_name)
        f1 = False
        f2 = False
        for t in tickets_list:
            if self.nonmn3_pastelid1 == t["ticket"]["pastelID"]:
                f1 = True
            if self.nonmn3_address1 == t["ticket"]["address"]:
                f2 = True
        assert_true(f1)
        assert_true(f2)

        print("Personal Pastel ID tickets tested")


    # ===============================================================================================================
    def generate_action_app_ticket_details(self):
        # action app_ticket
        action_ticket_json = {
            "action_app_ticket": "test"
        }
        return action_ticket_json


    # ===============================================================================================================
    def generate_nft_collection_app_ticket_details(self):
        # app_ticket structure
        # {
        #    "creator_website": string,
        #    "nft_collection_creation_video_youtube_url": string
        # }
        letters = string.ascii_letters
        nft_collection_ticket_json = {
            "creator_website": self.get_rand_testdata(letters, 10),
            "nft_collection_creation_video_youtube_url": self.get_rand_testdata(letters, 10),
        }
        return nft_collection_ticket_json


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
    def create_nft_collection_ticket(self, nft_collection_name, creator_node_num, closing_height_inc,
        nft_max_count, nft_copy_count, permitted_users, royalty, green, make_bad_signatures_dicts = False):
        """Create NFT collection ticket and signatures

        Args:
            creator_node_num (int): node that creates NFT collection ticket and signatures
            closing_height (int): [a "closing" block height after which no new NFTs would be allowed to be added to the collection]
            max_nft_count (int): [max number of NFTs allowed in this collection]
            royalty (int): [royalty fee, how much creators should get on all future resales (common for all NFTs in a collection)]
            green (bool): [is there Green NFT payment or not (common for all NFTs in a collection)]
            make_bad_signatures_dicts (bool): [if true - create bad signatures]
        """
        # Get current height
        ticket = self.tickets[TicketType.NFT_COLLECTION]
        ticket.reg_height = self.nodes[0].getblockcount()
        ticket.reg_pastelid = self.creator_pastelid1
        ticket.pastelid_node_id = self.non_mn3
        print(f"creator_ticket_height - {ticket.reg_height}")

        # Current nft_collection_ticket!!!!
        # {
        #   "nft_collection_ticket_version": integer  // 1
        #   "nft_collection_name": string, // NFT collection name
        #   "creator": bytes,              // Pastel ID of the NFT collection creator
        #   "permitted_users": ["..", "..", ...] // list of Pastel IDs that are permitted to register an NFT as part of the collection
        #   "blocknum": integer,           // block number when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "block_hash": bytes            // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "closing_height": integer,     // a "closing" block height after which no new NFTs would be allowed to be added to the collection
        #   "nft_max_count": integer,      // max number of NFTs allowed in this collection
        #   "nft_copy_count": integer,     // default number of copies for all NFTs in a collection
        #   "royalty": short,              // how much creator should get on all future resales
        #   "green": bool,                 //
        #   "app_ticket": {...}            // cNode parses app_ticket only for search
        # }

        block_hash = self.nodes[creator_node_num].getblock(str(ticket.reg_height))["hash"]
        app_ticket_json = self.generate_nft_collection_app_ticket_details()
        app_ticket = str_to_b64str(json.dumps(app_ticket_json))

        json_ticket = {
            "nft_collection_ticket_version": 1,
            "nft_collection_name": nft_collection_name,
            "creator": ticket.reg_pastelid,
            "blocknum": ticket.reg_height,
            "block_hash": block_hash,
            "closing_height": ticket.reg_height + closing_height_inc,
            "nft_max_count": nft_max_count,
            "nft_copy_count": nft_copy_count,
            "permitted_users": permitted_users,
            "royalty": royalty,
            "green": green,
            "app_ticket": app_ticket
        }
        ticket.reg_ticket = str_to_b64str(json.dumps(json_ticket, indent=4))
        print(f"nft_collection_ticket - {ticket.reg_ticket}")

        self.create_signatures(TicketType.NFT_COLLECTION, creator_node_num, make_bad_signatures_dicts)


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
        ticket = self.tickets[TicketType.NFT]
        ticket.reg_height = self.nodes[0].getblockcount()
        ticket.reg_pastelid = self.creator_pastelid1
        ticket.pastelid_node_id = self.non_mn3
        print(f"creator_ticket_height - {ticket.reg_height}")

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

        block_hash = self.nodes[creator_node_num].getblock(str(ticket.reg_height))["hash"]
        app_ticket_json = self.generate_nft_app_ticket_details()
        app_ticket = str_to_b64str(json.dumps(app_ticket_json))

        json_ticket = {
            "nft_ticket_version": 1,
            "author": ticket.reg_pastelid,
            "blocknum": ticket.reg_height,
            "block_hash": block_hash,
            "copies": total_copies,
            "royalty": royalty,
            "green": green,
            "app_ticket": app_ticket
        }
        nft_ticket_str = json.dumps(json_ticket, indent=4)
        ticket.reg_ticket = str_to_b64str(nft_ticket_str)
        print(f"nft_ticket v1 - {nft_ticket_str}")
        print(f"nft_ticket v1 (base64) - {ticket.reg_ticket}")

        self.create_signatures(TicketType.NFT, creator_node_num, make_bad_signatures_dicts)


    # ===============================================================================================================
    def create_nft_ticket_v2(self, creator_node_num: int, collection_txid: str = None, skip_optional: bool = True,
        total_copies: int = 0, royalty: float = 0.0, green: bool = False):
        """Create NFT ticket v2 and signatures (NFT collection support)

        Args:
            creator_node_num (int): node that creates NFT ticket and signatures
            collection_txid (string, optional): transaction id of the NFT collection that NFT belongs to (optional, can be empty)
            skip_optional (bool, optional): if True - skip optional properties. Defaults to True.
            total_copies (int, optional): number of copies. Defaults to 0.
            royalty (float, optional): [royalty fee, how much creator should get on all future resales]
            green (bool, optional): [is there Green NFT payment or not]
        """
        # Get current height
        ticket = self.tickets[TicketType.NFT]
        ticket.reg_height = self.nodes[0].getblockcount()
        ticket.reg_pastelid = self.creator_pastelid1
        ticket.pastelid_node_id = self.non_mn3
        print(f"creator_ticket_height - {ticket.reg_height}")

        # nft_ticket - v2
        # {
        #   "nft_ticket_version": integer  // 2
        #   "author": bytes,               // Pastel ID of the creator
        #   "blocknum": integer,           // block number when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "block_hash": bytes            // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "nft_collection_txid": bytes   // transaction id of the NFT collection that NFT belongs to (optional, can be empty)
        #   "copies": integer,             // number of copies, optional in v2
        #   "royalty": float,              // how much creator should get on all future resales, optional in v2
        #   "green": bool,                 // is green payment, optional in v2
        #   "app_ticket": ...
        # }

        block_hash = self.nodes[creator_node_num].getblock(str(ticket.reg_height))["hash"]
        app_ticket_json = self.generate_nft_app_ticket_details()
        app_ticket = str_to_b64str(json.dumps(app_ticket_json))

        json_ticket = {
            "nft_ticket_version": 2,
            "author": ticket.reg_pastelid,
            "blocknum": ticket.reg_height,
            "block_hash": block_hash,
            "app_ticket": app_ticket
        }
        if not skip_optional:
            json_ticket.update({
                "copies": total_copies,
                "royalty": royalty,
                "green": green,
            })
        if collection_txid:
            json_ticket["nft_collection_txid"] = collection_txid

        nft_ticket_str = json.dumps(json_ticket, indent=4)
        ticket.reg_ticket = str_to_b64str(nft_ticket_str)
        print(f"nft_ticket v2 - {nft_ticket_str}")
        print(f"nft_ticket v2 (base64) - {ticket.reg_ticket}")

        self.create_signatures(TicketType.NFT, creator_node_num)


    # ===============================================================================================================
    def create_action_ticket(self, creator_node_num: int, action_type: ActionType,
        caller_pastelid: str, make_bad_signatures_dicts: bool = False):
        """Create action ticket and signatures

        Args:
            creator_node_num (int): node that creates an action ticket and signatures
            action_type (ActionType): action type (sense or cascade)
            caller_pastelid (str): action caller Pastel ID
            make_bad_signatures_dicts (bool): if True - create invalid signatures
        """
        # Get current height
        reg_ticket_type = action_type.reg_ticket_type
        ticket = self.tickets[reg_ticket_type]
        ticket.reg_height = self.nodes[0].getblockcount()
        ticket.reg_pastelid = caller_pastelid
        ticket.pastelid_node_id = creator_node_num
        print(f"action created at height - {ticket.reg_height}")

        # Current action_ticket
        # {
        #   "action_ticket_version": integer  // 1
        #   "action_type": string             // action type (sense, cascade)
        #   "caller": bytes,                  // Pastel ID of the action caller
        #   "blocknum": integer,              // block number when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "block_hash": bytes               // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "app_ticket": bytes               // as ascii85(app_ticket),
        #                                     // actual structure of app_ticket is different for different API and is not parsed by pasteld !!!!
        # }

        block_hash = self.nodes[creator_node_num].getblock(str(ticket.reg_height))["hash"]
        app_ticket_json = self.generate_action_app_ticket_details()
        app_ticket = str_to_b64str(json.dumps(app_ticket_json))

        json_ticket = {
            "action_ticket_version": 1,
            "action_type": action_type.name,
            "caller": ticket.reg_pastelid,
            "blocknum": ticket.reg_height,
            "block_hash": block_hash,
            "app_ticket": app_ticket
        }
        action_ticket_str = json.dumps(json_ticket, indent=4)
        ticket.reg_ticket = str_to_b64str(action_ticket_str)
        print(f"action_ticket - {action_ticket_str}")
        print(f"action_ticket (base64) - {ticket.reg_ticket}")

        self.create_signatures(reg_ticket_type, creator_node_num, make_bad_signatures_dicts)


    def update_mn_indexes(self, nodeNum = 0, height = -1):
        """Get historical information about top MNs at given height.

        Args:
            nodeNum (int, optional): Use this node to get info. Defaults to 0 (mn0).
            height (int, optional): Get historical info at this height. Defaults to -1 (current blockchain height).

        Returns:
            list(): list of top mn indexes
        """
        # Get current top MNs on given node
        if height == -1:
            creator_height = self.nodes[nodeNum].getblockcount()
        else:
            creator_height = height
        top_masternodes = self.nodes[nodeNum].masternode("top", creator_height)[str(creator_height)]
        print(f"top_masternodes ({creator_height}) - {top_masternodes}")

        top_mns_indexes = []
        for mn in top_masternodes:
            index = self.mn_outpoints[mn["outpoint"]]
            top_mns_indexes.append(index)

        for i in range(3):
            idx = top_mns_indexes[i]
            self.top_mns[i] = TopMN(idx, self.mn_pastelids[idx])
            print(f"TopMN[{i}]: {self.top_mns[i]!r}")

        return top_mns_indexes


    # ===============================================================================================================
    def create_signatures(self, item_type: TicketType, principal_node_num: int, make_bad_signatures_dicts = False):
        """Create ticket signatures

        Args:
            principal_node_num (int): node# for principal signer
            make_bad_signatures_dicts (bool): if True - create invalid signatures
        """
        ticket = self.tickets[item_type]
        principal_pastelid = ticket.reg_pastelid

        mn_ticket_signatures = {}
        principal_signature = self.nodes[principal_node_num].pastelid("sign", ticket.reg_ticket, principal_pastelid, self.passphrase)["signature"]
        assert_true(principal_signature, f"Principal signer {principal_pastelid} failed to sign ticket")
        # save this principal signature for validation
        self.principal_signatures_dict[principal_pastelid] = principal_signature

        for n in range(0, 12):
            mn_ticket_signatures[n] = self.nodes[n].pastelid("sign", ticket.reg_ticket, self.mn_pastelids[n], self.passphrase)["signature"]
            assert_true(mn_ticket_signatures[n], f"MN{n} signer {self.mn_pastelids[n]} failed to sign ticket")
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
        print(f"signatures_dict - {self.signatures_dict!r}")

        if make_bad_signatures_dicts:
            self.same_mns_signatures_dict = dict(
                {
                    "principal": {principal_pastelid: principal_signature},
                    "mn2": {self.top_mns[0].pastelid: self.top_mns[0].signature},
                    "mn3": {self.top_mns[0].pastelid: self.top_mns[0].signature},
                }
            )
            print(f"same_mns_signatures_dict - {self.same_mns_signatures_dict!r}")

            not_top_mns_indexes = set(self.mn_outpoints.values()) ^ set(top_mns_indexes)
            print(not_top_mns_indexes)

            not_top_mns_index1 = list(not_top_mns_indexes)[0]
            not_top_mns_index2 = list(not_top_mns_indexes)[1]
            not_top_mn_pastelid1 = self.mn_pastelids[not_top_mns_index1]
            not_top_mn_pastelid2 = self.mn_pastelids[not_top_mns_index2]
            not_top_mn_ticket_signature1 = mn_ticket_signatures[not_top_mns_index1]
            not_top_mn_ticket_signature2 = mn_ticket_signatures[not_top_mns_index2]
            self.not_top_mns_signatures_dict = dict(
                {
                    "principal": {principal_pastelid: principal_signature},
                    "mn2": {not_top_mn_pastelid1: not_top_mn_ticket_signature1},
                    "mn3": {not_top_mn_pastelid2: not_top_mn_ticket_signature2},
                }
            )
            print(f"not_top_mns_signatures_dict - {self.not_top_mns_signatures_dict!r}")


    # ===============================================================================================================
    def register_mn_pastelid(self):
        """Create and register:
            - Pastel IDs for all master nodes (0..12) - all registered
            - Creator Pastel ID (non_mn3), used as a principal signer - registered
            - Creator Pasterl ID (non_mn3) - not registered
            - MN2 Pastel ID - not registered
        """
        if self.is_mn_pastel_ids_initialized:
            return
        self.is_mn_pastel_ids_initialized = True
        self.creator_pastelid1 = self.create_pastelid(self.non_mn3)[0]
        self.creator_pastelid2 = self.create_pastelid(self.non_mn3)[0]
        self.creator_pastelid3 = self.create_pastelid(self.non_mn3)[0]
        self.creator_nonregistered_pastelid1 = self.create_pastelid(self.non_mn3)[0]
        self.nonmn4_pastelid1 = self.create_pastelid(self.non_mn4)[0]
        self.nonmn4_pastelid2 = self.create_pastelid(self.non_mn4)[0]
        self.nonmn5_pastelid1 = self.create_pastelid(self.non_mn5)[0]

        print(f'Creator Pastel ID: {self.creator_pastelid1}')
        print(f'Creator Pastel ID (not registered): {self.creator_nonregistered_pastelid1}')
        self.mn2_nonregistered_pastelid1 = self.create_pastelid(2)[0]
        for n in range(self.number_of_master_nodes - 1):
            mn = self.mn_nodes[n]
            self.mn_addresses[n] = mn.mnid_reg_address
            self.nodes[self.mining_node_num].sendtoaddress(self.mn_addresses[n], 10000, "", "", False)
            self.mn_pastelids[n] = mn.mnid
            self.mn_outpoints[self.nodes[n].masternode("status")["outpoint"]] = n

        print(f"mn_addresses - {self.mn_addresses}")
        print(f"mn_pastelids - {self.mn_pastelids}")
        print(f"mn_outpoints - {self.mn_outpoints}")

        self.nodes[self.mining_node_num].sendtoaddress(self.miner_address, 100, "", "", False)
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 100, "", "", False)
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address2, 100, "", "", False)
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn5_address1, 100, "", "", False)
        self.__wait_for_sync_all(1)

        # register Pastel IDs
        REGISTER_IDS = [
            ( self.non_mn3, self.creator_pastelid1, self.nonmn3_address1 ),
            ( self.non_mn3, self.creator_pastelid2, self.nonmn3_address1 ),
            ( self.non_mn3, self.creator_pastelid3, self.nonmn3_address1 ),
            ( self.non_mn3, self.action_caller_pastelid, self.nonmn3_address1),
            ( self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1 ),
            ( self.non_mn4, self.nonmn4_pastelid2, self.nonmn4_address2 ),
            ( self.non_mn5, self.nonmn5_pastelid1, self.nonmn5_address1 ),
            ( self.mining_node_num, self.nonmn1_pastelid1, self.miner_address)
        ]
        for idreg_info in REGISTER_IDS:
            self.nodes[idreg_info[0]].tickets("register", "id", idreg_info[1], self.passphrase, idreg_info[2])
            self.generate_and_sync_inc(1, self.mining_node_num)
        self.inc_ticket_counter(TicketType.ID, len(REGISTER_IDS))
        self.__wait_for_ticket_tnx(5)

    # ===============================================================================================================
    # 
    def action_reg_ticket_tests(self, action_type: ActionType, label: str):
        """Action registration ticket tests - tested on non_mn3 node

        Args:
            action_type (ActionType): Pastel Action type
            label (str): action unique label
        """
        reg_ticket_type = action_type.reg_ticket_type
        desc = reg_ticket_type.description
        ticket = self.tickets[reg_ticket_type]
        ticket_type_name = reg_ticket_type.type_name

        print(f"== {desc} registration Tickets test ==")
        # Action registration ticket

        self.generate_and_sync_inc(10, self.mining_node_num)

        # create action ticket with bad signatures
        self.create_action_ticket(self.non_mn3, action_type, self.action_caller_pastelid, True)
        self.test_signatures(reg_ticket_type, label, self.creator_nonregistered_pastelid1, self.mn2_nonregistered_pastelid1)

        assert_shows_help(self.nodes[0].tickets, "register", ticket_type_name)

        # create action ticket with valid signatures
        self.create_action_ticket(self.non_mn3, action_type, self.action_caller_pastelid)
        # top mns are changed in create_signatures inside create_action_ticket
        top_mn_node = self.nodes[self.top_mns[0].index]

        coins_before = top_mn_node.getbalance()
        print(f"coins before ticket registration: {coins_before}")
        result = top_mn_node.tickets("register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, 
            label, str(self.storage_fee))
        ticket.reg_txid = result["txid"]
        ticket.reg_node_id = self.top_mns[0].index
        ticket.label = label
        ticket_key = result["key"]
        assert_true(ticket.reg_txid, "No action ticket was created")
        print(top_mn_node.getblockcount())
        self.inc_ticket_counter(action_type.reg_ticket_type)
        self.__wait_for_ticket_tnx()

        #       check correct amount of change and correct amount spent
        coins_after = top_mn_node.getbalance()
        print(f"coins after ticket registration: {coins_after}")
        assert_equal(coins_after, coins_before - ticket.ticket_price)  # no fee yet, but ticket cost action_ticket_price

        #   find registration ticket:
        #      1. by creators Pastel ID (this is MultiValue key)
        # TODO Pastel:

        #      2. by primary key
        tkt1 = self.nodes[self.non_mn3].tickets("find", ticket_type_name, ticket_key)["ticket"]
        assert_equal(tkt1["type"], "action-reg")
        assert_equal(tkt1["action_ticket"], ticket.reg_ticket)
        assert_equal(tkt1["key"], ticket_key)
        assert_equal(tkt1["label"], label)
        assert_equal(tkt1["called_at"], ticket.reg_height)
        assert_equal(tkt1["signatures"]["principal"][self.action_caller_pastelid], self.principal_signatures_dict[self.action_caller_pastelid])
        assert_equal(tkt1["signatures"]["mn2"][self.top_mns[1].pastelid], self.top_mns[1].signature)
        assert_equal(tkt1["signatures"]["mn3"][self.top_mns[2].pastelid], self.top_mns[2].signature)
        result = self.nodes[self.non_mn3].pastelid("verify", ticket.reg_ticket,
            tkt1["signatures"]["mn1"][self.top_mns[0].pastelid], self.top_mns[0].pastelid)["verification"]
        assert_equal(result, "OK")

        #       3. by fingerprints, compare to ticket from 2 (label)
        lblNodes = self.nodes[self.non_mn3].tickets("findbylabel", ticket_type_name, label)
        assert_equal(1, len(lblNodes), f"findbylabel {ticket_type_name} {label} was supposed to return only one ticket")
        tkt2 = lblNodes[0]["ticket"]
        assert_equal(tkt2["type"], "action-reg")
        assert_equal(tkt2["action_ticket"], ticket.reg_ticket)
        assert_equal(tkt2["key"], ticket_key)
        assert_equal(tkt2["label"], label)
        assert_equal(tkt2["called_at"], ticket.reg_height)
        assert_equal(tkt2["storage_fee"], self.storage_fee)
        assert_equal(tkt2["signatures"]["principal"][self.action_caller_pastelid], 
                     tkt1["signatures"]["principal"][self.action_caller_pastelid])

        #   get the same ticket by txid from c.a.6 and compare with ticket from c.b.2
        tkt3 = self.nodes[self.non_mn3].tickets("get", ticket.reg_txid)["ticket"]
        assert_equal(tkt3["signatures"]["principal"][self.action_caller_pastelid],
                     tkt1["signatures"]["principal"][self.action_caller_pastelid])

        #   list all action registration tickets, check Pastel IDs
        action_tickets_list = top_mn_node.tickets("list", ticket_type_name)
        f1 = False
        f2 = False
        for t in action_tickets_list:
            if ticket_key == t["ticket"]["key"]:
                f1 = True
            if label == t["ticket"]["label"]:
                f2 = True
        assert_true(f1)
        assert_true(f2)

        action_tickets_by_pid = top_mn_node.tickets("find", ticket_type_name, self.action_caller_pastelid)
        print(self.top_mns[0].pastelid)
        print(action_tickets_by_pid)

        print(f"{desc} registration tickets tested")

    # ===============================================================================================================
    # NFT collection registration ticket tests - tested on non_mn3 node
    def nft_collection_reg_ticket_tests(self, nft_collection_name, label):
        print("== NFT Collection registration Tickets test ==")

        ticket = self.tickets[TicketType.NFT_COLLECTION]
        ticket_type_name = TicketType.NFT_COLLECTION.type_name

        self.generate_and_sync_inc(10, self.mining_node_num)

        closing_height_inc = self.nftcoll_closing_height_inc
        nft_max_count = 2
        nft_copy_count = 10
        # create nft collection
        self.nft_collection_name = nft_collection_name
        self.create_nft_collection_ticket(self.nft_collection_name, self.non_mn3, closing_height_inc, 
            nft_max_count, 10, [], self.royalty, self.is_green, True)
        self.test_signatures(TicketType.NFT_COLLECTION, label, self.creator_nonregistered_pastelid1, self.mn2_nonregistered_pastelid1)
        top_mn_node = self.nodes[self.top_mns[0].index]

        assert_shows_help(self.nodes[0].tickets, "register", ticket_type_name)

        # check royalty max value 20
        self.create_nft_collection_ticket(self.nft_collection_name, self.non_mn3, closing_height_inc, 
            nft_max_count, nft_copy_count, [], 0.25, self.is_green, True)

        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Royalty can't be 25 percent, Min is 0 and Max is 20 percent",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))

        # check royalty negative value -5
        self.create_nft_collection_ticket(self.nft_collection_name, self.non_mn3, closing_height_inc, 
            nft_max_count, nft_copy_count, [], -5, self.is_green, True)
        
        # uint16_t -> 2 ^ 16 -> 65536 - 5 = 65531
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Royalty can't be -500 percent, Min is 0 and Max is 20 percent",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))

        # c.a.6 register without errors, if enough coins for tnx fee
        self.create_nft_collection_ticket(self.nft_collection_name, self.non_mn3, closing_height_inc, 
            nft_max_count, nft_copy_count, [self.creator_pastelid1, self.creator_pastelid2],
            self.royalty, self.is_green)
        # make sure we have latest top mns
        top_mn_node = self.nodes[self.top_mns[0].index]

        coins_before = top_mn_node.getbalance()
        print(f"Coins before '{ticket_type_name}' registration: {coins_before}")

        result = top_mn_node.tickets("register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase,
            label, str(self.storage_fee))
        ticket.reg_txid = result["txid"]
        ticket.reg_node_id = self.top_mns[0].index
        ticket_key = result["key"]
        ticket.label = label
        assert_true(ticket.reg_txid, "No nft-collection ticket was created")
        self.inc_ticket_counter(TicketType.NFT_COLLECTION)
        self.__wait_for_ticket_tnx()
        print(top_mn_node.getblockcount())

        #       c.a.7 check correct amount of change and correct amount spent
        coins_after = top_mn_node.getbalance()
        print(f"Coins after '{ticket_type_name}' registration: {coins_after}")
        assert_equal(coins_after, coins_before - ticket.ticket_price)  # no fee yet, but ticket cost NFT ticket price

        #   c.b find registration ticket
        #       c.b.1 by creators Pastel ID (this is MultiValue key)
        # TODO Pastel:

        #       c.b.2 by hash (primary key)
        tkt1 = self.nodes[self.non_mn3].tickets("find", ticket_type_name, ticket_key)["ticket"]
        assert_equal(tkt1['type'], "nft-collection-reg")
        assert_equal(tkt1['nft_collection_ticket'], ticket.reg_ticket)
        assert_equal(tkt1["key"], ticket_key)
        assert_equal(tkt1["label"], label)
        assert_equal(tkt1["creator_height"], ticket.reg_height)
        assert_equal(tkt1["closing_height"], ticket.reg_height + closing_height_inc)
        assert_equal(tkt1["nft_max_count"], nft_max_count)
        assert_equal(tkt1["nft_copy_count"], nft_copy_count)
        assert_equal(tkt1["storage_fee"], self.storage_fee)
        permitted_users = tkt1["permitted_users"]
        assert_true(self.creator_pastelid1 in permitted_users, f"User {self.creator_pastelid1} not in permitted users for NFT Collection")
        assert_true(self.creator_pastelid2 in permitted_users, f"User {self.creator_pastelid2} not in permitted users for NFT Collection")
        r = float(round(tkt1["royalty"], 3))
        assert_equal(r, self.royalty)
        if self.royalty > 0:
            assert_equal(tkt1["royalty_address"], self.nonmn3_address1)
        else:
            assert(len(tkt1["royalty_address"]) == 0)
        assert_equal(tkt1["green"], self.is_green)
        assert_equal(tkt1["signatures"]["principal"][self.creator_pastelid1], self.principal_signatures_dict[self.creator_pastelid1])
        assert_equal(tkt1["signatures"]["mn2"][self.top_mns[1].pastelid], self.top_mns[1].signature)
        assert_equal(tkt1["signatures"]["mn3"][self.top_mns[2].pastelid], self.top_mns[2].signature)
        result = self.nodes[self.non_mn3].pastelid("verify", ticket.reg_ticket,
            tkt1["signatures"]["mn1"][self.top_mns[0].pastelid], self.top_mns[0].pastelid)["verification"]
        assert_equal(result, "OK")

        #       c.b.3 by fingerprints, compare to ticket from c.b.2 (label)
        lblNodes = self.nodes[self.non_mn3].tickets("findbylabel", ticket_type_name, label)
        assert_equal(1, len(lblNodes), f"findbylabel {ticket_type_name} {label} was supposed to return only one ticket")
        tkt2 = lblNodes[0]["ticket"]
        assert_equal(tkt2['type'], "nft-collection-reg")
        assert_equal(tkt2['nft_collection_ticket'], ticket.reg_ticket)
        assert_equal(tkt2["key"], ticket_key)
        assert_equal(tkt2["label"], label)
        assert_equal(tkt2["creator_height"], ticket.reg_height)
        assert_equal(tkt2["closing_height"], ticket.reg_height + closing_height_inc)
        assert_equal(tkt2["nft_max_count"], nft_max_count)
        assert_equal(tkt2["nft_copy_count"], nft_copy_count)
        assert_equal(tkt2["storage_fee"], self.storage_fee)
        permitted_users = tkt2["permitted_users"]
        assert_true(self.creator_pastelid1 in permitted_users, f"User {self.creator_pastelid1} not in permitted users for NFT Collection")
        assert_true(self.creator_pastelid2 in permitted_users, f"User {self.creator_pastelid2} not in permitted users for NFT Collection")
        r = float(round(tkt2["royalty"], 3))
        assert_equal(r, self.royalty)
        if self.royalty > 0:
            assert_equal(tkt2["royalty_address"], self.nonmn3_address1)
        else:
            assert(len(tkt2["royalty_address"]) == 0)
        assert_equal(tkt2["green"], self.is_green)
        assert_equal(tkt2["signatures"]["principal"][self.creator_pastelid1],
                     tkt1["signatures"]["principal"][self.creator_pastelid1])

        #   c.c get the same ticket by txid from c.a.6 and compare with ticket from c.b.2
        tkt3 = self.nodes[self.non_mn3].tickets("get", ticket.reg_txid)["ticket"]
        assert_equal(tkt3["signatures"]["principal"][self.creator_pastelid1],
                     tkt1["signatures"]["principal"][self.creator_pastelid1])

        #   c.d list all NFT collection registration tickets, check Pastel IDs
        nft_tickets_list = top_mn_node.tickets("list", ticket_type_name)
        f1 = False
        f2 = False
        for t in nft_tickets_list:
            if ticket_key == t["ticket"]["key"]:
                f1 = True
            if label == t["ticket"]["label"]:
                f2 = True
        assert_true(f1)
        assert_true(f2)

        nft_tickets_by_pid = top_mn_node.tickets("find", ticket_type_name, self.creator_pastelid1)
        print(self.top_mns[0].pastelid)
        print(nft_tickets_by_pid)

        print("NFT collection registration tickets tested")

    # ===============================================================================================================
    # NFT collection tests - tested on non_mn3 node
    def nft_collection_tests(self, label):
        print("== NFT Collection Tickets test ==")

        ticket = self.tickets[TicketType.NFT_COLLECTION]
        nft_ticket_type_name = TicketType.NFT.type_name
        nftcoll_ticket_type_name = TicketType.NFT_COLLECTION.type_name

        self.generate_and_sync_inc(10, self.mining_node_num)

        # invalid NFT collection txid (txid format)
        self.create_nft_ticket_v2(self.non_mn3, "invalid_txid")
        top_mn_node = self.nodes[self.top_mns[0].index]
        nft_ticket = self.tickets[TicketType.NFT]
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Incorrect NFT collection txid",
            top_mn_node.tickets, "register", nft_ticket_type_name,
            nft_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))

        # NFT collection txid does not point to existing txid
        self.create_nft_ticket_v2(self.non_mn3, self.get_random_txid())
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not in the blockchain",
            top_mn_node.tickets, "register", nft_ticket_type_name,
            nft_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))

        # NFT collection txid does not point to NFT collection reg ticket
        self.create_nft_ticket_v2(self.non_mn3, self.mn_nodes[0].mnid_reg_txid)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "has invalid type",
            top_mn_node.tickets, "register", nft_ticket_type_name,
            nft_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))

        # save creator_pastelid1 
        saved_creator_pastelid1 = self.creator_pastelid1
        # creator_pastelid3 is not in permitted_users list
        self.creator_pastelid1 = self.creator_pastelid3
        self.create_nft_ticket_v2(self.non_mn3, ticket.reg_txid)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"User with Pastel ID '{self.creator_pastelid3}' is not allowed to add NFTs to the collection",
            top_mn_node.tickets, "register", nft_ticket_type_name,
            nft_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))

        # add nft-reg ticket #1 successfully (creator_pastelid1 user)
        self.creator_pastelid1 = saved_creator_pastelid1
        self.create_nft_ticket_v2(self.non_mn3, ticket.reg_txid)
        nft_ticket1_txid = top_mn_node.tickets("register", nft_ticket_type_name,
            nft_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label + "_1", str(self.storage_fee))["txid"]
        print(f'registered NFT ticket #1 [{nft_ticket1_txid}] in NFT collection [{self.nft_collection_name}]')
        self.inc_ticket_counter(TicketType.NFT)
        self.__wait_for_ticket_tnx()

        # add nft-reg ticket #2 successfully (creator_pastelid2 user)
        self.creator_pastelid1 = self.creator_pastelid2
        self.create_nft_ticket_v2(self.non_mn3, ticket.reg_txid)
        nft_ticket2_txid = self.nodes[self.top_mns[0].index].tickets("register", nft_ticket_type_name,
            nft_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label + "_2", str(self.storage_fee))["txid"]
        print(f'registered NFT ticket #2 [{nft_ticket2_txid}] in NFT collection [{self.nft_collection_name}]')
        self.inc_ticket_counter(TicketType.NFT)
        self.__wait_for_ticket_tnx()

        # cannot register more than 2 nft tickets (will exceed nft_max_count)
        self.create_nft_ticket_v2(self.non_mn3, ticket.reg_txid)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"Max number of NFTs ({2}) allowed in the collection '{self.nft_collection_name}' has been exceeded",
            self.nodes[self.top_mns[0].index].tickets, "register", nft_ticket_type_name,
            nft_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label + "_3", str(self.storage_fee))

        # test closing height, create nft collection with closing height +10 blocks
        # register new collection NFT collection #2
        # existing NFT collection in self.tickets saved in ticket local var
        new_nftcoll_ticket = TicketData()
        self.tickets[TicketType.NFT_COLLECTION] = new_nftcoll_ticket
        self.create_nft_collection_ticket(self.nft_collection_name + " (closing_height)", self.non_mn3, 10, 
            2, 10, [], self.royalty, self.is_green)
        nftcoll_ticket_txid = self.nodes[self.top_mns[0].index].tickets("register", nftcoll_ticket_type_name,
            new_nftcoll_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase,
            label, str(self.storage_fee))["txid"]
        assert_true(nftcoll_ticket_txid, "No NFT registration ticket was created")
        self.inc_ticket_counter(TicketType.NFT_COLLECTION)
        self.__wait_for_sync_all(1)
        self.__wait_for_sync_all(10)
        
        # cannot add NFTs to this collection any more
        self.create_nft_ticket_v2(self.non_mn3, nftcoll_ticket_txid)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "No new NFTs are allowed to be added to the NFT Collection",
            self.nodes[self.top_mns[0].index].tickets, "register", nft_ticket_type_name,
            nft_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label + "_4", str(self.storage_fee))
        # restore data for NFT collection #1
        self.tickets[TicketType.NFT_COLLECTION] = ticket

        # restore creator_pastelid1 
        self.creator_pastelid1 = saved_creator_pastelid1
        print("== NFT Collection Tickets tested ==")


    # ===============================================================================================================
    def nft_collection_activate_ticket_tests(self, skip_low_coins_tests):
        """NFT collection activation ticket tests

        Args:
            skip_low_coins_tests (bool): True to skip low coins tests
        """
        print("== NFT collection activation Tickets test ==")

        cmd = "activate"
        cmd_param = TicketType.NFT_COLLECTION.type_name
        nftcoll_ticket_type_name = TicketType.NFT_COLLECTION_ACTIVATE.type_name
        ticket = self.tickets[TicketType.NFT_COLLECTION]       
        nftcoll_act_ticket_price = self.tickets[TicketType.NFT_COLLECTION_ACTIVATE].ticket_price

        assert_shows_help(self.nodes[self.non_mn3].tickets, cmd, cmd_param)
        
        # d. NFT collection activation ticket
        #   d.a register NFT collection activation ticket (self.nftcollreg_ticket1_txid; self.storage_fee; ticket.reg_height)
        #       d.a.1 fail if wrong Pastel ID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
            str(ticket.reg_height), str(self.storage_fee), self.top_mns[1].pastelid, self.passphrase)

        #       d.a.2 fail if wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_INVALID_PASS,
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
            str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, "wrong")

        #       d.a.7 fail if not enough coins to pay 90% of registration price (from NFT Collection Reg ticket) (90) + tnx fee (act ticket price)
        self.make_zero_balance(self.non_mn3)
        if not skip_low_coins_tests:
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "Not enough coins to cover price [100 PSL]",
                self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
                str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, str(self.collateral), "", "", False)
        self.__wait_for_sync_all10()

        #       d.a.3 fail if txid points to invalid ticket (not a NFT Collection Reg ticket)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not valid ticket type",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param,
            self.mn_nodes[0].mnid_reg_txid, str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        #       d.a.3 fail if there is no NFT Collection Reg Ticket with this txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not in the blockchain",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param,
            self.get_random_txid(), str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        # not enough confirmations
        # print(self.nodes[self.non_mn3].getblockcount())
        # assert_raises_rpc(rpc.RPC_MISC_ERROR, "Activation ticket can be created only after",
        #    self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
        #    str(ticket.reg_height), str(self.storage_fee), self.ticket.reg_pastelid, self.passphrase)
        self.__wait_for_gen10_blocks()
        print(f"Current height: {self.nodes[self.non_mn4].getblockcount()}")

        #       d.a.4 fail if Caller's Pastel ID in the activation ticket
        #       is not matching Caller's Pastel ID in the registration ticket
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the Creator's Pastel ID",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
            str(ticket.reg_height), str(self.storage_fee), self.nonmn3_pastelid1, self.passphrase)

        #       d.a.5 fail if wrong creator ticket height
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the CreatorHeight",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
            "55", str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        #       d.a.6 fail if wrong storage fee
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the storage fee",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
            str(ticket.reg_height), "55", ticket.reg_pastelid, self.passphrase)

        # have to get historical top mns for NFT collection reg ticket
        self.update_mn_indexes(self.non_mn3, ticket.reg_height)

        #       d.a.7 register without errors
        #
        mn0_collateral_address = self.nodes[self.top_mns[0].index].masternode("status")["payee"]
        mn1_collateral_address = self.nodes[self.top_mns[1].index].masternode("status")["payee"]
        mn2_collateral_address = self.nodes[self.top_mns[2].index].masternode("status")["payee"]

        # MN's collateral addresses belong to hot_node - non_mn2
        mn0_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn0_collateral_address)
        mn1_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn1_collateral_address)
        mn2_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn2_collateral_address)

        coins_before = self.nodes[self.non_mn3].getbalance()
        print(f"Coins before '{nftcoll_ticket_type_name}' registration: {coins_before}")

        ticket.act_txid = self.nodes[self.non_mn3].tickets(cmd, cmd_param,
            ticket.reg_txid, ticket.reg_height,
            str(self.storage_fee), ticket.reg_pastelid, self.passphrase)["txid"]
        assert_true(ticket.act_txid, "NFT collection was not activated")
        self.__wait_for_ticket_tnx()

        #       d.a.9 check correct amount of change and correct amount spent and correct amount of fee paid
        main_mn_fee = self.storage_fee90percent*3/5
        other_mn_fee = self.storage_fee90percent/5

        coins_after = self.nodes[self.non_mn3].getbalance()
        print(f"Coins after '{nftcoll_ticket_type_name}' registration: {coins_after}")
        assert_equal(coins_after, coins_before-Decimal(self.storage_fee90percent)-Decimal(nftcoll_act_ticket_price))  # no fee yet, but ticket cost act ticket price

        # MN's collateral addresses belong to hot_node - non_mn2
        mn0_coins_after = self.nodes[self.hot_node_num].getreceivedbyaddress(mn0_collateral_address)
        mn1_coins_after = self.nodes[self.hot_node_num].getreceivedbyaddress(mn1_collateral_address)
        mn2_coins_after = self.nodes[self.hot_node_num].getreceivedbyaddress(mn2_collateral_address)

        # print("mn0: before="+str(mn0_coins_before)+"; after="+str(mn0_coins_after) +
        # ". fee should be="+str(mainMN_fee))
        # print("mn1: before="+str(mn1_coins_before)+"; after="+str(mn1_coins_after) +
        # ". fee should be="+str(otherMN_fee))
        # print("mn2: before="+str(mn2_coins_before)+"; after="+str(mn2_coins_after) +
        # ". fee should be="+str(otherMN_fee))
        assert_equal(mn0_coins_after-mn0_coins_before, main_mn_fee)
        assert_equal(mn1_coins_after-mn1_coins_before, other_mn_fee)
        assert_equal(mn2_coins_after-mn2_coins_before, other_mn_fee)

        #       d.a.10 fail if already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"The Activation ticket for the Collection Registration ticket with txid [{ticket.reg_txid}] already exists",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), 
                str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        #       d.a.11 from another node - get ticket transaction and check
        #           - there are 3 outputs to MN1, MN2 and MN3 with correct amounts
        #               (MN1: 60%; MN2, MN3: 20% each, of registration price)
        #           - amounts are totaling 10PSL
        nftcoll_act_ticket1_hash = self.nodes[0].getrawtransaction(ticket.act_txid)
        nftcoll_act_ticket1_tx = self.nodes[0].decoderawtransaction(nftcoll_act_ticket1_hash)
        amount = 0
        fee_amount = 0

        for v in nftcoll_act_ticket1_tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                fee_amount += v["value"]
            if v["scriptPubKey"]["type"] == "pubkeyhash":
                if v["scriptPubKey"]["addresses"][0] == mn0_collateral_address:
                    assert_equal(v["value"], main_mn_fee)
                    amount += v["value"]
                if v["scriptPubKey"]["addresses"][0] in [mn1_collateral_address, mn2_collateral_address]:
                    assert_equal(v["value"], other_mn_fee)
                    amount += v["value"]
        assert_equal(amount, self.storage_fee90percent)
        assert_equal(fee_amount, nftcoll_act_ticket_price)

        #   d.b find activation ticket
        #       d.b.1 by creators Pastel ID (this is MultiValue key)
        #       TODO Pastel: find activation ticket by creators Pastel ID (this is MultiValue key)

        #       d.b.3 by Registration height - creator_height from registration ticket (this is MultiValue key)
        #       TODO Pastel: find activation ticket by Registration height -
        #        creator_height from registration ticket (this is MultiValue key)

        #       d.b.2 by Registration txid - reg_txid from registration ticket, compare to ticket from d.b.2
        nftcoll_act_ticket1 = self.nodes[self.non_mn1].tickets("find", nftcoll_ticket_type_name, ticket.reg_txid)
        tkt1 = nftcoll_act_ticket1["ticket"]
        assert_equal(tkt1['type'], nftcoll_ticket_type_name)
        assert_equal(tkt1['pastelID'], ticket.reg_pastelid)
        assert_equal(tkt1['reg_txid'], ticket.reg_txid)
        assert_equal(tkt1['creator_height'], ticket.reg_height)
        assert_equal(tkt1['storage_fee'], self.storage_fee)
        assert_equal(nftcoll_act_ticket1['txid'], ticket.act_txid)

        #   d.c get the same ticket by txid from d.a.8 and compare with ticket from d.b.2
        nftcoll_act_ticket2 = self.nodes[self.non_mn1].tickets("get", ticket.act_txid)
        tkt2 = nftcoll_act_ticket2["ticket"]
        assert_equal(tkt2["signature"], tkt1["signature"])

        #   d.d list all Action activation tickets, check Pastel IDs
        nftcoll_act_tickets_list = self.nodes[0].tickets("list", nftcoll_ticket_type_name)
        f1 = False
        for t in nftcoll_act_tickets_list:
            if ticket.reg_txid == t["ticket"]["reg_txid"]:
                f1 = True
        assert_true(f1)

        nftcoll_tickets_by_pastelid = self.nodes[self.top_mns[0].index].tickets("find", nftcoll_ticket_type_name, ticket.reg_pastelid)
        print(self.top_mns[0].pastelid)
        print(nftcoll_tickets_by_pastelid)
        nftcoll_tickets_by_height = self.nodes[self.top_mns[0].index].tickets("find", nftcoll_ticket_type_name, str(ticket.reg_height))
        print(ticket.reg_height)
        print(nftcoll_tickets_by_height)

        print("NFT collection activation tickets tested")


    # ===============================================================================================================
    # NFT registration ticket tests - tested on non_mn3 node
    def nft_reg_ticket_tests(self, label):
        print("== NFT registration Tickets test ==")
        # c. NFT registration ticket

        self.generate_and_sync_inc(10, self.mining_node_num)

        self.total_copies = 10
        self.create_nft_ticket_v1(self.non_mn3, self.total_copies, self.royalty, self.is_green, True)
        ticket = self.tickets[TicketType.NFT]
        ticket_type_name = TicketType.NFT.type_name

        self.test_signatures(TicketType.NFT, label, self.creator_nonregistered_pastelid1, self.mn2_nonregistered_pastelid1)
        top_mn_node = self.nodes[self.top_mns[0].index]

        assert_shows_help(self.nodes[0].tickets, "register", ticket_type_name)

        # check royalty max value 20
        self.create_nft_ticket_v1(self.non_mn3, self.total_copies, 0.25, self.is_green, True)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Royalty can't be 25 percent, Min is 0 and Max is 20 percent",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))

        # check royalty negative value -5
        self.create_nft_ticket_v1(self.non_mn3, self.total_copies, -5, self.is_green, True)
        # uint16_t -> 2 ^ 16 -> 65536 - 5 = 65531
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Royalty can't be -500 percent, Min is 0 and Max is 20 percent",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))

        # c.a.6 register without errors, if enough coins for tnx fee
        self.create_nft_ticket_v1(self.non_mn3, self.total_copies, self.royalty, self.is_green)

        coins_before = top_mn_node.getbalance()
        print(f"Coins before '{ticket_type_name}' registration: {coins_before}")

        result = top_mn_node.tickets("register", ticket_type_name, ticket.reg_ticket, 
            json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))
        ticket.reg_txid = result["txid"]
        ticket.reg_node_id = self.top_mns[0].index
        ticket1_key = result["key"]
        ticket.label = label
        assert_true(ticket.reg_txid, "No NFT registration ticket was created")
        self.inc_ticket_counter(TicketType.NFT)
        print(f"NFT ticket created at {ticket.reg_height}")
        self.__wait_for_ticket_tnx()

        #       c.a.7 check correct amount of change and correct amount spent
        coins_after = top_mn_node.getbalance()
        print(f"Coins after '{ticket_type_name}' registration: {coins_after}")
        assert_equal(round(coins_after), round(coins_before - ticket.ticket_price))  # no fee yet, but ticket cost NFT ticket price

        #   c.b find registration ticket
        #       c.b.1 by creators Pastel ID (this is MultiValue key)
        # TODO Pastel:

        #       c.b.2 by hash (primary key)
        tkt1 = self.nodes[self.non_mn3].tickets("find", ticket_type_name, ticket1_key)["ticket"]
        assert_equal(tkt1['type'], "nft-reg")
        assert_equal(tkt1['nft_ticket'], ticket.reg_ticket)
        assert_equal(tkt1["key"], ticket1_key)
        assert_equal(tkt1["label"], label)
        assert_equal(tkt1["creator_height"], ticket.reg_height)
        assert_equal(tkt1["total_copies"], self.total_copies)
        assert_equal(tkt1["storage_fee"], self.storage_fee)
        r = float(round(tkt1["royalty"], 3))
        assert_equal(r, self.royalty)
        if self.royalty > 0:
            assert_equal(tkt1["royalty_address"], self.nonmn3_address1)
            ticket.royalty_address = self.nonmn3_address1
        else:
            assert(len(tkt1["royalty_address"]) == 0)
        assert_equal(tkt1["green"], self.is_green)
        assert_equal(tkt1["signatures"]["principal"][self.creator_pastelid1], self.principal_signatures_dict[self.creator_pastelid1])
        assert_equal(tkt1["signatures"]["mn2"][self.top_mns[1].pastelid], self.top_mns[1].signature)
        assert_equal(tkt1["signatures"]["mn3"][self.top_mns[2].pastelid], self.top_mns[2].signature)
        result = self.nodes[self.non_mn3].pastelid("verify", ticket.reg_ticket, 
            tkt1["signatures"]["mn1"][self.top_mns[0].pastelid], self.top_mns[0].pastelid)["verification"]
        assert_equal(result, "OK")

        #       c.b.3 by fingerprints, compare to ticket from c.b.2 (label)
        lblNodes = self.nodes[self.non_mn3].tickets("findbylabel", ticket_type_name, label)
        assert_equal(1, len(lblNodes), f"findbylabel {ticket_type_name} {label} was supposed to return only one ticket")
        tkt2 = lblNodes[0]["ticket"]
        assert_equal(tkt2['type'], "nft-reg")
        assert_equal(tkt2['nft_ticket'], ticket.reg_ticket)
        assert_equal(tkt2["key"], ticket1_key)
        assert_equal(tkt2["label"], label)
        assert_equal(tkt2["creator_height"], ticket.reg_height)
        assert_equal(tkt2["total_copies"], self.total_copies)
        assert_equal(tkt2["storage_fee"], self.storage_fee)
        r = float(round(tkt1["royalty"], 3))
        assert_equal(r, self.royalty)
        if self.royalty > 0:
            assert_equal(tkt2["royalty_address"], self.nonmn3_address1)
        else:
            assert(len(tkt2["royalty_address"]) == 0)
        assert_equal(tkt2["green"], self.is_green)
        assert_equal(tkt2["signatures"]["principal"][self.creator_pastelid1],
                     tkt1["signatures"]["principal"][self.creator_pastelid1])

        #   c.c get the same ticket by txid from c.a.6 and compare with ticket from c.b.2
        tkt3 = self.nodes[self.non_mn3].tickets("get", ticket.reg_txid)["ticket"]
        assert_equal(tkt3["signatures"]["principal"][self.creator_pastelid1],
                     tkt1["signatures"]["principal"][self.creator_pastelid1])

        #   c.d list all NFT registration tickets, check Pastel IDs
        nft_tickets_list = top_mn_node.tickets("list", ticket_type_name)
        f1 = False
        f2 = False
        for t in nft_tickets_list:
            if ticket1_key == t["ticket"]["key"]:
                f1 = True
            if label == t["ticket"]["label"]:
                f2 = True
        assert_true(f1)
        assert_true(f2)

        nft_tickets_by_pid = top_mn_node.tickets("find", ticket_type_name, self.creator_pastelid1)
        print(self.top_mns[0].pastelid)
        print(nft_tickets_by_pid)

        print("NFT registration tickets tested")


    # ===============================================================================================================
    def test_signatures(self, item_type: TicketType, label: str, non_registered_personal_pastelid1: str, non_registered_mn_pastelid1: str):
        """Test signatures for the given 'item_type'

        Args:
            item_type (str): ticket type (nft, action)
            label (str): label to use for ticket search
            non_registered_personal_pastelid1 (str): not registered personal Pastel ID
            non_registered_mn_pastelid1 (str): not registered MasterNode Pastel ID (mnid)
        """

        top_mn_node = self.nodes[self.top_mns[0].index]

        #   c.a register nft registration ticket
        #       c.a.1 fail if not MN
        ticket = self.tickets[item_type]
        principal_pastelid = ticket.reg_pastelid
        ticket_type_name = item_type.type_name
        assert_raises_rpc(rpc.RPC_INTERNAL_ERROR, "This is not an active masternode", 
            self.nodes[self.non_mn1].tickets, "register", ticket_type_name, 
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.nonmn1_pastelid2, self.passphrase, label, str(self.storage_fee))

        #       c.a.2 fail if not active MN
        assert_raises_rpc(rpc.RPC_INTERNAL_ERROR, "This is not an active masternode",
            self.nodes[self.non_active_mn].tickets, "register", ticket_type_name, 
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.non_active_mn_pastelid1, self.passphrase, label, str(self.storage_fee))

        #       c.a.3 fail if active MN, but wrong Pastel ID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"Pastel ID [{self.nonmn1_pastelid2}] is not stored in this local node",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.nonmn1_pastelid2, self.passphrase,
            label, str(self.storage_fee))

        #       c.a.4 fail if active MN, but wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_INVALID_PASS,
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, "wrong", label, str(self.storage_fee))

        #       c.a.5 fail if principal's signature is not matching
        self.signatures_dict["principal"][principal_pastelid] = self.top_mns[1].signature
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Principal signature is invalid", 
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))
        self.signatures_dict["principal"][principal_pastelid] = self.principal_signatures_dict[principal_pastelid]

        #       c.a.6 fail if MN2 and MN3 signatures are not matching
        self.signatures_dict["mn2"][self.top_mns[1].pastelid] = self.top_mns[2].signature
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "MN2 signature is invalid",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))
        self.signatures_dict["mn2"][self.top_mns[1].pastelid] = self.top_mns[1].signature

        self.signatures_dict["mn3"][self.top_mns[2].pastelid] = self.top_mns[1].signature
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "MN3 signature is invalid", 
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))
        self.signatures_dict["mn3"][self.top_mns[2].pastelid] = self.top_mns[2].signature

        #       c.a.7 fail if principal's Pastel ID is not registered
        self.signatures_dict["principal"][non_registered_personal_pastelid1] = self.principal_signatures_dict[principal_pastelid]
        del self.signatures_dict["principal"][principal_pastelid]
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Principal Pastel ID Registration ticket not found",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))
        self.signatures_dict["principal"][principal_pastelid] = self.principal_signatures_dict[principal_pastelid]
        del self.signatures_dict["principal"][non_registered_personal_pastelid1]

        #       c.a.8 fail if principal's Pastel ID is not personal
        self.signatures_dict["principal"][self.top_mns[1].pastelid] = self.top_mns[1].signature
        del self.signatures_dict["principal"][principal_pastelid]
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Principal Pastel ID is NOT personal Pastel ID",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))
        self.signatures_dict["principal"][principal_pastelid] = self.principal_signatures_dict[principal_pastelid]
        del self.signatures_dict["principal"][self.top_mns[1].pastelid]

        #       c.a.9 fail if MN Pastel ID is not registered
        self.signatures_dict["mn2"][non_registered_mn_pastelid1] = self.top_mns[1].signature
        del self.signatures_dict["mn2"][self.top_mns[1].pastelid]
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "MN2 Pastel ID Registration ticket not found",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))
        self.signatures_dict["mn2"][self.top_mns[1].pastelid] = self.top_mns[1].signature
        del self.signatures_dict["mn2"][non_registered_mn_pastelid1]

        #       c.a.10 fail if MN Pastel ID is personal
        self.signatures_dict["mn2"][self.creator_pastelid1] = self.top_mns[1].signature
        del self.signatures_dict["mn2"][self.top_mns[1].pastelid]
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "MN2 Pastel ID is NOT masternode Pastel ID",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))
        self.signatures_dict["mn2"][self.top_mns[1].pastelid] = self.top_mns[1].signature
        del self.signatures_dict["mn2"][self.creator_pastelid1]

        #       c.a.8 fail if MN1, MN2 and MN3 are not from top 10 list at the ticket's blocknum
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "was NOT in the top masternodes list for block", 
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.not_top_mns_signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))

        #       c.a.9 fail if MN1, MN2 and MN3 are the same
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "MNs Pastel IDs can not be the same",
            top_mn_node.tickets, "register", ticket_type_name,
            ticket.reg_ticket, json.dumps(self.same_mns_signatures_dict), self.top_mns[0].pastelid, self.passphrase, label, str(self.storage_fee))


    def make_zero_balance(self, nodeNum):
        """Make zero balance on the given node

        Args:
            nodeNum (int): node number
        """
        balance = self.nodes[nodeNum].getbalance()
        print(f"node{nodeNum} balance: {balance}")
        if balance > 0:
            self.nodes[nodeNum].sendtoaddress(self.miner_address, balance, "make_zero_balance", "miner", True)
            self.generate_and_sync_inc(1, self.mining_node_num)


    # ===============================================================================================================
    def nft_activate_ticket_tests(self, skip_low_coins_tests):
        """NFT activation ticket tests

        Args:
            skip_low_coins_tests (bool): True to skip low coins tests
        """
        print("== NFT activation Tickets test ==")

        cmd = "register"
        cmd_param = TicketType.ACTIVATE.type_name
        ticket = self.tickets[TicketType.NFT]
        ticket_type_name = TicketType.ACTIVATE.type_name
        nftact_ticket_price = self.tickets[TicketType.ACTIVATE].ticket_price

        assert_shows_help(self.nodes[self.non_mn3].tickets, cmd, cmd_param)

        # d. NFT activation ticket
        #   d.a register NFT activation ticket (ticket.reg_txid; self.storage_fee; ticket.reg_height)
        #       d.a.1 fail if wrong Pastel ID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), 
            str(self.storage_fee), self.top_mns[1].pastelid, self.passphrase)

        #       d.a.2 fail if wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_INVALID_PASS,
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
            str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, "wrong")

        #       d.a.7 fail if not enough coins to pay 90% of registration price (from ActionReg ticket) (90) + tnx fee (act ticket price)
        self.make_zero_balance(self.non_mn3)
        if not skip_low_coins_tests:
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "Not enough coins to cover price [100 PSL]",
                self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), 
                str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, str(self.collateral), "", "", False)
        self.__wait_for_sync_all10()

        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not valid ticket type",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.mn_nodes[0].mnid_reg_txid,
            str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        #       d.a.3 fail if there is no Action Ticket with this txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not in the blockchain",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.get_random_txid(),
            str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        current_height = self.nodes[self.non_mn3].getblockcount()
        if current_height - ticket.reg_height < 10:
            # not enough confirmations
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "Activation ticket can be created only after",
                self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
                str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        self.__wait_for_gen10_blocks()
        print(f"Current height: {self.nodes[self.non_mn3].getblockcount()}")

        #       d.a.4 fail if creator's Pastel ID in the activation ticket
        #       is not matching creator's Pastel ID in the registration ticket
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the Creator's Pastel ID",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
            str(ticket.reg_height), str(self.storage_fee), self.nonmn3_pastelid1, self.passphrase)

        #       d.a.5 fail if wrong creator ticket height
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the CreatorHeight",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
            "55", str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        #       d.a.6 fail if wrong storage fee
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the storage fee",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid,
            str(ticket.reg_height), "55", ticket.reg_pastelid, self.passphrase)

        self.update_mn_indexes(self.hot_node_num, ticket.reg_height)
        #       d.a.7 register without errors
        #
        mn0_collateral_address = self.nodes[self.top_mns[0].index].masternode("status")["payee"]
        mn1_collateral_address = self.nodes[self.top_mns[1].index].masternode("status")["payee"]
        mn2_collateral_address = self.nodes[self.top_mns[2].index].masternode("status")["payee"]

        # MN's collateral addresses belong to hot_node - non_mn2
        mn0_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn0_collateral_address)
        mn1_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn1_collateral_address)
        mn2_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn2_collateral_address)

        coins_before = self.nodes[self.non_mn3].getbalance()
        print(f"Coins before '{ticket_type_name}' registration: {coins_before}")

        ticket.act_txid = self.nodes[self.non_mn3].tickets(cmd, cmd_param,
            ticket.reg_txid, str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, self.passphrase)["txid"]
        assert_true(ticket.act_txid, "NFT was not activated")
        self.inc_ticket_counter(TicketType.ACTIVATE)
        self.__wait_for_ticket_tnx()

        #       d.a.9 check correct amount of change and correct amount spent and correct amount of fee paid
        main_mn_fee = self.storage_fee90percent*3/5
        other_mn_fee = self.storage_fee90percent/5

        coins_after = self.nodes[self.non_mn3].getbalance()
        print(f"Coins after '{ticket_type_name}' registration: {coins_after}")
        assert_equal(coins_after, coins_before - Decimal(self.storage_fee90percent) - Decimal(nftact_ticket_price))  # no fee yet, but ticket cost act ticket price

        # MN's collateral addresses belong to hot_node - non_mn2
        mn0_coins_after = self.nodes[self.hot_node_num].getreceivedbyaddress(mn0_collateral_address)
        mn1_coins_after = self.nodes[self.hot_node_num].getreceivedbyaddress(mn1_collateral_address)
        mn2_coins_after = self.nodes[self.hot_node_num].getreceivedbyaddress(mn2_collateral_address)

        # print("mn0: before="+str(mn0_coins_before)+"; after="+str(mn0_coins_after) +
        # ". fee should be="+str(mainMN_fee))
        # print("mn1: before="+str(mn1_coins_before)+"; after="+str(mn1_coins_after) +
        # ". fee should be="+str(otherMN_fee))
        # print("mn1: before="+str(mn2_coins_before)+"; after="+str(mn2_coins_after) +
        # ". fee should be="+str(otherMN_fee))
        assert_equal(mn0_coins_after-mn0_coins_before, main_mn_fee)
        assert_equal(mn1_coins_after-mn1_coins_before, other_mn_fee)
        assert_equal(mn2_coins_after-mn2_coins_before, other_mn_fee)

        #       d.a.10 fail if already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"The Activation ticket for the Registration ticket with txid [{ticket.reg_txid}] already exists",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, self.passphrase)

        #       d.a.11 from another node - get ticket transaction and check
        #           - there are 3 outputs to MN1, MN2 and MN3 with correct amounts
        #               (MN1: 60%; MN2, MN3: 20% each, of registration price)
        #           - amounts is totaling 10PSL
        nftact_ticket1_hash = self.nodes[0].getrawtransaction(ticket.act_txid)
        nftact_ticket1_tx = self.nodes[0].decoderawtransaction(nftact_ticket1_hash)
        amount = 0
        fee_amount = 0

        for v in nftact_ticket1_tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                fee_amount += v["value"]
            if v["scriptPubKey"]["type"] == "pubkeyhash":
                if v["scriptPubKey"]["addresses"][0] == mn0_collateral_address:
                    assert_equal(v["value"], main_mn_fee)
                    amount += v["value"]
                if v["scriptPubKey"]["addresses"][0] in [mn1_collateral_address, mn2_collateral_address]:
                    assert_equal(v["value"], other_mn_fee)
                    amount += v["value"]
        assert_equal(amount, self.storage_fee90percent)
        assert_equal(fee_amount, nftact_ticket_price)

        #   d.b find activation ticket
        #       d.b.1 by creators Pastel ID (this is MultiValue key)
        #       TODO Pastel: find activation ticket by creators Pastel ID (this is MultiValue key)

        #       d.b.3 by Registration height - creator_height from registration ticket (this is MultiValue key)
        #       TODO Pastel: find activation ticket by Registration height -
        #        creator_height from registration ticket (this is MultiValue key)

        #       d.b.2 by Registration txid - reg_txid from registration ticket, compare to ticket from d.b.2
        nftact_ticket1 = self.nodes[self.non_mn1].tickets("find", ticket_type_name, ticket.reg_txid)
        tkt1 = nftact_ticket1["ticket"]
        assert_equal(tkt1['type'], "nft-act")
        assert_equal(tkt1['pastelID'], ticket.reg_pastelid)
        assert_equal(tkt1['reg_txid'], ticket.reg_txid)
        assert_equal(tkt1['creator_height'], ticket.reg_height)
        assert_equal(tkt1['storage_fee'], self.storage_fee)
        assert_equal(nftact_ticket1['txid'], ticket.act_txid)

        #   d.c get the same ticket by txid from d.a.8 and compare with ticket from d.b.2
        nftact_ticket2 = self.nodes[self.non_mn1].tickets("get", ticket.act_txid)
        tkt2 = nftact_ticket2["ticket"]
        assert_equal(tkt2["signature"], tkt1["signature"])

        #   d.d list all NFT activation tickets, check Pastel IDs
        nftact_tickets_list = self.nodes[0].tickets("list", ticket_type_name)
        f1 = False
        for t in nftact_tickets_list:
            if ticket.reg_txid == t["ticket"]["reg_txid"]:
                f1 = True
        assert_true(f1)

        nft_tickets_by_pid = self.nodes[self.top_mns[0].index].tickets("find", ticket_type_name, ticket.reg_pastelid)
        print(self.top_mns[0].pastelid)
        print(nft_tickets_by_pid)
        nft_tickets_by_height = self.nodes[self.top_mns[0].index].tickets("find", ticket_type_name, str(ticket.reg_height))
        print(ticket.reg_height)
        print(nft_tickets_by_height)

        print("NFT activation tickets tested")


    # ===============================================================================================================
    def action_activate_ticket_tests(self, action_type: ActionType, skip_low_coins_tests):
        """Action activation ticket tests

        Args:
            skip_low_coins_tests (bool): True to skip low coins tests
        """
        print("== Action activation Tickets test ==")

        reg_ticket_type = action_type.reg_ticket_type
        cmd = "activate"
        cmd_param = reg_ticket_type.type_name
        ticket_type_name = action_type.act_ticket_type.type_name
        ticket = self.tickets[reg_ticket_type]
        actionact_ticket_price = self.tickets[action_type.act_ticket_type].ticket_price

        assert_shows_help(self.nodes[self.non_mn3].tickets, cmd, cmd_param)
        
        # d. Action activation ticket
        #   d.a register Action activation ticket (ticket.reg_txid; self.storage_fee; ticket.reg_height)
        #       d.a.1 fail if wrong Pastel ID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), 
            str(self.storage_fee), self.top_mns[1].pastelid, self.passphrase)

        #       d.a.2 fail if wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_INVALID_PASS,
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), str(self.storage_fee), self.action_caller_pastelid, "wrong")

        #       d.a.7 fail if not enough coins to pay 90% of registration price (from Action Reg ticket) (90) + tnx fee (act ticket price)
        self.make_zero_balance(self.non_mn3)
        if not skip_low_coins_tests:
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "Not enough coins to cover price",
                self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), str(self.storage_fee), 
                self.action_caller_pastelid, self.passphrase, self.nonmn3_address_nobalance)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, str(self.collateral), "", "", False)
        self.__wait_for_sync_all10()

        #       d.a.3 fail if there is no ActionReg Ticket with this txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not valid ticket type",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.mn_nodes[0].mnid_reg_txid, str(ticket.reg_height), str(self.storage_fee), self.action_caller_pastelid, self.passphrase)

        #       d.a.3 fail if there is no ActionReg Ticket with this txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not in the blockchain",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.get_random_txid(), str(ticket.reg_height), str(self.storage_fee), self.action_caller_pastelid, self.passphrase)

        # not enough confirmations
        print(f"Current height: {self.nodes[self.non_mn3].getblockcount()}")
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Activation ticket can be created only after",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), str(self.storage_fee), self.action_caller_pastelid, self.passphrase)
        self.__wait_for_gen10_blocks()
        print(f"Current height: {self.nodes[self.non_mn3].getblockcount()}")

        #       d.a.4 fail if Caller's Pastel ID in the activation ticket
        #       is not matching Caller's Pastel ID in the registration ticket
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the Action Caller's Pastel ID",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), str(self.storage_fee), self.nonmn3_pastelid1, self.passphrase)

        #       d.a.5 fail if wrong creator ticket height
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the CalledAtHeight",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, "55", str(self.storage_fee), self.action_caller_pastelid, self.passphrase)

        #       d.a.6 fail if wrong storage fee
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the storage fee",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), "55", self.action_caller_pastelid, self.passphrase)

        # update top mn indexes at historical action reg ticket creation height
        self.update_mn_indexes(0, ticket.reg_height)

        #       d.a.7 register without errors
        #
        mn0_collateral_address = self.nodes[self.top_mns[0].index].masternode("status")["payee"]
        mn1_collateral_address = self.nodes[self.top_mns[1].index].masternode("status")["payee"]
        mn2_collateral_address = self.nodes[self.top_mns[2].index].masternode("status")["payee"]

        # MN's collateral addresses belong to hot_node - non_mn2
        mn0_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn0_collateral_address)
        mn1_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn1_collateral_address)
        mn2_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn2_collateral_address)

        coins_before = self.nodes[self.non_mn3].getbalance()
        print(f"Coins before '{ticket_type_name}' registration: {coins_before}")

        ticket.act_txid = self.nodes[self.non_mn3].tickets(cmd, cmd_param,
            ticket.reg_txid, str(ticket.reg_height), str(self.storage_fee), self.action_caller_pastelid, self.passphrase)["txid"]
        assert_true(ticket.act_txid, "Action was not activated")
        self.__wait_for_ticket_tnx()

        #       d.a.9 check correct amount of change and correct amount spent and correct amount of fee paid
        main_mn_fee = self.storage_fee80percent*3/5
        other_mn_fee = self.storage_fee80percent/5

        coins_after = self.nodes[self.non_mn3].getbalance()
        print(f"Coins after '{ticket_type_name}' registration: {coins_after}")
        assert_equal(coins_after, coins_before-Decimal(self.storage_fee80percent)-Decimal(actionact_ticket_price))  # no fee yet, but ticket cost act ticket price

        # MN's collateral addresses belong to hot_node - non_mn2
        mn0_coins_after = self.nodes[self.hot_node_num].getreceivedbyaddress(mn0_collateral_address)
        mn1_coins_after = self.nodes[self.hot_node_num].getreceivedbyaddress(mn1_collateral_address)
        mn2_coins_after = self.nodes[self.hot_node_num].getreceivedbyaddress(mn2_collateral_address)

        # print("mn0: before="+str(mn0_coins_before)+"; after="+str(mn0_coins_after) +
        # ". fee should be="+str(mainMN_fee))
        # print("mn1: before="+str(mn1_coins_before)+"; after="+str(mn1_coins_after) +
        # ". fee should be="+str(otherMN_fee))
        # print("mn1: before="+str(mn2_coins_before)+"; after="+str(mn2_coins_after) +
        # ". fee should be="+str(otherMN_fee))
        assert_equal(mn0_coins_after-mn0_coins_before, main_mn_fee)
        assert_equal(mn1_coins_after-mn1_coins_before, other_mn_fee)
        assert_equal(mn2_coins_after-mn2_coins_before, other_mn_fee)

        #       d.a.10 fail if already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"The Activation ticket for the Registration ticket with txid [{ticket.reg_txid}] already exists",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, ticket.reg_txid, str(ticket.reg_height), 
                str(self.storage_fee), self.action_caller_pastelid, self.passphrase)

        #       d.a.11 from another node - get ticket transaction and check
        #           - there are 3 outputs to MN1, MN2 and MN3 with correct amounts
        #               (MN1: 60%; MN2, MN3: 20% each, of registration price)
        #           - amounts are totaling 10PSL
        actionact_ticket1_hash = self.nodes[0].getrawtransaction(ticket.act_txid)
        actionact_ticket1_tx = self.nodes[0].decoderawtransaction(actionact_ticket1_hash)
        amount = 0
        fee_amount = 0

        for v in actionact_ticket1_tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                fee_amount += v["value"]
            if v["scriptPubKey"]["type"] == "pubkeyhash":
                if v["scriptPubKey"]["addresses"][0] == mn0_collateral_address:
                    assert_equal(v["value"], main_mn_fee)
                    amount += v["value"]
                if v["scriptPubKey"]["addresses"][0] in [mn1_collateral_address, mn2_collateral_address]:
                    assert_equal(v["value"], other_mn_fee)
                    amount += v["value"]
        assert_equal(amount, self.storage_fee80percent)
        assert_equal(fee_amount, actionact_ticket_price)

        #   d.b find activation ticket
        #       d.b.1 by creators Pastel ID (this is MultiValue key)
        #       TODO Pastel: find activation ticket by creators Pastel ID (this is MultiValue key)

        #       d.b.3 by Registration height - creator_height from registration ticket (this is MultiValue key)
        #       TODO Pastel: find activation ticket by Registration height -
        #        creator_height from registration ticket (this is MultiValue key)

        #       d.b.2 by Registration txid - reg_txid from registration ticket, compare to ticket from d.b.2
        actionact_ticket1 = self.nodes[self.non_mn1].tickets("find", ticket_type_name, ticket.reg_txid)
        tkt1 = actionact_ticket1["ticket"]
        assert_equal(tkt1['type'], "action-act")
        assert_equal(tkt1['pastelID'], self.action_caller_pastelid)
        assert_equal(tkt1['reg_txid'], ticket.reg_txid)
        assert_equal(tkt1['called_at'], ticket.reg_height)
        assert_equal(tkt1['storage_fee'], self.storage_fee)
        assert_equal(actionact_ticket1['txid'], ticket.act_txid)

        #   d.c get the same ticket by txid from d.a.8 and compare with ticket from d.b.2
        actionact_ticket2 = self.nodes[self.non_mn1].tickets("get", ticket.act_txid)
        tkt2 = actionact_ticket2["ticket"]
        assert_equal(tkt2["signature"], tkt1["signature"])

        #   d.d list all Action activation tickets, check Pastel IDs
        actionact_tickets_list = self.nodes[0].tickets("list", ticket_type_name)
        f1 = False
        for t in actionact_tickets_list:
            if ticket.reg_txid == t["ticket"]["reg_txid"]:
                f1 = True
        assert_true(f1)

        action_tickets_by_pastelid = self.nodes[self.top_mns[0].index].tickets("find", ticket_type_name, self.action_caller_pastelid)
        print(self.top_mns[0].pastelid)
        print(action_tickets_by_pastelid)
        action_tickets_by_height = self.nodes[self.top_mns[0].index].tickets("find", ticket_type_name, str(ticket.reg_height))
        print(ticket.reg_height)
        print(action_tickets_by_height)

        print("Action activation tickets tested")


    # ===============================================================================================================
    def register_and_activate_item(self, item_type: TicketType, copies: int = 1, confirm_activation: bool = True):
        print(f"Registering and activating {item_type.description}")

        ticket = self.tickets[item_type]

        # send money for registration and activation to the node with creator/caller
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 5000 + self.collateral, "", "", False)
        self.generate_and_sync_inc(1, self.mining_node_num)

        action_type = get_action_type(item_type)
        # create item of the given type
        if item_type == TicketType.NFT:
            self.create_nft_ticket_v2(self.non_mn3, None, False, copies, self.royalty, self.is_green)
        elif action_type:
            self.create_action_ticket(self.non_mn3, action_type, self.action_caller_pastelid)

        # register item
        ticket.reg_txid = self.nodes[self.top_mns[0].index].tickets("register", item_type.type_name,
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase,
            self.get_random_txid(), str(self.storage_fee))["txid"]
        ticket.reg_node_id = self.top_mns[0].index
        ticket.pastelid_node_id = self.non_mn3
        assert_true(ticket.reg_txid, f"{item_type.description} was not registered")
        self.inc_ticket_counter(item_type)
        print(f' - registered {item_type.description} ticket at {ticket.reg_height}, reg_txid=[{ticket.reg_txid}, label=[{ticket.label}]]')
        ticket.royalty_address = self.nonmn3_address1

        # wait for 10 confirmations
        self.__wait_for_ticket_tnx(10)
        print(f' - {item_type.description} [{ticket.reg_txid}] registration confirmed')

        # activate item
        # tickets activate xxxxx "reg-ticket-txid" "creator-height" "fee" "PastelID" "passphrase" ["address"]
        act_type = get_activation_type(item_type)
        act_ticket = self.tickets[act_type]
        ticket.act_txid = self.nodes[self.non_mn3].tickets("activate", item_type.type_name,
            ticket.reg_txid, str(ticket.reg_height), str(self.storage_fee), ticket.reg_pastelid, self.passphrase)["txid"]
        assert_true(ticket.act_txid, f"{item_type.description} was not activated")
        self.inc_ticket_counter(act_type)
        act_ticket.reg_txid = ticket.act_txid
        act_ticket.reg_node_id = self.non_mn3
        ticket.act_height = self.nodes[self.non_mn3].getblockcount()
        print(f' - activated {item_type.description} ticket at {ticket.act_height}, act_txid=[{ticket.act_txid}]')

        confirmation_count = 10 if confirm_activation else 1
        # wait for confirmations
        self.__wait_for_ticket_tnx(confirmation_count)
        if confirm_activation:
            print(f' - {item_type.description} [{ticket.reg_txid}] activation confirmed')


    # ===============================================================================================================
    def nft_intended_for_tests(self):
        """ tests intendedFor for NFT offer tickets
        """
        print('=== Testing intendedFor feature for NFTs ===')

        ticket = self.tickets[TicketType.NFT]

        # register and activate nft
        self.register_and_activate_item(TicketType.NFT, 1, True)

        # register Offer ticket with intended recipient creator_pastelid3
        # tickets register offer "nft-txid" "price" "PastelID" "passphrase" [valid-after] [valid-before] [copy-number] ["address"] ["intendedFor"]
        ticket.item_price = 1000
        current_height = self.nodes[self.non_mn3].getblockcount()
        offer_ticket_txid = self.nodes[self.non_mn3].tickets("register", "offer",
            ticket.act_txid, str(ticket.item_price), ticket.reg_pastelid, self.passphrase, 0,
            current_height + 30, 1, "", self.creator_pastelid3)["txid"]
        assert_true(offer_ticket_txid, "No Offer ticket was created")
        print(f' - NFT Offer ticket created [{offer_ticket_txid}] with intended recipient [{self.creator_pastelid3}]')
        self.inc_ticket_counter(TicketType.OFFER, 1, TicketType.NFT)
        # wait for 10 confirmations
        self.generate_and_sync_inc(10, self.mining_node_num)
        print(f' - NFT Offer [{offer_ticket_txid}] confirmed')

        # tickets register accept "offer_txid" "price" "PastelID" "passphrase" ["address"]
        # try to accept this NFT offer with different Pastel ID (creator_pastelid2)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"does not match new owner's Pastel ID",
            self.nodes[self.non_mn3].tickets, "register", "accept", offer_ticket_txid, str(ticket.item_price), 
            self.creator_pastelid2, self.passphrase)

        # register accept for the correct intended recipient creator_pastelid3
        accept_ticket_txid = self.nodes[self.non_mn3].tickets("register", "accept", 
            offer_ticket_txid, str(ticket.item_price), self.creator_pastelid3, self.passphrase)["txid"]
        assert_true(accept_ticket_txid, "No Accept ticket was created")
        print(f' - NFT Accept ticket created [{accept_ticket_txid}]')
        self.inc_ticket_counter(TicketType.ACCEPT, 1, TicketType.NFT)

        # wait for 10 confirmations
        self.generate_and_sync_inc(10, self.mining_node_num)
        print(f' - NFT Accept [{accept_ticket_txid}] confirmed')

        # tickets register transfer "offer_txid" "accept_txid" "PastelID" "passphrase" ["address"]
        # try to create transfer ticket with incorrect Pastel ID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching",
            self.nodes[self.non_mn3].tickets, "register", "transfer", offer_ticket_txid, accept_ticket_txid, 
            self.creator_pastelid2, self.passphrase)

        # register transfer ticket
        print(f' - registering NFT transfer from [{ticket.reg_pastelid}] to [{self.creator_pastelid3}]')
        transfer_ticket_txid = self.nodes[self.non_mn3].tickets("register", "transfer", offer_ticket_txid, accept_ticket_txid,
            self.creator_pastelid3, self.passphrase)
        assert_true(transfer_ticket_txid, "No Transfer ticket was created")
        print(f' - NFT Transfer ticket created [{transfer_ticket_txid}]')
        self.inc_ticket_counter(TicketType.TRANSFER, 1, TicketType.NFT)
        self.__wait_for_ticket_tnx()
        print('=== intendedFor feature for NFTs tested ===')


    # ===============================================================================================================
    def action_intended_for_tests(self, action_type: ActionType):
        """ Tests intendedFor for Action offer tickets.

        Args:
            action_type (ActionType): action type to test
        """
        reg_ticket_type = action_type.reg_ticket_type
        desc = reg_ticket_type.description
        ticket = self.tickets[reg_ticket_type]
        print(f'=== Testing intendedFor feature for {desc} ===')

        # register and activate action of the specified type
        self.register_and_activate_item(reg_ticket_type, 1, True)

        # register Offer ticket with intended recipient creator_pastelid3
        # tickets register offer "item-txid" "price" "PastelID" "passphrase" [valid-after] [valid-before] [copy-number] ["address"] ["intendedFor"]
        ticket.item_price = 1000
        current_height = self.nodes[self.non_mn3].getblockcount()
        offer_ticket_txid = self.nodes[self.non_mn3].tickets("register", "offer",
            ticket.act_txid, str(ticket.item_price), ticket.reg_pastelid, self.passphrase, 0,
            current_height + 30, 1, "", self.creator_pastelid3)["txid"]
        assert_true(offer_ticket_txid, "No Offer ticket was created")
        print(f' - Offer ticket for {desc} created [{offer_ticket_txid}] with intended recipient [{self.creator_pastelid3}]')
        self.inc_ticket_counter(TicketType.OFFER, 1, reg_ticket_type)

        # wait for 10 confirmations
        self.generate_and_sync_inc(10, self.mining_node_num)
        print(f' - Offer for {desc} [{offer_ticket_txid}] confirmed')

        # tickets register accept "offer_txid" "price" "PastelID" "passphrase" ["address"]
        # try to accept this offer with different Pastel ID (creator_pastelid2)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"does not match new owner's Pastel ID",
            self.nodes[self.non_mn3].tickets, "register", "accept", offer_ticket_txid, str(ticket.item_price), 
            self.creator_pastelid2, self.passphrase)

        # register accept for the correct intended recipient creator_pastelid3
        accept_ticket_txid = self.nodes[self.non_mn3].tickets("register", "accept", 
            offer_ticket_txid, str(ticket.item_price), self.creator_pastelid3, self.passphrase)["txid"]
        assert_true(accept_ticket_txid, "No Accept ticket was created")
        print(f' - Accept ticket for {desc} created [{accept_ticket_txid}]')
        self.inc_ticket_counter(TicketType.ACCEPT, 1, reg_ticket_type)

        # wait for 10 confirmations
        self.generate_and_sync_inc(10, self.mining_node_num)
        print(f' - Accept ticket for {desc} [{accept_ticket_txid}] confirmed')

        # tickets register transfer "offer_txid" "accept_txid" "PastelID" "passphrase" ["address"]
        # try to create transfer ticket with incorrect Pastel ID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching",
            self.nodes[self.non_mn3].tickets, "register", "transfer", offer_ticket_txid, accept_ticket_txid, 
            self.creator_pastelid2, self.passphrase)

        # register transfer ticket
        print(f' - registering {desc} transfer from [{ticket.reg_pastelid}] to [{self.creator_pastelid3}]')
        transfer_ticket_txid = self.nodes[self.non_mn3].tickets("register", "transfer", offer_ticket_txid, accept_ticket_txid,
            self.creator_pastelid3, self.passphrase)
        assert_true(transfer_ticket_txid, "No Transfer ticket was created")
        print(f' - {desc} transfer ticket created [{transfer_ticket_txid}]')
        self.inc_ticket_counter(TicketType.TRANSFER, 1, reg_ticket_type)
        self.__wait_for_ticket_tnx()
        print(f'=== intendedFor feature for {desc} tested ===')


    # ===============================================================================================================
    def offer_ticket_tests(self, item_type: TicketType):
        print(f"== {item_type.description} Offer Tickets test (offering original ticket) ==")
        # tickets register offer item_txid price PastelID passphrase valid_after valid_before
        ticket = self.tickets[item_type]  # item to offer (NFT or Action)
        ticket_type_name = TicketType.OFFER.type_name
        desc = f"{ticket_type_name} ({item_type.description}):"

        assert_shows_help(self.nodes[0].tickets, "register", ticket_type_name)

        # register and activate item on non_mn3 node, don't confirm activation - only 1 confirmation
        self.register_and_activate_item(item_type, 10, False)

        ticket.item_price = 100000
        # Offer ticket fee: 2% of item price (100'000/50=2'000)
        offer_ticket_fee = round(ticket.item_price / 50)

        # 1. fail if not enough coins to pay tnx fee (2% from item price)
        self.make_zero_balance(self.non_mn3)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"Not enough coins to cover price [{offer_ticket_fee} PSL]",
            self.nodes[self.non_mn3].tickets, "register", ticket_type_name,
            ticket.act_txid, str(ticket.item_price), ticket.reg_pastelid, self.passphrase)
        
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 1000 + offer_ticket_fee, "", "", False)
        self.__wait_for_sync_all(1)
        coins_before = self.nodes[self.non_mn3].getbalance()
        print(f"{desc} coins before '{ticket_type_name}' ticket registration: {coins_before}")

        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not valid ticket type",
            self.nodes[self.non_mn3].tickets, "register", ticket_type_name,
            ticket.reg_txid, str(ticket.item_price), ticket.reg_pastelid, self.passphrase)

        # Check there is Activation ticket with this Reg txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not in the blockchain",
            self.nodes[self.non_mn3].tickets, "register", ticket_type_name,
            self.get_random_txid(), str(ticket.item_price), ticket.reg_pastelid, self.passphrase)

        #  not enough confirmations
        print(f"{desc} current height: {self.nodes[self.non_mn3].getblockcount()}")
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Offer ticket can be created only after",
            self.nodes[self.non_mn3].tickets, "register", ticket_type_name,
            ticket.act_txid, str(ticket.item_price), ticket.reg_pastelid, self.passphrase)
        self.__wait_for_gen10_blocks()
        print(f"{desc} current height: {self.nodes[self.non_mn3].getblockcount()}")

        # check Pastel ID in this ticket matches Pastel ID in the referred Activation ticket
        if item_type == TicketType.NFT:
            msg = f"The Pastel ID [{self.nonmn3_pastelid1}] in this ticket is not matching the Creator's Pastel ID [{ticket.reg_pastelid}] in the {TicketType.ACTIVATE.description} ticket with this txid [{ticket.act_txid}]"
        else:
            msg = f"The Pastel ID [{self.nonmn3_pastelid1}] in this ticket is not matching the Action Caller's Pastel ID [{ticket.reg_pastelid}] in the Action Activation ticket with this txid [{ticket.act_txid}]"
        assert_raises_rpc(rpc.RPC_MISC_ERROR, msg,
            self.nodes[self.non_mn3].tickets, "register", ticket_type_name,
            ticket.act_txid, str(ticket.item_price), self.nonmn3_pastelid1, self.passphrase)

        # Fail if asked price is 0
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"The asked price for Offer ticket with registration txid [{ticket.act_txid}] should be not 0",
            self.nodes[self.non_mn3].tickets, "register", ticket_type_name,
            ticket.act_txid, str(0), ticket.reg_pastelid, self.passphrase)

        # Create Offer ticket
        ticket.offer_txid = self.nodes[self.non_mn3].tickets("register", ticket_type_name,
            ticket.act_txid, str(ticket.item_price), ticket.reg_pastelid, self.passphrase)["txid"]
        assert_true(ticket.offer_txid, f"No {item_type.description} Offer ticket was created")
        self.inc_ticket_counter(TicketType.OFFER, 1, item_type)
        self.__wait_for_ticket_tnx()

        # check correct amount of change and correct amount spent
        coins_after = self.nodes[self.non_mn3].getbalance()
        print(f"{desc} coins after '{ticket_type_name}' ticket registration: {coins_after}")
        assert_equal(round(coins_after), round(coins_before - offer_ticket_fee))

        # find Offer ticket
        #   - by item activation txid and index
        offer_ticket1_1 = self.nodes[self.non_mn3].tickets("find", ticket_type_name, ticket.act_txid + ":1")
        assert_equal(offer_ticket1_1['ticket']['type'], ticket_type_name)
        assert_equal(offer_ticket1_1['ticket']['item_txid'], ticket.act_txid)
        assert_equal(offer_ticket1_1["ticket"]["asked_price"], ticket.item_price)

        #   - by Creator's Pastel ID (this is MultiValue key)
        offer_tickets_list1 = self.nodes[self.non_mn3].tickets("find", ticket_type_name, ticket.reg_pastelid)
        found_ticket = False
        for offer_ticket in offer_tickets_list1:
            tkt = offer_ticket['ticket']
            if tkt['item_txid'] == ticket.act_txid and tkt['asked_price'] == ticket.item_price:
                found_ticket = True
            assert_equal(tkt['type'], ticket_type_name)
        assert_true(found_ticket, f"No {item_type.description} {ticket_type_name} tickets found by creator's Pastel ID")

        #   - by item activation txid (this is MultiValue key)
        offer_tickets_list2 = self.nodes[self.non_mn3].tickets("find", ticket_type_name, ticket.act_txid)
        found_ticket = False
        for offer_ticket in offer_tickets_list2:
            tkt = offer_ticket['ticket']
            if tkt['item_txid'] == ticket.act_txid and tkt['asked_price'] == ticket.item_price:
                found_ticket = True
            assert_equal(tkt['type'], ticket_type_name)
        assert_true(found_ticket, f"No {item_type.description} {ticket_type_name} tickets found by txid {ticket.act_txid}")

        #   - get the same ticket by txid from c.a.6 and compare with ticket from c.b.2
        offer_ticket1_2 = self.nodes[self.non_mn3].tickets("get", ticket.offer_txid)
        assert_equal(offer_ticket1_2["ticket"]["item_txid"], offer_ticket1_1["ticket"]["item_txid"])
        assert_equal(offer_ticket1_2["ticket"]["asked_price"], offer_ticket1_1["ticket"]["asked_price"])

        # list all offer tickets
        tickets_list = self.nodes[self.non_mn3].tickets("list", ticket_type_name)
        f1 = False
        f2 = False
        for t in tickets_list:
            if ticket.act_txid == t["ticket"]["item_txid"]:
                f1 = True
            if "1" == str(t["ticket"]["copy_number"]):
                f2 = True
        assert_true(f1)
        assert_true(f2)

        # from another node - get ticket transaction and check
        #           - there are P2MS outputs with non-zero amounts
        #           - amounts is totaling price/50 PSL (100000/50=200)
        offer_ticket1_tx_hash = self.nodes[self.non_mn1].getrawtransaction(ticket.offer_txid)
        offer_ticket1_tx = self.nodes[self.non_mn1].decoderawtransaction(offer_ticket1_tx_hash)
        amount = 0
        for v in offer_ticket1_tx["vout"]:
            assert_greater_than(v["value"], 0)
            if v["scriptPubKey"]["type"] == "multisig":
                amount += v["value"]
        assert_equal(amount, offer_ticket_fee)

        print(f"{item_type.description} Offer tickets tested (first run)")


    # ===============================================================================================================
    def accept_ticket_tests(self, item_type: TicketType):
        print(f"== {item_type.description} Accept Tickets test (accepting original ticket offer) ==")

        ticket = self.tickets[item_type]
        ticket_type_name = TicketType.ACCEPT.type_name
        desc = f"{ticket_type_name} ({item_type.description}):"

        assert_shows_help(self.nodes[0].tickets, "register", ticket_type_name)

        accept_ticket_price = self.tickets[TicketType.ACCEPT].ticket_price
        # estimated accept ticket transaction fee (1% from item price): 100'000 / 100 = 1'000 PSL
        accept_ticket_fee = round(ticket.item_price / 100)
        # fail if not enough funds
        # price (100K) + accept ticket transaction fee(1K): 101'000
        self.make_zero_balance(self.non_mn4)
        coins_before = self.nodes[self.non_mn4].getbalance()
        print(f"{desc} balance before {coins_before}")
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"Not enough coins to cover price [{ticket.item_price + accept_ticket_fee} PSL]",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            ticket.offer_txid, str(ticket.item_price), self.nonmn4_pastelid1, self.passphrase)

        total_accept_tx_amount = ticket.item_price + accept_ticket_fee + accept_ticket_price
        print(f"{desc} estimated total amount for accept transaction - [{ticket.item_price} + {accept_ticket_fee} + {accept_ticket_price} = {total_accept_tx_amount} PSL]")
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 1000 + total_accept_tx_amount, "", "", False)
        self.__wait_for_sync_all(1)
        coins_before = self.nodes[self.non_mn4].getbalance()
        print(f"{desc} coins before '{ticket_type_name}' registration: {coins_before}")

        # Check there is an offer ticket with this offerTxId
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not valid ticket type",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            ticket.act_txid, str(ticket.item_price), self.nonmn4_pastelid1, self.passphrase)

        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not in the blockchain",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            self.get_random_txid(), str(ticket.item_price), self.nonmn4_pastelid1, self.passphrase)

        # fail if not enough confirmations
        print(f"{desc} current height: {self.nodes[self.non_mn4].getblockcount()}")
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Accept ticket can be created only after",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            ticket.offer_txid, str(ticket.item_price), self.nonmn4_pastelid1, self.passphrase)
        self.__wait_for_gen10_blocks()
        print(f"{desc} current height: {self.nodes[self.non_mn4].getblockcount()}")

        # fail if price does not covers the offer price
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"The offered price [100] is less than asked in the Offer ticket [{ticket.item_price}]",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            ticket.offer_txid, str("100"), self.nonmn4_pastelid1, self.passphrase)

        # Create accept ticket
        ticket.accept_txid = self.nodes[self.non_mn4].tickets("register", ticket_type_name,
            ticket.offer_txid, str(ticket.item_price), self.nonmn4_pastelid1, self.passphrase)["txid"]
        assert_true(ticket.accept_txid, "No Accept ticket was created")
        self.inc_ticket_counter(TicketType.ACCEPT, 1, item_type)
        self.__wait_for_ticket_tnx()

        # check correct amount of change and correct amount spent
        coins_after = self.nodes[self.non_mn4].getbalance()
        print(f"{desc} coins after '{ticket_type_name}' registration: {coins_after}")
        assert_equal(round(coins_after), round(coins_before - accept_ticket_fee))  # ticket cost price/100 PSL (100000/100=1000)

        # fail if there is another accept ticket referring to that offer ticket
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"Accept ticket [{ticket.accept_txid}] already exists and is not yet 1h old "
                     f"for this Offer ticket [{ticket.offer_txid}]",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            ticket.offer_txid, str(ticket.item_price), self.nonmn4_pastelid1, self.passphrase)
        print(f"{item_type.description} Accept tickets tested")


    # ===============================================================================================================
    def transfer_ticket_tests(self, item_type: TicketType):
        print(f"== {item_type.description} Transfer Tickets test (transferring original ticket) ==")

        ticket = self.tickets[item_type]
        ticket_type_name = TicketType.TRANSFER.type_name
        transfer_ticket_price = self.tickets[TicketType.TRANSFER].ticket_price
        desc = f"Transfer ({item_type.description}):"

        assert_shows_help(self.nodes[0].tickets, "register", ticket_type_name)

        # estimated transfer fee - 1% from item price: 100'000 / 100 = 1'000
        transfer_ticket_fee = round(ticket.item_price / 100)
        # fail if not enough funds
        # item price (100K) + transfer fee(1K) = 101000
        self.make_zero_balance(self.non_mn4)
        coins_before = self.nodes[self.non_mn4].getbalance()
        print(f"{desc} balance on node{self.non_mn4}: {coins_before}")
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"Not enough coins to cover price [{ticket.item_price + transfer_ticket_price} PSL]",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            ticket.offer_txid, ticket.accept_txid, self.nonmn4_pastelid1, self.passphrase)

        # calculate all fees and amount required to finance the transaction
        total_transfer_tx_amount = ticket.item_price + transfer_ticket_fee + transfer_ticket_price
        print(f"{desc} estimated total amount for transfer transaction - [{ticket.item_price} + {transfer_ticket_fee} + {transfer_ticket_price} = {total_transfer_tx_amount} PSL]")

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 1000 + total_transfer_tx_amount, "", "", True)
        self.generate_and_sync_inc(1)
        coins_before = self.nodes[self.non_mn4].getbalance()
        print(f"{desc} balance before (node{self.non_mn4}): {coins_before}")

        # Check that an Offer ticket with this offerTxId exists
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"The ticket with this txid [{ticket.accept_txid}] is not in the blockchain",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            ticket.accept_txid, ticket.accept_txid, self.nonmn4_pastelid1, self.passphrase)
        # This error is from CTransferTicket::Create where it tries to get Offer ticket to get price and txid

        # Check that an accept ticket with this acceptTxId exists
        assert_raises_rpc(rpc.RPC_MISC_ERROR, 
            f"The Accept ticket with this txid [{ticket.offer_txid}] referred by this Transfer ticket is not valid ticket type",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            ticket.offer_txid, ticket.offer_txid, self.nonmn4_pastelid1, self.passphrase)
        # This error is from CTransferTicket::IsValid -> common_ticket_validation

        # fail if not enough confirmations after accept ticket
        print(f"{desc} current height: {self.nodes[self.non_mn4].getblockcount()}")
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Transfer ticket can be created only after",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            ticket.offer_txid, ticket.accept_txid, self.nonmn4_pastelid1, self.passphrase)
        self.__wait_for_gen10_blocks()
        print(f"{desc} current height: {self.nodes[self.non_mn4].getblockcount()}")

        offerer_pastel_id = self.nodes[self.non_mn3].tickets("get", ticket.offer_txid)["ticket"]["pastelID"]
        print(f"{desc} current owner's Pastel ID: {offerer_pastel_id}")
        offerer_address = self.nodes[self.non_mn3].tickets("find", "id", offerer_pastel_id)["ticket"]["address"]
        offerer_coins_before = round(self.nodes[self.non_mn3].getreceivedbyaddress(offerer_address))
        print(f"{desc} current owner's '{offerer_address}' balance: {offerer_coins_before}")

        # register transfer
        ticket.transfer_txid = self.nodes[self.non_mn4].tickets("register", ticket_type_name,
            ticket.offer_txid, ticket.accept_txid, self.nonmn4_pastelid1, self.passphrase)["txid"]
        assert_true(ticket.transfer_txid, "No Transfer ticket was created")
        self.inc_ticket_counter(TicketType.TRANSFER, 1, item_type)
        self.__wait_for_ticket_tnx()

        # calculate amount that current owner is expected to receive
        offerer_coins_expected_to_receive = ticket.item_price
        royalty_coins_expected_fee = 0
        green_coins_expected_fee = 0
        if item_type == TicketType.NFT:
            royalty_coins_expected_fee = 0
            green_coins_expected_fee = 0
            if self.royalty > 0:
                royalty_coins_expected_fee = round(ticket.item_price * self.royalty)
                offerer_coins_expected_to_receive -= royalty_coins_expected_fee
                print(f"{desc} royalty fee to pay - {royalty_coins_expected_fee} PSL ({self.royalty*100}% of item price)")
            if self.is_green: # green fee is 2% of item price
                green_coins_expected_fee = round(ticket.item_price / 50)
                offerer_coins_expected_to_receive -= green_coins_expected_fee
                print(f"{desc} green fee to pay - {green_coins_expected_fee} PSL (2% of item price)")
        print(f"{desc} current owner is expected to receive - {offerer_coins_expected_to_receive} PSL")

        # from another node - get ticket transaction and check
        #   - there are 3 posiible outputs to offerer, royalty and green adresses
        tx_raw = self.nodes[0].getrawtransaction(ticket.transfer_txid)
        tx = self.nodes[0].decoderawtransaction(tx_raw)
        offerer_coins = 0
        royalty_coins = 0
        green_coins = 0
        multi_coins = 0

        for v in tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                multi_coins += v["value"]
            if v["scriptPubKey"]["type"] == "pubkeyhash":
                amount = v["value"]
                print(f"{desc} transaction pubkeyhash vout - {amount}")
                if v["scriptPubKey"]["addresses"][0] == offerer_address and amount == offerer_coins_expected_to_receive:
                    offerer_coins = amount
                    print(f"{desc} transaction to offerer's address [{offerer_address}] - {amount}")
                if v["scriptPubKey"]["addresses"][0] == ticket.royalty_address and amount == royalty_coins_expected_fee:
                    royalty_coins = amount
                    print(f"{desc} transaction to royalty's address [{ticket.royalty_address}] - {amount}")
                if v["scriptPubKey"]["addresses"][0] == self.green_address and self.is_green:
                    green_coins = amount
                    print(f"{desc} transaction to green's address [{self.green_address}] - {amount}")
        print(f"{desc} transaction multisig coins - {multi_coins}")
        assert_equal(round(offerer_coins), round(offerer_coins_expected_to_receive))
        if item_type == TicketType.NFT:
            assert_equal(royalty_coins, royalty_coins_expected_fee)
            assert_equal(green_coins, green_coins_expected_fee)
        assert_equal(round(offerer_coins + royalty_coins + green_coins), ticket.item_price)
        assert_equal(round(multi_coins), round(transfer_ticket_price))

        # check correct amount of change and correct amount spent
        coins_after = self.nodes[self.non_mn4].getbalance()
        print(f"{desc} node{self.non_mn4} balance changes: {coins_before} -> {coins_after}")
        # ticket cost is transfer ticket price, item cost is 100'000
        assert_equal(round(coins_after), round(coins_before - ticket.item_price - transfer_ticket_price))

        # check offerer gets correct amount
        offerer_coins_after = round(self.nodes[self.non_mn3].getreceivedbyaddress(offerer_address))
        # correct total amount that current owner is expected to receive if royalty
        # address is the same as offerer's address
        if offerer_address == ticket.royalty_address:
            offerer_coins_expected_to_receive += royalty_coins_expected_fee
        print(f"{desc} current owner's balance changes: {offerer_coins_before} -> {offerer_coins_after}")
        assert_equal(round(offerer_coins_after - offerer_coins_before), round(offerer_coins_expected_to_receive))

        # make sure we have enough funds on non_mn4 node, otherwise it will fail with another error - "Not enough coins to cover price..."
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 1000 + total_transfer_tx_amount, "", "", True)
        self.generate_and_sync_inc(1)
        nonmn4_balance = self.nodes[self.non_mn4].getbalance()
        print(f"{desc} balance (node{self.non_mn4}): {nonmn4_balance}")

        # fail if there is another transfer ticket referring to that offer ticket
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"Transfer ticket already exists for the Offer ticket with this txid [{ticket.offer_txid}]",
            self.nodes[self.non_mn4].tickets, "register", ticket_type_name,
            ticket.offer_txid, ticket.accept_txid, self.nonmn4_pastelid1, self.passphrase)

        print(f"{item_type.description} Transfer tickets tested")


    # ===============================================================================================================
    def offer_accept_transfer_tests(self, item_type: TicketType):
        print(f"== {item_type.description} Offer|Accept|Transfer Ticket tests ==")

        ticket = self.tickets[item_type]

        self.slow_mine(3, 10, 2, 0.5)
        self.sync_all()

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 5000, "", "", False)
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 10000, "", "", False)
        self.generate_and_sync_inc(1)

        # register and activate item on non_mn3 node, max 5 copies
        self.register_and_activate_item(item_type, 5)
        # set royalty to the address on non_mn5 to not interfere with balance calculations
        if item_type == TicketType.NFT:
            self.nft_change_royalty()

        offerer_node, offerer_pastelid = ticket.pastelid_node_id, ticket.reg_pastelid
        acceptor_node, acceptor_pastelid = self.non_mn4, self.nonmn4_pastelid1
        transfer_txid = ticket.act_txid

        # transfer item 4 times (3 transfers of transfer tickets):
        #    item (nonmn3, creator_pastelid1)
        #    --(T1)--> transfer1 (nonmn4, nonmn4_pastelid1)
        #                   --(T2)--> transfer1_2 (nonmn3, nonmn3_pastelid1)
        #                             --(T3)--> transfer2_3 (nonmn4, self.nonmn4_pastelid1)
        #                                       --(T4)--> transfer3_4 (nonmn3, nonmn3_pastelid1) (copy #1)
        for transfer_no in range(1, 5):
            transfer_txid = self.offer_accept_transfer_test(f"T{transfer_no}", item_type, 
                offerer_node, offerer_pastelid, acceptor_node, acceptor_pastelid,
                transfer_txid, self.is_green)
            # swap offerer and acceptor node & pastelid
            offerer_node, acceptor_node = acceptor_node, offerer_node
            offerer_pastelid, acceptor_pastelid = acceptor_pastelid, offerer_pastelid
            if transfer_no == 1:
                acceptor_pastelid = self.nonmn3_pastelid1
        # This transfer txid holds the ownership of copy 1 offered multiple times (nested)
        self.nested_ownership_transfer_txid = self.tickets[TicketType.TRANSFER].reg_txid

        if item_type == TicketType.NFT:
            original_transfer_tickets = []
            # offer & transfer original item 4 times
            #   item --(A1)--> transfer1 (copy #2, nonmn4, nonmn4_pastelid1)
            #   item --(A2)--> transfer1 (copy #3, nonmn4, nonmn4_pastelid1)
            #   item --(A3)--> transfer1 (copy #4, nonmn4, nonmn4_pastelid1)
            #   item --(A4)--> transfer1 (copy #5, nonmn4, nonmn4_pastelid1)
            #        --(A5-fail) - exceeds number of transferred copies
            for i in range(1, 5):
                original_transfer_tickets.append(self.offer_accept_transfer_test(f"A{i}", item_type, 
                    ticket.pastelid_node_id, ticket.reg_pastelid, self.non_mn4, self.nonmn4_pastelid1,
                    ticket.act_txid, self.is_green, True)
                                                )
            # now there are 8 Transfer tickets and 4 of them are transferred
            self.offer_accept_transfer_test("A5-Fail", item_type, self.non_mn3, ticket.reg_pastelid,
                                    self.non_mn4, self.nonmn4_pastelid1,
                                    ticket.act_txid, self.is_green, True, True)

        print(f"== {item_type.description} Offer|Accept|Transfer Ticket tested ==")


    class TransferBalanceTracker:
        def __init__(self, nodes, description: str, offerer_node: int, acceptor_node: int):
            self.description = description
            self.offerer_node = offerer_node
            self.acceptor_node = acceptor_node
            self.offerer_balances = list()
            self.acceptor_balances = list()
            self.nodes = nodes
            offerer_balance, acceptor_balance = self._add_balances()
            print(f"{self.description}:1 current owner's balance: {offerer_balance:.2f}")
            print(f"{self.description}:1 new owner's balance: {acceptor_balance:.2f}")

        def _add_balances(self):
            offerer_balance = self.nodes[self.offerer_node].getbalance()
            self.offerer_balances.append(offerer_balance)
            acceptor_balance = self.nodes[self.acceptor_node].getbalance()
            self.acceptor_balances.append(acceptor_balance)
            return offerer_balance, acceptor_balance

        def new_operation(self, log_balances: bool = True, log_total_diff: bool = False):
            prev_offerer_balance = self.offerer_balances[-1]
            prev_acceptor_balance = self.acceptor_balances[-1]
            offerer_balance, acceptor_balance = self._add_balances()
            op_count = len(self.acceptor_balances)
            if log_balances:
                print(f"{self.description}:{op_count} current owner's balance: {offerer_balance:.2f},"
                      f" diff: {offerer_balance:.2f} - {prev_offerer_balance:.2f} = {(offerer_balance - prev_offerer_balance):.2f}")
                print(f"{self.description}:{op_count} new owner's balance: {acceptor_balance:.2f},"
                      f" diff: {acceptor_balance:.2f} - {prev_acceptor_balance:.2f} = {(acceptor_balance - prev_acceptor_balance):.2f}")
            if log_total_diff:
                first_offerer_balance = self.offerer_balances[0]
                first_acceptor_balance = self.acceptor_balances[0]
                print(f"{self.description}:1->{op_count} current owner's balance changes:"
                      f"{offerer_balance:.2f} - {first_offerer_balance:.2f} = {(offerer_balance - first_offerer_balance):.2f}")
                print(f"{self.description}:1->{op_count} new owner's balance changes:"
                      f"{acceptor_balance:.2f} - {first_acceptor_balance:.2f} = {(acceptor_balance - first_acceptor_balance):.2f}")

    # ===============================================================================================================
    def offer_accept_transfer_test(self, test_id: str, item_type: TicketType, 
            offerer_node: int, offerer_pastelid: str,
            acceptor_node: int, acceptor_pastelid: str,
            item_to_offer_txid: str,
            is_green: bool = False, skip_last_fail_test: bool = False, will_fail: bool = False):

        print(f"===== Test {test_id} for '{item_type.description}' : node{offerer_node} offers and node{acceptor_node} accepts =====")
        self.print_heights()
        ticket = self.tickets[item_type]
        offer_ticket = self.tickets[TicketType.OFFER]
        accept_ticket = self.tickets[TicketType.ACCEPT]
        transfer_ticket = self.tickets[TicketType.TRANSFER]
        desc = f'{test_id} ({item_type.description})'

        # get ticket item type
        item_to_offer_type_name = self.nodes[offerer_node].tickets("get", item_to_offer_txid)["ticket"]["type"]
        print(f"{desc} item to offer type - [{item_to_offer_type_name}]")

        self.slow_mine(2, 10, 2, 0.5)

        is_action = (item_type == TicketType.SENSE_ACTION) or (item_type == TicketType.CASCADE_ACTION)

        ticket.item_price = 1000 # set offer item price to 1000 PSL
        # Offer ticket fee: 2% of item price (10'00/50=2'0)
        offer_ticket_fee = round(ticket.item_price / 50)

        # tickets register offer "txid" "price" "PastelID" "passphrase" [valid-after] [valid-before] [copy-number] ["address"] ["intendedFor"]
        if will_fail:
            print(f"===== Test {test_id} for '{item_type.description}' should fail =====")
            if is_action:
                errmsg = f"Ownership for the {item_type.description} ticket [{item_to_offer_txid}] - is already transferred"
            else:
                errmsg = f"The NFT you are trying to offer - from NFT Registration ticket [{item_to_offer_txid}] - is already transferred" + \
                " - there are already [5] transferred copies, but only [5] copies were available"
            assert_raises_rpc(rpc.RPC_MISC_ERROR, errmsg,
                self.nodes[offerer_node].tickets, "register", TicketType.OFFER.type_name,
                item_to_offer_txid, str(ticket.item_price), offerer_pastelid, self.passphrase)
            return

        # first check item we're trying to offer
        item_ticket = self.nodes[offerer_node].tickets("get", item_to_offer_txid)
        print(f'{desc} ticket:\n{json.dumps(item_ticket, indent=4)}')

        balance_tracker = self.TransferBalanceTracker(self.nodes, desc, offerer_node, acceptor_node)

        # create Offer ticket
        offer_ticket.reg_txid = self.nodes[offerer_node].tickets("register", TicketType.OFFER.type_name,
            item_to_offer_txid, str(ticket.item_price), offerer_pastelid, self.passphrase)["txid"]
        offer_ticket.reg_node_id = offerer_node
        assert_true(offer_ticket.reg_txid, "No Offer ticket was created")
        self.inc_ticket_counter(TicketType.OFFER, 1, item_type)
        print(f"{desc} offer_ticket_txid: {offer_ticket.reg_txid}")

        self.__wait_for_ticket_tnx()
        balance_tracker.new_operation()
        self.__wait_for_gen10_blocks()
        balance_tracker.new_operation()

        # create Accept ticket
        # tickets register accept "offer_txid" "price" "PastelID" "passphrase" ["address"]
        accept_ticket.reg_txid = self.nodes[acceptor_node].tickets("register", TicketType.ACCEPT.type_name, offer_ticket.reg_txid, 
            str(ticket.item_price), acceptor_pastelid, self.passphrase)["txid"]
        accept_ticket.reg_node_id = acceptor_node
        assert_true(accept_ticket.reg_txid, "No Accept ticket was created")
        self.inc_ticket_counter(TicketType.ACCEPT, 1, item_type)
        print(f"{desc} accept_ticket_txid: {accept_ticket.reg_txid}")
        self.__wait_for_ticket_tnx()

        balance_tracker.new_operation()
        self.__wait_for_gen10_blocks()
        balance_tracker.new_operation()

        # create transfer ticket
        # tickets register transfer "offer_txid" "accept_txid" "PastelID" "passphrase" ["address"]
        transfer_ticket.reg_txid = self.nodes[acceptor_node].tickets("register", TicketType.TRANSFER.type_name,
            offer_ticket.reg_txid, accept_ticket.reg_txid, acceptor_pastelid, self.passphrase)["txid"]
        transfer_ticket.reg_node_id = acceptor_node
        assert_true(transfer_ticket.reg_txid, "No Transfer ticket was created")
        self.inc_ticket_counter(TicketType.TRANSFER, 1, item_type)
        print(f"{desc} transfer_ticket_txid: {transfer_ticket.reg_txid}")
        # Chosen transfer ticket for validating ownership 
        # 1. We need a list ( at least with 1 element)
        # of non-transferred ticket
        #
        # 2. Filter that tickets by Pastel ID and get the
        # underlying Reg ticket found by txid from the request
        if (re.match("A{1,4}", test_id)):
            # This Pastel ID (and generated transfers) holds the ownership of copies (2-4)
            self.single_offer_transfer_txids.append(transfer_ticket.reg_txid)

        self.__wait_for_ticket_tnx()

        balance_tracker.new_operation(True, True)

        # check correct amount of change and correct amount spent
        assert_equal(balance_tracker.acceptor_balances[-1],
            balance_tracker.acceptor_balances[0] - accept_ticket.ticket_price - transfer_ticket.ticket_price - ticket.item_price)
        # example for NFT:
        #   accept ticket cost is 10 (1000/100),
        #   transfer ticket cost is 10,
        #   NFT cost is 1000

        # check current owner gets correct amount
        royalty_fee = 0
        green_fee = 0
        if not is_action:
            if self.royalty > 0:
                royalty_fee = round(ticket.item_price * self.royalty)  # self.royalty = 0.075 (75 PSL)
            if is_green:
                green_fee = round(ticket.item_price / 50) # green fee is 2% of item price (20 PSL)
        assert_equal(balance_tracker.offerer_balances[-1],
            balance_tracker.offerer_balances[0] + ticket.item_price - offer_ticket_fee - royalty_fee - green_fee)
        # Offer ticket cost is 20 (1000/50), NFT cost is 1000

        if not skip_last_fail_test and item_to_offer_type_name == "transfer":
            if is_action:
                errmsg = f"Ownership for the {TicketType.TRANSFER.description} ticket [{item_to_offer_txid}] is already transferred"
            else:
                errmsg = f"The NFT you are trying to offer - from {TicketType.TRANSFER.description} ticket [{item_to_offer_txid}] - is already transferred"
            # Verify we cannot offer already offered ticket
            # Verify there is no already transfer ticket referring to transfer ticket we are trying to transfer
            assert_raises_rpc(rpc.RPC_MISC_ERROR, errmsg,
                self.nodes[offerer_node].tickets, "register", "offer",
                item_to_offer_txid, str(ticket.item_price), offerer_pastelid, self.passphrase)
        print(f"Tested {test_id} for '{item_type.description}': node{offerer_node} offers and node{acceptor_node} accepts")

        return transfer_ticket.reg_txid


    # ===============================================================================================================
    def tickets_list_filter_tests(self, loop_number):
        print("== Tickets List Filter test ==")

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 200, "", "", False)
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 1100, "", "", False)
        self.generate_and_sync_inc(1)
        self.list_all_ticket_counters()

        print(' --- list id')
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.ID) + self.ticket_counter(TicketType.MNID))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id", "all")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.ID) + self.ticket_counter(TicketType.MNID))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id", "mn")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.MNID))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id", "personal")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.ID))

        nft_ticket = self.tickets[TicketType.NFT]
        self.create_nft_ticket_v1(self.non_mn3, 5, self.royalty, self.is_green)
        nft_ticket2_txid = self.nodes[self.top_mns[0].index].tickets("register", "nft",
            nft_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase,
            "nft-label3_"+str(loop_number), str(self.storage_fee))["txid"]
        assert_true(nft_ticket2_txid, "No NFT registration ticket was created")
        self.inc_ticket_counter(TicketType.NFT)
        self.__wait_for_ticket_tnx()

        self.slow_mine(2, 10, 2, 0.5)

        nft_ticket2_act_ticket_txid = self.nodes[self.non_mn3].tickets("register", "act",
            nft_ticket2_txid, str(nft_ticket.reg_height), str(self.storage_fee), nft_ticket.reg_pastelid, self.passphrase)["txid"]
        assert_true(nft_ticket2_act_ticket_txid, "NFT was not activated")
        self.inc_ticket_counter(TicketType.ACTIVATE)
        self.__wait_for_ticket_tnx()

        self.create_nft_ticket_v1(self.non_mn3, 1, self.royalty, self.is_green, False)
        nft_ticket3_txid = self.nodes[self.top_mns[0].index].tickets("register", "nft",
            nft_ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase,
            "nft-label4_"+str(loop_number), str(self.storage_fee))["txid"]
        assert_true(nft_ticket3_txid, "No NFT registration ticket was created")
        self.inc_ticket_counter(TicketType.NFT)
        self.__wait_for_ticket_tnx()

        self.slow_mine(2, 10, 2, 0.5)

        print(' --- list nft')
        tickets_list = self.nodes[self.non_mn3].tickets("list", "nft")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.NFT))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "nft", "all")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.NFT))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "nft", "active")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.ACTIVATE))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "nft", "inactive")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.NFT) - self.ticket_counter(TicketType.ACTIVATE))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "nft", "transferred")
        assert_equal(len(tickets_list), 1 + 1*(loop_number + 1))

        print(' --- list act')
        tickets_list = self.nodes[self.non_mn3].tickets("list", "act")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.ACTIVATE))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "act", "all")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.ACTIVATE))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "act", "available")
        assert_equal(len(tickets_list), 1 + 2*(loop_number + 1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "act", "transferred")
        assert_equal(len(tickets_list), 1 + 1*(loop_number + 1))

        cur_block = self.nodes[self.non_mn3].getblockcount()
        offer_ticket1_txid = self.nodes[self.non_mn3].tickets("register", "offer", nft_ticket2_act_ticket_txid,
                                                             str("1000"),
                                                             nft_ticket.reg_pastelid, self.passphrase,
                                                             cur_block+15, cur_block+20)["txid"]
        assert_true(offer_ticket1_txid, "No Offer ticket was created")
        print(offer_ticket1_txid)
        self.inc_ticket_counter(TicketType.OFFER, 1, TicketType.NFT)
        self.__wait_for_ticket_tnx()  # cur+5 block

        offer_ticket2_txid = self.nodes[self.non_mn3].tickets("register", "offer", nft_ticket2_act_ticket_txid,
                                                             str("1000"),
                                                             nft_ticket.reg_pastelid, self.passphrase,
                                                             cur_block+20, cur_block+30)["txid"]
        assert_true(offer_ticket2_txid, "No Offer ticket was created")
        print(offer_ticket2_txid)
        self.inc_ticket_counter(TicketType.OFFER, 1, TicketType.NFT)
        self.__wait_for_ticket_tnx()  # cur+10 block

        offer_ticket3_txid = self.nodes[self.non_mn3].tickets("register", "offer", nft_ticket2_act_ticket_txid,
                                                             str("1000"),
                                                             nft_ticket.reg_pastelid, self.passphrase,
                                                             cur_block+30, cur_block+40)["txid"]
        assert_true(offer_ticket3_txid, "No Offer ticket was created")
        self.__wait_for_ticket_tnx()  # +5 blocks
        self.slow_mine(1, 10, 2, 0.5)  # +25 blocks
        print(offer_ticket3_txid)
        self.inc_ticket_counter(TicketType.OFFER, 1, TicketType.NFT)

        self.list_all_ticket_counters()

        print(' --- list offer')
        tickets_list = self.nodes[self.non_mn3].tickets("list", "offer")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.OFFER))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "offer", "all")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.OFFER))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "offer", "available")
        assert_equal(len(tickets_list), 1)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "offer", "unavailable")
        assert_equal(len(tickets_list), 1)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "offer", "expired")
        assert_equal(len(tickets_list), 1 + (loop_number*2))

        accept_ticket_txid = self.nodes[self.non_mn4].tickets("register", "accept", offer_ticket2_txid, str("1000"),
                                                           self.nonmn4_pastelid1, self.passphrase)["txid"]
        assert_true(accept_ticket_txid, "No Accept ticket was created")
        self.inc_ticket_counter(TicketType.ACCEPT, 1, TicketType.NFT)
        print(f"accept_ticket_txid: {accept_ticket_txid}")
        self.__wait_for_ticket_tnx()  # +5 blocks
        self.slow_mine(2, 10, 2, 0.5)  # +25 blocks

        print(' --- list accept')
        tickets_list = self.nodes[self.non_mn3].tickets("list", "accept")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.ACCEPT))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "accept", "all")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.ACCEPT))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "accept", "expired")
        expired_accepted_tickets = 1*(loop_number + 1)
        assert_equal(len(tickets_list), expired_accepted_tickets)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "accept", "transferred")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.ACCEPT) - expired_accepted_tickets)

        print(' --- list transfer')
        tickets_list = self.nodes[self.non_mn3].tickets("list", "transfer")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.TRANSFER))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "transfer", "all")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.TRANSFER))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "transfer", "available")
        available_transfer_tickets = 10*(loop_number + 1)
        assert_equal(len(tickets_list), available_transfer_tickets)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "transfer", "transferred")
        assert_equal(len(tickets_list), self.ticket_counter(TicketType.TRANSFER) - available_transfer_tickets)

        print ("Test listing offer/accept/transfer tickets by Pastel ID")

        print(' --- list offer pastel-id-creator')
        tickets_list = self.nodes[self.non_mn3].tickets("list", "offer", nft_ticket.reg_pastelid)
        assert_equal(len(tickets_list), 2*(loop_number + 1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "offer", "all", nft_ticket.reg_pastelid)
        assert_equal(len(tickets_list), 2*(loop_number + 1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "offer", "available", nft_ticket.reg_pastelid)
        assert_equal(len(tickets_list), 0)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "offer", "unavailable", nft_ticket.reg_pastelid)
        assert_equal(len(tickets_list), 0)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "offer", "expired", nft_ticket.reg_pastelid)
        assert_equal(len(tickets_list), 2 + (loop_number*2))

        print(' --- list accept pastel-id1')
        tickets_list = self.nodes[self.non_mn3].tickets("list", "accept", self.nonmn4_pastelid1)
        assert_equal(len(tickets_list), 11*(loop_number + 1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "accept", "all", self.nonmn4_pastelid1)
        assert_equal(len(tickets_list), 11*(loop_number + 1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "accept", "expired", self.nonmn4_pastelid1)
        assert_equal(len(tickets_list), 1*(loop_number + 1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "accept", "transferred", self.nonmn4_pastelid1)
        assert_equal(len(tickets_list), 10*(loop_number + 1))

        print(' --- list transfer pastel-id1')
        tickets_list = self.nodes[self.non_mn3].tickets("list", "transfer", self.nonmn3_pastelid1)
        assert_equal(len(tickets_list), 4*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "transfer", "all", self.nonmn3_pastelid1)
        assert_equal(len(tickets_list), 4*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "transfer", "available", self.nonmn3_pastelid1)
        assert_equal(len(tickets_list), 1 + 1*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "transfer", "transferred", self.nonmn3_pastelid1)
        assert_equal(len(tickets_list), 2*(loop_number+1))

        print("Tickets List Filter tested")


    # ===============================================================================================================
    def takedown_ticket_tests(self):
        print("== Take down Tickets test ==")
        # ...
        print("Take down tickets tested")


# ===============================================================================================================
    def ethereum_address_ticket_tests(self):
        print("== Ethereum Address Tickets test ==")

        # New nonmn4_pastelid2 to test some functionalities
        self.nonmn4_address2 = self.nodes[self.non_mn4].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address2, 2000, "", "", False)
        self.__wait_for_sync_all10()
        self.nonmn4_pastelid2 = self.create_pastelid(self.non_mn4)[0]

        node_id = self.non_mn4
        self.nonmn8_address1 = self.nodes[node_id].getnewaddress()
        self.__wait_for_sync_all10()
        self.nonmn8_pastelid1 = self.create_pastelid(node_id)[0]

        # Register first time by Pastel ID of non-masternode 3
        tickets_ethereumaddress_txid1 = self.nodes[self.non_mn3].tickets("register", "ethereumaddress", "0x863c30dd122a21f815e46ec510777fd3e3398c26",
                                                    self.creator_pastelid1, self.passphrase)
        self.__wait_for_ticket_tnx()
        nonmn3_ticket_ethereumaddress_1 = self.nodes[self.non_mn4].tickets("get", tickets_ethereumaddress_txid1["txid"])
        print(nonmn3_ticket_ethereumaddress_1)

        self.__wait_for_ticket_tnx()
        assert_equal(nonmn3_ticket_ethereumaddress_1["ticket"]["pastelID"], self.creator_pastelid1)
        assert_equal(nonmn3_ticket_ethereumaddress_1["ticket"]["ethereumAddress"], "0x863c30dd122a21f815e46ec510777fd3e3398c26")
        assert_equal(nonmn3_ticket_ethereumaddress_1["ticket"]["fee"], 100)

        # Register by a new pastelID. Expect to get Exception that the ticket is invalid because there are Not enough 100 PSL to cover price 100
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Ticket (ethereum-address-change) is invalid - Not enough coins to cover price [100 PSL]",
            self.nodes[node_id].tickets, "register", "ethereumaddress",
            "0xf24C621e5108607F4EC60e9C4f91719a76c7B3C9", self.nonmn8_pastelid1, self.passphrase)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn8_address1, 200, "", "", False)
        self.__wait_for_sync_all10()

        # This should be success
        self.nodes[node_id].tickets("register", "ethereumaddress", "0xf24C621e5108607F4EC60e9C4f91719a76c7B3C9",
                                                        self.nonmn8_pastelid1, self.passphrase)
        self.__wait_for_ticket_tnx()

        # Expect to get Exception that the ticket is invalid because this Pastel ID do not have enough 5000PSL to pay the rechange fee
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Ticket (ethereum-address-change) is invalid - Not enough coins to cover price [5000 PSL]",
            self.nodes[node_id].tickets, "register", "ethereumaddress",
            "0x7cB11556A8883f002514B6878575811728f2A158 ", self.nonmn8_pastelid1, self.passphrase)

        # Send money to non-masternode3 to cover 5000 price
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 5100, "", "", False)
        self.__wait_for_sync_all10()

        # Expect to get Exception that the ticket is invalid because this Pastel ID changed EthereumAddress in last 24 hours
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Ticket (ethereum-address-change) is invalid - Ethereum Address Change ticket is invalid. Already changed in last 24 hours.",
            self.nodes[self.non_mn3].tickets, "register", "ethereumaddress",
            "0xD2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f ", self.creator_pastelid1, self.passphrase)

        # Wait till next 24 hours. Below test cases is commented because it took lots of time to complete.
        # To test this functionality on local machine, we should lower the waiting from 24 * 24 blocks to smaller value, ex: 15 blocks only.
        self.sync_all()
        print("Mining 577 blocks")
        for ind in range (577):
            self.nodes[self.mining_node_num].generate(1)
            time.sleep(1)

        print("Waiting 60 seconds")
        time.sleep(60)

        # Expect that nonmn3 can change ethereumaddress after 24 hours, fee should be 5000
        tickets_ethereumaddress_txid1 = self.nodes[self.non_mn3].tickets("register", "ethereumaddress", "0xD2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f",
                                                    self.creator_pastelid1, self.passphrase)
        self.__wait_for_ticket_tnx()
        nonmn3_ticket_ethereumaddress_1 = self.nodes[self.non_mn4].tickets("get", tickets_ethereumaddress_txid1["txid"])
        print(nonmn3_ticket_ethereumaddress_1)

        self.__wait_for_ticket_tnx()
        assert_equal(nonmn3_ticket_ethereumaddress_1["ticket"]["pastelID"], self.creator_pastelid1)
        assert_equal(nonmn3_ticket_ethereumaddress_1["ticket"]["ethereumAddress"], "0xD2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f")
        assert_equal(nonmn3_ticket_ethereumaddress_1["ticket"]["fee"], 5000)

        # Register by a new pastelID with invalid Ethereum address. Expect to get Exception that the Ethereum address is invalid
        try:
            tickets_ethereumaddress_txid1 = self.nodes[self.non_mn4].tickets("register", "ethereumaddress", "D2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f",
                                                        self.nonmn4_pastelid2, self.passphrase)
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Ticket (ethereum-address-change) is invalid - Invalid length of ethereum address, the length should be exactly 40 characters" in self.errorString, True)

        try:
            tickets_ethereumaddress_txid1 = self.nodes[self.non_mn4].tickets("register", "ethereumaddress", "1xD2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f",
                                                        self.nonmn4_pastelid2, self.passphrase)
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Invalid ethereum address, should start with the characters 0x" in self.errorString, True)

        try:
            tickets_ethereumaddress_txid1 = self.nodes[self.non_mn4].tickets("register", "ethereumaddress", "0xZ2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f",
                                                        self.nonmn4_pastelid2, self.passphrase)
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Invalid ethereum address, should contains only hex digits" in self.errorString, True)

        # length not valid
        non_mn1_bad_ethereumaddress_length = self.nodes[self.non_mn4].tickets("tools", "validateethereumaddress", "0xD2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E")
        print(non_mn1_bad_ethereumaddress_length)
        assert_equal(non_mn1_bad_ethereumaddress_length["isInvalid"], True)
        assert_equal(non_mn1_bad_ethereumaddress_length["validationError"], "Invalid length of ethereum address, the length should be exactly 40 characters")
        # not start with 0x
        non_mn1_bad_ethereumaddress_start = self.nodes[self.non_mn4].tickets("tools", "validateethereumaddress", "D2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E")
        print(non_mn1_bad_ethereumaddress_start)
        assert_equal(non_mn1_bad_ethereumaddress_start["isInvalid"], True)
        assert_equal(non_mn1_bad_ethereumaddress_start["validationError"], "Invalid ethereum address, should start with 0x")
        # contains characters that is different than hex digits
        non_mn1_bad_ethereumaddress_character = self.nodes[self.non_mn4].tickets("tools", "validateethereumaddress", "0xZ2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f ")
        print(non_mn1_bad_ethereumaddress_character)
        assert_equal(non_mn1_bad_ethereumaddress_character["isInvalid"], True)
        assert_equal(non_mn1_bad_ethereumaddress_character["validationError"], "Invalid Ethereum address, should only contain hex digits")
        # good ethereum address
        non_mn1_ethereum_address_good = self.nodes[self.non_mn4].tickets("tools", "validateethereumaddress", "0xD2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f")
        print(non_mn1_ethereum_address_good)
        assert_equal(non_mn1_ethereum_address_good["isInvalid"], False)
        assert_equal(non_mn1_ethereum_address_good["validationError"], "")

        print("Ethereum address tickets tested")


    # ===============================================================================================================
    def storage_fee_tests(self):
        print("== Storage fee test ==")
        # 4. storagefee tests
        # a. Get Network median storage fee
        #   a.1 from non-MN without errors
        ticket = self.tickets[TicketType.NFT]
        non_mn1_total_storage_fee1 = self.nodes[self.non_mn4].tickets("tools", "gettotalstoragefee",
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.nonmn4_pastelid1, self.passphrase,
            "key6", str(self.storage_fee), 5)["totalstoragefee"]
        #   a.2 from MN without errors
        mn0_total_storage_fee1 = self.nodes[self.top_mns[0].index].tickets("tools", "gettotalstoragefee",
            ticket.reg_ticket, json.dumps(self.signatures_dict),  self.top_mns[0].pastelid, self.passphrase,
            "key6", str(self.storage_fee), 5)["totalstoragefee"]
        mn0_total_storage_fee2 = self.nodes[self.top_mns[0].index].tickets("tools", "gettotalstoragefee",
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase,
            "key6", str(self.storage_fee), 4)["totalstoragefee"]

        #   a.3 compare a.1 and a.2
        print(non_mn1_total_storage_fee1)
        print(mn0_total_storage_fee1)
        print(mn0_total_storage_fee2)
        assert_equal(int(non_mn1_total_storage_fee1), int(mn0_total_storage_fee1))
        assert_greater_than(int(non_mn1_total_storage_fee1), int(mn0_total_storage_fee2))

        # b. Get local masternode storage fee
        #   b.1 fail from non-MN
        #   b.2 from MN without errors
        #   b.3 compare b.2 and a.1

        # c. Set storage fee for MN
        #   c.1 fail on non-MN
        #   c.2 on MN without errors
        #   c.3 get local MN storage fee and compare it with c.2

        # Test if storagefee works properly
        nfee_mn0 = self.nodes[0].storagefee("getnetworkfee")["networkfee"]
        nfee_mn1 = self.nodes[1].storagefee("getnetworkfee")["networkfee"]
        nfee_mn2 = self.nodes[2].storagefee("getnetworkfee")["networkfee"]
        assert_equal(nfee_mn0, 50)
        assert_equal(nfee_mn1, 50)
        assert_equal(nfee_mn2, 50)
        print(f"Network fee is {nfee_mn0}")

        lfee_mn0 = self.nodes[0].storagefee("getlocalfee")["localfee"]
        assert_equal(lfee_mn0, 50)
        print(f"Local fee of MN0 is {lfee_mn0}")

        # Check if the TRIM MEAN do NOT care the 25%
        self.nodes[0].storagefee("setfee", "1000")
        self.nodes[2].storagefee("setfee", "0")
        self.sync_all()

        time.sleep(30)
        lfee_mn0 = self.nodes[0].storagefee("getlocalfee")["localfee"]
        print("Local fee of MN0 after setfee is ", lfee_mn0)
        assert_equal(lfee_mn0, 1000)

        nfee_mn4 = self.nodes[2].storagefee("getnetworkfee")["networkfee"]
        print("Network fee after setfee is ", nfee_mn4)
        assert_equal(nfee_mn4, 50)

        # Check if the TRIM MEAN do care the middle 50%
        self.nodes[3].storagefee("setfee", "1000")
        self.nodes[4].storagefee("setfee", "1000")
        self.nodes[5].storagefee("setfee", "1000")
        self.nodes[6].storagefee("setfee", "1000")
        self.nodes[7].storagefee("setfee", "1000")
        self.nodes[8].storagefee("setfee", "1000")
        self.sync_all()

        time.sleep(30)
        lfee_mn0 = self.nodes[0].storagefee("getlocalfee")["localfee"]
        print("Local fee of MN0 after setfee is ", lfee_mn0)
        assert_equal(lfee_mn0, 1000)

        nfee_mn4 = self.nodes[2].storagefee("getnetworkfee")["networkfee"]
        print("Network fee after setfee is ", nfee_mn4)
        assert_greater_than(nfee_mn4, 50)

        print("Storage fee tested")


    def __wait_for_sync_all(self, v):
        time.sleep(2)
        self.generate_and_sync_inc(v, self.mining_node_num)

    def __wait_for_gen10_blocks(self):
        self.generate_and_sync_inc(10, self.mining_node_num)

    def __wait_for_sync_all10(self):
        time.sleep(2)
        self.sync_all(10, 30)
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10, 30)

    def __wait_for_ticket_tnx(self, blocks: int = 5):
        time.sleep(10)
        for _ in range(blocks):
            self.nodes[self.mining_node_num].generate(1)
            self.sync_all(10, 3)
        self.sync_all(10, 30)

    def print_heights(self):
        s = ''
        for x in range(17):
            s += str(self.nodes[x].getblockcount()) + ' '
        print(f"Nodes[0..16] heights: [{s}]")

    def slow_mine(self, number_of_bursts, num_in_each_burst, wait_between_bursts, wait_inside_burst):
        for x in range(number_of_bursts):
            for y in range(num_in_each_burst):
                self.nodes[self.mining_node_num].generate(1)
                time.sleep(wait_inside_burst)
            time.sleep(wait_between_bursts)


if __name__ == '__main__':
    MasterNodeTicketsTest().main()