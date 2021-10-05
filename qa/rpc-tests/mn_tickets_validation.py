#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.util import assert_equal, assert_greater_than, \
    assert_true, initialize_chain_clean, str_to_b64str
from mn_common import MasterNodeCommon
from test_framework.authproxy import JSONRPCException
import json
import time

from decimal import getcontext
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
    number_of_simple_nodes = 3
    total_number_of_nodes = number_of_master_nodes+number_of_simple_nodes

    non_mn1 = number_of_master_nodes        # mining node - will have coins #13
    non_mn2 = number_of_master_nodes+1      # hot node - will have collateral for all active MN #14
    non_mn3 = number_of_master_nodes+2      # will not have coins by default #15

    mining_node_num = number_of_master_nodes    # same as non_mn1
    hot_node_num = number_of_master_nodes+1     # same as non_mn2

    def __init__(self):
        self.errorString = ""
        self.is_network_split = False
        self.nodes = []
        self.storage_fee = 100
        self.storage_fee90percent = self.storage_fee*9/10

        self.mn_addresses = None
        self.mn_pastelids = None
        self.mn_outpoints = None
        self.mn_ticket_signatures = None

        self.nonmn1_pastelid1 = None
        self.creator_pastelid1 = None
        self.non_mn1_pastelid_txid = None

        self.top_mns_index0 = None
        self.top_mns_index1 = None
        self.top_mns_index2 = None
        self.top_mn_pastelid0 = None
        self.top_mn_pastelid1 = None
        self.top_mn_pastelid2 = None
        self.top_mn_ticket_signature0 = None
        self.top_mn_ticket_signature1 = None
        self.top_mn_ticket_signature2 = None
        self.signatures_dict = None

        self.not_top_mns_index0 = None
        self.not_top_mns_index1 = None
        self.not_top_mns_index2 = None
        self.not_top_mn_pastelid0 = None
        self.not_top_mn_pastelid1 = None
        self.not_top_mn_pastelid2 = None
        self.not_top_mn_ticket_signature0 = None
        self.not_top_mn_ticket_signature1 = None
        self.not_top_mn_ticket_signature2 = None
        self.not_top_mns_signatures_dict = None

        self.ticket = None
        self.creator_ticket_height = None
        self.nft_ticket1_txid = None


    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)

    def setup_network(self, split=False):
        self.setup_masternodes_network(private_keys_list, self.number_of_simple_nodes)

    def run_test(self):

        self.mining_enough(self.mining_node_num, self.number_of_master_nodes)
        cold_nodes = {k: v for k, v in enumerate(private_keys_list)}  # all but last!!!
        _, _, _ = self.start_mn(self.mining_node_num, self.hot_node_num, cold_nodes, self.total_number_of_nodes)

        self.reconnect_nodes(0, self.number_of_master_nodes)
        self.sync_all()

        self.mn0_address1 = self.nodes[0].getnewaddress()
        self.nonmn3_address1 = self.nodes[self.non_mn3].getnewaddress()

        self.nodes[self.mining_node_num].sendtoaddress(self.mn0_address1, self.collateral, "", "", False)
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, self.collateral, "", "", False)
        time.sleep(2)
        self.sync_all()
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10,30)

        #  These tests checks ability of the network to reject illegitimate tickets
        self.fake_pastelid_tnx_tests()
        self.fake_nftreg_tnx_tests()
        self.fake_nftact_tnx_tests()
        self.fake_nftsell_tnx_tests1()
        self.fake_nftbuy_tnx_tests()
        self.fake_nfttrade_tnx_tests()
        self.fake_nftsell_tnx_tests2()

    def fake_pastelid_tnx_tests(self):
        print("== Pastelid ticket transaction validation test ==")

        mn0_pastelid1 = self.create_pastelid(0)
        nonmn3_pastelid1 = self.create_pastelid(self.non_mn3)

        # makefaketicket mnid pastelID passphrase ticketPrice bChangeSignature
        # makefaketicket id pastelID passphrase address ticketPrice bChangeSignature

        tickets = {
            #  reject if outpoint is already registered

            #  reject if fake outpoint (Unknown Masternode) (Verb = 3 - will modify outpoint to make it invalid)
            "mnid1-fakeout": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, "passphrase", "10", "3"),

            #  reject if the PastelID ticket signature is invalid (Verb = 1 - will modify pastlelid signature to make it invalid)
            "mnid2-psig": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, "passphrase", "10", "1"),
            "id1-psig": self.nodes[self.non_mn3].tickets("makefaketicket", "id", nonmn3_pastelid1, "passphrase", self.nonmn3_address1, "10", "1"),

            #  reject if MN PastelID's MN signature is invalid (Verb = 2 - will modify MN signature to make it invalid)
            "mnid3-mnsig": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, "passphrase", "10", "2"),

            #  reject if doesn't pay correct ticket registration fee (10PSL)
            "mnid4-regfee": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, "passphrase", "1", "0"),
            "id2-regfee": self.nodes[self.non_mn3].tickets("makefaketicket", "id", nonmn3_pastelid1, "passphrase", self.nonmn3_address1, "1", "0")
        }

        for n, t in tickets.items():
            try:
                self.nodes[0].sendrawtransaction(t)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(n + ": " + self.errorString)
            assert_equal("bad-tx-invalid-ticket" in self.errorString, True)

        print("== Pastelid ticket transaction validation tested ==")

    def fake_nftreg_tnx_tests(self):
        print("== NFT Registration ticket transaction validation test ==")

        self.mn_addresses = {}
        self.mn_pastelids = {}
        self.mn_outpoints = {}
        self.mn_ticket_signatures = {}

        # generate pastelIDs
        self.nonmn1_pastelid1 = self.create_pastelid(self.non_mn1)
        self.creator_pastelid1 = self.create_pastelid(self.non_mn3)
        for n in range(0, 13):
            self.mn_addresses[n] = self.nodes[n].getnewaddress()
            self.nodes[self.mining_node_num].sendtoaddress(self.mn_addresses[n], 100, "", "", False)
            self.mn_pastelids[n] = self.create_pastelid(n)
            self.mn_outpoints[self.nodes[n].masternode("status")["outpoint"]] = n

        self.sync_all(10,30)
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10,30)

        # register pastelIDs
        nonmn1_address1 = self.nodes[self.non_mn1].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(nonmn1_address1, 100)
        self.sync_all()
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all()

        # nonmn3_address1 = self.nodes[self.non_mn3].getnewaddress()
        self.non_mn1_pastelid_txid = self.nodes[self.non_mn1].tickets("register", "id", self.nonmn1_pastelid1, "passphrase", nonmn1_address1)["txid"]
        self.nodes[self.non_mn3].tickets("register", "id", self.creator_pastelid1, "passphrase", self.nonmn3_address1)
        for n in range(0, 13):
            self.nodes[n].tickets("register", "mnid", self.mn_pastelids[n], "passphrase")

        self.sync_all(10,30)
        self.nodes[self.mining_node_num].generate(5)
        self.sync_all(10,30)

        self.total_copies = 10
        # Get current top MNs at Node 0
        self.creator_ticket_height = self.nodes[0].getinfo()["blocks"]
        top_masternodes = self.nodes[0].masternode("top")[str(self.creator_ticket_height)]

        json_ticket = {
            "nft_ticket_version": 1,
            "author": self.creator_pastelid1,
            "blocknum": self.creator_ticket_height,
            "data_hash": "ABCDEFG",
            "copies": self.total_copies,
            "royalty": 0.1,
            "green": True,
            "app_ticket": "HIJKLMNOP"}

        self.ticket = str_to_b64str(json.dumps(json_ticket))
        print(self.ticket)

        # create ticket signature
        ticket_signature_nonmn1 = self.nodes[self.non_mn1].pastelid("sign", self.ticket, self.nonmn1_pastelid1, "passphrase")["signature"]
        ticket_signature_creator = self.nodes[self.non_mn3].pastelid("sign", self.ticket, self.creator_pastelid1, "passphrase")["signature"]
        for n in range(0, 13):
            self.mn_ticket_signatures[n] = self.nodes[n].pastelid("sign", self.ticket, self.mn_pastelids[n], "passphrase")["signature"]

        top_mns_indexes = set()
        for mn in top_masternodes:
            index = self.mn_outpoints[mn["outpoint"]]
            top_mns_indexes.add(index)
        not_top_mns_indexes = set(self.mn_outpoints.values()) ^ top_mns_indexes

        self.top_mns_index0 = list(top_mns_indexes)[0]
        self.top_mns_index1 = list(top_mns_indexes)[1]
        self.top_mns_index2 = list(top_mns_indexes)[2]
        self.top_mn_pastelid0 = self.mn_pastelids[self.top_mns_index0]
        self.top_mn_pastelid1 = self.mn_pastelids[self.top_mns_index1]
        self.top_mn_pastelid2 = self.mn_pastelids[self.top_mns_index2]
        self.top_mn_ticket_signature0 = self.mn_ticket_signatures[self.top_mns_index0]
        self.top_mn_ticket_signature1 = self.mn_ticket_signatures[self.top_mns_index1]
        self.top_mn_ticket_signature2 = self.mn_ticket_signatures[self.top_mns_index2]
    
        self.signatures_dict = dict(
            {
                "creator": {self.creator_pastelid1: ticket_signature_creator},
                "mn2": {self.top_mn_pastelid1: self.top_mn_ticket_signature1},
                "mn3": {self.top_mn_pastelid2: self.top_mn_ticket_signature2},
            }
        )

        self.not_top_mns_index0 = list(not_top_mns_indexes)[0]
        self.not_top_mns_index1 = list(not_top_mns_indexes)[1]
        self.not_top_mns_index2 = list(not_top_mns_indexes)[2]
        self.not_top_mn_pastelid0 = self.mn_pastelids[self.not_top_mns_index0]
        self.not_top_mn_pastelid1 = self.mn_pastelids[self.not_top_mns_index1]
        self.not_top_mn_pastelid2 = self.mn_pastelids[self.not_top_mns_index2]
        self.not_top_mn_ticket_signature0 = self.mn_ticket_signatures[self.not_top_mns_index0]
        self.not_top_mn_ticket_signature1 = self.mn_ticket_signatures[self.not_top_mns_index1]
        self.not_top_mn_ticket_signature2 = self.mn_ticket_signatures[self.not_top_mns_index2]

        self.not_top_mns_signatures_dict = dict(
            {
                "creator": {self.creator_pastelid1: ticket_signature_creator},
                "mn2": {self.not_top_mn_pastelid1: self.not_top_mn_ticket_signature1},
                "mn3": {self.not_top_mn_pastelid2: self.not_top_mn_ticket_signature2},
            }
        )

        self.nodes[self.non_mn1].generate(10)

        # makefaketicket nft ticket signatures pastelID passphrase key1 key2 creatorheight nStorageFee ticketPrice bChangeSignature

        tickets = {
            #  non MN with real signatures of non top 10 MNs
            "nft1-non-mn123": self.nodes[self.non_mn1].tickets("makefaketicket", "nft",
                                                              self.ticket, json.dumps(self.not_top_mns_signatures_dict), self.nonmn1_pastelid1, "passphrase", "key1", "key2",
                                                              str(self.creator_ticket_height), str(self.storage_fee), "10", "0"),

            #  non MN with fake signatures of top 10 MNs
            "nft2-nonmn1-fake23": self.nodes[self.non_mn1].tickets("makefaketicket", "nft",
                                                                  self.ticket, json.dumps(self.signatures_dict), self.nonmn1_pastelid1, "passphrase", "key1", "key2",
                                                                  str(self.creator_ticket_height), str(self.storage_fee), "10", "1"),  # Verb = 1 - will modify pastlelid signature to make it invalid

            #  non top 10 MN with real signatures of non top 10 MNs
            "nft3-non-top-mn1-nonmn23": self.nodes[self.not_top_mns_index0].tickets("makefaketicket", "nft",
                                                                               self.ticket, json.dumps(self.not_top_mns_signatures_dict), self.not_top_mn_pastelid0, "passphrase", "key1", "key2",
                                                                               str(self.creator_ticket_height), str(self.storage_fee), "10", "0"),

            #  non top 10 MN with fake signatures of top 10 MNs
            "nft4-non-top-mn1-fake23": self.nodes[self.not_top_mns_index0].tickets("makefaketicket", "nft",
                                                                          self.ticket, json.dumps(self.signatures_dict), self.not_top_mn_pastelid0, "passphrase", "key1", "key2",
                                                                          str(self.creator_ticket_height), str(self.storage_fee), "10", "1"),  # Verb = 1 - will modify pastlelid signature to make it invalid

            #  top 10 MN with real signatures of non top 10 MNs
            "nft5-top-mn1-non-top-mn23": self.nodes[self.top_mns_index0].tickets("makefaketicket", "nft",
                                                                                self.ticket, json.dumps(self.not_top_mns_signatures_dict), self.top_mn_pastelid0, "passphrase", "key1", "key2",
                                                                                str(self.creator_ticket_height), str(self.storage_fee), "10", "0"),

            #  top 10 MN with fake signatures of top 10 MNs
            "nft6-top-mn1-fake23": self.nodes[self.top_mns_index0].tickets("makefaketicket", "nft",
                                                                          self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, "passphrase", "key1", "key2",
                                                                          str(self.creator_ticket_height), str(self.storage_fee), "10", "1"),  # Verb = 1 - will modify pastlelid signature to make it invalid

            #  good signatures of top 10 MNs, bad ticket fee
            "nft-top-mn1-bad-fee": self.nodes[self.top_mns_index0].tickets("makefaketicket", "nft",
                                                            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, "passphrase", "key1", "key2",
                                                            str(self.creator_ticket_height), str(self.storage_fee), "1", "0")
        }

        for n, t in tickets.items():
            try:
                self.nodes[0].sendrawtransaction(t)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(n + ": " + self.errorString)
            assert_equal("bad-tx-invalid-ticket" in self.errorString, True)

        print("== NFT Registration ticket transaction validation tested ==")

    def fake_nftact_tnx_tests(self):
        print("== NFT Registration Activation ticket transaction validation test ==")

        # valid ticket
        self.nft_ticket1_txid = self.nodes[self.top_mns_index0].tickets("register", "nft", self.ticket, json.dumps(self.signatures_dict),
                                                                        self.top_mn_pastelid0, "passphrase",
                                                                        "key1", "key2",
                                                                        str(self.storage_fee))["txid"]
        assert_true(self.nft_ticket1_txid, "No ticket was created")

        self.sync_all(10,30)
        self.nodes[self.mining_node_num].generate(5)
        self.sync_all(10,30)

        self.nodes[self.non_mn1].generate(10)

        mn0_fee = int(self.storage_fee90percent*0.6)
        mn1_fee = int(self.storage_fee90percent*0.2)
        mn2_fee = int(self.storage_fee90percent*0.2)

        # 3.c. validate activation ticket transaction
        # makefaketicket act regTicketTxID height fee pastelID KeyPass ticketPrice strVerb addresses1 ammount1 addresses2 ammount2 addresses3 ammount3
        # Correct ticketPrice for Act ticket is 10
        # Verb = 1 - will modify Act ticket's signature to make it invalid
        # Verb = 2 - will modify Act ticket's creator height
        tickets = {
            #       3.c.1 reject if there is no NFTTicket at txid referenced by this ticket
            "act1-no-nft-ticket": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                                                                  self.non_mn1_pastelid_txid, str(self.creator_ticket_height),
                                                                  str(self.storage_fee),
                                                                  self.creator_pastelid1, "passphrase",
                                                                  "10", "0"),

            #       3.c.2 reject if creator's PastelID in the activation ticket is not matching creator's PastelID in the registration ticket
            # This prevent someone else to create Act Ticket providing creator's PastelID, but wrong signature (only creator has private key to create correct signature)
            "act2-bad-creator_sign": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                                                                  self.nft_ticket1_txid, str(self.creator_ticket_height),
                                                                  str(self.storage_fee),
                                                                  self.creator_pastelid1, "passphrase",
                                                                  "10", "1"), # Verb = 1 - will modify Act ticket signature to make it invalid (non matching creator's PastelID)

            #       3.c.3 reject if wrong creator ticket height
            "act3-bad-creator-height": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                                                                  self.nft_ticket1_txid, str(self.creator_ticket_height),
                                                                  str(self.storage_fee),
                                                                  self.creator_pastelid1, "passphrase",
                                                                  "10", "2"),

            #       3.c.4 reject if doesn't pay ticket registration fee (10PSL)
            "act4-bad-reg_fee": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                                                                      self.nft_ticket1_txid, str(self.creator_ticket_height),
                                                                      str(self.storage_fee),
                                                                      self.creator_pastelid1, "passphrase",
                                                                      "0", "0"),

            #       3.c.5 reject if pay correct storage fee (90% = MN1(60%)+MN2(20%)+MN3(20%)) to wrong MNs
            "act5-bad-mns-addresses": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                                                                      self.nft_ticket1_txid, str(self.creator_ticket_height),
                                                                      str(self.storage_fee),
                                                                      self.creator_pastelid1, "passphrase",
                                                                      "10", "0",
                                                                      self.mn_addresses[self.not_top_mns_index0], str(mn0_fee),
                                                                      self.mn_addresses[self.not_top_mns_index1], str(mn1_fee),
                                                                      self.mn_addresses[self.not_top_mns_index2], str(mn2_fee)
                                                                      ),

            #       3.c.6 reject if pay wrong storage fee (90%=MN1(60%)+MN2(20%)+MN3(20%)), but to correct MNs
            "act6-bad-mns-fee": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                                                                      self.nft_ticket1_txid, str(self.creator_ticket_height),
                                                                      str(self.storage_fee),
                                                                      self.creator_pastelid1, "passphrase",
                                                                      "10", "0",
                                                                      self.mn_addresses[self.not_top_mns_index0], str(mn0_fee*10),
                                                                      self.mn_addresses[self.not_top_mns_index1], str(mn1_fee*10),
                                                                      self.mn_addresses[self.not_top_mns_index2], str(mn2_fee*10)
                                                                      ),
        }

        for n, t in tickets.items():
            try:
                self.nodes[0].sendrawtransaction(t)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(n + ": " + self.errorString)
            assert_equal("bad-tx-invalid-ticket" in self.errorString, True)

        print("== NFT Registration Activation ticket transaction validation tested ==")

    def fake_nftsell_tnx_tests1(self):
        print("== NFT Sell ticket transaction validation test (for activation ticket) ==")

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 200, "", "", False)
        time.sleep(2)
        self.sync_all(10, 30)
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10, 30)

        self.nft_ticket1_act_ticket_txid = self.nodes[self.non_mn3].tickets("register", "act", self.nft_ticket1_txid, str(self.creator_ticket_height),
                                                                            str(self.storage_fee),
                                                                            self.creator_pastelid1, "passphrase")["txid"]
        assert_true(self.nft_ticket1_act_ticket_txid, "No ticket was created")

        tickets = {
            # 1. check PastelID in this ticket matches PastelID in the referred Activation ticket
            "sell-bad-nfts-sign": self.nodes[self.non_mn3].tickets("makefaketicket", "sell",
                                                                    self.nft_ticket1_act_ticket_txid, "100000",
                                                                    self.creator_pastelid1, "passphrase",
                                                                    "0", "0",
                                                                    "10", "1"), # Verb = 1 - will modify Act ticket signature to make it invalid (non matchig creator's PastelID)
        }

        for n, t in tickets.items():
            try:
                self.nodes[0].sendrawtransaction(t)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(n + ": " + self.errorString)
            assert_equal("bad-tx-invalid-ticket" in self.errorString, True)

        print("== NFT Sell ticket transaction validation tested (for activation ticket) ==")

    def fake_nftbuy_tnx_tests(self):
        print("== NFT Buy ticket transaction validation test ==")

        print("== NFT Buy ticket transaction validation tested ==")

    def fake_nfttrade_tnx_tests(self):
        print("== NFT Trade ticket transaction validation test ==")

        print("== NFT Trade ticket transaction validation tested ==")

    def fake_nftsell_tnx_tests2(self):
        print("== NFT Sell ticket transaction validation test (for trade ticket) ==")
        # 1. check PastelID in this ticket matches PastelID in the referred Trade ticket
        # 2. Verify the NFT is not already sold
        #    Verify there is no already trade ticket referring to that trade ticket
        #    Verify the number of existing trade tickets less then number of copies in the registration ticket

        print("== NFT Sell ticket transaction validation tested (for trade ticket) ==")

if __name__ == '__main__':
    MasterNodeTicketsTest().main()
