#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
import math

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
from mn_common import MasterNodeCommon
from test_framework.authproxy import JSONRPCException
import json
import time
import random
import string
import test_framework.rpc_consts as rpc

from decimal import Decimal, getcontext
getcontext().prec = 16

# 12 Master Nodes
private_keys_list = ["91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe",  # 0
                     "923JtwGJqK6mwmzVkLiG6mbLkhk1ofKE1addiM8CYpCHFdHDNGo",  # 1
                     "91wLgtFJxdSRLJGTtbzns5YQYFtyYLwHhqgj19qnrLCa1j5Hp5Z",  # 2
                     "92XctTrjQbRwEAAMNEwKqbiSAJsBNuiR2B8vhkzDX4ZWQXrckZv",  # 3
                     "923JCnYet1pNehN6Dy4Ddta1cXnmpSiZSLbtB9sMRM1r85TWym6",  # 4
                     "93BdbmxmGp6EtxFEX17FNqs2rQfLD5FMPWoN1W58KEQR24p8A6j",  # 5
                     "92av9uhRBgwv5ugeNDrioyDJ6TADrM6SP7xoEqGMnRPn25nzviq",  # 6
                     "91oHXFR2NVpUtBiJk37i8oBMChaQRbGjhnzWjN9KQ8LeAW7JBdN",  # 7
                     "92MwGh67mKTcPPTCMpBA6tPkEE5AK3ydd87VPn8rNxtzCmYf9Yb",  # 8
                     "92VSXXnFgArfsiQwuzxSAjSRuDkuirE1Vf7KvSX7JE51dExXtrc",  # 9
                     "91hruvJfyRFjo7JMKnAPqCXAMiJqecSfzn9vKWBck2bKJ9CCRuo",  # 10
                     "92sYv5JQHzn3UDU6sYe5kWdoSWEc6B98nyY5JN7FnTTreP8UNrq",  # 11
                     "92pfBHQaf5K2XBnFjhLaALjhCqV8Age3qUgJ8j8oDB5eESFErsM"   # 12
                     ]


class MasterNodeTicketsTest(MasterNodeCommon):
    number_of_master_nodes = len(private_keys_list)
    number_of_simple_nodes = 8
    total_number_of_nodes = number_of_master_nodes+number_of_simple_nodes

    non_active_mn = number_of_master_nodes-1

    non_mn1 = number_of_master_nodes        # mining node - will have coins #13
    non_mn2 = number_of_master_nodes+1      # hot node - will have collateral for all active MN #14
    non_mn3 = number_of_master_nodes+2      # will not have coins by default #15
    non_mn4 = number_of_master_nodes+3      # will not have coins by default #16
    non_mn5 = number_of_master_nodes+4      # will not have coins by default #17, for royalty first change
    non_mn6 = number_of_master_nodes+5      # will not have coins by default #18, for royalty second change
    non_mn7 = number_of_master_nodes+6      # will not have coins by default #19, for green
    non_mn8 = number_of_master_nodes+7      # will not have coins by default #20, for etherium test

    mining_node_num = number_of_master_nodes    # same as non_mn1
    hot_node_num = number_of_master_nodes+1     # same as non_mn2

    def __init__(self):
        super().__init__()
        self.errorString = ""
        self.is_network_split = False
        self.nodes = []
        self.storage_fee = 100
        self.storage_fee90percent = self.storage_fee*9/10

        self.mn_addresses = {}
        self.mn_pastelids = {}
        self.mn_outpoints = {}
        self.ticket = None
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
        self.action_caller_pastelid = None          # Action Caller Pastel ID
        self.nonmn5_royalty_pastelid1 = None
        self.nonmn6_royalty_pastelid1 = None

        self.mn0_ticket1_txid = None
        self.nonmn3_address1 = None
        self.nonmn4_address1 = None
        self.nonmn5_royalty_address1 = None
        self.nonmn6_royalty_address1 = None
        self.creator_pastelid1 = None
        self.creator_ticket_height = None
        self.total_copies = None
        self.ticket_principal_signature = None
        self.nft_ticket1_txid = None                # NFT registration ticket txid
        self.nftact_ticket1_txid = None             # NFT activation ticket txid
        self.actionreg_ticket1_txid = None          # Action registration ticket txid
        self.actionact_ticket1_txid = None          # Action activation ticket txid
        self.top_mns_index0 = None
        self.top_mns_index1 = None
        self.top_mns_index2 = None
        self.top_mn_pastelid0 = None
        self.top_mn_pastelid1 = None
        self.top_mn_pastelid2 = None
        self.top_mn_ticket_signature0 = None
        self.top_mn_ticket_signature1 = None
        self.top_mn_ticket_signature2 = None
        self.nft_ticket1_sell_ticket_txid = None
        self.nft_ticket1_buy_ticket_txid = None
        self.nft_ticket1_trade_ticket_txid = None
        self.trade_ticket1_sell_ticket_txid = None
        self.trade_ticket1_buy_ticket_txid = None
        self.trade_ticket1_trade_ticket_txid = None
        self.nested_ownership_trade_txid  = None
        self.single_sell_trade_txids = []

        self.id_ticket_price = 10
        self.nftreg_ticket_price = 10
        self.nftact_ticket_price = 10
        self.trade_ticket_price = 10
        self.actionreg_ticket_price = 10
        self.actionact_ticket_price = 10

        self.royalty = 0.075
        self.royalty_tickets_tests = 2
        self.royalty_null_tests = False
        self.royalty_address = None
        self.is_green = True

        self.test_high_heights = False

        self.green_address = "tPj5BfCrLfLpuviSJrD3B1yyWp3XkgtFjb6"

    def setup_chain(self):
        print(f"Initializing test directory {self.options.tmpdir}")
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)

    def setup_network(self, split=False):
        self.setup_masternodes_network(private_keys_list, self.number_of_simple_nodes)

    # generate blocks up to new_height on mining node, sync all nodes
    def mining_inc(self, new_height, nodeNo = 0):
        self.generate_and_sync_inc()

    def run_test(self):

        print("starting MNs for the first time - no shift")
        self.mining_enough(self.mining_node_num, self.number_of_master_nodes)
        cold_nodes = {k: v for k, v in enumerate(private_keys_list[:-1])}  # all but last!!!
        _, _, _ = self.start_mn(self.mining_node_num, self.hot_node_num, cold_nodes, self.total_number_of_nodes)

        self.reconnect_nodes(0, self.number_of_master_nodes)
        self.sync_all()

        self.pastelid_tests()
        self.mn_pastelid_ticket_tests(False)
        self.personal_pastelid_ticket_tests(False)
        self.register_mn_pastelid()
        self.action_reg_ticket_tests("sense", "action-key1", "action-key2");
        self.action_activate_ticket_tests(False)
        self.nft_reg_ticket_tests("key1", "key2")

        if self.royalty > 0:
            if self.royalty_tickets_tests > 0:
                self.personal_nonmn5_royalty_initialize_tests()
                self.nftroyalty_ticket_tests(self.non_mn3, self.creator_pastelid1,
                                             self.nonmn5_royalty_pastelid1, self.nonmn5_royalty_address1, 1)
            if self.royalty_tickets_tests > 1:
                self.personal_nonmn6_royalty_initialize_tests()
                self.nftroyalty_ticket_tests(self.non_mn5, self.nonmn5_royalty_pastelid1,
                                             self.nonmn6_royalty_pastelid1, self.nonmn6_royalty_address1, 2)
        else:
            self.royalty_tickets_tests = 0
            self.royalty_null_tests = True
            self.nftroyalty_null_ticket_tests()

        self.nft_activate_ticket_tests(False)
        self.nft_sell_ticket_tests1(False)
        self.nft_buy_ticket_tests(False)
        self.nft_trade_ticket_tests(False)
        self.nft_sell_buy_trade_tests()
        self.takedown_ticket_tests()
        self.storage_fee_tests()
        self.tickets_list_filter_tests(0)
        self.list_and_validate_ticket_ownerships()

        if self.test_high_heights:
            self.id_ticket_price = 1000

            print(f"Pastel ID ticket price - {self.id_ticket_price}")
            print(f"NFT registration ticket price - {self.nftreg_ticket_price}")
            print(f"NFT activation ticket price - {self.nftact_ticket_price}")
            print(f"NFT Trade ticket price - {self.trade_ticket_price}")
            print(f"Action registration ticket price - {self.actionreg_ticket_price}")
            print(f"Action activation ticket price - {self.action_act_ticket_price}")

            print("mining {} blocks".format(10000))
            for i in range(100):
                self.slow_mine(10, 10, 2, 0.01)
                print(f"mined {100*i} blocks")
                self.reconnect_nodes(0, self.number_of_master_nodes)
                self.sync_all()

            self.pastelid_tests()
            self.personal_pastelid_ticket_tests(True)
            self.action_reg_ticket_tests("cascade", "action-key1", "action-key2");
            self.action_activate_ticket_tests(True)
            self.nft_reg_ticket_tests("key10001", "key20001")
            self.nft_activate_ticket_tests(True)
            self.nft_sell_ticket_tests1(True)
            self.nft_buy_ticket_tests(True)
            self.nft_trade_ticket_tests(True)
            self.nft_sell_buy_trade_tests()
            self.takedown_ticket_tests()
            self.storage_fee_tests()
            self.tickets_list_filter_tests(1)

# ===============================================================================================================
    def list_and_validate_ticket_ownerships(self):
        print("== Ownership validation tests ==")
        tickets_list = self.nodes[self.non_mn4].tickets("list", "nft", "all")

        # Test not available pastelID
        try:
            self.nodes[self.non_mn4].tickets("tools", "validateownership", self.nft_ticket1_txid, "NOT_A_VALID_PASTELID", self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Error: Corresponding PastelID not found!"
                     in self.errorString, True)

        # Test incorrect passphrase
        try:
            self.nodes[self.non_mn3].tickets("tools", "validateownership", self.nft_ticket1_txid, self.creator_pastelid1, self.new_passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Error: Failed to validate passphrase!"
                     in self.errorString, True)

        # Check if author
        res1 = self.nodes[self.non_mn3].tickets("tools", "validateownership", self.nft_ticket1_txid, self.creator_pastelid1, self.passphrase)
        assert_equal( self.nft_ticket1_txid, res1['nft'])
        assert_equal( "", res1['trade'])

        # Test 'single sale' (without re-selling)
        res1 = self.nodes[self.non_mn4].tickets("tools", "validateownership", self.nft_ticket1_txid, self.nonmn4_pastelid1, self.passphrase)
        assert_equal( self.nft_ticket1_txid, res1['nft'])
        assert_equals( self.single_sell_trade_txids, res1['trade'])

        # Test ownership with or re-sold NFT
        res1 = self.nodes[self.non_mn3].tickets("tools", "validateownership", self.nft_ticket1_txid, self.nonmn3_pastelid1, self.passphrase)
        assert_equal( self.nft_ticket1_txid, res1['nft'] )
        assert_equal( self.nested_ownership_trade_txid, res1['trade'])

        # Test no ownership
        res1 = self.nodes[self.non_mn1].tickets("tools", "validateownership", self.nft_ticket1_txid, self.nonmn1_pastelid2, self.passphrase)
        assert_equal( "", res1['nft'])
        assert_equal( "", res1['trade'])

        print("== Ownership validation tested ==")

    # ===============================================================================================================
    def pastelid_tests(self):
        print("== Pastel ID tests ==")
	# most of the pastelid tests moved to the separate script secure_container.py

        # 1. pastelid tests
        # a. Generate new PastelID and associated keys (EdDSA448). Return PastelID and LegRoast pubkey base58-encoded
        # a.a - generate with no errors two keys at MN and non-MN
        self.mn0_pastelid1, self.mn0_id1_lrkey = self.create_pastelid(0)
        self.mn0_pastelid2 = self.create_pastelid(0)[0]

        # for non active MN
        self.non_active_mn_pastelid1 = self.create_pastelid(self.non_active_mn)[0]
        self.nonmn1_pastelid1 = self.create_pastelid(self.non_mn1)[0]
        self.nonmn1_pastelid2 = self.create_pastelid(self.non_mn1)[0]
        # action caller Pastel ID (non MN 4)
        self.action_caller_pastelid = self.create_pastelid(self.non_mn4)[0]

        # for node without coins
        self.nonmn3_pastelid1, self.nonmn3_id1_lrkey = self.create_pastelid(self.non_mn3)
        # b. Import private "key" (EdDSA448) as PKCS8 encrypted string in PEM format. Return PastelID base58-encoded
        # NOT IMPLEMENTED
         # e. Sign "text" with the private "key" (EdDSA448) as PKCS8 encrypted string in PEM format
        # NOT IMPLEMENTED

    # ===============================================================================================================
    def mn_pastelid_ticket_tests(self, skip_low_coins_tests):
        print("== Masternode PastelID Tickets test ==")
        # 2. tickets tests
        # a. PastelID ticket
        #   a.a register MN PastelID
        #       a.a.1 fail if not MN
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "This is not an active masternode",
            self.nodes[self.non_mn1].tickets, "register", "mnid", self.nonmn1_pastelid2, self.passphrase)

        #       a.a.2 fail if not active MN
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "This is not an active masternode",
            self.nodes[self.non_active_mn].tickets, "register", "mnid", self.non_active_mn_pastelid1, self.passphrase)

        #       a.a.3 fail if active MN, but wrong PastelID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[0].tickets, "register", "mnid", self.nonmn1_pastelid2, self.passphrase)

        # TODO: provide better error for unknown PastelID

        #       a.a.4 fail if active MN, but wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[0].tickets, "register", "mnid", self.mn0_pastelid1, "wrong")
        # TODO: provide better error for wrong passphrase

        #       a.a.5 fail if active MN, but not enough coins - ~11PSL
        if not skip_low_coins_tests:
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "No unspent transaction found",
                self.nodes[0].tickets, "register", "mnid", self.mn0_pastelid1, self.passphrase)

        #       a.a.6 register without errors from active MN with enough coins
        mn0_address1 = self.nodes[0].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(mn0_address1, 100, "", "", False)
        self.__wait_for_sync_all(1)

        coins_before = self.nodes[0].getbalance()
        # print(coins_before)

        self.mn0_ticket1_txid = self.nodes[0].tickets("register", "mnid", self.mn0_pastelid1, self.passphrase)["txid"]
        assert_true(self.mn0_ticket1_txid, "No ticket was created")
        self.__wait_for_sync_all(1)

        #       a.a.7 check correct amount of change
        coins_after = self.nodes[0].getbalance()
        print(f"id ticket price - {self.id_ticket_price}")
        assert_equal(coins_after, coins_before - self.id_ticket_price)  # no fee yet

        #       a.a.8 from another node - get ticket transaction and check
        #           - there are P2MS outputs with non-zero amounts
        #           - amounts is totaling ID ticket price
        mn0_ticket1_tx_hash = self.nodes[self.non_mn3].getrawtransaction(self.mn0_ticket1_txid)
        mn0_ticket1_tx = self.nodes[self.non_mn3].decoderawtransaction(mn0_ticket1_tx_hash)
        amount = 0
        for v in mn0_ticket1_tx["vout"]:
            assert_greater_than(v["value"], 0)
            if v["scriptPubKey"]["type"] == "multisig":
                amount += v["value"]
        print(f"id ticket price - {self.id_ticket_price}")
        assert_equal(amount, self.id_ticket_price)

        #       a.a.9.1 fail if PastelID is already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "This PastelID is already registered in blockchain",
            self.nodes[0].tickets, "register", "mnid", self.mn0_pastelid1, self.passphrase)

        #       a.a.9.2 fail if outpoint is already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Ticket (pastelid) is invalid - Masternode's outpoint",
            self.nodes[0].tickets, "register", "mnid", self.mn0_pastelid2, self.passphrase)

        #   a.b find MN PastelID ticket
        #       a.b.1 by PastelID
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
        mn0_ticket1_3 = self.nodes[self.non_mn3].tickets("get", self.mn0_ticket1_txid)
        assert_equal(mn0_ticket1_1["ticket"]["signature"], mn0_ticket1_3["ticket"]["signature"])

        #   a.d list all id tickets, check PastelIDs
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id")
        assert_equal(self.mn0_pastelid1, tickets_list[0]["ticket"]["pastelID"])
        assert_equal(mn0_outpoint, tickets_list[0]["ticket"]["outpoint"])

        print("MN PastelID tickets tested")

    # ===============================================================================================================
    def personal_pastelid_ticket_tests(self, skip_low_coins_tests):
        print("== Personal PastelID Tickets test ==")
        # b. personal PastelID ticket
        self.nonmn3_address1 = self.nodes[self.non_mn3].getnewaddress()
        self.nonmn4_address1 = self.nodes[self.non_mn4].getnewaddress()

        #   b.a register personal PastelID
        #       b.a.1 fail if wrong PastelID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.non_mn3].tickets, "register", "id", self.nonmn1_pastelid2, self.passphrase, self.nonmn3_address1)
        # TODO Pastel: provide better error for unknown PastelID

        #       b.a.2 fail if wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.non_mn3].tickets, "register", "id", self.nonmn3_pastelid1, "wrong", self.nonmn3_address1)
        # TODO Pastel: provide better error for wrong passphrase

        #       b.a.3 fail if not enough coins - ~11PSL
        if not skip_low_coins_tests:
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "No unspent transaction found",
                self.nodes[self.non_mn3].tickets, "register", "id", self.nonmn3_pastelid1, self.passphrase, self.nonmn3_address1)

        #       b.a.4 register without errors from non MN with enough coins
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 100, "", "", False)
        self.__wait_for_sync_all(1)

        coins_before = self.nodes[self.non_mn3].getbalance()
        # print(coins_before)

        nonmn3_ticket1_txid = self.nodes[self.non_mn3].tickets("register", "id", self.nonmn3_pastelid1, self.passphrase,
                                                               self.nonmn3_address1)["txid"]
        assert_true(nonmn3_ticket1_txid, "No ticket was created")
        self.__wait_for_sync_all(1)

        #       a.a.5 check correct amount of change
        coins_after = self.nodes[self.non_mn3].getbalance()
        # print(coins_after)
        print(f"id ticket price - {self.id_ticket_price}")
        assert_equal(coins_after, coins_before - self.id_ticket_price)  # no fee yet

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
        print(f"id ticket price - {self.id_ticket_price}")
        assert_equal(amount, self.id_ticket_price)

        #       b.a.7 fail if already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "This PastelID is already registered in blockchain",
            self.nodes[self.non_mn3].tickets, "register", "id", self.nonmn3_pastelid1, self.passphrase, self.nonmn3_address1)

        #   b.b find personal PastelID
        #       b.b.1 by PastelID
        nonmn3_ticket1_1 = self.nodes[0].tickets("find", "id", self.nonmn3_pastelid1)
        assert_equal(nonmn3_ticket1_1["ticket"]["pastelID"], self.nonmn3_pastelid1)
        assert_equal(nonmn3_ticket1_1["ticket"]["pq_key"], self.nonmn3_id1_lrkey)
        assert_equal(nonmn3_ticket1_1['ticket']['type'], "pastelid")
        assert_equal(nonmn3_ticket1_1['ticket']['id_type'], "personal")

        #       b.b.2 by Address
        nonmn3_ticket1_2 = self.nodes[0].tickets("find", "id", self.nonmn3_address1)
        assert_equal(nonmn3_ticket1_1["ticket"]["signature"], nonmn3_ticket1_2["ticket"]["signature"])

        #   b.c get the ticket by txid from b.a.3 and compare with ticket from b.b.1
        nonmn3_ticket1_3 = self.nodes[self.non_mn3].tickets("get", nonmn3_ticket1_txid)
        assert_equal(nonmn3_ticket1_1["ticket"]["signature"], nonmn3_ticket1_3["ticket"]["signature"])

        #   b.d list all id tickets, check PastelIDs
        tickets_list = self.nodes[0].tickets("list", "id")
        f1 = False
        f2 = False
        for t in tickets_list:
            if self.nonmn3_pastelid1 == t["ticket"]["pastelID"]:
                f1 = True
            if self.nonmn3_address1 == t["ticket"]["address"]:
                f2 = True
        assert_true(f1)
        assert_true(f2)

        print("Personal PastelID tickets tested")

    # ===============================================================================================================
    def personal_nonmn5_royalty_initialize_tests(self):
        # personal royalty PastelID ticket
        self.nonmn5_royalty_pastelid1 = self.create_pastelid(self.non_mn5)[0]
        self.nonmn5_royalty_address1 = self.nodes[self.non_mn5].getnewaddress()

        # register without errors from non MN with enough coins
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn5_royalty_address1, 100, "", "", False)
        self.__wait_for_sync_all(1)

        coins_before = self.nodes[self.non_mn5].getbalance()
        nonmn5_ticket1_txid = self.nodes[self.non_mn5].tickets("register", "id", self.nonmn5_royalty_pastelid1,
                                                               self.passphrase, self.nonmn5_royalty_address1)["txid"]
        assert_true(nonmn5_ticket1_txid, "No ticket was created")
        self.__wait_for_sync_all(1)

        # check correct amount of change
        coins_after = self.nodes[self.non_mn5].getbalance()
        print(f"id ticket price - {self.id_ticket_price}")
        assert_equal(coins_after, coins_before - self.id_ticket_price)  # no fee yet


        print("Personal royalty initialize tested")

    # ===============================================================================================================
    def generate_action_app_ticket_details(self):
        # action app_ticket
        action_ticket_json = {
            "action_app_ticket": "test"
        }
        return action_ticket_json

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
    def personal_nonmn6_royalty_initialize_tests(self):
        # personal royalty PastelID ticket
        self.nonmn6_royalty_pastelid1 = self.create_pastelid(self.non_mn6)[0]
        self.nonmn6_royalty_address1 = self.nodes[self.non_mn6].getnewaddress()


        # register without errors from non MN with enough coins
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn6_royalty_address1, 100, "", "", False)
        self.__wait_for_sync_all(1)

        coins_before = self.nodes[self.non_mn6].getbalance()
        nonmn6_ticket1_txid = self.nodes[self.non_mn6].tickets("register", "id", self.nonmn6_royalty_pastelid1,
                                                               self.passphrase, self.nonmn6_royalty_address1)["txid"]
        assert_true(nonmn6_ticket1_txid, "No ticket was created")
        self.__wait_for_sync_all(1)

        # check correct amount of change
        coins_after = self.nodes[self.non_mn6].getbalance()
        print(f"id ticket price - {self.id_ticket_price}")
        assert_equal(coins_after, coins_before - self.id_ticket_price)  # no fee yet

        print("Personal non_mn6 royalty initialize tested")

    # ===============================================================================================================
    def create_nft_ticket_and_signatures(self, creator_node_num, total_copies, royalty, green, make_bad_signatures_dicts = False):
        """Create NFT ticket and signatures

        Args:
            creator_node_num (int): node that creates NFT ticket and signatures
            total_copies (int): [description]
            royalty (int): [description]
            green (bool): [description]
            make_bad_signatures_dicts (bool): [description]
        """
        # Get current height
        self.creator_ticket_height = self.nodes[0].getblockcount()
        print(f"creator_ticket_height - {self.creator_ticket_height}")
        # Get current top MNs at Node 0
        top_masternodes = self.nodes[0].masternode("top")[str(self.creator_ticket_height)]
        print(f"top_masternodes - {top_masternodes}")

        # Current nft_ticket - 8 Items!!!!
        # {
        #   "nft_ticket_version": integer  // 1
        #   "author": bytes,               // PastelID of the author (creator)
        #   "blocknum": integer,           // block number when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "block_hash": bytes            // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "copies": integer,             // number of copies
        #   "royalty": short,              // how much creator should get on all future resales
        #   "green": bool,                 //
        #   "app_ticket": ...
        # }

        block_hash = self.nodes[creator_node_num].getblock(str(self.creator_ticket_height))["hash"]
        app_ticket_json = self.generate_nft_app_ticket_details()
        app_ticket = str_to_b64str(json.dumps(app_ticket_json))

        json_ticket = {
            "nft_ticket_version": 1,
            "author": self.creator_pastelid1,
            "blocknum": self.creator_ticket_height,
            "block_hash": block_hash,
            "copies": total_copies,
            "royalty": royalty,
            "green": green,
            "app_ticket": app_ticket
        }
        self.ticket = str_to_b64str(json.dumps(json_ticket))
        print(f"nft_ticket - {self.ticket}")

        self.create_signatures(creator_node_num, make_bad_signatures_dicts, top_masternodes)

    # ===============================================================================================================
    def create_action_ticket_and_signatures(self, creator_node_num, action_type, caller_pastelid, make_bad_signatures_dicts = False):
        """Create action ticket and signatures

        Args:
            creator_node_num (int): node that creates an action ticket and signatures
            action_type (str): action type (sense or cascade)
            caller_pastelid (str): action caller Pastel ID
            make_bad_signatures_dicts (bool): if True - create invalid signatures
        """
        # Get current height
        self.creator_ticket_height = self.nodes[0].getblockcount()
        print(f"action called at height - {self.creator_ticket_height}")
        # Get current top MNs at Node 0
        top_masternodes = self.nodes[0].masternode("top")[str(self.creator_ticket_height)]
        print(f"top_masternodes - {top_masternodes}")

        # Current action_ticket
        # {
        #   "action_ticket_version": integer  // 1
        #   "action_type": string             // action type (sense, cascade)
        #   "caller": bytes,                  // PastelID of the action caller
        #   "blocknum": integer,              // block number when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "block_hash": bytes               // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "app_ticket": bytes               // as ascii85(app_ticket),
        #                                     // actual structure of app_ticket is different for different API and is not parsed by pasteld !!!!
        # }

        block_hash = self.nodes[creator_node_num].getblock(str(self.creator_ticket_height))["hash"]
        app_ticket_json = self.generate_action_app_ticket_details()
        app_ticket = str_to_b64str(json.dumps(app_ticket_json))

        json_ticket = {
            "action_ticket_version": 1,
            "action_type": action_type,
            "caller": caller_pastelid,
            "blocknum": self.creator_ticket_height,
            "block_hash": block_hash,
            "app_ticket": app_ticket
        }
        self.ticket = str_to_b64str(json.dumps(json_ticket))
        print(f"action_ticket - {self.ticket}")

        self.create_signatures(creator_node_num, make_bad_signatures_dicts, top_masternodes)


    # ===============================================================================================================
    def create_signatures(self, principal_node_num, make_bad_signatures_dicts, top_masternodes):
        """create ticket signatures

        Args:
            principal_node_num (int): node# for principal signer
            make_bad_signatures_dicts (bool): if True - create invalid signatures
            mn_ticket_signatures (dict): masternode ticket signatures
            top_masternodes (list): list of top master nodes used for signing
        """
        mn_ticket_signatures = {}
        principal_pastelid = self.creator_pastelid1
        self.ticket_principal_signature = self.nodes[principal_node_num].pastelid("sign", self.ticket, principal_pastelid, self.passphrase)["signature"]
        for n in range(0, 12):
            mn_ticket_signatures[n] = self.nodes[n].pastelid("sign", self.ticket, self.mn_pastelids[n], self.passphrase)["signature"]
        print(f"principal ticket signer - {self.ticket_principal_signature}")
        print(f"mn_ticket_signatures - {mn_ticket_signatures}")

        top_mns_indexes = set()
        for mn in top_masternodes:
            index = self.mn_outpoints[mn["outpoint"]]
            top_mns_indexes.add(index)
        print(f"top_mns_indexes - {top_mns_indexes}")

        self.top_mns_index0 = list(top_mns_indexes)[0]
        self.top_mns_index1 = list(top_mns_indexes)[1]
        self.top_mns_index2 = list(top_mns_indexes)[2]
        self.top_mn_pastelid0 = self.mn_pastelids[self.top_mns_index0]
        self.top_mn_pastelid1 = self.mn_pastelids[self.top_mns_index1]
        self.top_mn_pastelid2 = self.mn_pastelids[self.top_mns_index2]
        self.top_mn_ticket_signature0 = mn_ticket_signatures[self.top_mns_index0]
        self.top_mn_ticket_signature1 = mn_ticket_signatures[self.top_mns_index1]
        self.top_mn_ticket_signature2 = mn_ticket_signatures[self.top_mns_index2]

        print(f"top_mns_index0 - {self.top_mns_index0}")
        print(f"top_mn_pastelid0 - {self.top_mn_pastelid0}")

        self.signatures_dict = dict(
            {
                "principal": {principal_pastelid: self.ticket_principal_signature},
                "mn2": {self.top_mn_pastelid1: self.top_mn_ticket_signature1},
                "mn3": {self.top_mn_pastelid2: self.top_mn_ticket_signature2},
            }
        )
        print(f"signatures_dict - {self.signatures_dict!r}")

        if make_bad_signatures_dicts:
            self.same_mns_signatures_dict = dict(
                {
                    "principal": {principal_pastelid: self.ticket_principal_signature},
                    "mn2": {self.top_mn_pastelid0: self.top_mn_ticket_signature0},
                    "mn3": {self.top_mn_pastelid0: self.top_mn_ticket_signature0},
                }
            )
            print(f"same_mns_signatures_dict - {self.same_mns_signatures_dict!r}")

            not_top_mns_indexes = set(self.mn_outpoints.values()) ^ top_mns_indexes
            print(not_top_mns_indexes)

            not_top_mns_index1 = list(not_top_mns_indexes)[0]
            not_top_mns_index2 = list(not_top_mns_indexes)[1]
            not_top_mn_pastelid1 = self.mn_pastelids[not_top_mns_index1]
            not_top_mn_pastelid2 = self.mn_pastelids[not_top_mns_index2]
            not_top_mn_ticket_signature1 = mn_ticket_signatures[not_top_mns_index1]
            not_top_mn_ticket_signature2 = mn_ticket_signatures[not_top_mns_index2]
            self.not_top_mns_signatures_dict = dict(
                {
                    "principal": {principal_pastelid: self.ticket_principal_signature},
                    "mn2": {not_top_mn_pastelid1: not_top_mn_ticket_signature1},
                    "mn3": {not_top_mn_pastelid2: not_top_mn_ticket_signature2},
                }
            )
            print(f"not_top_mns_signatures_dict - {self.not_top_mns_signatures_dict!r}")

    # ===============================================================================================================
    def register_mn_pastelid(self):
        """Create and register:
            - Pastel IDs for all master nodes (0..12)
            - Creator PastelID (non_mn3), used as a principal signer
        """
        self.creator_pastelid1 = self.create_pastelid(self.non_mn3)[0]
        for n in range(0, 12):
            self.mn_addresses[n] = self.nodes[n].getnewaddress()
            self.nodes[self.mining_node_num].sendtoaddress(self.mn_addresses[n], 200, "", "", False)
            if n == 0:
                self.mn_pastelids[n] = self.mn0_pastelid1  # mn0 has its PastelID registered already
            else:
                self.mn_pastelids[n] = self.create_pastelid(n)[0]
            self.mn_outpoints[self.nodes[n].masternode("status")["outpoint"]] = n

        print(f"mn_addresses - {self.mn_addresses}")
        print(f"mn_pastelids - {self.mn_pastelids}")
        print(f"mn_outpoints - {self.mn_outpoints}")

        self.__wait_for_sync_all(1)

        # register Pastel IDs
        self.nodes[self.non_mn3].tickets("register", "id", self.creator_pastelid1, self.passphrase, self.nonmn3_address1)
        for n in range(1, 12):  # mn0 has its PastelID registered already
            self.nodes[n].tickets("register", "mnid", self.mn_pastelids[n], self.passphrase)

    # ===============================================================================================================
    # Action registration ticket tests - tested on non_mn3 node
    def action_reg_ticket_tests(self, action_type, key1, key2):
        print("== Action registration Tickets test ==")
        # Action registration ticket

        self.generate_and_sync_inc(10, self.mining_node_num)

        # generate pastelIDs
        non_registered_personal_pastelid1 = self.create_pastelid(self.non_mn3)[0]
        non_registered_mn_pastelid1 = self.create_pastelid(2)[0]
        for n in range(0, 12):
            self.mn_addresses[n] = self.nodes[n].getnewaddress()
            self.nodes[self.mining_node_num].sendtoaddress(self.mn_addresses[n], 10000, "", "", False)
        self.__wait_for_sync_all(10)

        # create action ticket with bad signatures
        self.create_action_ticket_and_signatures(self.non_mn3, action_type, self.action_caller_pastelid, True)
        ticket_type = "action"
        self.test_signatures(ticket_type, key1, key2, non_registered_personal_pastelid1, non_registered_mn_pastelid1)

        assert_shows_help(self.nodes[0].tickets, "register", ticket_type)

        # create action ticket with valid signatures
        self.create_action_ticket_and_signatures(self.non_mn3, action_type, self.action_caller_pastelid)
        coins_before = self.nodes[self.top_mns_index0].getbalance()
        print(f"coins before ticket registration: {coins_before}")
        self.actionreg_ticket1_txid = self.nodes[self.top_mns_index0].tickets("register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, 
            key1, key2, str(self.storage_fee))["txid"]
        assert_true(self.actionreg_ticket1_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()
        print(self.nodes[self.top_mns_index0].getblockcount())

        #       check correct amount of change and correct amount spent
        coins_after = self.nodes[self.top_mns_index0].getbalance()
        print(f"coins after ticket registration: {coins_after}")
        print(f"Action registration ticket price - {self.actionreg_ticket_price}")
        assert_equal(coins_after, coins_before-self.actionreg_ticket_price)  # no fee yet, but ticket cost action_ticket_price

        #   find registration ticket:
        #      1. by creators PastelID (this is MultiValue key)
        # TODO Pastel:

        #      2. by hash (key1 for now)
        tkt1 = self.nodes[self.non_mn3].tickets("find", ticket_type, key1)["ticket"]
        assert_equal(tkt1["type"], "action-reg")
        assert_equal(tkt1["action_ticket"], self.ticket)
        assert_equal(tkt1["key1"], key1)
        assert_equal(tkt1["key2"], key2)
        assert_equal(tkt1["called_at"], self.creator_ticket_height)
        assert_equal(tkt1["signatures"]["principal"][self.creator_pastelid1], self.ticket_principal_signature)
        assert_equal(tkt1["signatures"]["mn2"][self.top_mn_pastelid1], self.top_mn_ticket_signature1)
        assert_equal(tkt1["signatures"]["mn3"][self.top_mn_pastelid2], self.top_mn_ticket_signature2)
        result = self.nodes[self.non_mn3].pastelid("verify", self.ticket, tkt1["signatures"]["mn1"][self.top_mn_pastelid0], self.top_mn_pastelid0)["verification"]
        assert_equal(result, "OK")

        #       3. by fingerprints, compare to ticket from 2 (key2 for now)
        tkt2 = self.nodes[self.non_mn3].tickets("find", ticket_type, key2)["ticket"]
        assert_equal(tkt2["type"], "action-reg")
        assert_equal(tkt2["action_ticket"], self.ticket)
        assert_equal(tkt2["key1"], key1)
        assert_equal(tkt2["key2"], key2)
        assert_equal(tkt2["called_at"], self.creator_ticket_height)
        assert_equal(tkt2["storage_fee"], self.storage_fee)
        assert_equal(tkt2["signatures"]["principal"][self.creator_pastelid1], 
                     tkt1["signatures"]["principal"][self.creator_pastelid1])

        #   get the same ticket by txid from c.a.6 and compare with ticket from c.b.2
        tkt3 = self.nodes[self.non_mn3].tickets("get", self.actionreg_ticket1_txid)["ticket"]
        assert_equal(tkt3["signatures"]["principal"][self.creator_pastelid1],
                     tkt1["signatures"]["principal"][self.creator_pastelid1])

        #   list all action registration tickets, check PastelIDs
        action_tickets_list = self.nodes[self.top_mns_index0].tickets("list", ticket_type)
        f1 = False
        f2 = False
        for t in action_tickets_list:
            if key1 == t["ticket"]["key1"]:
                f1 = True
            if key2 == t["ticket"]["key2"]:
                f2 = True
        assert_true(f1)
        assert_true(f2)

        action_tickets_by_pid = self.nodes[self.top_mns_index0].tickets("find", ticket_type, self.creator_pastelid1)
        print(self.top_mn_pastelid0)
        print(action_tickets_by_pid)

        print("Action registration tickets tested")


    # ===============================================================================================================
    # NFT registration ticket tests - tested on non_mn3 node
    def nft_reg_ticket_tests(self, key1, key2):
        print("== NFT registration Tickets test ==")
        # c. NFT registration ticket

        self.generate_and_sync_inc(10, self.mining_node_num)

        # generate pastelIDs
        non_registered_personal_pastelid1 = self.create_pastelid(self.non_mn3)[0]
        non_registered_mn_pastelid1 = self.create_pastelid(2)[0]
        for n in range(0, 12):
            self.mn_addresses[n] = self.nodes[n].getnewaddress()
            self.nodes[self.mining_node_num].sendtoaddress(self.mn_addresses[n], 10000, "", "", False)

        self.__wait_for_sync_all(10)

        self.total_copies = 10
        self.create_nft_ticket_and_signatures(self.non_mn3, self.total_copies, self.royalty, self.is_green, True)
        ticket_type = "nft"
        self.test_signatures(ticket_type, key1, key2, non_registered_personal_pastelid1, non_registered_mn_pastelid1)

        assert_shows_help(self.nodes[0].tickets, "register", ticket_type)

        # check royalty max value 20
        self.create_nft_ticket_and_signatures(self.non_mn3, self.total_copies, 0.25, self.is_green, True)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Royalty can't be 25 per cent, Min is 0 and Max is 20 per cent",
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))

        # check royalty negative value -5
        self.create_nft_ticket_and_signatures(self.non_mn3, self.total_copies, -5, self.is_green, True)
        # uint16_t -> 2 ^ 16 -> 65536 - 5 = 65531
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Royalty can't be -500 per cent, Min is 0 and Max is 20 per cent",
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))

        # c.a.6 register without errors, if enough coins for tnx fee
        self.create_nft_ticket_and_signatures(self.non_mn3, self.total_copies, self.royalty, self.is_green)

        coins_before = self.nodes[self.top_mns_index0].getbalance()
        # print(coins_before)

        self.nft_ticket1_txid = self.nodes[self.top_mns_index0].tickets("register", ticket_type,
                                                                        self.ticket, json.dumps(self.signatures_dict),
                                                                        self.top_mn_pastelid0, self.passphrase,
                                                                        key1, key2, str(self.storage_fee))["txid"]
        assert_true(self.nft_ticket1_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()
        print(self.nodes[self.top_mns_index0].getblockcount())

        #       c.a.7 check correct amount of change and correct amount spent
        coins_after = self.nodes[self.top_mns_index0].getbalance()
        # print(coins_after)
        print(f"NFT registration ticket price - {self.nftreg_ticket_price}")
        assert_equal(coins_after, coins_before-self.nftreg_ticket_price)  # no fee yet, but ticket cost NFT ticket price

        #       c.a.8 fail if already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "This NFT is already registered in blockchain",
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, "newkey", str(self.storage_fee))

        assert_raises_rpc(rpc.RPC_MISC_ERROR, "This NFT is already registered in blockchain",
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, "newkey", key2, str(self.storage_fee))

        #   c.b find registration ticket
        #       c.b.1 by creators PastelID (this is MultiValue key)
        # TODO Pastel:

        #       c.b.2 by hash (key1 for now)
        tkt1 = self.nodes[self.non_mn3].tickets("find", ticket_type, key1)["ticket"]
        assert_equal(tkt1['type'], "nft-reg")
        assert_equal(tkt1['nft_ticket'], self.ticket)
        assert_equal(tkt1["key1"], key1)
        assert_equal(tkt1["key2"], key2)
        assert_equal(tkt1["creator_height"], self.creator_ticket_height)
        assert_equal(tkt1["total_copies"], self.total_copies)
        assert_equal(tkt1["storage_fee"], self.storage_fee)
        r = float(round(tkt1["royalty"], 3))
        print(type(r))
        print(type(self.royalty))
        assert_equal(r, self.royalty)
        if self.royalty > 0:
            assert_equal(tkt1["royalty_address"], self.nonmn3_address1)
        else:
            assert(len(tkt1["royalty_address"]) == 0)
        assert_equal(tkt1["green"], self.is_green)
        assert_equal(tkt1["signatures"]["principal"][self.creator_pastelid1],
                     self.ticket_principal_signature)
        assert_equal(tkt1["signatures"]["mn2"][self.top_mn_pastelid1], self.top_mn_ticket_signature1)
        assert_equal(tkt1["signatures"]["mn3"][self.top_mn_pastelid2], self.top_mn_ticket_signature2)
        result = self.nodes[self.non_mn3].pastelid("verify", self.ticket, tkt1["signatures"]["mn1"][self.top_mn_pastelid0], self.top_mn_pastelid0)["verification"]
        assert_equal(result, "OK")

        #       c.b.3 by fingerprints, compare to ticket from c.b.2 (key2 for now)
        tkt2 = self.nodes[self.non_mn3].tickets("find", ticket_type, key2)["ticket"]
        assert_equal(tkt2['type'], "nft-reg")
        assert_equal(tkt2['nft_ticket'], self.ticket)
        assert_equal(tkt2["key1"], key1)
        assert_equal(tkt2["key2"], key2)
        assert_equal(tkt2["creator_height"], self.creator_ticket_height)
        assert_equal(tkt2["total_copies"], self.total_copies)
        assert_equal(tkt2["storage_fee"], self.storage_fee)
        r = float(round(tkt2["royalty"], 3))
        print(type(r))
        print(type(self.royalty))
        assert_equal(r, self.royalty)
        if self.royalty > 0:
            assert_equal(tkt2["royalty_address"], self.nonmn3_address1)
        else:
            assert(len(tkt2["royalty_address"]) == 0)
        assert_equal(tkt2["green"], self.is_green)
        assert_equal(tkt2["signatures"]["principal"][self.creator_pastelid1],
                     tkt1["signatures"]["principal"][self.creator_pastelid1])

        #   c.c get the same ticket by txid from c.a.6 and compare with ticket from c.b.2
        tkt3 = self.nodes[self.non_mn3].tickets("get", self.nft_ticket1_txid)["ticket"]
        assert_equal(tkt3["signatures"]["principal"][self.creator_pastelid1],
                     tkt1["signatures"]["principal"][self.creator_pastelid1])

        #   c.d list all NFT registration tickets, check PastelIDs
        nft_tickets_list = self.nodes[self.top_mns_index0].tickets("list", ticket_type)
        f1 = False
        f2 = False
        for t in nft_tickets_list:
            if key1 == t["ticket"]["key1"]:
                f1 = True
            if key2 == t["ticket"]["key2"]:
                f2 = True
        assert_true(f1)
        assert_true(f2)

        nft_tickets_by_pid = self.nodes[self.top_mns_index0].tickets("find", ticket_type, self.creator_pastelid1)
        print(self.top_mn_pastelid0)
        print(nft_tickets_by_pid)

        self.royalty_address = self.nonmn3_address1

        print("NFT registration tickets tested")

    # ===============================================================================================================
    def test_signatures(self, ticket_type, key1, key2, non_registered_personal_pastelid1, non_registered_mn_pastelid1):
        """Test signatures for the given 'ticket_type'

        Args:
            ticket_type (str): ticket type (nft, action)
            key1 (str): [description]
            key2 (str): [description]
            non_registered_personal_pastelid1 (str): [description]
            non_registered_mn_pastelid1 (str): [description]
        """
        #   c.a register nft registration ticket
        #       c.a.1 fail if not MN
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "This is not an active masternode", 
            self.nodes[self.non_mn1].tickets, "register", ticket_type, 
            self.ticket, json.dumps(self.signatures_dict), self.nonmn1_pastelid2, self.passphrase, key1, key2, str(self.storage_fee))

        #       c.a.2 fail if not active MN
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "This is not an active masternode",
            self.nodes[self.non_active_mn].tickets, "register", ticket_type, 
            self.ticket, json.dumps(self.signatures_dict), self.non_active_mn_pastelid1, self.passphrase, key1, key2, str(self.storage_fee))

        #       c.a.3 fail if active MN, but wrong PastelID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.nonmn1_pastelid2, self.passphrase,
            key1, key2, str(self.storage_fee))
        # TODO: provide better error for unknown PastelID

        #       c.a.4 fail if active MN, but wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, "wrong", key1, key2, str(self.storage_fee))
        # TODO: provide better error for wrong passphrase

        #       c.a.5 fail if creator's signature is not matching
        self.signatures_dict["principal"][self.creator_pastelid1] = self.top_mn_ticket_signature1
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Principal signature is invalid", 
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))
        self.signatures_dict["principal"][self.creator_pastelid1] = self.ticket_principal_signature

        #       c.a.6 fail if MN2 and MN3 signatures are not matching
        self.signatures_dict["mn2"][self.top_mn_pastelid1] = self.top_mn_ticket_signature2
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "MN2 signature is invalid",
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))
        self.signatures_dict["mn2"][self.top_mn_pastelid1] = self.top_mn_ticket_signature1

        self.signatures_dict["mn3"][self.top_mn_pastelid2] = self.top_mn_ticket_signature1
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "MN3 signature is invalid", 
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))
        self.signatures_dict["mn3"][self.top_mn_pastelid2] = self.top_mn_ticket_signature2

        #       c.a.7 fail if creator's PastelID is not registered
        self.signatures_dict["principal"][non_registered_personal_pastelid1] = self.ticket_principal_signature
        del self.signatures_dict["principal"][self.creator_pastelid1]
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Creator PastelID is not registered",
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))
        self.signatures_dict["principal"][self.creator_pastelid1] = self.ticket_principal_signature
        del self.signatures_dict["principal"][non_registered_personal_pastelid1]

        #       c.a.8 fail if creator's PastelID is not personal

        self.signatures_dict["principal"][self.top_mn_pastelid1] = self.top_mn_ticket_signature1
        del self.signatures_dict["principal"][self.creator_pastelid1]
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Creator PastelID is NOT personal PastelID",
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))
        self.signatures_dict["principal"][self.creator_pastelid1] = self.ticket_principal_signature
        del self.signatures_dict["principal"][self.top_mn_pastelid1]

        #       c.a.9 fail if MN PastelID is not registered
        self.signatures_dict["mn2"][non_registered_mn_pastelid1] = self.top_mn_ticket_signature1
        del self.signatures_dict["mn2"][self.top_mn_pastelid1]
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "MN2 PastelID is not registered",
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))
        self.signatures_dict["mn2"][self.top_mn_pastelid1] = self.top_mn_ticket_signature1
        del self.signatures_dict["mn2"][non_registered_mn_pastelid1]

        #       c.a.10 fail if MN PastelID is personal
        self.signatures_dict["mn2"][self.creator_pastelid1] = self.top_mn_ticket_signature1
        del self.signatures_dict["mn2"][self.top_mn_pastelid1]
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "MN2 PastelID is NOT masternode PastelID",
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))
        self.signatures_dict["mn2"][self.top_mn_pastelid1] = self.top_mn_ticket_signature1
        del self.signatures_dict["mn2"][self.creator_pastelid1]

        #       c.a.8 fail if MN1, MN2 and MN3 are not from top 10 list at the ticket's blocknum
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "was NOT in the top masternodes list for block", 
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.not_top_mns_signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))

        #       c.a.9 fail if MN1, MN2 and MN3 are the same
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "MNs PastelIDs can not be the same",
            self.nodes[self.top_mns_index0].tickets, "register", ticket_type,
            self.ticket, json.dumps(self.same_mns_signatures_dict), self.top_mn_pastelid0, self.passphrase, key1, key2, str(self.storage_fee))

    # ===============================================================================================================
    def nftroyalty_null_ticket_tests(self):
        print("== NFT royalty null tickets test ==")

        assert_equal(self.royalty, 0)

        # not enough confirmations
        print(self.nodes[self.non_mn3].getblockcount())
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Royalty ticket can be created only after",
            self.nodes[self.non_mn3].tickets, "register", "royalty", self.nft_ticket1_txid, "new_pastelid1", self.creator_pastelid1, self.passphrase)
        self.__wait_for_gen10_blocks()
        print(self.nodes[self.non_mn3].getblockcount())

        assert_raises_rpc(rpc.RPC_MISC_ERROR, "The NFT Reg ticket with txid [" + self.nft_ticket1_txid + "] has no royalty",
            self.nodes[self.non_mn3].tickets, "register", "royalty", self.nft_ticket1_txid, "new_pastelid1", self.creator_pastelid1, self.passphrase)

        print("NFT royalty null tickets tested")

    # ===============================================================================================================
    def nftroyalty_ticket_tests(self, nonmn_id, old_pastelid1, new_pastelid1, new_address1, num):
        print(f"== NFT royalty tickets test {num} ==")

        # fail if wrong PastelID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[nonmn_id].tickets, "register", "royalty", self.nft_ticket1_txid, new_pastelid1, self.top_mn_pastelid1, self.passphrase)

        # fail if wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[nonmn_id].tickets, "register", "royalty", self.nft_ticket1_txid, new_pastelid1, old_pastelid1, "wrong")

        # fail if there is not NFTTicket with this txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"The NFT ticket with this txid [{self.mn0_ticket1_txid}] referred by this Royalty ticket is not in the blockchain",
            self.nodes[nonmn_id].tickets, "register", "royalty", self.mn0_ticket1_txid, new_pastelid1, old_pastelid1, self.passphrase)

        assert(num == 1 or num == 2)
        if num == 1:
            assert_equal(nonmn_id, self.non_mn3)
            assert_equal(old_pastelid1, self.creator_pastelid1)
            assert_equal(new_pastelid1, self.nonmn5_royalty_pastelid1)
            assert_equal(new_address1, self.nonmn5_royalty_address1)

            # not enough confirmations
            print(self.nodes[nonmn_id].getblockcount())
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "Royalty ticket can be created only after",
                self.nodes[nonmn_id].tickets, "register", "royalty", self.nft_ticket1_txid, new_pastelid1, self.creator_pastelid1, self.passphrase)
            self.__wait_for_gen10_blocks()
            print(self.nodes[nonmn_id].getblockcount())

            # fail if creator's PastelID is not matching creator's PastelID in the registration ticket
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the Creator's PastelID",
                self.nodes[nonmn_id].tickets, "register", "royalty", self.nft_ticket1_txid, new_pastelid1, self.nonmn3_pastelid1, self.passphrase)
        else:
            assert_equal(nonmn_id, self.non_mn5)
            assert_equal(old_pastelid1, self.nonmn5_royalty_pastelid1)
            assert_equal(new_pastelid1, self.nonmn6_royalty_pastelid1)
            assert_equal(new_address1, self.nonmn6_royalty_address1)

            # fail if is not matching current PastelID of the royalty payee
            non_registered_pastelid1 = self.create_pastelid(self.non_mn5)[0]
            assert_raises_rpc(rpc.RPC_MISC_ERROR, f"The PastelID [{non_registered_pastelid1}] is not matching the PastelID [{self.nonmn5_royalty_pastelid1}] in the Change Royalty ticket with NFT txid [{self.nft_ticket1_txid}]",
                self.nodes[nonmn_id].tickets, "register", "royalty", self.nft_ticket1_txid, new_pastelid1, non_registered_pastelid1, self.passphrase)

        assert_raises_rpc(rpc.RPC_MISC_ERROR, "The Change Royalty ticket new_pastelID is empty",
            self.nodes[nonmn_id].tickets, "register", "royalty", self.nft_ticket1_txid, "", old_pastelid1, self.passphrase)

        assert_raises_rpc(rpc.RPC_MISC_ERROR, "The Change Royalty ticket new_pastelID is equal to current pastelID",
            self.nodes[nonmn_id].tickets, "register", "royalty", self.nft_ticket1_txid, old_pastelid1, old_pastelid1, self.passphrase)

        coins_before = self.nodes[nonmn_id].getbalance()

        # successful royalty ticket registration
        nft_royalty_txid = self.nodes[nonmn_id].tickets("register", "royalty",
                                                        self.nft_ticket1_txid, new_pastelid1,
                                                        old_pastelid1, self.passphrase)["txid"]
        assert_true(nft_royalty_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        # fail if already registered
        assert_raises_rpc(rpc.RPC_MISC_ERROR, f"The PastelID [{old_pastelid1}] is not matching the PastelID [{new_pastelid1}] in the Change Royalty ticket with NFT txid [{self.nft_ticket1_txid}]",
            self.nodes[nonmn_id].tickets, "register", "royalty", self.nft_ticket1_txid, new_pastelid1, old_pastelid1, self.passphrase)

        coins_after = self.nodes[nonmn_id].getbalance()
        print(f"coins before - {coins_before}")
        print(f"coins after - {coins_after}")
        assert_equal(coins_before - coins_after, 10)

        # from another node - get ticket transaction and check
        #   - amounts is totaling 10PSL
        nft_ticket1_royalty_ticket_hash = self.nodes[0].getrawtransaction(nft_royalty_txid)
        nft_ticket1_royalty_ticket_tx = self.nodes[0].decoderawtransaction(nft_ticket1_royalty_ticket_hash)
        fee_amount = 0

        for v in nft_ticket1_royalty_ticket_tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                fee_amount += v["value"]
        assert_equal(fee_amount, 10)

        # find ticket by pastelID
        nft_ticket1_royalty_ticket_1 = self.nodes[self.non_mn1].tickets("find", "royalty", old_pastelid1)
        tkt1 = nft_ticket1_royalty_ticket_1[0]["ticket"]
        assert_equal(tkt1['type'], "nft-royalty")
        assert_equal(tkt1['pastelID'], old_pastelid1)
        assert_equal(tkt1['new_pastelID'], new_pastelid1)
        assert_equal(tkt1['nft_txid'], self.nft_ticket1_txid)
        assert_equal(nft_ticket1_royalty_ticket_1[0]['txid'], nft_royalty_txid)

        # get the same ticket by txid and compare with ticket found by pastelID
        nft_ticket1_royalty_ticket_2 = self.nodes[self.non_mn1].tickets("get", nft_royalty_txid)
        tkt2 = nft_ticket1_royalty_ticket_2["ticket"]
        assert_equal(tkt2["signature"], tkt1["signature"])

        # list all NFT royalty tickets, check PastelIDs
        royalty_tickets_list = self.nodes[0].tickets("list", "royalty")
        f1 = False
        f2 = False
        for t in royalty_tickets_list:
            if nft_royalty_txid == t["txid"]:
                f1 = True
                f2 = (self.nft_ticket1_txid == t["ticket"]["nft_txid"])
        assert_true(f1)
        assert_true(f2)

        self.royalty_address = new_address1

        print(f"NFT royalty tickets {num} tested")

    # ===============================================================================================================
    def nft_activate_ticket_tests(self, skip_low_coins_tests):
        """NFT activation ticket tests

        Args:
            skip_low_coins_tests (bool): True to skip low coins tests
        """
        print("== NFT activation Tickets test ==")

        cmd = "register"
        cmd_param = "act"
        ticket_type = "act"

        assert_shows_help(self.nodes[self.non_mn3].tickets, cmd, cmd_param)

        # d. NFT activation ticket
        #   d.a register NFT activation ticket (self.nft_ticket1_txid; self.storage_fee; self.creator_ticket_height)
        #       d.a.1 fail if wrong PastelID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.nft_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.top_mn_pastelid1, self.passphrase)

        #       d.a.2 fail if wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.nft_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.creator_pastelid1, "wrong")

        #       d.a.7 fail if not enough coins to pay 90% of registration price (from ActionReg ticket) (90) + tnx fee (act ticket price)
        # print(self.nodes[self.non_mn3].getbalance())
        if not skip_low_coins_tests:
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "Not enough coins to cover price [100]",
                self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.nft_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.creator_pastelid1, self.passphrase)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, str(self.collateral), "", "", False)
        self.__wait_for_sync_all10()

        #       d.a.3 fail if there is no Action Ticket with this txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "The NFT ticket with this txid ["+self.mn0_ticket1_txid + "] referred by this Activation ticket is not in the blockchain",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.mn0_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.creator_pastelid1, self.passphrase)

        if not self.royalty_tickets_tests and not self.royalty_null_tests:
            # not enough confirmations
            print(self.nodes[self.non_mn3].getblockcount())
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "Activation ticket can be created only after",
                self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.nft_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.creator_pastelid1, self.passphrase)
            self.__wait_for_gen10_blocks()
            print(self.nodes[self.non_mn3].getblockcount())

        #       d.a.4 fail if creator's PastelID in the activation ticket
        #       is not matching creator's PastelID in the registration ticket
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the Creator's PastelID",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.nft_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.nonmn3_pastelid1, self.passphrase)

        #       d.a.5 fail if wrong creator ticket height
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the CreatorHeight",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.nft_ticket1_txid, "55", str(self.storage_fee), self.creator_pastelid1, self.passphrase)

        #       d.a.6 fail if wrong storage fee
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the storage fee",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.nft_ticket1_txid, str(self.creator_ticket_height), "55", self.creator_pastelid1, self.passphrase)

        #       d.a.7 register without errors
        #
        mn0_collateral_address = self.nodes[self.top_mns_index0].masternode("status")["payee"]
        mn1_collateral_address = self.nodes[self.top_mns_index1].masternode("status")["payee"]
        mn2_collateral_address = self.nodes[self.top_mns_index2].masternode("status")["payee"]

        # MN's collateral addresses belong to hot_node - non_mn2
        mn0_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn0_collateral_address)
        mn1_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn1_collateral_address)
        mn2_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn2_collateral_address)

        coins_before = self.nodes[self.non_mn3].getbalance()
        # print(coins_before)

        self.nftact_ticket1_txid = self.nodes[self.non_mn3].tickets(cmd, cmd_param,
            self.nft_ticket1_txid, (self.creator_ticket_height), str(self.storage_fee), self.creator_pastelid1, self.passphrase)["txid"]
        assert_true(self.nftact_ticket1_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        #       d.a.9 check correct amount of change and correct amount spent and correct amount of fee paid
        main_mn_fee = self.storage_fee90percent*3/5
        other_mn_fee = self.storage_fee90percent/5

        coins_after = self.nodes[self.non_mn3].getbalance()
        print(f"NFT activation ticket price - {self.nftact_ticket_price}")
        assert_equal(coins_after, coins_before-Decimal(self.storage_fee90percent)-Decimal(self.nftact_ticket_price))  # no fee yet, but ticket cost act ticket price

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
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "The Activation ticket for the Registration ticket with txid [" + self.nft_ticket1_txid + "] already exists",
            self.nodes[self.non_mn3].tickets, cmd, cmd_param, self.nft_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.creator_pastelid1, self.passphrase)

        #       d.a.11 from another node - get ticket transaction and check
        #           - there are 3 outputs to MN1, MN2 and MN3 with correct amounts
        #               (MN1: 60%; MN2, MN3: 20% each, of registration price)
        #           - amounts is totaling 10PSL
        nftact_ticket1_hash = self.nodes[0].getrawtransaction(self.nftact_ticket1_txid)
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
        assert_equal(fee_amount, self.nftact_ticket_price)

        #   d.b find activation ticket
        #       d.b.1 by creators PastelID (this is MultiValue key)
        #       TODO Pastel: find activation ticket by creators PastelID (this is MultiValue key)

        #       d.b.3 by Registration height - creator_height from registration ticket (this is MultiValue key)
        #       TODO Pastel: find activation ticket by Registration height -
        #        creator_height from registration ticket (this is MultiValue key)

        #       d.b.2 by Registration txid - reg_txid from registration ticket, compare to ticket from d.b.2
        nftact_ticket1 = self.nodes[self.non_mn1].tickets("find", ticket_type, self.nft_ticket1_txid)
        tkt1 = nftact_ticket1["ticket"]
        assert_equal(tkt1['type'], "nft-act")
        assert_equal(tkt1['pastelID'], self.creator_pastelid1)
        assert_equal(tkt1['reg_txid'], self.nft_ticket1_txid)
        assert_equal(tkt1['creator_height'], self.creator_ticket_height)
        assert_equal(tkt1['storage_fee'], self.storage_fee)
        assert_equal(nftact_ticket1['txid'], self.nftact_ticket1_txid)

        #   d.c get the same ticket by txid from d.a.8 and compare with ticket from d.b.2
        nftact_ticket2 = self.nodes[self.non_mn1].tickets("get", self.nftact_ticket1_txid)
        tkt2 = nftact_ticket2["ticket"]
        assert_equal(tkt2["signature"], tkt1["signature"])

        #   d.d list all NFT activation tickets, check PastelIDs
        nftact_tickets_list = self.nodes[0].tickets("list", ticket_type)
        f1 = False
        for t in nftact_tickets_list:
            if self.nft_ticket1_txid == t["ticket"]["reg_txid"]:
                f1 = True
        assert_true(f1)

        nft_tickets_by_pid = self.nodes[self.top_mns_index0].tickets("find", ticket_type, self.creator_pastelid1)
        print(self.top_mn_pastelid0)
        print(nft_tickets_by_pid)
        nft_tickets_by_height = self.nodes[self.top_mns_index0].tickets("find", ticket_type, str(self.creator_ticket_height))
        print(self.creator_ticket_height)
        print(nft_tickets_by_height)

        print("NFT activation tickets tested")

    # ===============================================================================================================
    def action_activate_ticket_tests(self, skip_low_coins_tests):
        """Action activation ticket tests

        Args:
            skip_low_coins_tests (bool): True to skip low coins tests
        """
        print("== Action activation Tickets test ==")

        cmd = "activate"
        cmd_param = "action"
        ticket_type = "action-act"

        assert_shows_help(self.nodes[self.non_mn4].tickets, cmd, cmd_param)
        
        # d. Action activation ticket
        #   d.a register Action activation ticket (self.actionreg_ticket1_txid; self.storage_fee; self.creator_ticket_height)
        #       d.a.1 fail if wrong PastelID
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.non_mn4].tickets, cmd, cmd_param, self.actionreg_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.top_mn_pastelid1, self.passphrase)

        #       d.a.2 fail if wrong passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[self.non_mn4].tickets, cmd, cmd_param, self.actionreg_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.creator_pastelid1, "wrong")

        #       d.a.7 fail if not enough coins to pay 90% of registration price (from Action Reg ticket) (90) + tnx fee (act ticket price)
        # print(self.nodes[self.non_mn3].getbalance())
        if not skip_low_coins_tests:
            assert_raises_rpc(rpc.RPC_MISC_ERROR, "Not enough coins to cover price [100]",
                self.nodes[self.non_mn4].tickets, cmd, cmd_param, self.actionreg_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.creator_pastelid1, self.passphrase)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, str(self.collateral), "", "", False)
        self.__wait_for_sync_all10()

        #       d.a.3 fail if there is no ActionReg Ticket with this txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "The Action-Reg ticket with this txid ["+self.mn0_ticket1_txid + "] referred by this Activation ticket is not in the blockchain",
            self.nodes[self.non_mn4].tickets, cmd, cmd_param, self.mn0_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.creator_pastelid1, self.passphrase)

        # not enough confirmations
        print(self.nodes[self.non_mn4].getblockcount())
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Activation ticket can be created only after",
            self.nodes[self.non_mn4].tickets, cmd, cmd_param, self.actionreg_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.creator_pastelid1, self.passphrase)
        self.__wait_for_gen10_blocks()
        print(self.nodes[self.non_mn4].getblockcount())

        #       d.a.4 fail if Caller's PastelID in the activation ticket
        #       is not matching Caller's PastelID in the registration ticket
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the Action Caller's PastelID",
            self.nodes[self.non_mn4].tickets, cmd, cmd_param, self.actionreg_ticket1_txid, str(self.creator_ticket_height), str(self.storage_fee), self.nonmn3_pastelid1, self.passphrase)

        #       d.a.5 fail if wrong creator ticket height
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the CalledAtHeight",
            self.nodes[self.non_mn4].tickets, cmd, cmd_param, self.actionreg_ticket1_txid, "55", str(self.storage_fee), self.creator_pastelid1, self.passphrase)

        #       d.a.6 fail if wrong storage fee
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not matching the storage fee",
            self.nodes[self.non_mn4].tickets, cmd, cmd_param, self.actionreg_ticket1_txid, str(self.creator_ticket_height), "55", self.creator_pastelid1, self.passphrase)

        #       d.a.7 register without errors
        #
        mn0_collateral_address = self.nodes[self.top_mns_index0].masternode("status")["payee"]
        mn1_collateral_address = self.nodes[self.top_mns_index1].masternode("status")["payee"]
        mn2_collateral_address = self.nodes[self.top_mns_index2].masternode("status")["payee"]

        # MN's collateral addresses belong to hot_node - non_mn2
        mn0_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn0_collateral_address)
        mn1_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn1_collateral_address)
        mn2_coins_before = self.nodes[self.hot_node_num].getreceivedbyaddress(mn2_collateral_address)

        coins_before = self.nodes[self.non_mn4].getbalance()
        # print(coins_before)

        self.actionact_ticket1_txid = self.nodes[self.non_mn4].tickets(cmd, cmd_param,
            self.actionreg_ticket1_txid, (self.creator_ticket_height), str(self.storage_fee), self.action_caller_pastelid, self.passphrase)["txid"]
        assert_true(self.actionact_ticket1_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        #       d.a.9 check correct amount of change and correct amount spent and correct amount of fee paid
        main_mn_fee = self.storage_fee90percent*3/5
        other_mn_fee = self.storage_fee90percent/5

        coins_after = self.nodes[self.non_mn4].getbalance()
        print(f"Action activation ticket price - {self.actionact_ticket_price}")
        assert_equal(coins_after, coins_before-Decimal(self.storage_fee90percent)-Decimal(self.actionact_ticket_price))  # no fee yet, but ticket cost act ticket price

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
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "The Activation ticket for the Registration ticket with txid [" + self.actionreg_ticket1_txid + "] already exists",
            self.nodes[self.non_mn4].tickets, cmd, cmd_param, self.actionreg_ticket1_txid, str(self.creator_ticket_height), 
                str(self.storage_fee), self.action_caller_pastelid, self.passphrase)

        #       d.a.11 from another node - get ticket transaction and check
        #           - there are 3 outputs to MN1, MN2 and MN3 with correct amounts
        #               (MN1: 60%; MN2, MN3: 20% each, of registration price)
        #           - amounts are totaling 10PSL
        actionact_ticket1_hash = self.nodes[0].getrawtransaction(self.actionact_ticket1_txid)
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
        assert_equal(amount, self.storage_fee90percent)
        assert_equal(fee_amount, self.actionact_ticket_price)

        #   d.b find activation ticket
        #       d.b.1 by creators PastelID (this is MultiValue key)
        #       TODO Pastel: find activation ticket by creators PastelID (this is MultiValue key)

        #       d.b.3 by Registration height - creator_height from registration ticket (this is MultiValue key)
        #       TODO Pastel: find activation ticket by Registration height -
        #        creator_height from registration ticket (this is MultiValue key)

        #       d.b.2 by Registration txid - reg_txid from registration ticket, compare to ticket from d.b.2
        actionact_ticket1 = self.nodes[self.non_mn1].tickets("find", ticket_type, self.actionreg_ticket1_txid)
        tkt1 = actionact_ticket1["ticket"]
        assert_equal(tkt1['type'], "action-act")
        assert_equal(tkt1['pastelID'], self.action_caller_pastelid)
        assert_equal(tkt1['reg_txid'], self.actionreg_ticket1_txid)
        assert_equal(tkt1['called_at'], self.creator_ticket_height)
        assert_equal(tkt1['storage_fee'], self.storage_fee)
        assert_equal(actionact_ticket1['txid'], self.actionact_ticket1_txid)

        #   d.c get the same ticket by txid from d.a.8 and compare with ticket from d.b.2
        actionact_ticket2 = self.nodes[self.non_mn1].tickets("get", self.actionact_ticket1_txid)
        tkt2 = actionact_ticket2["ticket"]
        assert_equal(tkt2["signature"], tkt1["signature"])

        #   d.d list all Action activation tickets, check PastelIDs
        actionact_tickets_list = self.nodes[0].tickets("list", ticket_type)
        f1 = False
        for t in actionact_tickets_list:
            if self.actionreg_ticket1_txid == t["ticket"]["reg_txid"]:
                f1 = True
        assert_true(f1)

        action_tickets_by_pastelid = self.nodes[self.top_mns_index0].tickets("find", ticket_type, self.action_caller_pastelid)
        print(self.top_mn_pastelid0)
        print(action_tickets_by_pastelid)
        action_tickets_by_height = self.nodes[self.top_mns_index0].tickets("find", ticket_type, str(self.creator_ticket_height))
        print(self.creator_ticket_height)
        print(action_tickets_by_height)

        print("Action activation tickets tested")

    # ===============================================================================================================
    def nft_sell_ticket_tests1(self, skip_some_tests):
        print("== NFT sell Tickets test (selling original NFT ticket) ==")
        # tickets register sell nft_txid price PastelID passphrase valid_after valid_before
        #

        # 1. fail if not enough coins to pay tnx fee (2% from price - 2M from 100M)
        if not skip_some_tests:
            try:
                self.nodes[self.non_mn3].tickets("register", "sell",
                                                 self.nftact_ticket1_txid, str("100000000"),
                                                 self.creator_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Not enough coins to cover price [2000000]" in self.errorString, True)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 5000, "", "", False)
        self.__wait_for_sync_all(1)
        coins_before = self.nodes[self.non_mn3].getbalance()
        print(coins_before)

        # 2. Check there is Activation ticket with this NFTTxnId
        try:
            self.nodes[self.non_mn3].tickets("register", "sell",
                                             self.nft_ticket1_txid, str("100000"),
                                             self.creator_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The activation or trade ticket with this txid ["+self.nft_ticket1_txid +
                     "] referred by this Sell ticket is not in the blockchain" in self.errorString, True)

        #  not enough confirmations
        print(self.nodes[self.non_mn3].getblockcount())
        try:
            self.nodes[self.non_mn3].tickets("register", "sell",
                                             self.nftact_ticket1_txid, str("100000"),
                                             self.creator_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Sell ticket can be created only after" in self.errorString, True)
        self.__wait_for_gen10_blocks()
        print(self.nodes[self.non_mn3].getblockcount())

        # 2. check PastelID in this ticket matches PastelID in the referred Activation ticket
        try:
            self.nodes[self.non_mn3].tickets("register", "sell",
                                             self.nftact_ticket1_txid, str("100000"),
                                             self.nonmn3_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The PastelID ["+self.nonmn3_pastelid1 +
                     "] in this ticket is not matching the Creator's PastelID [" +
                     self.creator_pastelid1+"] in the NFT Activation ticket with this txid [" +
                     self.nftact_ticket1_txid+"]" in self.errorString, True)

        # 3. Fail if asked price is 0
        try:
            self.nodes[self.non_mn3].tickets("register", "sell",
                                             self.nftact_ticket1_txid, str(0),
                                             self.creator_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The asked price for Sell ticket with NFT txid [" + self.nftact_ticket1_txid + "] "
                     "should be not 0" in self.errorString, True)

        # 4. Create Sell ticket
        self.nft_ticket1_sell_ticket_txid = \
            self.nodes[self.non_mn3].tickets("register", "sell",
                                             self.nftact_ticket1_txid,
                                             str("100000"),
                                             self.creator_pastelid1, self.passphrase)["txid"]
        assert_true(self.nft_ticket1_sell_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        # 5. check correct amount of change and correct amount spent
        coins_after = self.nodes[self.non_mn3].getbalance()
        print(coins_after)
        assert_equal(coins_after, coins_before-2000)  # ticket cost price/50 PSL (100000/50=2000)

        # 6. find Sell ticket
        #   6.1 by NFT's transaction and index
        sell_ticket1_1 = self.nodes[self.non_mn3].tickets("find", "sell", self.nftact_ticket1_txid+":1")
        assert_equal(sell_ticket1_1['ticket']['type'], "nft-sell")
        assert_equal(sell_ticket1_1['ticket']['nft_txid'], self.nftact_ticket1_txid)
        assert_equal(sell_ticket1_1["ticket"]["asked_price"], 100000)

        #   6.2 by creators PastelID (this is MultiValue key)
        sell_tickets_list1 = self.nodes[self.non_mn3].tickets("find", "sell", self.creator_pastelid1)
        found_ticket = False
        for ticket in sell_tickets_list1:
            if ticket['ticket']['nft_txid'] == self.nftact_ticket1_txid \
                    and ticket["ticket"]["asked_price"] == 100000:
                found_ticket = True
            assert_equal(ticket['ticket']['type'], "nft-sell")
        assert_true(found_ticket)

        #   6.3 by NFT's transaction (this is MultiValue key)
        sell_tickets_list2 = self.nodes[self.non_mn3].tickets("find", "sell", self.nftact_ticket1_txid)
        found_ticket = False
        for ticket in sell_tickets_list2:
            if ticket['ticket']['nft_txid'] == self.nftact_ticket1_txid \
                    and ticket["ticket"]["asked_price"] == 100000:
                found_ticket = True
            assert_equal(ticket['ticket']['type'], "nft-sell")
        assert_true(found_ticket)

        #   6.4 get the same ticket by txid from c.a.6 and compare with ticket from c.b.2
        sell_ticket1_2 = self.nodes[self.non_mn3].tickets("get", self.nft_ticket1_sell_ticket_txid)
        assert_equal(sell_ticket1_2["ticket"]["nft_txid"], sell_ticket1_1["ticket"]["nft_txid"])
        assert_equal(sell_ticket1_2["ticket"]["asked_price"], sell_ticket1_1["ticket"]["asked_price"])

        # 7. list all sell tickets
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell")
        f1 = False
        f2 = False
        for t in tickets_list:
            if self.nftact_ticket1_txid == t["ticket"]["nft_txid"]:
                f1 = True
            if "1" == str(t["ticket"]["copy_number"]):
                f2 = True
        assert_true(f1)
        assert_true(f2)

        # 8. from another node - get ticket transaction and check
        #           - there are P2MS outputs with non-zero amounts
        #           - amounts is totaling price/50 PSL (100000/50=200)
        sell_ticket1_tx_hash = self.nodes[self.non_mn1].getrawtransaction(self.nft_ticket1_sell_ticket_txid)
        sell_ticket1_tx = self.nodes[self.non_mn1].decoderawtransaction(sell_ticket1_tx_hash)
        amount = 0
        for v in sell_ticket1_tx["vout"]:
            assert_greater_than(v["value"], 0)
            if v["scriptPubKey"]["type"] == "multisig":
                amount += v["value"]
        assert_equal(amount, 2000)

        print("NFT sell tickets tested (first run)")

    # ===============================================================================================================
    def nft_buy_ticket_tests(self, skip_low_coins_tests):
        print("== NFT buy Tickets test (buying original NFT ticket) ==")

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 2000, "", "", False)
        self.__wait_for_sync_all10()

        self.nonmn4_pastelid1 = self.create_pastelid(self.non_mn4)[0]
        self.nodes[self.non_mn4].tickets("register", "id", self.nonmn4_pastelid1, self.passphrase, self.nonmn4_address1)

        # fail if not enough funds
        # price (100K) and tnx fee(1% from price - 1K from 100K) = 101000
        coins_before = self.nodes[self.non_mn4].getbalance()
        if not skip_low_coins_tests:
            print(coins_before)
            try:
                self.nodes[self.non_mn4].tickets("register", "buy",
                                                 self.nft_ticket1_sell_ticket_txid, str("100000"),
                                                 self.nonmn4_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Not enough coins to cover price [101000]" in self.errorString, True)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 100010, "", "", False)
        self.__wait_for_sync_all(1)
        coins_before = self.nodes[self.non_mn4].getbalance()
        print(coins_before)

        # Check there is Sell ticket with this sellTxnId
        try:
            self.nodes[self.non_mn4].tickets("register", "buy",
                                             self.nftact_ticket1_txid, str("100000"),
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The sell ticket with this txid ["+self.nftact_ticket1_txid +
                     "] referred by this Buy ticket is not in the blockchain" in self.errorString, True)

        # fail if not enough confirmations
        print(self.nodes[self.non_mn4].getblockcount())
        try:
            self.nodes[self.non_mn4].tickets("register", "buy",
                                             self.nft_ticket1_sell_ticket_txid, str("100000"),
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Buy ticket can be created only after" in self.errorString, True)
        self.__wait_for_gen10_blocks()
        print(self.nodes[self.non_mn4].getblockcount())

        # fail if price does not covers the sell price
        try:
            self.nodes[self.non_mn4].tickets("register", "buy",
                                             self.nft_ticket1_sell_ticket_txid, str("100"),
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The offered price [100] is less than asked in the sell ticket [100000]" in self.errorString, True)

        # Create buy ticket
        self.nft_ticket1_buy_ticket_txid = \
            self.nodes[self.non_mn4].tickets("register", "buy",
                                             self.nft_ticket1_sell_ticket_txid, str("100000"),
                                             self.nonmn4_pastelid1, self.passphrase)["txid"]
        assert_true(self.nft_ticket1_buy_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        # check correct amount of change and correct amount spent
        coins_after = self.nodes[self.non_mn4].getbalance()
        print(coins_after)
        assert_equal(coins_after, coins_before-1000)  # ticket cost price/100 PSL (100000/100=1000)

        # fail if there is another buy ticket referring to that sell ticket
        try:
            self.nodes[self.non_mn4].tickets("register", "buy",
                                             self.nft_ticket1_sell_ticket_txid, str("100000"),
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Buy ticket [" + self.nft_ticket1_buy_ticket_txid + "] already exists and is not yet 1h old "
                     "for this sell ticket [" + self.nft_ticket1_sell_ticket_txid + "]" in self.errorString, True)

        print("NFT buy tickets tested")

    # ===============================================================================================================
    def nft_trade_ticket_tests(self, skip_low_coins_tests):
        print("== NFT trade Tickets test (trading original NFT ticket) ==")

        # sends some coins back
        mining_node_address1 = self.nodes[self.mining_node_num].getnewaddress()
        self.nodes[self.non_mn4].sendtoaddress(mining_node_address1, 100000, "", "", False)
        self.__wait_for_sync_all10()

        # fail if not enough funds
        # price (100K) and tnx fee(1% from price - 1K from 100K) = 101000
        if not skip_low_coins_tests:
            coins_before = self.nodes[self.non_mn4].getbalance()
            print(coins_before)
            try:
                self.nodes[self.non_mn4].tickets("register", "trade",
                                                 self.nft_ticket1_sell_ticket_txid, self.nft_ticket1_buy_ticket_txid,
                                                 self.nonmn4_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Not enough coins to cover price [100010]" in self.errorString, True)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 101000, "", "", False)
        self.__wait_for_sync_all(1)
        coins_before = math.floor(self.nodes[self.non_mn4].getbalance())
        print(coins_before)

        # Check there is Sell ticket with this sellTxnId
        try:
            self.nodes[self.non_mn4].tickets("register", "trade",
                                             self.nft_ticket1_buy_ticket_txid, self.nft_ticket1_buy_ticket_txid,
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The ticket with this txid ["+self.nft_ticket1_buy_ticket_txid+"] is not in the blockchain"
                     in self.errorString, True)
        # This error is from CNFTTradeTicket::Create where it tries to get Sell ticket to get price and NFTTxId

        # Check there is Buy ticket with this buyTxnId
        try:
            self.nodes[self.non_mn4].tickets("register", "trade",
                                             self.nft_ticket1_sell_ticket_txid, self.nft_ticket1_sell_ticket_txid,
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The buy ticket with this txid ["+self.nft_ticket1_sell_ticket_txid +
                     "] referred by this Trade ticket is not in the blockchain" in self.errorString, True)
        # This error is from CNFTTradeTicket::IsValid -> common_validation

        # fail if not enough confirmations after buy ticket
        print(self.nodes[self.non_mn4].getblockcount())
        try:
            self.nodes[self.non_mn4].tickets("register", "trade",
                                             self.nft_ticket1_sell_ticket_txid, self.nft_ticket1_buy_ticket_txid,
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Trade ticket can be created only after" in self.errorString, True)
        self.__wait_for_gen10_blocks()
        print(self.nodes[self.non_mn4].getblockcount())

        sellers_pastel_id = self.nodes[self.non_mn3].tickets("get", self.nft_ticket1_sell_ticket_txid)["ticket"]["pastelID"]
        print(sellers_pastel_id)
        sellers_address = self.nodes[self.non_mn3].tickets("find", "id", sellers_pastel_id)["ticket"]["address"]
        print(sellers_address)
        sellers_coins_before = math.floor(self.nodes[self.non_mn3].getreceivedbyaddress(sellers_address))

        # consolidate funds into single address
        balance = self.nodes[self.non_mn4].getbalance()
        consaddress = self.nodes[self.non_mn4].getnewaddress()
        self.nodes[self.non_mn4].sendtoaddress(consaddress, balance, "", "", True)

        # Create trade ticket
        self.nft_ticket1_trade_ticket_txid = \
            self.nodes[self.non_mn4].tickets("register", "trade",
                                             self.nft_ticket1_sell_ticket_txid, self.nft_ticket1_buy_ticket_txid,
                                             self.nonmn4_pastelid1, self.passphrase)["txid"]
        assert_true(self.nft_ticket1_trade_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        # check correct amount of change and correct amount spent
        coins_after = math.floor(self.nodes[self.non_mn4].getbalance())
        print(coins_before)
        print(coins_after)
        print(f"trade ticket price - {self.trade_ticket_price}")
        assert_equal(coins_after, coins_before-self.trade_ticket_price-100000)  # ticket cost is trade ticket price, NFT cost is 100000

        # check seller gets correct amount
        sellers_coins_after = math.floor(self.nodes[self.non_mn3].getreceivedbyaddress(sellers_address))
        sellers_coins_expected_to_receive = 100000
        royalty_coins_expected_fee = 0
        green_coins_expected_fee = 0
        if self.royalty > 0:
            royalty_coins_expected_fee = 100000 * self.royalty
            sellers_coins_expected_to_receive -= royalty_coins_expected_fee
        if self.is_green:
            green_coins_expected_fee = 2000
            sellers_coins_expected_to_receive -= green_coins_expected_fee
        print(sellers_coins_before)
        print(sellers_coins_after)
        assert_equal(sellers_coins_after - sellers_coins_before, sellers_coins_expected_to_receive)

        # from another node - get ticket transaction and check
        #   - there are 3 posiible outputs to seller, royalty and green adresses
        nft_ticket1_trade_ticket_hash = self.nodes[0].getrawtransaction(self.nft_ticket1_trade_ticket_txid)
        nft_ticket1_trade_ticket_tx = self.nodes[0].decoderawtransaction(nft_ticket1_trade_ticket_hash)
        sellers_coins = 0
        royalty_coins = 0
        green_coins = 0
        multi_coins = 0

        for v in nft_ticket1_trade_ticket_tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                multi_coins += v["value"]
            if v["scriptPubKey"]["type"] == "pubkeyhash":
                amount = v["value"]
                print(f"trade transiction pubkeyhash vout - {amount}")
                if v["scriptPubKey"]["addresses"][0] == sellers_address and amount == sellers_coins_expected_to_receive:
                    sellers_coins = amount
                    print(f"trade transaction to seller's address - {amount}")
                if v["scriptPubKey"]["addresses"][0] == self.royalty_address and amount == royalty_coins_expected_fee:
                    royalty_coins = amount
                    print(f"trade transaction to royalty's address - {amount}")
                if v["scriptPubKey"]["addresses"][0] == self.green_address and self.is_green:
                    green_coins = amount
                    print(f"trade transaction to green's address - {amount}")
        print(f"trade transiction multisig coins - {multi_coins}")
        assert_equal(sellers_coins, sellers_coins_expected_to_receive)
        assert_equal(royalty_coins, royalty_coins_expected_fee)
        assert_equal(green_coins, green_coins_expected_fee)
        assert_equal(sellers_coins + royalty_coins + green_coins, 100000)
        assert_equal(multi_coins, self.id_ticket_price)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 100000, "", "", False)
        self.__wait_for_sync_all(1)
        # fail if there is another trade ticket referring to that sell ticket
        try:
            self.nodes[self.non_mn4].tickets("register", "trade",
                                             self.nft_ticket1_sell_ticket_txid, self.nft_ticket1_buy_ticket_txid,
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("There is already exist trade ticket for the sell ticket with this txid [" +
                     self.nft_ticket1_sell_ticket_txid+"]" in self.errorString, True)

        print("NFT trade tickets tested")

    # ===============================================================================================================
    def nft_sell_buy_trade_tests(self):
        print("== NFT sell Tickets test (selling, buying and trading Trade ticket) ==")

        self.slow_mine(12, 10, 2, 0.5)
        time.sleep(2)
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 100000, "", "", False)
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 100000, "", "", False)
        time.sleep(2)

        # now there is 1 Trade ticket and it is non-sold
        new_trade_ticket = self.sell_buy_trade_test("T1", self.non_mn4, self.nonmn4_pastelid1,
                                                    self.non_mn3, self.nonmn3_pastelid1,
                                                    self.nft_ticket1_trade_ticket_txid, self.is_green)
        # now there are 2 Trade ticket and 1 of it's sold
        new_trade_ticket = self.sell_buy_trade_test("T2", self.non_mn3, self.nonmn3_pastelid1,
                                                    self.non_mn4, self.nonmn4_pastelid1,
                                                    new_trade_ticket, self.is_green)
        # now there are 3 Trade ticket and 2 of it's sold
        new_trade_ticket = self.sell_buy_trade_test("T3", self.non_mn4, self.nonmn4_pastelid1,
                                                    self.non_mn3, self.nonmn3_pastelid1,
                                                    new_trade_ticket, self.is_green)
        # now there are 4 Trade ticket and 3 of it's sold
        new_trade_ticket = self.sell_buy_trade_test("T4", self.non_mn3, self.nonmn3_pastelid1,
                                                    self.non_mn4, self.nonmn4_pastelid1,
                                                    new_trade_ticket, self.is_green)
        # now there are 5 Trade ticket and 4 of it's sold
        self.sell_buy_trade_test("T5", self.non_mn4, self.nonmn4_pastelid1,
                                 self.non_mn3, self.nonmn3_pastelid1,
                                 new_trade_ticket, self.is_green)
        # now there are 6 Trade ticket and 5 of it's sold

        original_nft_trade_tickets = []
        for i in range(1, 10):
            original_nft_trade_tickets.append(self.sell_buy_trade_test(f"A{i}",
                                                                       self.non_mn3, self.creator_pastelid1,
                                                                       self.non_mn4, self.nonmn4_pastelid1,
                                                                       self.nftact_ticket1_txid,
                                                                       self.is_green, True)
                                              )
        # now there are 15 Trade ticket and 5 of it's sold

        self.sell_buy_trade_test("A10-Fail", self.non_mn3, self.creator_pastelid1,
                                 self.non_mn4, self.nonmn4_pastelid1,
                                 self.nftact_ticket1_txid, self.is_green, True, True)

    # ===============================================================================================================
    def sell_buy_trade_test(self, test_num, seller_node, seller_pastelid,
                            buyer_node, buyer_pastelid, nft_to_sell_txid,
                            is_green, skip_last_fail_test=False, will_fail=False):
        print(f"===== Test {test_num} : {seller_node} sells and {buyer_node} buys =====")
        self.print_heights()

        self.slow_mine(2, 10, 2, 0.5)

        if will_fail:
            print(f"===== Test {test_num} should fail =====")
            try:
                self.nodes[seller_node].tickets("register", "sell", nft_to_sell_txid, str("1000"),
                                                seller_pastelid, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("The NFT you are trying to sell - from registration ticket ["+nft_to_sell_txid +
                         "] - is already sold - there are already [10] sold copies, "
                         "but only [10] copies were available"
                         in self.errorString, True)
            return

        # 1.
        buyer_coins_before = self.nodes[buyer_node].getbalance()
        seller_coins_before = self.nodes[seller_node].getbalance()
        print("buyer_coins_before: " + str(buyer_coins_before))
        print("seller_coins_before: " + str(seller_coins_before))

        # 2. Create Sell ticket
        sell_ticket_txid = self.nodes[seller_node].tickets("register", "sell", nft_to_sell_txid, str("1000"),
                                                           seller_pastelid, self.passphrase)["txid"]
        assert_true(sell_ticket_txid, "No ticket was created")
        print(f"sell_ticket_txid: {sell_ticket_txid}")

        self.__wait_for_ticket_tnx()
        print("buyer's balance 1: " + str(self.nodes[buyer_node].getbalance()))
        print("seller's balance 1: " + str(self.nodes[seller_node].getbalance()))
        self.__wait_for_gen10_blocks()
        print("buyer's balance 2: " + str(self.nodes[buyer_node].getbalance()))
        print("seller's balance 2: " + str(self.nodes[seller_node].getbalance()))

        # 3. Create buy ticket
        buy_ticket_txid = self.nodes[buyer_node].tickets("register", "buy", sell_ticket_txid, str("1000"),
                                                         buyer_pastelid, self.passphrase)["txid"]
        assert_true(buy_ticket_txid, "No ticket was created")
        print(f"buy_ticket_txid: {buy_ticket_txid}")

        self.__wait_for_ticket_tnx()
        print("buyer's balance 3: " + str(self.nodes[buyer_node].getbalance()))
        print("seller's balance 3: " + str(self.nodes[seller_node].getbalance()))
        self.__wait_for_gen10_blocks()
        print("buyer's balance 4: " + str(self.nodes[buyer_node].getbalance()))
        print("seller's balance 4: " + str(self.nodes[seller_node].getbalance()))

        # 5. Create trade ticket
        trade_ticket_txid = self.nodes[buyer_node].tickets("register", "trade",
                                                           sell_ticket_txid, buy_ticket_txid,
                                                           buyer_pastelid, self.passphrase)["txid"]
        assert_true(trade_ticket_txid, "No ticket was created")
        print(f"trade_ticket_txid: {trade_ticket_txid}")
        # Choosen trade ticket for validating ownership 
        # 1. We need a list ( at least with 1 element)
        # of non-sold trade ticket
        #
        # 2. Filter that tickets by pastelID and get the
        # underlying NFTReg ticket found by txid from the
        # request
        if (
            test_num == 'A1' or test_num == 'A2' or
        test_num == 'A3' or test_num == 'A4' or
        test_num == 'A5' or test_num == 'A6' or
        test_num == 'A7' or test_num == 'A8' or
        test_num == 'A9'
        ):
            # This pastelID (and generated trades) holds the ownership of copies (2-10)
            self.single_sell_trade_txids.append(trade_ticket_txid)

        if test_num == 'T5':
            # This pastelID (and generated trade) holds the ownership of copy 1 sold multiple times (nested)
            self.nested_ownership_trade_txid = trade_ticket_txid

        self.__wait_for_ticket_tnx()

        buyer_coins_after = self.nodes[buyer_node].getbalance()
        seller_coins_after = self.nodes[seller_node].getbalance()
        print("buyer_coins_after: " + str(buyer_coins_after))
        print("seller_coins_after: " + str(seller_coins_after))

        # check correct amount of change and correct amount spent
        print(f"trade ticket price - {self.trade_ticket_price}")
        assert_equal(buyer_coins_after, buyer_coins_before-10-self.trade_ticket_price-1000)
        # buy ticket cost is 10 (1000/100), trade ticket cost is self.trade_ticket_price, NFT cost is 1000

        # check seller gets correct amount
        royalty_fee = 0
        green_fee = 0
        if self.royalty > 0:
            royalty_fee = 75    # self.royalty = 0.075
        if is_green:
            green_fee = 20
        assert_equal(seller_coins_after, seller_coins_before + 1000 - 20 - royalty_fee - green_fee)
        # sell ticket cost is 20 (1000/50), NFT cost is 1000

        if not skip_last_fail_test:
            # 6. Verify we cannot sell already sold trade ticket
            #  Verify there is no already trade ticket referring to trade ticket we are trying to sell
            try:
                self.nodes[seller_node].tickets("register", "sell", nft_to_sell_txid, str("1000"),
                                                seller_pastelid, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("The NFT you are trying to sell - from trade ticket ["+nft_to_sell_txid+"] - is already sold"
                         in self.errorString, True)

        print("NFT sell tickets tested (second run)")

        return trade_ticket_txid

    # ===============================================================================================================
    def tickets_list_filter_tests(self, loop_number):
        print("== Tickets List Filter test ==")

        number_personal_nodes = 3
        number_personal_nodes += self.royalty_tickets_tests
        # if self.is_green:
        #     number_personal_nodes += 1

        tickets_list = self.nodes[self.non_mn3].tickets("list", "id")
        assert_equal(len(tickets_list), 12 + number_personal_nodes + loop_number * 2)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id", "all")
        assert_equal(len(tickets_list), 12 + number_personal_nodes + loop_number * 2)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id", "mn")
        assert_equal(len(tickets_list), 12)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id", "personal")
        assert_equal(len(tickets_list), number_personal_nodes + loop_number * 2)

        self.create_nft_ticket_and_signatures(self.non_mn3, 5, self.royalty, self.is_green, False)
        nft_ticket2_txid = self.nodes[self.top_mns_index0].tickets("register", "nft",
                                                                   self.ticket, json.dumps(self.signatures_dict),
                                                                   self.top_mn_pastelid0, self.passphrase,
                                                                   "key3"+str(loop_number), "key4"+str(loop_number), str(self.storage_fee))["txid"]
        assert_true(nft_ticket2_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        self.slow_mine(2, 10, 2, 0.5)

        nft_ticket2_act_ticket_txid = self.nodes[self.non_mn3].tickets("register", "act",
                                                                       nft_ticket2_txid,
                                                                       str(self.creator_ticket_height),
                                                                       str(self.storage_fee),
                                                                       self.creator_pastelid1, self.passphrase)["txid"]
        assert_true(nft_ticket2_act_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        self.create_nft_ticket_and_signatures(self.non_mn3, 1, self.royalty, self.is_green, False)
        nft_ticket3_txid = self.nodes[self.top_mns_index0].tickets("register", "nft",
                                                                   self.ticket, json.dumps(self.signatures_dict),
                                                                   self.top_mn_pastelid0, self.passphrase,
                                                                   "key5"+str(loop_number), "key6"+str(loop_number), str(self.storage_fee))["txid"]
        assert_true(nft_ticket3_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        self.slow_mine(2, 10, 2, 0.5)

        tickets_list = self.nodes[self.non_mn3].tickets("list", "nft")
        assert_equal(len(tickets_list), 3*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "nft", "all")
        assert_equal(len(tickets_list), 3*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "nft", "active")
        assert_equal(len(tickets_list), 2*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "nft", "inactive")
        assert_equal(len(tickets_list), 1*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "nft", "sold")
        assert_equal(len(tickets_list), 1*(loop_number+1))

        tickets_list = self.nodes[self.non_mn3].tickets("list", "act")
        assert_equal(len(tickets_list), 2*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "act", "all")
        assert_equal(len(tickets_list), 2*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "act", "available")
        assert_equal(len(tickets_list), 1*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "act", "sold")
        assert_equal(len(tickets_list), 1*(loop_number+1))

        cur_block = self.nodes[self.non_mn3].getblockcount()
        sell_ticket1_txid = self.nodes[self.non_mn3].tickets("register", "sell", nft_ticket2_act_ticket_txid,
                                                             str("1000"),
                                                             self.creator_pastelid1, self.passphrase,
                                                             cur_block+15, cur_block+20)["txid"]
        assert_true(sell_ticket1_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()  # cur+5 block
        print(sell_ticket1_txid)

        sell_ticket2_txid = self.nodes[self.non_mn3].tickets("register", "sell", nft_ticket2_act_ticket_txid,
                                                             str("1000"),
                                                             self.creator_pastelid1, self.passphrase,
                                                             cur_block+20, cur_block+30)["txid"]
        assert_true(sell_ticket2_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()  # cur+10 block
        print(sell_ticket2_txid)

        sell_ticket3_txid = self.nodes[self.non_mn3].tickets("register", "sell", nft_ticket2_act_ticket_txid,
                                                             str("1000"),
                                                             self.creator_pastelid1, self.passphrase,
                                                             cur_block+30, cur_block+40)["txid"]
        assert_true(sell_ticket3_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()  # cur+15 block
        self.slow_mine(1, 10, 2, 0.5)  # cur+25
        print(sell_ticket3_txid)

        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell")
        assert_equal(len(tickets_list), 18*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "all")
        assert_equal(len(tickets_list), 18*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "available")
        assert_equal(len(tickets_list), 1)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "unavailable")
        assert_equal(len(tickets_list), 1)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "expired")
        assert_equal(len(tickets_list), 1 + (loop_number*2))

        buy_ticket_txid = self.nodes[self.non_mn4].tickets("register", "buy", sell_ticket2_txid, str("1000"),
                                                           self.nonmn4_pastelid1, self.passphrase)["txid"]
        assert_true(buy_ticket_txid, "No ticket was created")
        print(f"buy_ticket_txid: {buy_ticket_txid}")
        self.__wait_for_ticket_tnx()  # +15 block
        self.slow_mine(2, 10, 2, 0.5)  # +25
        tickets_list = self.nodes[self.non_mn3].tickets("list", "buy")
        assert_equal(len(tickets_list), 16*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "buy", "all")
        assert_equal(len(tickets_list), 16*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "buy", "expired")
        assert_equal(len(tickets_list), 1*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "buy", "sold")
        assert_equal(len(tickets_list), 15*(loop_number+1))

        tickets_list = self.nodes[self.non_mn3].tickets("list", "trade")
        assert_equal(len(tickets_list), 15*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "trade", "all")
        assert_equal(len(tickets_list), 15*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "trade", "available")
        assert_equal(len(tickets_list), 10*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "trade", "sold")
        assert_equal(len(tickets_list), 5*(loop_number+1))

        print ("Test listing buy/sell/trade tickets by Pastel ID")

        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", self.creator_pastelid1)
        assert_equal(len(tickets_list), 2*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "all", self.creator_pastelid1)
        assert_equal(len(tickets_list), 2*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "available", self.creator_pastelid1)
        assert_equal(tickets_list is None, True)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "unavailable", self.creator_pastelid1)
        assert_equal(tickets_list is None, True)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "expired", self.creator_pastelid1)
        assert_equal(len(tickets_list), 2 + (loop_number*2))

        tickets_list = self.nodes[self.non_mn3].tickets("list", "buy", self.nonmn4_pastelid1)
        assert_equal(len(tickets_list), 13*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "buy", "all", self.nonmn4_pastelid1)
        assert_equal(len(tickets_list), 13*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "buy", "expired", self.nonmn4_pastelid1)
        assert_equal(len(tickets_list), 1*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "buy", "sold", self.nonmn4_pastelid1)
        assert_equal(len(tickets_list), 12*(loop_number+1))

        tickets_list = self.nodes[self.non_mn3].tickets("list", "trade", self.nonmn3_pastelid1)
        assert_equal(len(tickets_list), 3*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "trade", "all", self.nonmn3_pastelid1)
        assert_equal(len(tickets_list), 3*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "trade", "available", self.nonmn3_pastelid1)
        assert_equal(len(tickets_list), 1*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "trade", "sold", self.nonmn3_pastelid1)
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

        self.nonmn8_address1 = self.nodes[self.non_mn8].getnewaddress()
        self.__wait_for_sync_all10()
        self.nonmn8_pastelid1 = self.create_pastelid(self.non_mn8)[0]

        # Register first time by PastelID of non-masternode 3
        tickets_ethereumaddress_txid1 = self.nodes[self.non_mn3].tickets("register", "ethereumaddress", "0x863c30dd122a21f815e46ec510777fd3e3398c26",
                                                    self.creator_pastelid1, "passphrase")
        self.__wait_for_ticket_tnx()
        nonmn3_ticket_ethereumaddress_1 = self.nodes[self.non_mn4].tickets("get", tickets_ethereumaddress_txid1["txid"])
        print(nonmn3_ticket_ethereumaddress_1)

        self.__wait_for_ticket_tnx()
        assert_equal(nonmn3_ticket_ethereumaddress_1["ticket"]["pastelID"], self.creator_pastelid1)
        assert_equal(nonmn3_ticket_ethereumaddress_1["ticket"]["ethereumAddress"], "0x863c30dd122a21f815e46ec510777fd3e3398c26")
        assert_equal(nonmn3_ticket_ethereumaddress_1["ticket"]["fee"], 100)

        # Register by a new pastelID. Expect to get Exception that the ticket is invalid because there are Not enough 100 PSL to cover price 100
        try:
            self.nodes[self.non_mn8].tickets("register", "ethereumaddress", "0xf24C621e5108607F4EC60e9C4f91719a76c7B3C9",
                                                        self.nonmn8_pastelid1, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(self.errorString, "Ticket (ethereum-address-change) is invalid - Not enough coins to cover price [100]")

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn8_address1, 200, "", "", False)
        self.__wait_for_sync_all10()

        # This should be success
        self.nodes[self.non_mn8].tickets("register", "ethereumaddress", "0xf24C621e5108607F4EC60e9C4f91719a76c7B3C9",
                                                        self.nonmn8_pastelid1, "passphrase")
        self.__wait_for_ticket_tnx()

        # Expect to get Exception that the ticket is invalid because this PastelID do not have enough 5000PSL to pay the rechange fee
        try:
            self.nodes[self.non_mn8].tickets("register", "ethereumaddress", "0x7cB11556A8883f002514B6878575811728f2A158 ",
                                                    self.nonmn8_pastelid1, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(self.errorString, "Ticket (ethereum-address-change) is invalid - Not enough coins to cover price [5000]")

        # Send money to non-masternode3 to cover 5000 price
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 5100, "", "", False)
        self.__wait_for_sync_all10()

        # Expect to get Exception that the ticket is invalid because this PastelID changed EthereumAddress in last 24 hours
        try:
            self.nodes[self.non_mn3].tickets("register", "ethereumaddress", "0xD2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f ",
                                                    self.creator_pastelid1, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Ticket (ethereum-address-change) is invalid - Ethereum Address Change ticket is invalid. Already changed in last 24 hours." in self.errorString, True)

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
                                                    self.creator_pastelid1, "passphrase")
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
                                                        self.nonmn4_pastelid2, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Ticket (ethereum-address-change) is invalid - Invalid length of ethereum address, the length should be exactly 40 characters" in self.errorString, True)

        try:
            tickets_ethereumaddress_txid1 = self.nodes[self.non_mn4].tickets("register", "ethereumaddress", "1xD2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f",
                                                        self.nonmn4_pastelid2, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Invalid ethereum address, should start with the characters 0x" in self.errorString, True)

        try:
            tickets_ethereumaddress_txid1 = self.nodes[self.non_mn4].tickets("register", "ethereumaddress", "0xZ2cBc412BE9D6c6c3fDBb3c8d6554CC4D5E3A13f",
                                                        self.nonmn4_pastelid2, "passphrase")
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
        non_mn1_total_storage_fee1 = self.nodes[self.non_mn4].tickets("tools", "gettotalstoragefee",
                                                                   self.ticket, json.dumps(self.signatures_dict),
                                                                   self.nonmn4_pastelid1, self.passphrase,
                                                                   "key5", "key6", str(self.storage_fee), 5)["totalstoragefee"]
        #   a.2 from MN without errors
        mn0_total_storage_fee1 = self.nodes[self.top_mns_index0].tickets("tools", "gettotalstoragefee",
                                                                   self.ticket, json.dumps(self.signatures_dict),
                                                                   self.top_mn_pastelid0, self.passphrase,
                                                                   "key5", "key6", str(self.storage_fee), 5)["totalstoragefee"]
        mn0_total_storage_fee2 = self.nodes[self.top_mns_index0].tickets("tools", "gettotalstoragefee",
                                                                   self.ticket, json.dumps(self.signatures_dict),
                                                                   self.top_mn_pastelid0, self.passphrase,
                                                                   "key5", "key6", str(self.storage_fee), 4)["totalstoragefee"]

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
        print("Network fee is ", nfee_mn0)

        lfee_mn0 = self.nodes[0].storagefee("getlocalfee")["localfee"]
        assert_equal(lfee_mn0, 50)
        print("Local fee of MN0 is ", lfee_mn0)

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

    def __wait_for_gen10_blocks(self):
        time.sleep(2)
        self.nodes[self.mining_node_num].generate(10)
        time.sleep(2)
        self.nodes[self.mining_node_num].generate(10)

    def __wait_for_sync_all(self, v):
        time.sleep(2)
        self.sync_all()
        self.nodes[self.mining_node_num].generate(v)
        self.sync_all()

    def __wait_for_sync_all10(self):
        time.sleep(2)
        self.sync_all(10, 30)
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10, 30)

    def __wait_for_ticket_tnx(self):
        time.sleep(10)
        for x in range(5):
            self.nodes[self.mining_node_num].generate(1)
            self.sync_all(10, 3)
        self.sync_all(10, 30)

    def print_heights(self):
        for x in range(17):
            print(f"Node{x} height is {self.nodes[x].getblockcount()}")

    def slow_mine(self, number_of_bursts, num_in_each_burst, wait_between_bursts, wait_inside_burst):
        for x in range(number_of_bursts):
            for y in range(num_in_each_burst):
                self.nodes[self.mining_node_num].generate(1)
                time.sleep(wait_inside_burst)
            time.sleep(wait_between_bursts)


if __name__ == '__main__':
    MasterNodeTicketsTest().main()