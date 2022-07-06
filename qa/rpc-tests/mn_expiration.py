#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import math
import json
import time
import random
import string
from decimal import getcontext
from test_framework.util import (
    assert_equal,
    assert_raises_rpc,
    assert_true,
    initialize_chain_clean,
    str_to_b64str
)
from test_framework.authproxy import JSONRPCException
import test_framework.rpc_consts as rpc
from mn_common import MasterNodeCommon

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
        self.same_mns_signatures_dict = None
        self.not_top_mns_signatures_dict = None

        self.nonmn3_pastelid1 = None
        self.nonmn4_pastelid1 = None
        self.nonmn5_pastelid1 = None
        self.nonmn6_pastelid1 = None

        self.nonmn3_address1 = None
        self.nonmn4_address1 = None
        self.nonmn5_address1 = None
        self.nonmn6_address1 = None
        self.creator_pastelid1 = None
        self.creator_ticket_height = None
        self.ticket_principal_signature = None
        self.top_mns_index0 = None
        self.top_mns_index1 = None
        self.top_mns_index2 = None
        self.top_mn_pastelid0 = None
        self.top_mn_pastelid1 = None
        self.top_mn_pastelid2 = None
        self.top_mn_ticket_signature0 = None
        self.top_mn_ticket_signature1 = None
        self.top_mn_ticket_signature2 = None

        self.total_copies = 2
        self.nft_copy_price = 1000
        self.id_ticket_price = 10
        self.nft_ticket_price = 10
        self.act_ticket_price = 10
        self.transfer_ticket_price = 10

        self.royalty = 0.075                # default royalty fee
        self.is_green = True                # is green fee payment?

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

        offer_ticket2_txid = None

        self.initialize()

        nft_ticket_txid = self.register_nft_reg_ticket("nft-label")
        self.__wait_for_confirmation(self.non_mn3)

        act_ticket_txid = self.register_nft_act_ticket(nft_ticket_txid)
        self.__wait_for_confirmation(self.non_mn3)

        offer_ticket1_txid = self.register_offer_ticket(act_ticket_txid)
        offer_ticket_txid = offer_ticket1_txid
        print(f"offer ticket 1 txid {offer_ticket1_txid}")
        self.__wait_for_confirmation(self.non_mn3)

        if (self.total_copies == 1):
            # fail if not enough copies to offer
            assert_raises_rpc(rpc.RPC_MISC_ERROR, 
                "Invalid Offer ticket - copy number [2] cannot exceed the total number of available copies [1] or be 0",
                self.register_offer_ticket, act_ticket_txid)

        # fail as the replace copy can be created after 5 days
        assert_raises_rpc(rpc.RPC_MISC_ERROR, 
            f"Can only replace Offer ticket after 5 days, txid - [{offer_ticket1_txid}] copyNumber [1]",
            self.register_offer_ticket, act_ticket_txid, 1)

        print("Generate 3000 blocks that is > 5 days. 1 chunk is 10 blocks")
        for ind in range (300):
            print(f"chunk - {ind + 1}")
            self.nodes[self.mining_node_num].generate(10)
            time.sleep(2)

        print("Waiting 300 seconds")
        time.sleep(300)
        self.__wait_for_sync_all()

        offer_ticket2_txid = self.register_offer_ticket(act_ticket_txid, 1)
        offer_ticket_txid = offer_ticket2_txid
        print(f"offer ticket 2 txid {offer_ticket2_txid}")

        offer_ticket3_txid = None
        if (self.total_copies > 1):
            offer_ticket3_txid = self.register_offer_ticket(act_ticket_txid)
            print(f"offer ticket 3 txid {offer_ticket3_txid}")

            offer_ticket1_1 = self.nodes[self.non_mn3].tickets("find", "offer", act_ticket_txid+":2")
            assert_equal(offer_ticket1_1['ticket']['type'], "offer")
            assert_equal(offer_ticket1_1['ticket']['nft_txid'], act_ticket_txid)
            assert_equal(offer_ticket1_1["ticket"]["copy_number"], 2)

            offer_ticket1_2 = self.nodes[self.non_mn3].tickets("get", offer_ticket3_txid)
            assert_equal(offer_ticket1_2["ticket"]["nft_txid"], offer_ticket1_1["ticket"]["nft_txid"])
            assert_equal(offer_ticket1_2["ticket"]["copy_number"], offer_ticket1_1["ticket"]["copy_number"])

        self.__send_coins_to_accept(self.non_mn4, self.nonmn4_address1, 5)

        if offer_ticket2_txid:
            # fail if old offer ticket has been replaced
            assert_raises_rpc(rpc.RPC_MISC_ERROR,
               f"This Offer ticket has been replaced with another ticket, txid - [{offer_ticket2_txid}], copyNumber [1]",
               self.register_accept_ticket, self.non_mn4, self.nonmn4_pastelid1, offer_ticket1_txid)

        accept_ticket1_txid = self.register_accept_ticket(self.non_mn4, self.nonmn4_pastelid1, offer_ticket_txid)
        accept_ticket_txid = accept_ticket1_txid

        # fail if there is another Accept ticket1 created < 1 hour ago
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"Accept ticket [{accept_ticket1_txid}] already exists and is not yet 1h old for this Offer ticket [{offer_ticket_txid}]",
            self.register_accept_ticket, self.non_mn4, self.nonmn4_pastelid1, offer_ticket_txid)

        print("Generate 30 blocks that is > 1 hour. 1 chunk is 10 blocks")
        for ind in range (3):
            print(f"chunk - {ind + 1}")
            self.nodes[self.mining_node_num].generate(10)
            time.sleep(2)

        print("Waiting 180 seconds")
        time.sleep(180)
        self.__wait_for_sync_all()

        accept_ticket2_txid = self.register_accept_ticket(self.non_mn4, self.nonmn4_pastelid1, offer_ticket_txid)
        accept_ticket_txid = accept_ticket2_txid

        # fail if there is another Accept ticket2 created < 1 hour ago
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"Accept ticket [{accept_ticket2_txid}]  already exists and is not yet 1h old for this Offer ticket [{offer_ticket_txid}]",
            self.register_accept_ticket, self.non_mn4, self.nonmn4_pastelid1, offer_ticket_txid)

        print("Generate 30 blocks that is > 1 hour. 1 chunk is 10 blocks")
        for ind in range (3):
            print(f"chunk - {ind + 1}")
            self.nodes[self.mining_node_num].generate(10)
            time.sleep(2)

        print("Waiting 180 seconds")
        time.sleep(180)
        self.__wait_for_sync_all()

        accept_ticket3_txid = self.register_accept_ticket(self.non_mn4, self.nonmn4_pastelid1, offer_ticket_txid)
        accept_ticket_txid = accept_ticket3_txid

        # fail if old accept ticket1 has been replaced
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"This Accept ticket has been replaced with another ticket, txid - [{accept_ticket_txid}]",
            self.register_transfer_ticket, 
            self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1, offer_ticket_txid, accept_ticket1_txid)

        # fail if old accept ticket2 has been replaced
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"This Accept ticket has been replaced with another ticket, txid - [{accept_ticket_txid}]",
            self.register_transfer_ticket, 
            self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1, offer_ticket_txid, accept_ticket2_txid)

        transfer_ticket1_txid = self.register_transfer_ticket(
            self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1,
            offer_ticket_txid, accept_ticket_txid)

        # fail if there is another Transfer ticket referring to that offer ticket
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            "Transfer ticket already exists for the Offer ticket with this txid [{offer_ticket_txid}]",
            self.register_transfer_ticket,
            self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1, offer_ticket_txid, accept_ticket_txid)

        if self.total_copies == 1:
            assert_raises_rpc(rpc.RPC_MISC_ERROR,
                f"The NFT you are trying to offer - from NFT Registration ticket [{act_ticket_txid}]"
                " - is already offered - there are already [1] offered copies, "
                "but only [1] copies were available",
                self.register_offer_ticket, act_ticket_txid, 1)
        elif self.total_copies == 2:
            # fail if not enough copies to offer
            assert_raises_rpc(rpc.RPC_MISC_ERROR,
                "Invalid Offer ticket - copy number [3] cannot exceed the total number of available copies [2] or be 0",
                self.register_offer_ticket, act_ticket_txid)

            assert_raises_rpc(rpc.RPC_MISC_ERROR,
                f"Cannot replace Offer ticket - it has been already transferred, txid - [{offer_ticket_txid}] copyNumber [1]",
                self.register_offer_ticket, act_ticket_txid, 1)

            # fail as the replace copy can be created after 5 days
            assert_raises_rpc(rpc.RPC_MISC_ERROR,
                f"Can only replace Offer ticket after 5 days, txid - [{offer_ticket3_txid}] copyNumber [2]",
                self.register_offer_ticket, act_ticket_txid, 2)


    # ===============================================================================================================
    def initialize(self):
        print("== Initialize nodes ==")

        self.nodes[self.mining_node_num].generate(10)

        # generate pastelIDs & send coins
        self.creator_pastelid1 = self.create_pastelid(self.non_mn3)[0]
        self.nonmn3_pastelid1 = self.create_pastelid(self.non_mn3)[0]
        self.nonmn3_address1 = self.nodes[self.non_mn3].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 1000, "", "", False)

        self.nonmn4_pastelid1 = self.create_pastelid(self.non_mn4)[0]
        self.nonmn4_address1 = self.nodes[self.non_mn4].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 100, "", "", False)

        self.nonmn5_pastelid1 = self.create_pastelid(self.non_mn5)[0]
        self.nonmn5_address1 = self.nodes[self.non_mn5].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn5_address1, 100, "", "", False)

        for n in range(0, 12):
            self.mn_addresses[n] = self.nodes[n].getnewaddress()
            self.nodes[self.mining_node_num].sendtoaddress(self.mn_addresses[n], 1000, "", "", False)
            self.mn_pastelids[n] = self.create_pastelid(n)[0]
            self.mn_outpoints[self.nodes[n].masternode("status")["outpoint"]] = n

        print(f"mn_addresses - {self.mn_addresses}")
        print(f"mn_pastelids - {self.mn_pastelids}")
        print(f"mn_outpoints - {self.mn_outpoints}")

        self.__wait_for_sync_all()

        # register pastelIDs
        self.nodes[self.non_mn3].tickets("register", "id", self.creator_pastelid1, self.passphrase, self.nonmn3_address1)
        self.nodes[self.non_mn3].tickets("register", "id", self.nonmn3_pastelid1, self.passphrase, self.nonmn3_address1)
        self.nodes[self.non_mn4].tickets("register", "id", self.nonmn4_pastelid1, self.passphrase, self.nonmn4_address1)
        self.nodes[self.non_mn5].tickets("register", "id", self.nonmn5_pastelid1, self.passphrase, self.nonmn5_address1)
        for n in range(0, 12):
            self.nodes[n].tickets("register", "mnid", self.mn_pastelids[n], self.passphrase)

        self.__wait_for_sync_all(5)

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

        top_mns_indexes = list()
        for mn in top_masternodes:
            index = self.mn_outpoints[mn["outpoint"]]
            top_mns_indexes.append(index)

        self.top_mns_index0 = top_mns_indexes[0]
        self.top_mns_index1 = top_mns_indexes[1]
        self.top_mns_index2 = top_mns_indexes[2]
        self.top_mn_pastelid0 = self.mn_pastelids[self.top_mns_index0]
        self.top_mn_pastelid1 = self.mn_pastelids[self.top_mns_index1]
        self.top_mn_pastelid2 = self.mn_pastelids[self.top_mns_index2]

        print(f"top_mns_indexes - {top_mns_indexes}")
        print(f"top_mns_pastelids - {self.top_mn_pastelid0},{self.top_mn_pastelid1},{self.top_mn_pastelid2}")
        return top_mns_indexes

    # ===============================================================================================================
    def create_signatures(self, principal_node_num, make_bad_signatures_dicts = False):
        """Create ticket signatures

        Args:
            principal_node_num (int): node# for principal signer
            make_bad_signatures_dicts (bool): if True - create invalid signatures
        """

        mn_ticket_signatures = {}
        principal_pastelid = self.creator_pastelid1
        self.ticket_principal_signature = self.nodes[principal_node_num].pastelid("sign", self.ticket, principal_pastelid, self.passphrase)["signature"]
        for n in range(0, 12):
            mn_ticket_signatures[n] = self.nodes[n].pastelid("sign", self.ticket, self.mn_pastelids[n], self.passphrase)["signature"]
        print(f"principal ticket signer - {self.ticket_principal_signature}")
        print(f"mn_ticket_signatures - {mn_ticket_signatures}")

        # update top master nodes used for signing
        top_mns_indexes = self.update_mn_indexes()

        self.top_mn_ticket_signature0 = mn_ticket_signatures[self.top_mns_index0]
        self.top_mn_ticket_signature1 = mn_ticket_signatures[self.top_mns_index1]
        self.top_mn_ticket_signature2 = mn_ticket_signatures[self.top_mns_index2]

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
                    "principal": {principal_pastelid: self.ticket_principal_signature},
                    "mn2": {not_top_mn_pastelid1: not_top_mn_ticket_signature1},
                    "mn3": {not_top_mn_pastelid2: not_top_mn_ticket_signature2},
                }
            )
            print(f"not_top_mns_signatures_dict - {self.not_top_mns_signatures_dict!r}")

    # ===============================================================================================================
    def create_nft_ticket_v1(self, creator_node_num, total_copies, royalty, green, make_bad_signatures_dicts = False):
        """Create NFT ticket v1 and signatures

        Args:
            creator_node_num (int): node that creates NFT ticket and signatures
            total_copies (int): [number of copies]
            royalty (int): [royalty fee, how much creator should get on all future resales]
            green (bool): [is there Green NFT payment or not]
            make_bad_signatures_dicts (bool): [create bad signatures]
        """
        # Get current height
        self.creator_ticket_height = self.nodes[0].getinfo()["blocks"]
        print(f"creator_ticket_height - {self.creator_ticket_height}")

        # nft_ticket - v1
        # {
        #   "nft_ticket_version": integer  // 1
        #   "author": bytes,               // PastelID of the author (creator)
        #   "blocknum": integer,           // block number when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "block_hash": bytes            // hash of the top block when the ticket was created - this is to map the ticket to the MNs that should process it
        #   "copies": integer,             // number of copies
        #   "royalty": float,              // how much creator should get on all future resales
        #   "green": bool,                 // is green payment
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
        nft_ticket_str = json.dumps(json_ticket, indent=4)
        self.ticket = str_to_b64str(nft_ticket_str)
        print(f"nft_ticket v1 - {nft_ticket_str}")
        print(f"nft_ticket v1 (base64) - {self.ticket}")

        self.create_signatures(creator_node_num, make_bad_signatures_dicts)


    def register_nft_reg_ticket(self, label):
        print("== Create the NFT registration ticket ==")

        self.create_nft_ticket_v1(self.non_mn3, self.total_copies, self.royalty, self.is_green)
        nft_ticket_txid = self.nodes[self.top_mns_index0].tickets("register", "nft",
            self.ticket, json.dumps(self.signatures_dict), self.top_mn_pastelid0, self.passphrase,
            label, str(self.storage_fee))["txid"]
        print(nft_ticket_txid)
        assert_true(nft_ticket_txid, "No NFT registration ticket was created")

        self.__wait_for_ticket_tnx()
        print(self.nodes[self.top_mns_index0].getblockcount())

        return nft_ticket_txid

    # ===============================================================================================================
    def register_nft_act_ticket(self, nft_ticket_txid):
        print("== Create the nft activation ticket ==")

        act_ticket_txid = self.nodes[self.non_mn3].tickets("register", "act", nft_ticket_txid,
                                             str(self.creator_ticket_height), str(self.storage_fee),
                                             self.creator_pastelid1, self.passphrase)["txid"]
        assert_true(act_ticket_txid, "No NFT Registration ticket was created")
        self.__wait_for_ticket_tnx()

        return act_ticket_txid

    # ===============================================================================================================
    def register_offer_ticket(self, act_ticket_txid, copyNumber = 0):
        print("== Create Offer ticket ==")

        offer_ticket_txid = self.nodes[self.non_mn3].tickets("register", "offer", act_ticket_txid, str(self.nft_copy_price),
                                             self.creator_pastelid1, self.passphrase, 0, 0, copyNumber)["txid"]
        assert_true(offer_ticket_txid, "No Offer ticket was created")
        self.__wait_for_ticket_tnx()

        return offer_ticket_txid

    # ===============================================================================================================
    def __send_coins_to_accept(self, node, address, num):
        cover_price = self.nft_copy_price + max(10, int(self.nft_copy_price / 100)) + 5

        self.nodes[self.mining_node_num].sendtoaddress(address, num * cover_price, "", "", False)
        self.__wait_for_confirmation(node)

    def register_accept_ticket(self, acceptor_node, acceptor_pastelid1, offer_ticket_txid):
        print("== Create Accept ticket ==")

        accept_ticket_txid = self.nodes[acceptor_node].tickets("register", "accept", offer_ticket_txid, str(self.nft_copy_price),
                                           acceptor_pastelid1, self.passphrase)["txid"]
        assert_true(accept_ticket_txid, "No Accept ticket was created")
        self.__wait_for_ticket_tnx()

        return accept_ticket_txid

    # ===============================================================================================================
    def register_transfer_ticket(self, offerer_node, acceptor_node, acceptor_pastelid1, acceptor_address1,
                                  offer_ticket_txid, accept_ticket_txid):
        print("== Create Transfer ticket ==")

        cover_price = self.nft_copy_price + 10

        # sends coins back, keep 1 PSL to cover transaction fee
        self.__send_coins_back(self.non_mn4)
        self.__wait_for_sync_all10()

        coins_before = self.nodes[acceptor_node].getbalance()
        print(coins_before)
        assert_true(coins_before > 0 and coins_before < 1)

        self.nodes[self.mining_node_num].sendtoaddress(acceptor_address1, cover_price, "", "", False)
        self.__wait_for_confirmation(acceptor_node)

        coins_before = self.nodes[acceptor_node].getbalance()
        print(coins_before)

        offerer_pastel_id = self.nodes[offerer_node].tickets("get", offer_ticket_txid)["ticket"]["pastelID"]
        print(offerer_pastel_id)
        offerer_address = self.nodes[offerer_node].tickets("find", "id", offerer_pastel_id)["ticket"]["address"]
        print(offerer_address)
        creators_coins_before = self.nodes[offerer_node].getreceivedbyaddress(offerer_address)

        # consolidate funds into single address
        consaddress = self.nodes[acceptor_node].getnewaddress()
        self.nodes[acceptor_node].sendtoaddress(consaddress, coins_before, "", "", True)

        transfer_ticket_txid = self.nodes[acceptor_node].tickets("register", "transfer", offer_ticket_txid, accept_ticket_txid,
                                           acceptor_pastelid1, self.passphrase)["txid"]
        assert_true(transfer_ticket_txid, "No Transfer ticket was created")
        self.__wait_for_ticket_tnx()

        # check correct amount of change and correct amount spent
        coins_after = self.nodes[acceptor_node].getbalance()
        print(coins_before)
        print(coins_after)
        # ticket cost is Transfer ticket price, NFT cost is nft_copy_price
        assert_true(math.isclose(coins_after, coins_before - self.transfer_ticket_price - self.nft_copy_price, rel_tol=0.005))

        # check current owner gets correct amount
        creators_coins_after = self.nodes[self.non_mn3].getreceivedbyaddress(offerer_address)
        print(creators_coins_before)
        print(creators_coins_after)
        assert_true(math.isclose(creators_coins_after - creators_coins_before, self.nft_copy_price, rel_tol=0.005))

        # from another node - get ticket transaction and check
        #   - there is 1 posiible output to current owner
        transfer_ticket_hash = self.nodes[0].getrawtransaction(transfer_ticket_txid)
        transfer_ticket_tx = self.nodes[0].decoderawtransaction(transfer_ticket_hash)
        offerer_amount = 0
        multi_fee = 0

        for v in transfer_ticket_tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                multi_fee += v["value"]
            if v["scriptPubKey"]["type"] == "pubkeyhash":
                amount = v["value"]
                print(f"transfer transiction pubkeyhash vout - {amount}")
                if v["scriptPubKey"]["addresses"][0] == offerer_address:
                    offerer_amount = amount
                    print(f"transfer transaction to current owner's address - {amount}")
        print(f"transfer transiction multisig fee_amount - {multi_fee}")
        assert_true(math.isclose(offerer_amount, self.nft_copy_price))
        assert_equal(multi_fee, self.id_ticket_price)

        return transfer_ticket_txid

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
