#!/usr/bin/env python2
# Copyright (c) 2018-19 The Pastel developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from __future__ import print_function

from test_framework.util import assert_equal, assert_greater_than, \
    assert_true, initialize_chain_clean
from mn_common import MasterNodeCommon
from test_framework.authproxy import JSONRPCException
import json
import time
import base64

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
        self.fake_artreg_tnx_tests()
        self.fake_artact_tnx_tests()

    def fake_pastelid_tnx_tests(self):
        print("== Pastelid ticket transaction validation test ==")

        mn0_pastelid1 = self.nodes[0].pastelid("newkey", "passphrase")["pastelid"]
        assert_true(mn0_pastelid1, "No Pastelid was created")

        nonmn3_pastelid1 = self.nodes[self.non_mn3].pastelid("newkey", "passphrase")["pastelid"]
        assert_true(nonmn3_pastelid1, "No Pastelid was created")

        # makefaketicket mnid pastelID passphrase ticketPrice bChangeSignature
        # makefaketicket id pastelID passphrase address ticketPrice bChangeSignature

        tickets = {
            #  reject if outpoint is already registered

            #  reject if fake outpoint (Unknown Masternode) (Verb = 3 - will modify outpoint to make it invalid)
            "mnid-fakeout": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, "passphrase", "10", "3"),

            #  reject if the PastelID ticket signature is invalid (Verb = 1 - will modify pastlelid signature to make it invalid)
            "mnid-psig": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, "passphrase", "10", "1"),
            "id-psig": self.nodes[self.non_mn3].tickets("makefaketicket", "id", nonmn3_pastelid1, "passphrase", self.nonmn3_address1, "10", "1"),

            #  reject if MN PastelID's MN signature is invalid (Verb = 2 - will modify MN signature to make it invalid)
            "mnid-mnsig": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, "passphrase", "10", "2"),

            #  reject if doesn't pay correct ticket registration fee (10PSL)
            "mnid-regfee": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, "passphrase", "1", "0"),
            "id-regfee": self.nodes[self.non_mn3].tickets("makefaketicket", "id", nonmn3_pastelid1, "passphrase", self.nonmn3_address1, "1", "0")
        }

        for n, t in tickets.items():
            try:
                self.nodes[0].sendrawtransaction(t)
            except JSONRPCException, e:
                self.errorString = e.error['message']
                print(n + ": " + self.errorString)
            assert_equal("bad-tx-invalid-ticket" in self.errorString, True)

        print("== Pastelid ticket transaction validation tested ==")

    def fake_artreg_tnx_tests(self):
        print("== Art Registration ticket transaction validation test ==")

        mn_addresses = {}
        mn_pastelids = {}
        mn_outpoints = {}
        mn_ticket_signatures = {}

        # generate pastelIDs
        nonmn1_pastelid1 = self.nodes[self.non_mn1].pastelid("newkey", "passphrase")["pastelid"]
        artist_pastelid1 = self.nodes[self.non_mn3].pastelid("newkey", "passphrase")["pastelid"]
        for n in range(0, 13):
            mn_addresses[n] = self.nodes[n].getnewaddress()
            self.nodes[self.mining_node_num].sendtoaddress(mn_addresses[n], 100, "", "", False)
            mn_pastelids[n] = self.nodes[n].pastelid("newkey", "passphrase")["pastelid"]
            mn_outpoints[self.nodes[n].masternode("status")["outpoint"]] = n

        self.sync_all(10,30)
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10,30)

        # register pastelIDs
        nonmn1_address1 = self.nodes[self.non_mn1].getnewaddress()
        nonmn3_address1 = self.nodes[self.non_mn3].getnewaddress()
        self.nodes[self.non_mn1].tickets("register", "id", nonmn1_pastelid1, "passphrase", nonmn1_address1)
        self.nodes[self.non_mn3].tickets("register", "id", artist_pastelid1, "passphrase", nonmn3_address1)
        for n in range(0, 13):
            self.nodes[n].tickets("register", "mnid", mn_pastelids[n], "passphrase")

        self.sync_all(10,30)
        self.nodes[self.mining_node_num].generate(5)
        self.sync_all(10,30)

        # create ticket signature
        ticket_signature_nonmn1 = self.nodes[self.non_mn1].pastelid("sign", "12345", nonmn1_pastelid1, "passphrase")["signature"]
        ticket_signature_artist = self.nodes[self.non_mn3].pastelid("sign", "12345", artist_pastelid1, "passphrase")["signature"]
        for n in range(0, 13):
            mn_ticket_signatures[n] = self.nodes[n].pastelid("sign", "12345", mn_pastelids[n], "passphrase")["signature"]

        artist_ticket_height = self.nodes[0].getinfo()["blocks"]
        top_masternodes = self.nodes[0].masternode("top")[str(artist_ticket_height)]

        top_mns_indexes = set()
        for mn in top_masternodes:
            index = mn_outpoints[mn["outpoint"]]
            top_mns_indexes.add(index)
        not_top_mns_indexes = set(mn_outpoints.values()) ^ top_mns_indexes

        top_mns_index0 = list(top_mns_indexes)[0]
        top_mns_index1 = list(top_mns_indexes)[1]
        top_mns_index2 = list(top_mns_indexes)[2]
        top_mn_pastelid0 = mn_pastelids[top_mns_index0]
        top_mn_pastelid1 = mn_pastelids[top_mns_index1]
        top_mn_pastelid2 = mn_pastelids[top_mns_index2]
        top_mn_ticket_signature1 = mn_ticket_signatures[top_mns_index1]
        top_mn_ticket_signature2 = mn_ticket_signatures[top_mns_index2]
    
        signatures_dict = dict(
            {
                "artist": {artist_pastelid1: ticket_signature_artist},
                "mn2": {top_mn_pastelid1: top_mn_ticket_signature1},
                "mn3": {top_mn_pastelid2: top_mn_ticket_signature2},
            }
        )

        not_top_mns_index0 = list(not_top_mns_indexes)[0]
        not_top_mns_index1 = list(not_top_mns_indexes)[1]
        not_top_mns_index2 = list(not_top_mns_indexes)[2]
        not_top_mn_pastelid0 = mn_pastelids[not_top_mns_index0]
        not_top_mn_pastelid1 = mn_pastelids[not_top_mns_index1]
        not_top_mn_pastelid2 = mn_pastelids[not_top_mns_index2]
        not_top_mn_ticket_signature1 = mn_ticket_signatures[not_top_mns_index1]
        not_top_mn_ticket_signature2 = mn_ticket_signatures[not_top_mns_index2]

        not_top_mns_signatures_dict = dict(
            {
                "artist": {artist_pastelid1: ticket_signature_artist},
                "mn2": {not_top_mn_pastelid1: not_top_mn_ticket_signature1},
                "mn3": {not_top_mn_pastelid2: not_top_mn_ticket_signature2},
            }
        )

        # makefaketicket ticket signatures pastelID passphrase key1 key2 artisthieght nStorageFee ticketPrice bChangeSignature

        tickets = {
            #  non MN with real signatures of non top 10 MNs
            "art-non-mn123": self.nodes[self.non_mn1].tickets("makefaketicket", "art",
                                                              "12345", json.dumps(not_top_mns_signatures_dict), nonmn1_pastelid1, "passphrase", "key1", "key2",
                                                              str(artist_ticket_height), str(self.storage_fee), "10", "0"),

            #  non MN with fake signatures of top 10 MNs
            "art-nonmn1-fake23": self.nodes[self.non_mn1].tickets("makefaketicket", "art",
                                                                  "12345", json.dumps(signatures_dict), nonmn1_pastelid1, "passphrase", "key1", "key2",
                                                                  str(artist_ticket_height), str(self.storage_fee), "10", "1"),  # Verb = 1 - will modify pastlelid signature to make it invalid

            #  non top 10 MN with real signatures of non top 10 MNs
            "art-non-top-mn1-nonmn23": self.nodes[not_top_mns_index0].tickets("makefaketicket", "art",
                                                                               "12345", json.dumps(not_top_mns_signatures_dict), not_top_mn_pastelid0, "passphrase", "key1", "key2",
                                                                               str(artist_ticket_height), str(self.storage_fee), "10", "0"),

            #  non top 10 MN with fake signatures of top 10 MNs
            "art-non-top-mn1-fake23": self.nodes[not_top_mns_index0].tickets("makefaketicket", "art",
                                                                          "12345", json.dumps(signatures_dict), not_top_mn_pastelid0, "passphrase", "key1", "key2",
                                                                          str(artist_ticket_height), str(self.storage_fee), "10", "1"),  # Verb = 1 - will modify pastlelid signature to make it invalid

            #  top 10 MN with real signatures of non top 10 MNs
            "art-top-mn1-non-top-mn23": self.nodes[top_mns_index0].tickets("makefaketicket", "art",
                                                                                "12345", json.dumps(not_top_mns_signatures_dict), top_mn_pastelid0, "passphrase", "key1", "key2",
                                                                                str(artist_ticket_height), str(self.storage_fee), "10", "0"),

            #  top 10 MN with fake signatures of top 10 MNs
            "art-top-mn1-fake23": self.nodes[top_mns_index0].tickets("makefaketicket", "art",
                                                                          "12345", json.dumps(signatures_dict), top_mn_pastelid0, "passphrase", "key1", "key2",
                                                                          str(artist_ticket_height), str(self.storage_fee), "10", "1"),  # Verb = 1 - will modify pastlelid signature to make it invalid

            #  good signatures of top 10 MNs, bad ticket fee
            "art-": self.nodes[top_mns_index0].tickets("makefaketicket", "art",
                                                            "12345", json.dumps(signatures_dict), top_mn_pastelid0, "passphrase", "key1", "key2",
                                                            str(artist_ticket_height), str(self.storage_fee), "1", "0")
        }

        for n, t in tickets.items():
            try:
                self.nodes[0].sendrawtransaction(t)
            except JSONRPCException, e:
                self.errorString = e.error['message']
                print(n + ": " + self.errorString)
            assert_equal("bad-tx-invalid-ticket" in self.errorString, True)

        print("== Art Registration ticket transaction validation tested ==")

    def fake_artact_tnx_tests(self):
        print("== Art Registration Activation ticket transaction validation test ==")
        # 3.c. validate activation ticket transaction
        #       3.c.1 reject if there is no ArtTicket at txid referenced by this ticket
        #       3.c.2 reject if referenced ArtTicket is invalid
        #       3.c.4 reject if artist's PastelID in the activation ticket is not matching artist's PastelID in the registration ticket
        #       3.c.5 reject if wrong artist ticket height
        #       3.c.6 reject if doesn't pay ticket registration fee (10PSL)
        #       3.c.7 reject if doesn't pay 90% of storage fee to MN1 (60%), MN2(20%), MN3(20%)

        print("== Art Registration Activation ticket transaction validation tested ==")


if __name__ == '__main__':
    MasterNodeTicketsTest().main()
