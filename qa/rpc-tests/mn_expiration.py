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
    number_of_simple_nodes = 6
    total_number_of_nodes = number_of_master_nodes + number_of_simple_nodes

    non_active_mn = number_of_master_nodes - 1

    non_mn1 = number_of_master_nodes           # mining node - will have coins #13
    non_mn2 = number_of_master_nodes + 1       # hot node - will have collateral for all active MN #14
    non_mn3 = number_of_master_nodes + 2       # will not have coins by default #15
    non_mn4 = number_of_master_nodes + 3       # will not have coins by default #16
    non_mn5 = number_of_master_nodes + 4       # will not have coins by default #17
    non_mn6 = number_of_master_nodes + 5       # will not have coins by default #18

    mining_node_num = number_of_master_nodes   # same as non_mn1
    hot_node_num = number_of_master_nodes + 1  # same as non_mn2

    def __init__(self):
        self.errorString = ""
        self.is_network_split = False
        self.nodes = []
        self.storage_fee = 100

        self.mn_addresses = {}
        self.mn_pastelids = {}
        self.mn_outpoints = {}
        self.ticket = None
        self.signatures_dict = None

        self.nonmn3_pastelid1 = None
        self.nonmn4_pastelid1 = None
        self.nonmn5_pastelid1 = None
        self.nonmn6_pastelid1 = None

        self.nonmn3_address1 = None
        self.nonmn4_address1 = None
        self.nonmn5_address1 = None
        self.nonmn6_address1 = None
        self.artist_pastelid1 = None
        self.artist_ticket_height = None
        self.ticket_signature_artist = None
        self.top_mns_index0 = None
        self.top_mns_index1 = None
        self.top_mns_index2 = None
        self.top_mn_pastelid0 = None
        self.top_mn_pastelid1 = None
        self.top_mn_pastelid2 = None

        self.total_copies = 2
        self.art_copy_price = 1000
        self.id_ticket_price = 10
        self.art_ticket_price = 10
        self.act_ticket_price = 10
        self.trade_ticket_price = 10

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

        sell_ticket2_txid = None

        self.initialize()

        art_ticket_txid = self.register_nft_reg_ticket("key1", "key2")
        self.__wait_for_confirmation(self.non_mn3)

        act_ticket_txid = self.register_nft_act_ticket(art_ticket_txid)
        self.__wait_for_confirmation(self.non_mn3)

        sell_ticket1_txid = self.register_nft_sell_ticket(act_ticket_txid)
        sell_ticket_txid = sell_ticket1_txid
        print(f"sell ticket 1 txid {sell_ticket1_txid}")
        self.__wait_for_confirmation(self.non_mn3)

        if (self.total_copies == 1):
            # fail if not enough copies to sell
            try:
                self.register_nft_sell_ticket(act_ticket_txid)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Invalid Sell ticket - copy number [2] cannot exceed the total number "
                         "of available copies [1] or be 0" in self.errorString, True)

        # fail as the replace copy can be created after 5 days
        try:
            self.register_nft_sell_ticket(act_ticket_txid, 1)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Can only replace Sell ticket after 5 days. txid - [" + sell_ticket1_txid + "] "
                     "copyNumber [1]" in self.errorString, True)

        print("Generate 3000 blocks that is > 5 days. 1 chunk is 10 blocks")
        for ind in range (300):
            print(f"chunk - {ind + 1}")
            self.nodes[self.mining_node_num].generate(10)
            time.sleep(2)

        print("Waiting 300 seconds")
        time.sleep(300)
        self.__wait_for_sync_all()

        sell_ticket2_txid = self.register_nft_sell_ticket(act_ticket_txid, 1)
        sell_ticket_txid = sell_ticket2_txid
        print(f"sell ticket 2 txid {sell_ticket2_txid}")

        sell_ticket3_txid = None
        if (self.total_copies > 1):
            sell_ticket3_txid = self.register_nft_sell_ticket(act_ticket_txid)
            print(f"sell ticket 3 txid {sell_ticket3_txid}")

            sell_ticket1_1 = self.nodes[self.non_mn3].tickets("find", "sell", act_ticket_txid+":2")
            assert_equal(sell_ticket1_1['ticket']['type'], "art-sell")
            assert_equal(sell_ticket1_1['ticket']['art_txid'], act_ticket_txid)
            assert_equal(sell_ticket1_1["ticket"]["copy_number"], 2)

            sell_ticket1_2 = self.nodes[self.non_mn3].tickets("get", sell_ticket3_txid)
            assert_equal(sell_ticket1_2["ticket"]["art_txid"], sell_ticket1_1["ticket"]["art_txid"])
            assert_equal(sell_ticket1_2["ticket"]["copy_number"], sell_ticket1_1["ticket"]["copy_number"])

        self.__send_coins_to_buy(self.non_mn4, self.nonmn4_address1, 5)

        if sell_ticket2_txid:
            # fail if old sell ticket has been replaced
            try:
                self.register_nft_buy_ticket(self.non_mn4, self.nonmn4_pastelid1, sell_ticket1_txid)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("This Sell ticket has been replaced with another ticket. "
                         "txid - [" + sell_ticket2_txid + "] copyNumber [1]" in self.errorString, True)

        buy_ticket1_txid = self.register_nft_buy_ticket(self.non_mn4, self.nonmn4_pastelid1, sell_ticket_txid)
        buy_ticket_txid = buy_ticket1_txid

        # fail if there is another buy ticket1 created < 1 hour ago
        try:
            self.register_nft_buy_ticket(self.non_mn4, self.nonmn4_pastelid1, sell_ticket_txid)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Buy ticket [" + buy_ticket1_txid + "] already exists and is not yet 1h old "
                     "for this sell ticket [" + sell_ticket_txid + "]" in self.errorString, True)

        print("Generate 30 blocks that is > 1 hour. 1 chunk is 10 blocks")
        for ind in range (3):
            print(f"chunk - {ind + 1}")
            self.nodes[self.mining_node_num].generate(10)
            time.sleep(2)

        print("Waiting 180 seconds")
        time.sleep(180)
        self.__wait_for_sync_all()

        buy_ticket2_txid = self.register_nft_buy_ticket(self.non_mn4, self.nonmn4_pastelid1, sell_ticket_txid)
        buy_ticket_txid = buy_ticket2_txid

        # fail if there is another buy ticket2 created < 1 hour ago
        try:
            self.register_nft_buy_ticket(self.non_mn4, self.nonmn4_pastelid1, sell_ticket_txid)
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("Buy ticket [" + buy_ticket2_txid + "] already exists and is not yet 1h old "
                     "for this sell ticket [" + sell_ticket_txid + "]" in self.errorString, True)

        print("Generate 30 blocks that is > 1 hour. 1 chunk is 10 blocks")
        for ind in range (3):
            print(f"chunk - {ind + 1}")
            self.nodes[self.mining_node_num].generate(10)
            time.sleep(2)

        print("Waiting 180 seconds")
        time.sleep(180)
        self.__wait_for_sync_all()

        buy_ticket3_txid = self.register_nft_buy_ticket(self.non_mn4, self.nonmn4_pastelid1, sell_ticket_txid)
        buy_ticket_txid = buy_ticket3_txid

        # fail if old buy ticket1 has been replaced
        try:
            self.register_nft_trade_ticket(
                self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1,
                sell_ticket_txid, buy_ticket1_txid
            )
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("This Buy ticket has been replaced with another ticket. "
                     "txid - [" + buy_ticket_txid + "]" in self.errorString, True)

        # fail if old buy ticket2 has been replaced
        try:
            self.register_nft_trade_ticket(
                self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1,
                sell_ticket_txid, buy_ticket2_txid
            )
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("This Buy ticket has been replaced with another ticket. "
                     "txid - [" + buy_ticket_txid + "]" in self.errorString, True)

        trade_ticket1_txid = self.register_nft_trade_ticket(
            self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1,
            sell_ticket_txid, buy_ticket_txid
        )

        # fail if there is another trade ticket referring to that sell ticket
        try:
            self.register_nft_trade_ticket(
                self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1,
                sell_ticket_txid, buy_ticket_txid
            )
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("There is already exist trade ticket for the sell ticket with this txid [" +
                     sell_ticket_txid + "]" in self.errorString, True)

        if self.total_copies == 1:
            try:
                self.register_nft_sell_ticket(act_ticket_txid, 1)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("The Art you are trying to sell - from registration ticket [" +
                         act_ticket_txid + "] - is already sold - there are already [1] sold copies, "
                         "but only [1] copies were available" in self.errorString, True)
        elif self.total_copies == 2:
            # fail if not enough copies to sell
            try:
                self.register_nft_sell_ticket(act_ticket_txid)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Invalid Sell ticket - copy number [3] cannot exceed the total number "
                         "of available copies [2] or be 0" in self.errorString, True)

            try:
                self.register_nft_sell_ticket(act_ticket_txid, 1)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Cannot replace Sell ticket - it has been already sold. " +
                         "txid - [" + sell_ticket_txid + "] copyNumber [1]" in self.errorString, True)

            # fail as the replace copy can be created after 5 days
            try:
                self.register_nft_sell_ticket(act_ticket_txid, 2)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(self.errorString)
            assert_equal("Can only replace Sell ticket after 5 days. txid - [" + sell_ticket3_txid + "] "
                         "copyNumber [2]" in self.errorString, True)


    # ===============================================================================================================
    def initialize(self):
        print("== Initialize nodes ==")

        self.nodes[self.mining_node_num].generate(10)

        # generate pastelIDs & send coins
        self.artist_pastelid1 = self.create_pastelid(self.non_mn3)
        self.nonmn3_pastelid1 = self.create_pastelid(self.non_mn3)
        self.nonmn3_address1 = self.nodes[self.non_mn3].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 1000, "", "", False)

        self.nonmn4_pastelid1 = self.create_pastelid(self.non_mn4)
        self.nonmn4_address1 = self.nodes[self.non_mn4].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 100, "", "", False)

        self.nonmn5_pastelid1 = self.create_pastelid(self.non_mn5)
        self.nonmn5_address1 = self.nodes[self.non_mn5].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn5_address1, 100, "", "", False)

        for n in range(0, 12):
            self.mn_addresses[n] = self.nodes[n].getnewaddress()
            self.nodes[self.mining_node_num].sendtoaddress(self.mn_addresses[n], 100, "", "", False)
            self.mn_pastelids[n] = self.create_pastelid(n)
            self.mn_outpoints[self.nodes[n].masternode("status")["outpoint"]] = n

        self.__wait_for_sync_all()

        # register pastelIDs
        self.nodes[self.non_mn3].tickets("register", "id", self.artist_pastelid1, self.passphrase, self.nonmn3_address1)
        self.nodes[self.non_mn3].tickets("register", "id", self.nonmn3_pastelid1, self.passphrase, self.nonmn3_address1)
        self.nodes[self.non_mn4].tickets("register", "id", self.nonmn4_pastelid1, self.passphrase, self.nonmn4_address1)
        self.nodes[self.non_mn5].tickets("register", "id", self.nonmn5_pastelid1, self.passphrase, self.nonmn5_address1)
        for n in range(0, 12):
            self.nodes[n].tickets("register", "mnid", self.mn_pastelids[n], self.passphrase)

        self.__wait_for_sync_all(5)

    # ===============================================================================================================
    def create_nft_ticket_and_signatures(self, artist_pastelid, artist_node_num,
                                         app_ticket, data_hash, total_copies):
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
        #   "royalty": short,           // (not yet supported by cNode) how much artist should get on all future resales
        #   "green": string,            // address for Green NFT payment (not yet supported by cNode)
        #   "app_ticket": ...
        # }
        json_ticket = {
            "version": 1,
            "author": artist_pastelid,
            "blocknum": self.artist_ticket_height,
            "block_hash": data_hash,
            "copies": total_copies,
            "royalty": 0,
            "green": "",
            "app_ticket": app_ticket
        }
        self.ticket = str_to_b64str(json.dumps(json_ticket))
        print(f"ticket - {self.ticket}")

        # create ticket signature
        self.ticket_signature_artist = \
            self.nodes[artist_node_num].pastelid("sign", self.ticket, artist_pastelid, self.passphrase)["signature"]
        for n in range(0, 12):
            mn_ticket_signatures[n] = \
                self.nodes[n].pastelid("sign", self.ticket, self.mn_pastelids[n], self.passphrase)["signature"]
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

        print(f"top_mns_index0 - {self.top_mns_index0}")
        print(f"top_mn_pastelid0 - {self.top_mn_pastelid0}")

        self.signatures_dict = dict({
            "artist": {self.artist_pastelid1: self.ticket_signature_artist},
            "mn2":    {self.top_mn_pastelid1: mn_ticket_signatures[self.top_mns_index1]},
            "mn3":    {self.top_mn_pastelid2: mn_ticket_signatures[self.top_mns_index2]},
        })
        print(f"signatures_dict - {self.signatures_dict!r}")

    def register_nft_reg_ticket(self, key1, key2):
        print("== Create the NFT registration ticket ==")

        self.create_nft_ticket_and_signatures(self.artist_pastelid1, self.non_mn3,
                                              "HIJKLMNOP", "ABCDEFG", self.total_copies)
        art_ticket_txid = \
            self.nodes[self.top_mns_index0].tickets("register", "art",
                                                    self.ticket, json.dumps(self.signatures_dict),
                                                    self.top_mn_pastelid0, self.passphrase,
                                                    key1, key2, str(self.storage_fee))["txid"]
        assert_true(art_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()
        print(self.nodes[self.top_mns_index0].getblockcount())

        return art_ticket_txid

    # ===============================================================================================================
    def register_nft_act_ticket(self, art_ticket_txid):
        print("== Create the NFT activation ticket ==")

        act_ticket_txid = \
            self.nodes[self.non_mn3].tickets("register", "act", art_ticket_txid,
                                             str(self.artist_ticket_height), str(self.storage_fee),
                                             self.artist_pastelid1, self.passphrase)["txid"]
        assert_true(act_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        return act_ticket_txid

    # ===============================================================================================================
    def register_nft_sell_ticket(self, act_ticket_txid, copyNumber = 0):
        print("== Create the NFT sell ticket ==")

        sell_ticket_txid = \
            self.nodes[self.non_mn3].tickets("register", "sell", act_ticket_txid, str(self.art_copy_price),
                                             self.artist_pastelid1, self.passphrase, 0, 0, copyNumber)["txid"]
        assert_true(sell_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        return sell_ticket_txid

    # ===============================================================================================================
    def __send_coins_to_buy(self, node, address, num):
        cover_price = self.art_copy_price + max(10, int(self.art_copy_price / 100)) + 5

        self.nodes[self.mining_node_num].sendtoaddress(address, num * cover_price, "", "", False)
        self.__wait_for_confirmation(node)

    def register_nft_buy_ticket(self, buyer_node, buyer_pastelid1, sell_ticket_txid):
        print("== Create the NFT buy ticket ==")

        buy_ticket_txid = \
            self.nodes[buyer_node].tickets("register", "buy", sell_ticket_txid, str(self.art_copy_price),
                                           buyer_pastelid1, self.passphrase)["txid"]
        assert_true(buy_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        return buy_ticket_txid

    # ===============================================================================================================
    def register_nft_trade_ticket(self, seller_node, buyer_node, buyer_pastelid1, buyer_address1,
                                  sell_ticket_txid, buy_ticket_txid):
        print("== Create the NFT trade ticket ==")

        cover_price = self.art_copy_price + 10

        # sends coins back, keep 1 PSL to cover transaction fee
        self.__send_coins_back(self.non_mn4)
        self.__wait_for_sync_all10()

        coins_before = self.nodes[buyer_node].getbalance()
        print(coins_before)
        assert_true(coins_before > 0 and coins_before < 1)

        self.nodes[self.mining_node_num].sendtoaddress(buyer_address1, cover_price, "", "", False)
        self.__wait_for_confirmation(buyer_node)

        coins_before = self.nodes[buyer_node].getbalance()
        print(coins_before)

        sellers_pastel_id = self.nodes[seller_node].tickets("get", sell_ticket_txid)["ticket"]["pastelID"]
        print(sellers_pastel_id)
        sellers_address = self.nodes[seller_node].tickets("find", "id", sellers_pastel_id)["ticket"]["address"]
        print(sellers_address)
        artists_coins_before = self.nodes[seller_node].getreceivedbyaddress(sellers_address)

        # consolidate funds into single address
        consaddress = self.nodes[buyer_node].getnewaddress()
        self.nodes[buyer_node].sendtoaddress(consaddress, coins_before, "", "", True)

        trade_ticket_txid = \
            self.nodes[buyer_node].tickets("register", "trade", sell_ticket_txid, buy_ticket_txid,
                                           buyer_pastelid1, self.passphrase)["txid"]
        assert_true(trade_ticket_txid, "No ticket was created")
        self.__wait_for_ticket_tnx()

        # check correct amount of change and correct amount spent
        coins_after = self.nodes[buyer_node].getbalance()
        print(coins_before)
        print(coins_after)
        # ticket cost is trade ticket price, art cost is art_copy_price
        assert_true(math.isclose(coins_after, coins_before - self.trade_ticket_price - self.art_copy_price, rel_tol=0.005))

        # check seller gets correct amount
        artists_coins_after = self.nodes[self.non_mn3].getreceivedbyaddress(sellers_address)
        print(artists_coins_before)
        print(artists_coins_after)
        assert_true(math.isclose(artists_coins_after - artists_coins_before, self.art_copy_price, rel_tol=0.005))

        # from another node - get ticket transaction and check
        #   - there is 1 posiible output to seller
        trade_ticket_hash = self.nodes[0].getrawtransaction(trade_ticket_txid)
        trade_ticket_tx = self.nodes[0].decoderawtransaction(trade_ticket_hash)
        seller_amount = 0
        multi_fee = 0

        for v in trade_ticket_tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                multi_fee += v["value"]
            if v["scriptPubKey"]["type"] == "pubkeyhash":
                amount = v["value"]
                print(f"trade transiction pubkeyhash vout - {amount}")
                if v["scriptPubKey"]["addresses"][0] == sellers_address:
                    seller_amount = amount
                    print(f"trade transaction to seller's address - {amount}")
        print(f"trade transiction multisig fee_amount - {multi_fee}")
        assert_true(math.isclose(seller_amount, self.art_copy_price))
        assert_equal(multi_fee, self.id_ticket_price)

        return trade_ticket_txid

    def __send_coins_back(self, node):
        coins_after = self.nodes[node].getbalance()
        if coins_after > 1:
            mining_node_address1 = self.nodes[self.mining_node_num].getnewaddress()
            self.nodes[node].sendtoaddress(mining_node_address1, coins_after - 1, "", "", False)

    def __wait_for_ticket_tnx(self):
        time.sleep(10)
        for x in range(5):
            self.nodes[self.mining_node_num].generate(1)
            self.sync_all(10, 3)
        self.sync_all(10, 30)

    def __wait_for_sync_all(self, g = 1):
        time.sleep(2)
        self.sync_all()
        self.nodes[self.mining_node_num].generate(g)
        self.sync_all()

    def __wait_for_sync_all10(self):    
        time.sleep(2)
        self.sync_all(10, 30)
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10, 30)

    def __wait_for_confirmation(self, node):
        print(f"block count - {self.nodes[node].getblockcount()}")
        time.sleep(2)
        self.nodes[self.mining_node_num].generate(10)
        time.sleep(2)
        self.nodes[self.mining_node_num].generate(10)
        print(f"block count - {self.nodes[node].getblockcount()}")

if __name__ == '__main__':
    MasterNodeTicketsTest().main()
