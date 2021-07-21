#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import math

from test_framework.util import assert_equal, assert_greater_than, \
    assert_true, initialize_chain_clean, str_to_b64str
from mn_common import MasterNodeCommon
from test_framework.authproxy import JSONRPCException
import json
import time
import base64
import random, string
import sys
import hashlib
  
if sys.version_info < (3, 6):
    import sha3

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


# error strings
ERR_READ_PASTELID_FILE = "Failed to read Pastel secure container file";

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
    non_mn8 = number_of_master_nodes+7      # will not have coins by default #20, for Username test

    mining_node_num = number_of_master_nodes    # same as non_mn1
    hot_node_num = number_of_master_nodes+1     # same as non_mn2

    def __init__(self):
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

        self.nonmn1_pastelid2 = None
        self.nonmn3_pastelid1 = None
        self.nonmn4_pastelid1 = None
        self.nonmn5_royalty_pastelid1 = None
        self.nonmn6_royalty_pastelid1 = None
        self.nonmn7_green_pastelid1 = None

        self.mn0_ticket1_txid = None
        self.nonmn3_address1 = None
        self.nonmn4_address1 = None
        self.nonmn5_royalty_address1 = None
        self.nonmn6_royalty_address1 = None
        self.nonmn7_green_address1 = None
        self.artist_pastelid1 = None
        self.artist_ticket_height = None
        self.total_copies = None
        self.ticket_signature_artist = None
        self.art_ticket1_txid = None
        self.top_mns_index0 = None
        self.top_mns_index1 = None
        self.top_mns_index2 = None
        self.top_mn_pastelid0 = None
        self.top_mn_pastelid1 = None
        self.top_mn_pastelid2 = None
        self.top_mn_ticket_signature0 = None
        self.top_mn_ticket_signature1 = None
        self.top_mn_ticket_signature2 = None
        self.art_ticket1_act_ticket_txid = None
        self.art_ticket1_sell_ticket_txid = None
        self.art_ticket1_buy_ticket_txid = None
        self.art_ticket1_trade_ticket_txid = None
        self.trade_ticket1_sell_ticket_txid = None
        self.trade_ticket1_buy_ticket_txid = None
        self.trade_ticket1_trade_ticket_txid = None

        self.id_ticket_price = 10
        self.art_ticket_price = 10
        self.act_ticket_price = 10
        self.trade_ticket_price = 10

        self.royalty = 7
        self.royalty_tickets_tests = 2
        self.royalty_null_tests = False
        self.royalty_address = None
        self.is_green = True

        self.test_high_heights = False

    def setup_chain(self):
        print(f"Initializing test directory {self.options.tmpdir}")
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)

    def setup_network(self, split=False):
        self.setup_masternodes_network(private_keys_list, self.number_of_simple_nodes)

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
        if self.is_green:
            self.personal_nonmn7_green_initialize_tests()
        else:
            self.nonmn7_green_address1 = ""
        self.artreg_ticket_tests(False, "key1", "key2")

        if self.royalty > 0:
            if self.royalty_tickets_tests > 0:
                self.personal_nonmn5_royalty_initialize_tests()
                self.artroyalty_ticket_tests(self.non_mn3, self.artist_pastelid1,
                                             self.nonmn5_royalty_pastelid1, self.nonmn5_royalty_address1, 1)
            if self.royalty_tickets_tests > 1:
                self.personal_nonmn6_royalty_initialize_tests()
                self.artroyalty_ticket_tests(self.non_mn5, self.nonmn5_royalty_pastelid1,
                                             self.nonmn6_royalty_pastelid1, self.nonmn6_royalty_address1, 2)
        else:
            self.royalty_tickets_tests = 0
            self.royalty_null_tests = True
            self.artroyalty_null_ticket_tests()

        self.artact_ticket_tests(False)
        self.artsell_ticket_tests1(False)
        self.artbuy_ticket_tests(False)
        self.arttrade_ticket_tests(False)
        self.sell_buy_trade_tests()
        self.takedown_ticket_tests()
        self.storage_fee_tests()
        self.tickets_list_filter_tests(0)
        self.username_ticket_tests()

        if self.test_high_heights:
            self.id_ticket_price = 1000

            print(f"id ticket price - {self.id_ticket_price}")
            print(f"art ticket price - {self.art_ticket_price}")
            print(f"activation ticket price - {self.act_ticket_price}")
            print(f"trade ticket price - {self.trade_ticket_price}")

            print("mining {} blocks".format(10000))
            for i in range(100):
                self.slow_mine(10, 10, 2, 0.01)
                print(f"mined {100*i} blocks")
                self.reconnect_nodes(0, self.number_of_master_nodes)
                self.sync_all()

            self.pastelid_tests()
            self.personal_pastelid_ticket_tests(True)
            self.artreg_ticket_tests(True, "key10001", "key20001")
            self.artact_ticket_tests(True)
            self.artsell_ticket_tests1(True)
            self.artbuy_ticket_tests(True)
            self.arttrade_ticket_tests(True)
            self.sell_buy_trade_tests()
            self.takedown_ticket_tests()
            self.storage_fee_tests()
            self.tickets_list_filter_tests(1)
            self.username_ticket_tests()

    # ===============================================================================================================
    def pastelid_tests(self):
        print("== Pastelid tests ==")
        # 1. pastelid tests
        # a. Generate new PastelID and associated keys (EdDSA448). Return PastelID and LegRoast pubkey base58-encoded
        # a.a - generate with no errors two keys at MN and non-MN
        self.mn0_pastelid1 = self.create_pastelid(0, self.mn0_id1_lrkey)
        self.mn0_pastelid2 = self.create_pastelid(0)

        # for non active MN
        self.non_active_mn_pastelid1 = self.create_pastelid(self.non_active_mn)
        nonmn1_pastelid1 = self.create_pastelid(self.non_mn1)
        self.nonmn1_pastelid2 = self.create_pastelid(self.non_mn1)

        # for node without coins
        self.nonmn3_pastelid1 = self.create_pastelid(self.non_mn3)

        # a.b - fail if empty passphrase
        try:
            self.nodes[self.non_mn1].pastelid("newkey", "")
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("passphrase for new key cannot be empty" in self.errorString, True)

        # b. Import private "key" (EdDSA448) as PKCS8 encrypted string in PEM format. Return PastelID base58-encoded
        # NOT IMPLEMENTED

        # c. List all internally stored PastelID and keys
        id_list = self.nodes[0].pastelid("list")
        id_list = dict((key+str(i), val) for i, k in enumerate(id_list) for key, val in k.items())
        assert_true(self.mn0_pastelid1 in id_list.values(), f"PastelID {self.mn0_pastelid1} not in the list")
        assert_true(self.mn0_pastelid2 in id_list.values(), f"PastelID {self.mn0_pastelid2} not in the list")

        id_list = self.nodes[self.non_mn1].pastelid("list")
        id_list = dict((key+str(i), val) for i, k in enumerate(id_list) for key, val in k.items())
        assert_true(nonmn1_pastelid1 in id_list.values(), f"PastelID {nonmn1_pastelid1} not in the list")
        assert_true(self.nonmn1_pastelid2 in id_list.values(), f"PastelID {self.nonmn1_pastelid2} not in the list")

        print(f"Pastelid test: 2 PastelID's each generate at node0 (MN) and node {self.non_mn1} (non-MN)")

        # d. Sign "text" with the internally stored private key associated with the PastelID
        # d.a - sign with no errors using key from 1.a.a, default algorithm - EdDSA448
        text_to_sign = "1234567890"
        mn0_signature1 = self.nodes[0].pastelid("sign", text_to_sign, self.mn0_pastelid1, self.passphrase)["signature"]
        assert_true(mn0_signature1, "No ed448 signature was created")
        assert_equal(len(base64.b64decode(mn0_signature1)), 114)
        
        # d.b - sign with no errors using encoded legroast public key from 1.a.a
        # returns base64 encoded signature
        mn0_lr_signature1 = self.nodes[0].pastelid("sign", text_to_sign, self.mn0_pastelid1, self.passphrase, "legroast")["signature"]
        assert_true(mn0_lr_signature1, "No LegRoast signature was created")

        # e. Sign "text" with the private "key" (EdDSA448) as PKCS8 encrypted string in PEM format
        # NOT IMPLEMENTED

        # f. Verify "text"'s "signature" with the PastelID
        # f.a - verify ed448 signature with no errors using key from 1.a.a
        result = self.nodes[0].pastelid("verify", text_to_sign, mn0_signature1, self.mn0_pastelid1)["verification"]
        assert_equal(result, "OK")
        # f.a.a - verify with no errors using key from 1.a.a
        result = self.nodes[0].pastelid("verify", text_to_sign, mn0_lr_signature1, self.mn0_pastelid1, "legroast")["verification"]
        assert_equal(result, "OK")

        # f.b - fail to verify ed448 signature with the different key from 1.a.a
        result = self.nodes[0].pastelid("verify", text_to_sign, mn0_signature1, self.mn0_pastelid2)["verification"]
        assert_equal(result, "Failed")
        # f.c - fail to verify ed448 signature with the different key from 1.a.a
        result = self.nodes[0].pastelid("verify", text_to_sign, mn0_lr_signature1, self.mn0_pastelid2, "legroast")["verification"]
        assert_equal(result, "Failed")

        # f.d - fail to verify modified text (ed448 signature)
        result = self.nodes[0].pastelid("verify", "1234567890AAA", mn0_signature1, self.mn0_pastelid1)["verification"]
        assert_equal(result, "Failed")

        # f.e - fail to verify modified text (legroast signature)
        result = self.nodes[0].pastelid("verify", "1234567890AAA", mn0_lr_signature1, self.mn0_pastelid1, "legroast")["verification"]
        assert_equal(result, "Failed")

        print("Pastelid test: Message signed and verified")

    # ===============================================================================================================
    def mn_pastelid_ticket_tests(self, skip_low_coins_tests):
        print("== Masternode PastelID Tickets test ==")
        # 2. tickets tests
        # a. PastelID ticket
        #   a.a register MN PastelID
        #       a.a.1 fail if not MN
        try:
            self.nodes[self.non_mn1].tickets("register", "mnid", self.nonmn1_pastelid2, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("This is not an active masternode" in self.errorString, True)

        #       a.a.2 fail if not active MN
        try:
            self.nodes[self.non_active_mn].tickets("register", "mnid", self.non_active_mn_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("This is not an active masternode" in self.errorString, True)

        #       a.a.3 fail if active MN, but wrong PastelID
        try:
            self.nodes[0].tickets("register", "mnid", self.nonmn1_pastelid2, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(ERR_READ_PASTELID_FILE in self.errorString, True)
        # TODO: provide better error for unknown PastelID

        #       a.a.4 fail if active MN, but wrong passphrase
        try:
            self.nodes[0].tickets("register", "mnid", self.mn0_pastelid1, "wrong")
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(ERR_READ_PASTELID_FILE in self.errorString, True)
        # TODO: provide better error for wrong passphrase

        #       a.a.5 fail if active MN, but not enough coins - ~11PSL
        if not skip_low_coins_tests:
            try:
                self.nodes[0].tickets("register", "mnid", self.mn0_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("No unspent transaction found" in self.errorString, True)

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
        try:
            self.nodes[0].tickets("register", "mnid", self.mn0_pastelid1, self.passphrase)["txid"]
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("This PastelID is already registered in blockchain" in self.errorString, True)

        #       a.a.9.2 fail if outpoint is already registered
        try:
            self.nodes[0].tickets("register", "mnid", self.mn0_pastelid2, self.passphrase)["txid"]
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Ticket (pastelid) is invalid - Masternode's outpoint" in self.errorString, True)

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

        #   b.a register personal PastelID
        #       b.a.1 fail if wrong PastelID
        try:
            self.nodes[self.non_mn3].tickets("register", "id", self.nonmn1_pastelid2, self.passphrase,
                                             self.nonmn3_address1)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(ERR_READ_PASTELID_FILE in self.errorString, True)
        # TODO Pastel: provide better error for unknown PastelID

        #       b.a.2 fail if wrong passphrase
        try:
            self.nodes[self.non_mn3].tickets("register", "id", self.nonmn3_pastelid1, "wrong", self.nonmn3_address1)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(ERR_READ_PASTELID_FILE in self.errorString, True)
        # TODO Pastel: provide better error for wrong passphrase

        #       b.a.3 fail if not enough coins - ~11PSL
        if not skip_low_coins_tests:
            try:
                self.nodes[self.non_mn3].tickets("register", "id", self.nonmn3_pastelid1, self.passphrase,
                                                 self.nonmn3_address1)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("No unspent transaction found" in self.errorString, True)

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
        try:
            self.nodes[self.non_mn3].tickets("register", "id", self.nonmn3_pastelid1, self.passphrase,
                                             self.nonmn3_address1)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("This PastelID is already registered in blockchain" in self.errorString, True)

        #   b.b find personal PastelID
        #       b.b.1 by PastelID
        nonmn3_ticket1_1 = self.nodes[0].tickets("find", "id", self.nonmn3_pastelid1)
        assert_equal(nonmn3_ticket1_1["ticket"]["pastelID"], self.nonmn3_pastelid1)
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
        self.nonmn5_royalty_pastelid1 = self.create_pastelid(self.non_mn5)
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

    def get_rand_testdata(self, scope, len):
        return ''.join(random.choice(scope) for i in range(len))

    # ===============================================================================================================

    def generate_art_ticket_details(self):
        # Art_ticket structure 
        # {
        # "artist_name": string,
        # "artwork_title": string,
        # "artwork_series_name": string,
        # "artwork_keyword_set": string,
        # "artist_website": string,
        # "artist_written_statement": string,
        # "artwork_creation_video_youtube_url": string,
        # "thumbnail_hash": bytes,    //hash of the thumbnail !!!!SHA3-256!!!!
        # "data_hash": bytes,         // hash of the image (or any other asset) that this ticket represents !!!!SHA3-256!!!!
        # "fingerprints_hash": bytes,       //hash of the fingerprint !!!!SHA3-256!!!!
        # "fingerprints": bytes,            //compressed fingerprint
        # "fingerprints_signature": bytes,  //signature on raw image fingerprint
        # "rq_ids": [list of strings],//raptorq symbol identifiers -  !!!!SHA3-256 of symbol block!!!!
        # "rq_oti": [array of 12 bytes],    //raptorq CommonOTI and SchemeSpecificOTI
        # "rareness_score": integer,  // 0 to 1000
        # "nsfw_score": integer,      // 0 to 1000
        # "seen_score": integer,      // 0 to 1000
        # }
        # Data for art-ticket generation
        artist_first_names=('John','Andy','Joe', 'Jennifer', 'August', 'Dave', 'Blanca', 'Diana', 'Tia', 'Michael')
        artist_last_names=('Johnson','Smith','Williams', 'Ecclestone', 'Schumacher', 'Faye', 'Counts', 'Wesley')
        letters = string.ascii_letters

        # initialize hash base strings or lists
        thumbnail_to_be_hashed = self.get_rand_testdata(letters, 10)#''.join(random.choice(letters) for i in range(10))
        data_to_be_hashed = self.get_rand_testdata(letters, 10)#''.join(random.choice(letters) for i in range(10))
        fingerprints_to_be_hashed = self.get_rand_testdata(letters, 10)#''.join(random.choice(letters) for i in range(10))
        rq_oti = self.get_rand_testdata(letters, 12)#''.join(random.choice(letters) for i in range(12))
        rq_ids_to_be_hashed = ""
        for _ in range (5):
            rq_ids_to_be_hashed += (self.get_rand_testdata(letters, 10))
        
        # encode the string
        encoded_thumbnail = thumbnail_to_be_hashed.encode()
        encoded_data = data_to_be_hashed.encode()
        encoded_fingerprint = fingerprints_to_be_hashed.encode()
        encoded_rq_ids = rq_ids_to_be_hashed.encode()
        
        # create sha3-256 hash objects
        obj_sha3_256_thumbnail = hashlib.sha3_256(encoded_thumbnail)
        obj_sha3_256_data = hashlib.sha3_256(encoded_data)
        obj_sha3_256_fingerprint = hashlib.sha3_256(encoded_fingerprint)
        obj_sha3_256_rq_ids = hashlib.sha3_256(encoded_rq_ids)

        art_ticket_json = {
            "artist_name": "".join(random.choice(artist_first_names)+" "+random.choice(artist_last_names)),
            "artwork_title": self.get_rand_testdata(letters, 10),
            "artwork_series_name": self.get_rand_testdata(letters, 10),
            "artwork_keyword_set": self.get_rand_testdata(letters, 10),
            "artist_website": self.get_rand_testdata(letters, 10),
            "artist_written_statement": self.get_rand_testdata(letters, 10),
            "artwork_creation_video_youtube_url": self.get_rand_testdata(letters, 10),
            "thumbnail_hash": obj_sha3_256_thumbnail.hexdigest(),    #hash of the thumbnail !!!!SHA3-256!!!!
            "data_hash": obj_sha3_256_data.hexdigest(),         #hash of the image (or any other asset) that this ticket represents !!!!SHA3-256!!!!
            "fingerprints_hash": obj_sha3_256_fingerprint.hexdigest(),       #hash of the fingerprint !!!!SHA3-256!!!!
            "fingerprints": fingerprints_to_be_hashed,            #compressed fingerprint
            "fingerprints_signature": self.get_rand_testdata(letters, 20), #signature on raw image fingerprint
            "rq_ids": obj_sha3_256_rq_ids.hexdigest(), #[list of strings],//raptorq symbol identifiers -  !!!!SHA3-256 of symbol block!!!!
            "rq_oti": rq_oti,    #raptorq CommonOTI and SchemeSpecificOTI
            "rareness_score": str(random.randint(0, 1000)),   # 0 to 1000
            "nsfw_score": str(random.randint(0, 1000)),   # 0 to 1000
            "seen_score": str(random.randint(0, 1000)),   # 0 to 1000
        }

        return art_ticket_json

     # ===============================================================================================================
    def personal_green_initialize_tests(self):
        # personal green PastelID ticket
        self.nonmn6_green_pastelid1 = self.create_pastelid(self.non_mn6)
        self.nonmn6_green_address1 = self.nodes[self.non_mn6].getnewaddress()

        print("Personal non_mn5 royalty initialize tested")

    # ===============================================================================================================
    def personal_nonmn6_royalty_initialize_tests(self):
        # personal royalty PastelID ticket
        self.nonmn6_royalty_pastelid1 = self.create_pastelid(self.non_mn6)
        assert_true(self.nonmn6_royalty_pastelid1, "No Pastelid was created")
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
    def personal_nonmn7_green_initialize_tests(self):
        # personal green PastelID ticket
        self.nonmn7_green_pastelid1 = self.create_pastelid(self.non_mn7)
        assert_true(self.nonmn7_green_pastelid1, "No Pastelid was created")
        self.nonmn7_green_address1 = self.nodes[self.non_mn7].getnewaddress()

        # register without errors from non MN with enough coins
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn7_green_address1, 100, "", "", False)
        self.__wait_for_sync_all(1)

        coins_before = self.nodes[self.non_mn7].getbalance()
        nonmn7_ticket1_txid = self.nodes[self.non_mn7].tickets("register", "id", self.nonmn7_green_pastelid1,
                                                               self.passphrase, self.nonmn7_green_address1)["txid"]
        assert_true(nonmn7_ticket1_txid, "No ticket was created")
        self.__wait_for_sync_all(1)

        # check correct amount of change
        coins_after = self.nodes[self.non_mn7].getbalance()
        print(f"id ticket price - {self.id_ticket_price}")
        assert_equal(coins_after, coins_before - self.id_ticket_price)  # no fee yet

        print("Personal green initialize tested")

     # ===============================================================================================================
    def create_art_ticket_and_signatures(self, artist_pastelid, artist_node_num,
                                         app_ticket, data_hash, total_copies, royalty, green_address,
                                         make_bad_signatures_dicts):
        mn_ticket_signatures = {}

        # Get current height
        self.artist_ticket_height = self.nodes[0].getinfo()["blocks"]
        print(f"artist_ticket_height - {self.artist_ticket_height}")
        # Get current top MNs at Node 0
        top_masternodes = self.nodes[0].masternode("top")[str(self.artist_ticket_height)]
        print(f"top_masternodes - {top_masternodes}")

        # Current art_ticket - 8 Items!!!!
        # {
        #   "version": integer          // 1
        #   "author": bytes,            // PastelID of the author (artist)
        #   "blocknum": integer,        // block number when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "block_hash": bytes         // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "copies": integer,          // number of copies
        #   "royalty": short,           // how much artist should get on all future resales (not yet supported by cNode)
        #   "green_address": string,    // address for Green NFT payment (not yet supported by cNode)
        #   "app_ticket": ...
        # }


        res1 = self.nodes[artist_node_num].getblock(str(self.artist_ticket_height))
        
        block_hash = res1["hash"]

        app_ticket_json = self.generate_art_ticket_details()

        app_ticket = str_to_b64str(json.dumps(app_ticket_json))

        json_ticket = {
            "version": 1,
            "author": artist_pastelid,
            "blocknum": self.artist_ticket_height,
            "block_hash": block_hash,
            "copies": total_copies,
            "royalty": royalty,
            "green_address": green_address,
            "app_ticket": app_ticket
        }
        self.ticket = str_to_b64str(json.dumps(json_ticket))
        print(f"ticket - {self.ticket}")

        # create ticket signature
        self.ticket_signature_artist = \
            self.nodes[artist_node_num].pastelid("sign", self.ticket, artist_pastelid, self.passphrase)["signature"]
        for n in range(0, 12):
            mn_ticket_signatures[n] = self.nodes[n].pastelid("sign",
                                                             self.ticket, self.mn_pastelids[n], self.passphrase)["signature"]
        print(f"ticket_signature_artist - {self.ticket_signature_artist}")
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
                "artist": {self.artist_pastelid1: self.ticket_signature_artist},
                "mn2": {self.top_mn_pastelid1: self.top_mn_ticket_signature1},
                "mn3": {self.top_mn_pastelid2: self.top_mn_ticket_signature2},
            }
        )
        print(f"signatures_dict - {self.signatures_dict!r}")

        if make_bad_signatures_dicts:
            self.same_mns_signatures_dict = dict(
                {
                    "artist": {self.artist_pastelid1: self.ticket_signature_artist},
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
                    "artist": {self.artist_pastelid1: self.ticket_signature_artist},
                    "mn2": {not_top_mn_pastelid1: not_top_mn_ticket_signature1},
                    "mn3": {not_top_mn_pastelid2: not_top_mn_ticket_signature2},
                }
            )
            print(f"not_top_mns_signatures_dict - {self.not_top_mns_signatures_dict!r}")

    def artreg_ticket_tests(self, skip_mn_pastelid_registration, key1, key2):
        print("== Art registration Tickets test ==")
        # c. art registration ticket

        self.nodes[self.mining_node_num].generate(10)

        # generate pastelIDs
        non_registered_personal_pastelid1 = self.create_pastelid(self.non_mn3)
        non_registered_mn_pastelid1 = self.create_pastelid(2)
        if not skip_mn_pastelid_registration:
            self.artist_pastelid1 = self.create_pastelid(self.non_mn3)
            for n in range(0, 12):
                self.mn_addresses[n] = self.nodes[n].getnewaddress()
                self.nodes[self.mining_node_num].sendtoaddress(self.mn_addresses[n], 100, "", "", False)
                if n == 0:
                    self.mn_pastelids[n] = self.mn0_pastelid1  # mn0 has its PastelID registered already
                else:
                    self.mn_pastelids[n] = self.create_pastelid(n)
                self.mn_outpoints[self.nodes[n].masternode("status")["outpoint"]] = n

            print(f"mn_addresses - {self.mn_addresses}")
            print(f"mn_pastelids - {self.mn_pastelids}")
            print(f"mn_outpoints - {self.mn_outpoints}")

            self.__wait_for_sync_all(1)

            # register pastelIDs
            self.nodes[self.non_mn3].tickets("register", "id", self.artist_pastelid1, self.passphrase, self.nonmn3_address1)
            for n in range(1, 12):  # mn0 has its PastelID registered already
                self.nodes[n].tickets("register", "mnid", self.mn_pastelids[n], self.passphrase)

        else:
            for n in range(0, 12):
                self.mn_addresses[n] = self.nodes[n].getnewaddress()
                self.nodes[self.mining_node_num].sendtoaddress(self.mn_addresses[n], 10000, "", "", False)

        self.__wait_for_sync_all(5)

        self.total_copies = 10
        self.create_art_ticket_and_signatures(self.artist_pastelid1, self.non_mn3,
                                              "HIJKLMNOP", "ABCDEFG", self.total_copies,
                                              self.royalty, self.nonmn7_green_address1,
                                              True)

        #   c.a register art registration ticket
        #       c.a.1 fail if not MN
        try:
            self.nodes[self.non_mn1].tickets("register", "art", self.ticket, json.dumps(self.signatures_dict),
                                             self.nonmn1_pastelid2, self.passphrase, key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("This is not an active masternode" in self.errorString, True)

        #       c.a.2 fail if not active MN
        try:
            self.nodes[self.non_active_mn].tickets("register", "art", self.ticket, json.dumps(self.signatures_dict),
                                                   self.non_active_mn_pastelid1, self.passphrase, key1, key2,
                                                   str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("This is not an active masternode" in self.errorString, True)

        #       c.a.3 fail if active MN, but wrong PastelID
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.nonmn1_pastelid2, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(ERR_READ_PASTELID_FILE in self.errorString, True)
        # TODO: provide better error for unknown PastelID

        #       c.a.4 fail if active MN, but wrong passphrase
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, "wrong",
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(ERR_READ_PASTELID_FILE in self.errorString, True)
        # TODO: provide better error for wrong passphrase

        #       c.a.5 fail if artist's signature is not matching
        self.signatures_dict["artist"][self.artist_pastelid1] = self.top_mn_ticket_signature1
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Artist signature is invalid" in self.errorString, True)
        self.signatures_dict["artist"][self.artist_pastelid1] = self.ticket_signature_artist

        #       c.a.6 fail if MN2 and MN3 signatures are not matching
        self.signatures_dict["mn2"][self.top_mn_pastelid1] = self.top_mn_ticket_signature2
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("MN2 signature is invalid" in self.errorString, True)
        self.signatures_dict["mn2"][self.top_mn_pastelid1] = self.top_mn_ticket_signature1

        self.signatures_dict["mn3"][self.top_mn_pastelid2] = self.top_mn_ticket_signature1
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("MN3 signature is invalid" in self.errorString, True)
        self.signatures_dict["mn3"][self.top_mn_pastelid2] = self.top_mn_ticket_signature2

        #       c.a.7 fail if artist's PastelID is not registered
        self.signatures_dict["artist"][non_registered_personal_pastelid1] = self.ticket_signature_artist
        del self.signatures_dict["artist"][self.artist_pastelid1]
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Artist PastelID is not registered" in self.errorString, True)
        self.signatures_dict["artist"][self.artist_pastelid1] = self.ticket_signature_artist
        del self.signatures_dict["artist"][non_registered_personal_pastelid1]

        #       c.a.8 fail if artist's PastelID is not personal

        self.signatures_dict["artist"][self.top_mn_pastelid1] = self.top_mn_ticket_signature1
        del self.signatures_dict["artist"][self.artist_pastelid1]
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Artist PastelID is NOT personal PastelID" in self.errorString, True)
        self.signatures_dict["artist"][self.artist_pastelid1] = self.ticket_signature_artist
        del self.signatures_dict["artist"][self.top_mn_pastelid1]

        #       c.a.9 fail if MN PastelID is not registered
        self.signatures_dict["mn2"][non_registered_mn_pastelid1] = self.top_mn_ticket_signature1
        del self.signatures_dict["mn2"][self.top_mn_pastelid1]
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("MN2 PastelID is not registered" in self.errorString, True)
        self.signatures_dict["mn2"][self.top_mn_pastelid1] = self.top_mn_ticket_signature1
        del self.signatures_dict["mn2"][non_registered_mn_pastelid1]

        #       c.a.10 fail if MN PastelID is personal
        self.signatures_dict["mn2"][self.artist_pastelid1] = self.top_mn_ticket_signature1
        del self.signatures_dict["mn2"][self.top_mn_pastelid1]
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("MN2 PastelID is NOT masternode PastelID" in self.errorString, True)
        self.signatures_dict["mn2"][self.top_mn_pastelid1] = self.top_mn_ticket_signature1
        del self.signatures_dict["mn2"][self.artist_pastelid1]

        #       c.a.8 fail if MN1, MN2 and MN3 are not from top 10 list at the ticket's blocknum
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.not_top_mns_signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("was NOT in the top masternodes list for block" in self.errorString, True)

        #       c.a.9 fail if MN1, MN2 and MN3 are the same
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.same_mns_signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("MNs PastelIDs can not be the same" in self.errorString, True)

        # check royalty max value 20
        self.create_art_ticket_and_signatures(self.artist_pastelid1, self.non_mn3,
                                              "HIJKLMNOP", "ABCDEFG", self.total_copies,
                                              25, self.nonmn7_green_address1,
                                              True)
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Royalty can't be 25 per cent, Max is 20 per cent" in self.errorString, True)

        # check royalty negative value -5
        self.create_art_ticket_and_signatures(self.artist_pastelid1, self.non_mn3,
                                              "HIJKLMNOP", "ABCDEFG", self.total_copies,
                                              -5, self.nonmn7_green_address1,
                                              True)
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        # uint16_t -> 2 ^ 16 -> 65536 - 5 = 65531
        assert_equal("Royalty can't be 65531 per cent, Max is 20 per cent" in self.errorString, True)

        # check "wrong_green_address"
        self.create_art_ticket_and_signatures(self.artist_pastelid1, self.non_mn3,
                                              "HIJKLMNOP", "ABCDEFG", self.total_copies,
                                              20, "wrong_green_address",
                                              True)
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The Green NFT address [wrong_green_address] is invalid" in self.errorString, True)

        # c.a.6 register without errors, if enough coins for tnx fee
        self.create_art_ticket_and_signatures(self.artist_pastelid1, self.non_mn3,
                                              "HIJKLMNOP", "ABCDEFG", self.total_copies,
                                              self.royalty, self.nonmn7_green_address1,
                                              True)

        coins_before = self.nodes[self.top_mns_index0].getbalance()
        # print(coins_before)

        self.art_ticket1_txid = self.nodes[self.top_mns_index0].tickets("register", "art",
                                                                        self.ticket, json.dumps(self.signatures_dict),
                                                                        self.top_mn_pastelid0, self.passphrase,
                                                                        key1, key2, str(self.storage_fee))["txid"]
        assert_true(self.art_ticket1_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()
        print(self.nodes[self.top_mns_index0].getblockcount())

        #       c.a.7 check correct amount of change and correct amount spent
        coins_after = self.nodes[self.top_mns_index0].getbalance()
        # print(coins_after)
        print(f"art registration ticket price - {self.art_ticket_price}")
        assert_equal(coins_after, coins_before-self.art_ticket_price)  # no fee yet, but ticket cost art ticket price

        #       c.a.8 fail if already registered
        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, "newkey", str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("This Art is already registered in blockchain"
                     in self.errorString, True)

        try:
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    "newkey", key2, str(self.storage_fee))
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("This Art is already registered in blockchain"
                     in self.errorString, True)

        #   c.b find registration ticket
        #       c.b.1 by artists PastelID (this is MultiValue key)
        # TODO Pastel:

        #       c.b.2 by hash (key1 for now)
        art_ticket1_1 = self.nodes[self.non_mn3].tickets("find", "art", key1)
        assert_equal(art_ticket1_1['ticket']['type'], "art-reg")
        assert_equal(art_ticket1_1['ticket']['art_ticket'], self.ticket)
        assert_equal(art_ticket1_1["ticket"]["key1"], key1)
        assert_equal(art_ticket1_1["ticket"]["key2"], key2)
        assert_equal(art_ticket1_1["ticket"]["artist_height"], self.artist_ticket_height)
        assert_equal(art_ticket1_1["ticket"]["total_copies"], self.total_copies)
        assert_equal(art_ticket1_1["ticket"]["storage_fee"], self.storage_fee)
        assert_equal(art_ticket1_1["ticket"]["royalty"], self.royalty)
        if self.royalty > 0:
            assert_equal(art_ticket1_1["ticket"]["royalty_address"], self.nonmn3_address1)
        else:
            assert(len(art_ticket1_1["ticket"]["royalty_address"]) == 0)
        assert_equal(art_ticket1_1["ticket"]["green"], 2)
        assert_equal(art_ticket1_1["ticket"]["green_address"], self.nonmn7_green_address1)
        assert_equal(art_ticket1_1["ticket"]["signatures"]["artist"][self.artist_pastelid1],
                     self.ticket_signature_artist)
        assert_equal(art_ticket1_1["ticket"]["signatures"]["mn2"][self.top_mn_pastelid1], self.top_mn_ticket_signature1)
        assert_equal(art_ticket1_1["ticket"]["signatures"]["mn3"][self.top_mn_pastelid2], self.top_mn_ticket_signature2)

        #       c.b.3 by fingerprints, compare to ticket from c.b.2 (key2 for now)
        art_ticket1_2 = self.nodes[self.non_mn3].tickets("find", "art", key2)
        assert_equal(art_ticket1_2['ticket']['type'], "art-reg")
        assert_equal(art_ticket1_2['ticket']['art_ticket'], self.ticket)
        assert_equal(art_ticket1_2["ticket"]["key1"], key1)
        assert_equal(art_ticket1_2["ticket"]["key2"], key2)
        assert_equal(art_ticket1_2["ticket"]["artist_height"], self.artist_ticket_height)
        assert_equal(art_ticket1_2["ticket"]["total_copies"], self.total_copies)
        assert_equal(art_ticket1_2["ticket"]["storage_fee"], self.storage_fee)
        assert_equal(art_ticket1_2["ticket"]["royalty"], self.royalty)
        if self.royalty > 0:
            assert_equal(art_ticket1_2["ticket"]["royalty_address"], self.nonmn3_address1)
        else:
            assert(len(art_ticket1_2["ticket"]["royalty_address"]) == 0)
        assert_equal(art_ticket1_2["ticket"]["green"], 2)
        assert_equal(art_ticket1_2["ticket"]["green_address"], self.nonmn7_green_address1)
        assert_equal(art_ticket1_2["ticket"]["signatures"]["artist"][self.artist_pastelid1],
                     art_ticket1_1["ticket"]["signatures"]["artist"][self.artist_pastelid1])

        #   c.c get the same ticket by txid from c.a.6 and compare with ticket from c.b.2
        art_ticket1_3 = self.nodes[self.non_mn3].tickets("get", self.art_ticket1_txid)
        assert_equal(art_ticket1_3["ticket"]["signatures"]["artist"][self.artist_pastelid1],
                     art_ticket1_1["ticket"]["signatures"]["artist"][self.artist_pastelid1])

        #   c.d list all art registration tickets, check PastelIDs
        art_tickets_list = self.nodes[self.top_mns_index0].tickets("list", "art")
        f1 = False
        f2 = False
        for t in art_tickets_list:
            if key1 == t["ticket"]["key1"]:
                f1 = True
            if key2 == t["ticket"]["key2"]:
                f2 = True
        assert_true(f1)
        assert_true(f2)

        art_tickets_by_pid = self.nodes[self.top_mns_index0].tickets("find", "art", self.artist_pastelid1)
        print(self.top_mn_pastelid0)
        print(art_tickets_by_pid)

        self.royalty_address = self.nonmn3_address1

        print("Art registration tickets tested")

    # ===============================================================================================================
    def artroyalty_null_ticket_tests(self):
        print("== Art royalty null tickets test ==")

        assert_equal(self.royalty, 0)

        # not enough confirmations
        print(self.nodes[self.non_mn3].getblockcount())
        try:
            self.nodes[self.non_mn3].tickets("register", "royalty",
                                             self.art_ticket1_txid, "new_pastelid1", self.artist_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Royalty ticket can be created only after" in self.errorString, True)
        self.__wait_for_gen10_blocks()
        print(self.nodes[self.non_mn3].getblockcount())

        try:
            self.nodes[self.non_mn3].tickets("register", "royalty",
                                             self.art_ticket1_txid, "new_pastelid1", self.artist_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The art Reg ticket with txid [" + self.art_ticket1_txid + "] has no royalty" in self.errorString, True)

        print("Art royalty null tickets tested")

    # ===============================================================================================================
    def artroyalty_ticket_tests(self, nonmn_id, old_pastelid1, new_pastelid1, new_address1, num):
        print(f"== Art royalty tickets test {num} ==")

        # fail if wrong PastelID
        try:
            self.nodes[nonmn_id].tickets("register", "royalty",
                                         self.art_ticket1_txid, new_pastelid1, self.top_mn_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(ERR_READ_PASTELID_FILE in self.errorString, True)

        # fail if wrong passphrase
        try:
            self.nodes[nonmn_id].tickets("register", "royalty",
                                         self.art_ticket1_txid, new_pastelid1, old_pastelid1, "wrong")
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(ERR_READ_PASTELID_FILE in self.errorString, True)

        # fail if there is not ArtTicket with this txid
        try:
            self.nodes[nonmn_id].tickets("register", "royalty",
                                         self.mn0_ticket1_txid, new_pastelid1, old_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The art ticket with this txid ["+self.mn0_ticket1_txid +
                     "] referred by this Royalty ticket is not in the blockchain" in self.errorString, True)

        assert(num == 1 or num == 2)
        if num == 1:
            assert_equal(nonmn_id, self.non_mn3)
            assert_equal(old_pastelid1, self.artist_pastelid1)
            assert_equal(new_pastelid1, self.nonmn5_royalty_pastelid1)
            assert_equal(new_address1, self.nonmn5_royalty_address1)

            # not enough confirmations
            print(self.nodes[nonmn_id].getblockcount())
            try:
                self.nodes[nonmn_id].tickets("register", "royalty",
                                             self.art_ticket1_txid, new_pastelid1, self.artist_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Royalty ticket can be created only after" in self.errorString, True)
            self.__wait_for_gen10_blocks()
            print(self.nodes[nonmn_id].getblockcount())

            # fail if artist's PastelID is not matching artist's PastelID in the registration ticket
            try:
                self.nodes[nonmn_id].tickets("register", "royalty",
                                             self.art_ticket1_txid, new_pastelid1, self.nonmn3_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("is not matching the Artist's PastelID" in self.errorString, True)
        else:
            assert_equal(nonmn_id, self.non_mn5)
            assert_equal(old_pastelid1, self.nonmn5_royalty_pastelid1)
            assert_equal(new_pastelid1, self.nonmn6_royalty_pastelid1)
            assert_equal(new_address1, self.nonmn6_royalty_address1)

            # fail if is not matching current PastelID of the royalty payee
            non_registered_pastelid1 = self.create_pastelid(self.non_mn5)
            try:
                self.nodes[nonmn_id].tickets("register", "royalty",
                                             self.art_ticket1_txid, new_pastelid1, non_registered_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("The PastelID [" + non_registered_pastelid1 + "] is not matching the PastelID [" +
                         self.nonmn5_royalty_pastelid1 + "] in the Change Royalty ticket with art txid [" +
                         self.art_ticket1_txid + "]" in self.errorString, True)

        try:
            self.nodes[nonmn_id].tickets("register", "royalty",
                                         self.art_ticket1_txid, "", old_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The Change Royalty ticket new_pastelID is empty" in self.errorString, True)

        try:
            self.nodes[nonmn_id].tickets("register", "royalty",
                                         self.art_ticket1_txid, old_pastelid1, old_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The Change Royalty ticket new_pastelID is equal to current pastelID" in self.errorString, True)

        coins_before = self.nodes[nonmn_id].getbalance()

        art_royalty_txid = self.nodes[nonmn_id].tickets("register", "royalty",
                                                        self.art_ticket1_txid, new_pastelid1,
                                                        old_pastelid1, self.passphrase)["txid"]
        assert_true(art_royalty_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        # fail if already registered
        try:
            self.nodes[nonmn_id].tickets("register", "royalty",
                                         self.art_ticket1_txid, new_pastelid1, old_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The PastelID [" + old_pastelid1 + "] is not matching the PastelID [" +
                     new_pastelid1 + "] in the Change Royalty ticket with art txid [" +
                     self.art_ticket1_txid + "]" in self.errorString, True)

        coins_after = self.nodes[nonmn_id].getbalance()
        print(f"coins before - {coins_before}")
        print(f"coins after - {coins_after}")
        assert_equal(coins_before - coins_after, 10)

        # from another node - get ticket transaction and check
        #   - amounts is totaling 10PSL
        art_ticket1_royalty_ticket_hash = self.nodes[0].getrawtransaction(art_royalty_txid)
        art_ticket1_royalty_ticket_tx = self.nodes[0].decoderawtransaction(art_ticket1_royalty_ticket_hash)
        fee_amount = 0

        for v in art_ticket1_royalty_ticket_tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                fee_amount += v["value"]
        assert_equal(fee_amount, 10)

        # find ticket by pastelID
        art_ticket1_royalty_ticket_1 = self.nodes[self.non_mn1].tickets("find", "royalty", old_pastelid1)
        assert_equal(art_ticket1_royalty_ticket_1[0]["ticket"]['type'], "art-royalty")
        assert_equal(art_ticket1_royalty_ticket_1[0]["ticket"]['pastelID'], old_pastelid1)
        assert_equal(art_ticket1_royalty_ticket_1[0]["ticket"]['new_pastelID'], new_pastelid1)
        assert_equal(art_ticket1_royalty_ticket_1[0]["ticket"]['art_txid'], self.art_ticket1_txid)
        assert_equal(art_ticket1_royalty_ticket_1[0]['txid'], art_royalty_txid)

        # get the same ticket by txid and compare with ticket found by pastelID
        art_ticket1_royalty_ticket_2 = self.nodes[self.non_mn1].tickets("get", art_royalty_txid)
        assert_equal(art_ticket1_royalty_ticket_2["ticket"]["signature"],
                     art_ticket1_royalty_ticket_1[0]["ticket"]["signature"])

        # list all art royalty tickets, check PastelIDs
        royalty_tickets_list = self.nodes[0].tickets("list", "royalty")
        f1 = False
        f2 = False
        for t in royalty_tickets_list:
            if art_royalty_txid == t["txid"]:
                f1 = True
                f2 = (self.art_ticket1_txid == t["ticket"]["art_txid"])
        assert_true(f1)
        assert_true(f2)

        self.royalty_address = new_address1

        print(f"Art royalty tickets {num} tested")

    # ===============================================================================================================
    def artact_ticket_tests(self, skip_low_coins_tests):
        print("== Art activation Tickets test ==")
        # d. art activation ticket
        #   d.a register art activation ticket (self.art_ticket1_txid; self.storage_fee; self.artist_ticket_height)
        #       d.a.1 fail if wrong PastelID
        try:
            self.nodes[self.non_mn3].tickets("register", "act",
                                             self.art_ticket1_txid, str(self.artist_ticket_height),
                                             str(self.storage_fee), self.top_mn_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(ERR_READ_PASTELID_FILE in self.errorString, True)
        # TODO Pastel: provide better error for unknown PastelID

        #       d.a.2 fail if wrong passphrase
        try:
            self.nodes[self.non_mn3].tickets("register", "act",
                                             self.art_ticket1_txid, str(self.artist_ticket_height),
                                             str(self.storage_fee), self.artist_pastelid1, "wrong")
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(ERR_READ_PASTELID_FILE in self.errorString, True)
        # TODO Pastel: provide better error for wrong passphrase

        #       d.a.7 fail if not enough coins to pay 90% of registration price (from artReg ticket) (90) + tnx fee (act ticket price)
        # print(self.nodes[self.non_mn3].getbalance())
        if not skip_low_coins_tests:
            try:
                self.nodes[self.non_mn3].tickets("register", "act",
                                                 self.art_ticket1_txid, str(self.artist_ticket_height),
                                                 str(self.storage_fee), self.artist_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Not enough coins to cover price [100]" in self.errorString, True)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, str(self.collateral), "", "", False)
        self.__wait_for_sync_all10()

        #       d.a.3 fail if there is not ArtTicket with this txid
        try:
            self.nodes[self.non_mn3].tickets("register", "act",
                                             self.mn0_ticket1_txid, str(self.artist_ticket_height),
                                             str(self.storage_fee), self.artist_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The art ticket with this txid ["+self.mn0_ticket1_txid +
                     "] referred by this Activation ticket is not in the blockchain" in self.errorString, True)

        if not self.royalty_tickets_tests and not self.royalty_null_tests:
            # not enough confirmations
            print(self.nodes[self.non_mn3].getblockcount())
            try:
                self.nodes[self.non_mn3].tickets("register", "act",
                                                 self.art_ticket1_txid, str(self.artist_ticket_height),
                                                 str(self.storage_fee), self.artist_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Activation ticket can be created only after" in self.errorString, True)
            self.__wait_for_gen10_blocks()
            print(self.nodes[self.non_mn3].getblockcount())

        #       d.a.4 fail if artist's PastelID in the activation ticket
        #       is not matching artist's PastelID in the registration ticket
        try:
            self.nodes[self.non_mn3].tickets("register", "act",
                                             self.art_ticket1_txid, str(self.artist_ticket_height),
                                             str(self.storage_fee), self.nonmn3_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("is not matching the Artist's PastelID" in self.errorString, True)

        #       d.a.5 fail if wrong artist ticket height
        try:
            self.nodes[self.non_mn3].tickets("register", "act",
                                             self.art_ticket1_txid, "55", str(self.storage_fee),
                                             self.artist_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("is not matching the artistHeight" in self.errorString, True)

        #       d.a.6 fail if wrong storage fee
        try:
            self.nodes[self.non_mn3].tickets("register", "act",
                                             self.art_ticket1_txid, str(self.artist_ticket_height), "55",
                                             self.artist_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("is not matching the storage fee" in self.errorString, True)

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

        self.art_ticket1_act_ticket_txid = self.nodes[self.non_mn3].tickets("register", "act",
                                                                            self.art_ticket1_txid,
                                                                            str(self.artist_ticket_height),
                                                                            str(self.storage_fee),
                                                                            self.artist_pastelid1, self.passphrase)["txid"]
        assert_true(self.art_ticket1_act_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        #       d.a.9 check correct amount of change and correct amount spent and correct amount of fee paid
        main_mn_fee = self.storage_fee90percent*3/5
        other_mn_fee = self.storage_fee90percent/5

        coins_after = self.nodes[self.non_mn3].getbalance()
        print(f"activation ticket price - {self.act_ticket_price}")
        assert_equal(coins_after, coins_before-Decimal(self.storage_fee90percent)-Decimal(self.act_ticket_price))  # no fee yet, but ticket cost act ticket price

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
        try:
            self.nodes[self.non_mn3].tickets("register", "act",
                                             self.art_ticket1_txid, str(self.artist_ticket_height),
                                             str(self.storage_fee),
                                             self.artist_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The Activation ticket for the Registration ticket with txid ["+self.art_ticket1_txid +
                     "] is already exist" in self.errorString, True)

        #       d.a.11 from another node - get ticket transaction and check
        #           - there are 3 outputs to MN1, MN2 and MN3 with correct amounts
        #               (MN1: 60%; MN2, MN3: 20% each, of registration price)
        #           - amounts is totaling 10PSL
        art_ticket1_act_ticket_hash = self.nodes[0].getrawtransaction(self.art_ticket1_act_ticket_txid)
        art_ticket1_act_ticket_tx = self.nodes[0].decoderawtransaction(art_ticket1_act_ticket_hash)
        amount = 0
        fee_amount = 0

        for v in art_ticket1_act_ticket_tx["vout"]:
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
        print(f"activation ticket price - {self.act_ticket_price}")
        assert_equal(fee_amount, self.act_ticket_price)

        #   d.b find activation ticket
        #       d.b.1 by artists PastelID (this is MultiValue key)
        #       TODO Pastel: find activation ticket by artists PastelID (this is MultiValue key)

        #       d.b.3 by Registration height - artist_height from registration ticket (this is MultiValue key)
        #       TODO Pastel: find activation ticket by Registration height -
        #        artist_height from registration ticket (this is MultiValue key)

        #       d.b.2 by Registration txid - reg_txid from registration ticket, compare to ticket from d.b.2
        art_ticket1_act_ticket_1 = self.nodes[self.non_mn1].tickets("find", "act", self.art_ticket1_txid)
        assert_equal(art_ticket1_act_ticket_1['ticket']['type'], "art-act")
        assert_equal(art_ticket1_act_ticket_1['ticket']['pastelID'], self.artist_pastelid1)
        assert_equal(art_ticket1_act_ticket_1['ticket']['reg_txid'], self.art_ticket1_txid)
        assert_equal(art_ticket1_act_ticket_1['ticket']['artist_height'], self.artist_ticket_height)
        assert_equal(art_ticket1_act_ticket_1['ticket']['storage_fee'], self.storage_fee)
        assert_equal(art_ticket1_act_ticket_1['txid'], self.art_ticket1_act_ticket_txid)

        #   d.c get the same ticket by txid from d.a.8 and compare with ticket from d.b.2
        art_ticket1_act_ticket_2 = self.nodes[self.non_mn1].tickets("get", self.art_ticket1_act_ticket_txid)
        assert_equal(art_ticket1_act_ticket_2["ticket"]["signature"], art_ticket1_act_ticket_1["ticket"]["signature"])

        #   d.d list all art registration tickets, check PastelIDs
        act_tickets_list = self.nodes[0].tickets("list", "act")
        f1 = False
        for t in act_tickets_list:
            if self.art_ticket1_txid == t["ticket"]["reg_txid"]:
                f1 = True
        assert_true(f1)

        art_tickets_by_pid = self.nodes[self.top_mns_index0].tickets("find", "act", self.artist_pastelid1)
        print(self.top_mn_pastelid0)
        print(art_tickets_by_pid)
        art_tickets_by_height = self.nodes[self.top_mns_index0].tickets("find", "act", str(self.artist_ticket_height))
        print(self.artist_ticket_height)
        print(art_tickets_by_height)

        print("Art activation tickets tested")

    # ===============================================================================================================
    def artsell_ticket_tests1(self, skip_some_tests):
        print("== Art sell Tickets test (selling original Art ticket) ==")
        # tickets register sell art_txid price PastelID passphrase valid_after valid_before
        #

        # 1. fail if not enough coins to pay tnx fee (2% from price - 2M from 100M)
        if not skip_some_tests:
            try:
                self.nodes[self.non_mn3].tickets("register", "sell",
                                                 self.art_ticket1_act_ticket_txid, str("100000000"),
                                                 self.artist_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Not enough coins to cover price [2000000]" in self.errorString, True)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 5000, "", "", False)
        self.__wait_for_sync_all(1)
        coins_before = self.nodes[self.non_mn3].getbalance()
        print(coins_before)

        # 2. Check there is Activation ticket with this artTnxId
        try:
            self.nodes[self.non_mn3].tickets("register", "sell",
                                             self.art_ticket1_txid, str("100000"),
                                             self.artist_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The activation or trade ticket with this txid ["+self.art_ticket1_txid +
                     "] referred by this Sell ticket is not in the blockchain" in self.errorString, True)

        #  not enough confirmations
        print(self.nodes[self.non_mn3].getblockcount())
        try:
            self.nodes[self.non_mn3].tickets("register", "sell",
                                             self.art_ticket1_act_ticket_txid, str("100000"),
                                             self.artist_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Sell ticket can be created only after" in self.errorString, True)
        self.__wait_for_gen10_blocks()
        print(self.nodes[self.non_mn3].getblockcount())

        # 2. check PastelID in this ticket matches PastelID in the referred Activation ticket
        try:
            self.nodes[self.non_mn3].tickets("register", "sell",
                                             self.art_ticket1_act_ticket_txid, str("100000"),
                                             self.nonmn3_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The PastelID ["+self.nonmn3_pastelid1 +
                     "] in this ticket is not matching the Artist's PastelID [" +
                     self.artist_pastelid1+"] in the Art Activation ticket with this txid [" +
                     self.art_ticket1_act_ticket_txid+"]" in self.errorString, True)

        # 3. Fail if asked price is 0
        try:
            self.nodes[self.non_mn3].tickets("register", "sell",
                                             self.art_ticket1_act_ticket_txid, str(0),
                                             self.artist_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The asked price for Sell ticket with NFT txid [" + self.art_ticket1_act_ticket_txid + "] "
                     "should be not 0" in self.errorString, True)

        # 4. Create Sell ticket
        self.art_ticket1_sell_ticket_txid = \
            self.nodes[self.non_mn3].tickets("register", "sell",
                                             self.art_ticket1_act_ticket_txid,
                                             str("100000"),
                                             self.artist_pastelid1, self.passphrase)["txid"]
        assert_true(self.art_ticket1_sell_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        # 5. check correct amount of change and correct amount spent
        coins_after = self.nodes[self.non_mn3].getbalance()
        print(coins_after)
        assert_equal(coins_after, coins_before-2000)  # ticket cost price/50 PSL (100000/50=2000)

        # 6. find Sell ticket
        #   6.1 by art's transaction and index
        sell_ticket1_1 = self.nodes[self.non_mn3].tickets("find", "sell", self.art_ticket1_act_ticket_txid+":1")
        assert_equal(sell_ticket1_1['ticket']['type'], "art-sell")
        assert_equal(sell_ticket1_1['ticket']['art_txid'], self.art_ticket1_act_ticket_txid)
        assert_equal(sell_ticket1_1["ticket"]["asked_price"], 100000)

        #   6.2 by artists PastelID (this is MultiValue key)
        sell_tickets_list1 = self.nodes[self.non_mn3].tickets("find", "sell", self.artist_pastelid1)
        found_ticket = False
        for ticket in sell_tickets_list1:
            if ticket['ticket']['art_txid'] == self.art_ticket1_act_ticket_txid \
                    and ticket["ticket"]["asked_price"] == 100000:
                found_ticket = True
            assert_equal(ticket['ticket']['type'], "art-sell")
        assert_true(found_ticket)

        #   6.3 by art's transaction (this is MultiValue key)
        sell_tickets_list2 = self.nodes[self.non_mn3].tickets("find", "sell", self.art_ticket1_act_ticket_txid)
        found_ticket = False
        for ticket in sell_tickets_list2:
            if ticket['ticket']['art_txid'] == self.art_ticket1_act_ticket_txid \
                    and ticket["ticket"]["asked_price"] == 100000:
                found_ticket = True
            assert_equal(ticket['ticket']['type'], "art-sell")
        assert_true(found_ticket)

        #   6.4 get the same ticket by txid from c.a.6 and compare with ticket from c.b.2
        sell_ticket1_2 = self.nodes[self.non_mn3].tickets("get", self.art_ticket1_sell_ticket_txid)
        assert_equal(sell_ticket1_2["ticket"]["art_txid"], sell_ticket1_1["ticket"]["art_txid"])
        assert_equal(sell_ticket1_2["ticket"]["asked_price"], sell_ticket1_1["ticket"]["asked_price"])

        # 7. list all sell tickets
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell")
        f1 = False
        f2 = False
        for t in tickets_list:
            if self.art_ticket1_act_ticket_txid == t["ticket"]["art_txid"]:
                f1 = True
            if "1" == str(t["ticket"]["copy_number"]):
                f2 = True
        assert_true(f1)
        assert_true(f2)

        # 8. from another node - get ticket transaction and check
        #           - there are P2MS outputs with non-zero amounts
        #           - amounts is totaling price/50 PSL (100000/50=200)
        sell_ticket1_tx_hash = self.nodes[self.non_mn1].getrawtransaction(self.art_ticket1_sell_ticket_txid)
        sell_ticket1_tx = self.nodes[self.non_mn1].decoderawtransaction(sell_ticket1_tx_hash)
        amount = 0
        for v in sell_ticket1_tx["vout"]:
            assert_greater_than(v["value"], 0)
            if v["scriptPubKey"]["type"] == "multisig":
                amount += v["value"]
        assert_equal(amount, 2000)

        print("Art sell tickets tested (first run)")

    # ===============================================================================================================
    def artbuy_ticket_tests(self, skip_low_coins_tests):
        print("== Art buy Tickets test (buying original Art ticket) ==")

        self.nonmn4_address1 = self.nodes[self.non_mn4].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 2000, "", "", False)
        self.__wait_for_sync_all10()

        self.nonmn4_pastelid1 = self.create_pastelid(self.non_mn4)
        self.nodes[self.non_mn4].tickets("register", "id", self.nonmn4_pastelid1, self.passphrase, self.nonmn4_address1)

        # fail if not enough funds
        # price (100K) and tnx fee(1% from price - 1K from 100K) = 101000
        coins_before = self.nodes[self.non_mn4].getbalance()
        if not skip_low_coins_tests:
            print(coins_before)
            try:
                self.nodes[self.non_mn4].tickets("register", "buy",
                                                 self.art_ticket1_sell_ticket_txid, str("100000"),
                                                 self.nonmn4_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Not enough coins to cover price [101000]" in self.errorString, True)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 100010, "", "", False)
        self.__wait_for_sync_all(1)
        coins_before = self.nodes[self.non_mn4].getbalance()
        print(coins_before)

        # Check there is Sell ticket with this sellTnxId
        try:
            self.nodes[self.non_mn4].tickets("register", "buy",
                                             self.art_ticket1_act_ticket_txid, str("100000"),
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The sell ticket with this txid ["+self.art_ticket1_act_ticket_txid +
                     "] referred by this Buy ticket is not in the blockchain" in self.errorString, True)

        # fail if not enough confirmations
        print(self.nodes[self.non_mn4].getblockcount())
        try:
            self.nodes[self.non_mn4].tickets("register", "buy",
                                             self.art_ticket1_sell_ticket_txid, str("100000"),
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
                                             self.art_ticket1_sell_ticket_txid, str("100"),
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The offered price [100] is less than asked in the sell ticket [100000]" in self.errorString, True)

        # Create buy ticket
        self.art_ticket1_buy_ticket_txid = \
            self.nodes[self.non_mn4].tickets("register", "buy",
                                             self.art_ticket1_sell_ticket_txid, str("100000"),
                                             self.nonmn4_pastelid1, self.passphrase)["txid"]
        assert_true(self.art_ticket1_buy_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        # check correct amount of change and correct amount spent
        coins_after = self.nodes[self.non_mn4].getbalance()
        print(coins_after)
        assert_equal(coins_after, coins_before-1000)  # ticket cost price/100 PSL (100000/100=1000)

        # fail if there is another buy ticket referring to that sell ticket
        try:
            self.nodes[self.non_mn4].tickets("register", "buy",
                                             self.art_ticket1_sell_ticket_txid, str("100000"),
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Buy ticket [" + self.art_ticket1_buy_ticket_txid + "] already exists and is not yet 1h old "
                     "for this sell ticket [" + self.art_ticket1_sell_ticket_txid + "]" in self.errorString, True)

        print("Art buy tickets tested")

    # ===============================================================================================================
    def arttrade_ticket_tests(self, skip_low_coins_tests):
        print("== Art trade Tickets test (trading original Art ticket) ==")

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
                                                 self.art_ticket1_sell_ticket_txid, self.art_ticket1_buy_ticket_txid,
                                                 self.nonmn4_pastelid1, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Not enough coins to cover price [100010]" in self.errorString, True)

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 101000, "", "", False)
        self.__wait_for_sync_all(1)
        coins_before = math.floor(self.nodes[self.non_mn4].getbalance())
        print(coins_before)

        # Check there is Sell ticket with this sellTnxId
        try:
            self.nodes[self.non_mn4].tickets("register", "trade",
                                             self.art_ticket1_buy_ticket_txid, self.art_ticket1_buy_ticket_txid,
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The ticket with this txid ["+self.art_ticket1_buy_ticket_txid+"] is not in the blockchain"
                     in self.errorString, True)
        # This error is from CArtTradeTicket::Create where it tries to get Sell ticket to get price and artTxId

        # Check there is Buy ticket with this buyTnxId
        try:
            self.nodes[self.non_mn4].tickets("register", "trade",
                                             self.art_ticket1_sell_ticket_txid, self.art_ticket1_sell_ticket_txid,
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("The buy ticket with this txid ["+self.art_ticket1_sell_ticket_txid +
                     "] referred by this Trade ticket is not in the blockchain" in self.errorString, True)
        # This error is from CArtTradeTicket::IsValid -> common_validation

        # fail if not enough confirmations after buy ticket
        print(self.nodes[self.non_mn4].getblockcount())
        try:
            self.nodes[self.non_mn4].tickets("register", "trade",
                                             self.art_ticket1_sell_ticket_txid, self.art_ticket1_buy_ticket_txid,
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Trade ticket can be created only after" in self.errorString, True)
        self.__wait_for_gen10_blocks()
        print(self.nodes[self.non_mn4].getblockcount())

        sellers_pastel_id = self.nodes[self.non_mn3].tickets("get", self.art_ticket1_sell_ticket_txid)["ticket"]["pastelID"]
        print(sellers_pastel_id)
        sellers_address = self.nodes[self.non_mn3].tickets("find", "id", sellers_pastel_id)["ticket"]["address"]
        print(sellers_address)
        sellers_coins_before = math.floor(self.nodes[self.non_mn3].getreceivedbyaddress(sellers_address))

        # consolidate funds into single address
        balance = self.nodes[self.non_mn4].getbalance()
        consaddress = self.nodes[self.non_mn4].getnewaddress()
        self.nodes[self.non_mn4].sendtoaddress(consaddress, balance, "", "", True)

        # Create trade ticket
        self.art_ticket1_trade_ticket_txid = \
            self.nodes[self.non_mn4].tickets("register", "trade",
                                             self.art_ticket1_sell_ticket_txid, self.art_ticket1_buy_ticket_txid,
                                             self.nonmn4_pastelid1, self.passphrase)["txid"]
        assert_true(self.art_ticket1_trade_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        # check correct amount of change and correct amount spent
        coins_after = math.floor(self.nodes[self.non_mn4].getbalance())
        print(coins_before)
        print(coins_after)
        print(f"trade ticket price - {self.trade_ticket_price}")
        assert_equal(coins_after, coins_before-self.trade_ticket_price-100000)  # ticket cost is trade ticket price, art cost is 100000

        # check seller gets correct amount
        sellers_coins_after = math.floor(self.nodes[self.non_mn3].getreceivedbyaddress(sellers_address))
        sellers_coins_expected_to_receive = 100000
        royalty_coins_expected_fee = 0
        green_coins_expected_fee = 0
        if self.royalty > 0:
            royalty_coins_expected_fee = 100000 * self.royalty / 100
            sellers_coins_expected_to_receive -= royalty_coins_expected_fee
        if self.is_green:
            green_coins_expected_fee = 2000
            sellers_coins_expected_to_receive -= green_coins_expected_fee
        print(sellers_coins_before)
        print(sellers_coins_after)
        assert_equal(sellers_coins_after - sellers_coins_before, sellers_coins_expected_to_receive)

        # from another node - get ticket transaction and check
        #   - there are 3 posiible outputs to seller, royalty and green adresses
        art_ticket1_trade_ticket_hash = self.nodes[0].getrawtransaction(self.art_ticket1_trade_ticket_txid)
        art_ticket1_trade_ticket_tx = self.nodes[0].decoderawtransaction(art_ticket1_trade_ticket_hash)
        sellers_coins = 0
        royalty_coins = 0
        green_coins = 0
        multi_coins = 0

        for v in art_ticket1_trade_ticket_tx["vout"]:
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
                if v["scriptPubKey"]["addresses"][0] == self.nonmn7_green_address1 and self.is_green:
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
                                             self.art_ticket1_sell_ticket_txid, self.art_ticket1_buy_ticket_txid,
                                             self.nonmn4_pastelid1, self.passphrase)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("There is already exist trade ticket for the sell ticket with this txid [" +
                     self.art_ticket1_sell_ticket_txid+"]" in self.errorString, True)

        print("Art trade tickets tested")

    # ===============================================================================================================
    def sell_buy_trade_tests(self):
        print("== Art sell Tickets test (selling, buying and trading Trade ticket) ==")

        self.slow_mine(12, 10, 2, 0.5)
        time.sleep(2)
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 100000, "", "", False)
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 100000, "", "", False)
        time.sleep(2)

        # now there is 1 Trade ticket and it is non-sold
        new_trade_ticket = self.sell_buy_trade_test("T1", self.non_mn4, self.nonmn4_pastelid1,
                                                    self.non_mn3, self.nonmn3_pastelid1,
                                                    self.art_ticket1_trade_ticket_txid, self.is_green)
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

        original_art_trade_tickets = []
        for i in range(1, 10):
            original_art_trade_tickets.append(self.sell_buy_trade_test(f"A{i}",
                                                                       self.non_mn3, self.artist_pastelid1,
                                                                       self.non_mn4, self.nonmn4_pastelid1,
                                                                       self.art_ticket1_act_ticket_txid,
                                                                       self.is_green, True)
                                              )
        # now there are 15 Trade ticket and 5 of it's sold

        self.sell_buy_trade_test("A10-Fail", self.non_mn3, self.artist_pastelid1,
                                 self.non_mn4, self.nonmn4_pastelid1,
                                 self.art_ticket1_act_ticket_txid, self.is_green, True, True)

    # ===============================================================================================================
    def sell_buy_trade_test(self, test_num, seller_node, seller_pastelid,
                            buyer_node, buyer_pastelid, art_to_sell_txid,
                            is_green, skip_last_fail_test=False, will_fail=False):
        print(f"===== Test {test_num} : {seller_node} sells and {buyer_node} buys =====")
        self.print_heights()

        self.slow_mine(2, 10, 2, 0.5)

        if will_fail:
            print(f"===== Test {test_num} should fail =====")
            try:
                self.nodes[seller_node].tickets("register", "sell", art_to_sell_txid, str("1000"),
                                                seller_pastelid, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("The Art you are trying to sell - from registration ticket ["+art_to_sell_txid +
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
        sell_ticket_txid = self.nodes[seller_node].tickets("register", "sell", art_to_sell_txid, str("1000"),
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

        self.__wait_for_ticket_tnx()

        buyer_coins_after = self.nodes[buyer_node].getbalance()
        seller_coins_after = self.nodes[seller_node].getbalance()
        print("buyer_coins_after: " + str(buyer_coins_after))
        print("seller_coins_after: " + str(seller_coins_after))

        # check correct amount of change and correct amount spent
        print(f"trade ticket price - {self.trade_ticket_price}")
        assert_equal(buyer_coins_after, buyer_coins_before-10-self.trade_ticket_price-1000)
        # buy ticket cost is 10 (1000/100), trade ticket cost is self.trade_ticket_price, art cost is 1000

        # check seller gets correct amount
        royalty_fee = 0
        green_fee = 0
        if self.royalty > 0:
            royalty_fee = 70
        if is_green:
            green_fee = 20
        assert_equal(seller_coins_after, seller_coins_before + 1000 - 20 - royalty_fee - green_fee)
        # sell ticket cost is 20 (1000/50), art cost is 1000

        if not skip_last_fail_test:
            # 6. Verify we cannot sell already sold trade ticket
            #  Verify there is no already trade ticket referring to trade ticket we are trying to sell
            try:
                self.nodes[seller_node].tickets("register", "sell", art_to_sell_txid, str("1000"),
                                                seller_pastelid, self.passphrase)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("The Art you are trying to sell - from trade ticket ["+art_to_sell_txid+"] - is already sold"
                         in self.errorString, True)

        print("Art sell tickets tested (second run)")

        return trade_ticket_txid

    # ===============================================================================================================
    def tickets_list_filter_tests(self, loop_number):
        print("== Tickets List Filter test ==")

        number_personal_nodes = 3
        number_personal_nodes += self.royalty_tickets_tests
        if self.is_green:
            number_personal_nodes += 1

        tickets_list = self.nodes[self.non_mn3].tickets("list", "id")
        assert_equal(len(tickets_list), 12 + number_personal_nodes + loop_number * 2)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id", "all")
        assert_equal(len(tickets_list), 12 + number_personal_nodes + loop_number * 2)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id", "mn")
        assert_equal(len(tickets_list), 12)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "id", "personal")
        assert_equal(len(tickets_list), number_personal_nodes + loop_number * 2)

        self.create_art_ticket_and_signatures(self.artist_pastelid1, self.non_mn3,
                                              "HashOfTicket2", "Ticket2", 5,
                                              self.royalty, self.nonmn7_green_address1,
                                              False)
        art_ticket2_txid = self.nodes[self.top_mns_index0].tickets("register", "art",
                                                                   self.ticket, json.dumps(self.signatures_dict),
                                                                   self.top_mn_pastelid0, self.passphrase,
                                                                   "key3"+str(loop_number), "key4"+str(loop_number), str(self.storage_fee))["txid"]
        assert_true(art_ticket2_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        self.slow_mine(2, 10, 2, 0.5)

        art_ticket2_act_ticket_txid = self.nodes[self.non_mn3].tickets("register", "act",
                                                                       art_ticket2_txid,
                                                                       str(self.artist_ticket_height),
                                                                       str(self.storage_fee),
                                                                       self.artist_pastelid1, self.passphrase)["txid"]
        assert_true(art_ticket2_act_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        self.create_art_ticket_and_signatures(self.artist_pastelid1, self.non_mn3,
                                              "HashOfTicket3", "Ticket3", 1,
                                              self.royalty, self.nonmn7_green_address1,
                                              False)
        art_ticket3_txid = self.nodes[self.top_mns_index0].tickets("register", "art",
                                                                   self.ticket, json.dumps(self.signatures_dict),
                                                                   self.top_mn_pastelid0, self.passphrase,
                                                                   "key5"+str(loop_number), "key6"+str(loop_number), str(self.storage_fee))["txid"]
        assert_true(art_ticket3_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        self.slow_mine(2, 10, 2, 0.5)

        tickets_list = self.nodes[self.non_mn3].tickets("list", "art")
        assert_equal(len(tickets_list), 3*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "art", "all")
        assert_equal(len(tickets_list), 3*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "art", "active")
        assert_equal(len(tickets_list), 2*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "art", "inactive")
        assert_equal(len(tickets_list), 1*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "art", "sold")
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
        sell_ticket1_txid = self.nodes[self.non_mn3].tickets("register", "sell", art_ticket2_act_ticket_txid,
                                                             str("1000"),
                                                             self.artist_pastelid1, self.passphrase,
                                                             cur_block+15, cur_block+20)["txid"]
        assert_true(sell_ticket1_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()  # cur+5 block
        print(sell_ticket1_txid)

        sell_ticket2_txid = self.nodes[self.non_mn3].tickets("register", "sell", art_ticket2_act_ticket_txid,
                                                             str("1000"),
                                                             self.artist_pastelid1, self.passphrase,
                                                             cur_block+20, cur_block+30)["txid"]
        assert_true(sell_ticket2_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()  # cur+10 block
        print(sell_ticket2_txid)

        sell_ticket3_txid = self.nodes[self.non_mn3].tickets("register", "sell", art_ticket2_act_ticket_txid,
                                                             str("1000"),
                                                             self.artist_pastelid1, self.passphrase,
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

        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", self.artist_pastelid1)
        assert_equal(len(tickets_list), 2*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "all", self.artist_pastelid1)
        assert_equal(len(tickets_list), 2*(loop_number+1))
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "available", self.artist_pastelid1)
        assert_equal(tickets_list is None, True)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "unavailable", self.artist_pastelid1)
        assert_equal(tickets_list is None, True)
        tickets_list = self.nodes[self.non_mn3].tickets("list", "sell", "expired", self.artist_pastelid1)
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
    def username_ticket_tests(self):
        print("== Username Tickets test ==")

        # New nonmn4_pastelid2 to test some functionalities
        self.nonmn4_address2 = self.nodes[self.non_mn4].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address2, 2000, "", "", False)
        self.__wait_for_sync_all10()
        self.nonmn4_pastelid2 = self.create_pastelid(self.non_mn4)

        self.nonmn8_address1 = self.nodes[self.non_mn8].getnewaddress()
        self.__wait_for_sync_all10()
        self.nonmn8_pastelid1 = self.create_pastelid(self.non_mn8)

        # Register first time by PastelID of non-masternode 3
        tickets_username_txid1 = self.nodes[self.non_mn3].tickets("register", "username", "bsmith84",
                                                    self.artist_pastelid1, "passphrase")
        self.__wait_for_ticket_tnx()
        nonmn3_ticket_username_1 = self.nodes[self.non_mn4].tickets("get", tickets_username_txid1["txid"])
        print(nonmn3_ticket_username_1)

        self.__wait_for_ticket_tnx()
        assert_equal(nonmn3_ticket_username_1["ticket"]["pastelID"], self.artist_pastelid1)
        assert_equal(nonmn3_ticket_username_1["ticket"]["username"], "bsmith84")
        assert_equal(nonmn3_ticket_username_1["ticket"]["fee"], 100)

        # Register second time with same name by non-masternode 4. Expect to get Exception that the ticket is invalid because the username is registered
        try:
            self.nodes[self.non_mn4].tickets("register", "username", "bsmith84",
                                                        self.nonmn4_pastelid2, "passphrase")
            self.__wait_for_ticket_tnx()

        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(self.errorString , "Ticket (username-change) is invalid - This Username is already registered in blockchain [Username = bsmith84]")

        # Register by a new pastelID. Expect to get Exception that the ticket is invalid because there are Not enough 100 PSL to cover price 100
        try:
            self.nodes[self.non_mn8].tickets("register", "username", "anicename5",
                                                        self.nonmn8_pastelid1, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(self.errorString, "Ticket (username-change) is invalid - Not enough coins to cover price [100]")

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn8_address1, 200, "", "", False)
        self.__wait_for_sync_all10()

        # This should be success
        self.nodes[self.non_mn8].tickets("register", "username", "anicename5",
                                                        self.nonmn8_pastelid1, "passphrase")
        self.__wait_for_ticket_tnx()

        # Expect to get Exception that the ticket is invalid because this PastelID do not have enough 5000PSL to pay the rechange fee
        try:
            self.nodes[self.non_mn8].tickets("register", "username", "Banksy",
                                                    self.nonmn8_pastelid1, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal(self.errorString, "Ticket (username-change) is invalid - Not enough coins to cover price [5000]")

        # Send money to non-masternode3 to cover 5000 price
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 5100, "", "", False)
        self.__wait_for_sync_all10()

        # Expect to get Exception that the ticket is invalid because this PastelID changed Username last 24 hours
        try:
            self.nodes[self.non_mn3].tickets("register", "username", "Banksy",
                                                    self.artist_pastelid1, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Ticket (username-change) is invalid - Username Change ticket is invalid. Already changed in last 24 hours." in self.errorString, True)

        # Wait till next 24 hours. Below test cases is commented because it took lots of time to complete.
        # To test this functionality on local machine, we should lower the waiting from 24 * 24 blocks to smaller value, ex: 15 blocks only.
        self.sync_all()
        print("Mining 577 blocks")
        for ind in range (577):
            self.nodes[self.mining_node_num].generate(1)
            time.sleep(1)

        print("Waiting 60 seconds")
        time.sleep(60)

        # Expect that nonmn3 can change username to "Banksky" after 24 hours, fee should be 5000
        tickets_username_txid1 = self.nodes[self.non_mn3].tickets("register", "username", "Banksy",
                                                    self.artist_pastelid1, "passphrase")
        self.__wait_for_ticket_tnx()
        nonmn3_ticket_username_1 = self.nodes[self.non_mn4].tickets("get", tickets_username_txid1["txid"])
        print(nonmn3_ticket_username_1)

        self.__wait_for_ticket_tnx()
        assert_equal(nonmn3_ticket_username_1["ticket"]["pastelID"], self.artist_pastelid1)
        assert_equal(nonmn3_ticket_username_1["ticket"]["username"], "Banksy")
        assert_equal(nonmn3_ticket_username_1["ticket"]["fee"], 5000)

        # Expect that nonmn4 can register new Username "bsmith84", because the username "bsmith84" no longer belong to self.artist_pastelid1
        tickets_username_2 = self.nodes[self.non_mn4].tickets("register", "username", "bsmith84",
                                                    self.nonmn4_pastelid2, "passphrase")
        self.__wait_for_ticket_tnx()
        nonmn4_ticket_username_1 = self.nodes[self.non_mn4].tickets("get", tickets_username_2["txid"])
        print(nonmn4_ticket_username_1)

        self.__wait_for_ticket_tnx()
        assert_equal(nonmn4_ticket_username_1["ticket"]["pastelID"], self.nonmn4_pastelid2)
        assert_equal(nonmn4_ticket_username_1["ticket"]["username"], "bsmith84")
        assert_equal(nonmn4_ticket_username_1["ticket"]["fee"], 100)

        # Register by a new pastelID with invalid name. Expect to get Exception that the usernamename is invalid
        try:
            tickets_username_txid1 = self.nodes[self.non_mn4].tickets("register", "username", "abc",
                                                        self.nonmn4_pastelid2, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Ticket (username-change) is invalid - Invalid size of username, the size should have at least 4 characters, and at most 12 characters" in self.errorString, True)

        try:
            tickets_username_txid1 = self.nodes[self.non_mn4].tickets("register", "username", "1a4bc",
                                                        self.nonmn4_pastelid2, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Invalid username, should start with a letter A-Z or a-z only" in self.errorString, True)

        try:
            tickets_username_txid1 = self.nodes[self.non_mn4].tickets("register", "username", "a#bcsa",
                                                        self.nonmn4_pastelid2, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Invalid username, should contains letters A-Z a-z, or digits 0-9 only" in self.errorString, True)

        try:
            tickets_username_txid1 = self.nodes[self.non_mn4].tickets("register", "username", "abdagoc",
                                                        self.nonmn4_pastelid2, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Invalid username, should NOT contains swear, racist... words" in self.errorString, True)

        try:
            tickets_username_txid1 = self.nodes[self.non_mn4].tickets("register", "username", "abDaGoc",
                                                        self.nonmn4_pastelid2, "passphrase")
            self.__wait_for_ticket_tnx()
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Invalid username, should NOT contains swear, racist... words" in self.errorString, True)

        # length not valid
        non_mn1_bad_username_length = self.nodes[self.non_mn4].tickets("tools", "validateusername", "abc")
        print(non_mn1_bad_username_length)
        assert_equal(non_mn1_bad_username_length["isBad"], True)
        assert_equal(non_mn1_bad_username_length["validationError"], "Invalid size of username, the size should have at least 4 characters, and at most 12 characters")
        # not start with a letter A-Z a-z
        non_mn1_bad_username_start = self.nodes[self.non_mn4].tickets("tools", "validateusername", "1a4bc")
        print(non_mn1_bad_username_start)
        assert_equal(non_mn1_bad_username_start["isBad"], True)
        assert_equal(non_mn1_bad_username_start["validationError"], "Invalid username, should start with a letter A-Z or a-z only")
        # contains characters that is different than A-Z a-z 0-9
        non_mn1_bad_username_character = self.nodes[self.non_mn4].tickets("tools", "validateusername", "a#bcsa")
        print(non_mn1_bad_username_character)
        assert_equal(non_mn1_bad_username_character["isBad"], True)
        assert_equal(non_mn1_bad_username_character["validationError"], "Invalid username, should contains letters A-Z a-z, or digits 0-9 only")
        # contains bad word
        non_mn1_bad_username_bad = self.nodes[self.non_mn4].tickets("tools", "validateusername", "abdagoc")
        print(non_mn1_bad_username_bad)
        assert_equal(non_mn1_bad_username_bad["isBad"], True)
        assert_equal(non_mn1_bad_username_bad["validationError"], "Invalid username, should NOT contains swear, racist... words")
        # contains bad word with some upper case
        non_mn1_bad_username_bad = self.nodes[self.non_mn4].tickets("tools", "validateusername", "abDaGoc")
        print(non_mn1_bad_username_bad)
        assert_equal(non_mn1_bad_username_bad["isBad"], True)
        assert_equal(non_mn1_bad_username_bad["validationError"], "Invalid username, should NOT contains swear, racist... words")
        # username is registered
        non_mn1_bad_username_registered = self.nodes[self.non_mn4].tickets("tools", "validateusername", "bsmith84")
        print(non_mn1_bad_username_registered)
        assert_equal(non_mn1_bad_username_registered["isBad"], True)
        assert_equal(non_mn1_bad_username_registered["validationError"], "Username is not valid, it is already registered")

        # good username
        non_mn1_bad_username_good = self.nodes[self.non_mn4].tickets("tools", "validateusername", "goodname")
        print(non_mn1_bad_username_good)
        assert_equal(non_mn1_bad_username_good["isBad"], False)
        assert_equal(non_mn1_bad_username_good["validationError"], "")

        print("Username tickets tested")

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