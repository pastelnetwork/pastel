#!/usr/bin/env python3
# Copyright (c) 2018-2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
import json
import time

from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc,
    assert_true,
    initialize_chain_clean,
    str_to_b64str
)
import test_framework.rpc_consts as rpc
from pastel_test_framework import (
    TicketType
)
from mn_common import (
    MasterNodeCommon
)
from test_framework.authproxy import JSONRPCException

from decimal import getcontext
getcontext().prec = 16


class MasterNodeTicketsTest(MasterNodeCommon):

    def __init__(self):
        super().__init__()
        
        self.number_of_master_nodes = 12
        self.number_of_simple_nodes = 3
        self.number_of_cold_nodes = self.number_of_master_nodes

        self.non_mn1 = self.number_of_master_nodes        # mining node - will have coins #12
        self.non_mn2 = self.number_of_master_nodes+1      # hot node - will have collateral for all active MN #13
        self.non_mn3 = self.number_of_master_nodes+2      # will not have coins by default #1

        self.mining_node_num = self.number_of_master_nodes    # same as non_mn1
        self.hot_node_num = self.number_of_master_nodes+1     # same as non_mn2

        self.errorString = ""
        self.is_network_split = False
        self.nodes = []
        self.storage_fee = 100
        self.storage_fee90percent = self.storage_fee*9/10

        self.mn_ticket_signatures = {}
 
        self.nonmn1_pastelid1 = None
        self.creator_pastelid1 = None
        self.non_mn1_pastelid_txid = None

        self.ticket = None
        self.creator_ticket_height = None


    def setup_chain(self):
        print(f"Initializing test directory {self.options.tmpdir}")
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)


    def setup_network(self, split=False):
        self.setup_masternodes_network()


    def run_test(self):
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
        self.fake_offer_tnx_tests1()
        self.fake_accept_tnx_tests()
        self.fake_transfer_tnx_tests()
        self.fake_offer_tnx_tests2()


    def fake_pastelid_tnx_tests(self):
        print("== Pastelid ticket transaction validation test ==")

        mn0_pastelid1 = self.create_pastelid(0)[0]
        nonmn3_pastelid1 = self.create_pastelid(self.non_mn3)[0]

        # makefaketicket mnid pastelID passphrase ticketPrice bChangeSignature
        # makefaketicket id pastelID passphrase address ticketPrice bChangeSignature

        tickets = {
            #  reject if outpoint is already registered

            #  reject if fake outpoint (Unknown Masternode) (Verb = 3 - will modify outpoint to make it invalid)
            "mnid1-fakeout": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, self.passphrase, "10", "3"),

            #  reject if the Pastel ID ticket signature is invalid (Verb = 1 - will modify pastlelid signature to make it invalid)
            "mnid2-psig": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, self.passphrase, "10", "1"),
            "id1-psig": self.nodes[self.non_mn3].tickets("makefaketicket", "id", nonmn3_pastelid1, self.passphrase, self.nonmn3_address1, "10", "1"),

            #  reject if MN Pastel ID's MN signature is invalid (Verb = 2 - will modify MN signature to make it invalid)
            "mnid3-mnsig": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, self.passphrase, "10", "2"),

            #  reject if doesn't pay correct ticket registration fee (10PSL)
            "mnid4-regfee": self.nodes[0].tickets("makefaketicket", "mnid", mn0_pastelid1, self.passphrase, "1", "0"),
            "id2-regfee": self.nodes[self.non_mn3].tickets("makefaketicket", "id", nonmn3_pastelid1, self.passphrase, self.nonmn3_address1, "1", "0")
        }

        for n, t in tickets.items():
            print(f'sending fake ticket [{n}] transaction')
            assert_raises_rpc(rpc.RPC_VERIFY_REJECTED, "bad-tx-invalid-ticket",
                self.nodes[0].sendrawtransaction, t)

        print("== Pastelid ticket transaction validation tested ==")


    def fake_nftreg_tnx_tests(self):
        print("== NFT Registration ticket transaction validation test ==")

        # generate pastelIDs
        self.nonmn1_pastelid1 = self.create_pastelid(self.non_mn1)[0]
        self.creator_pastelid1 = self.create_pastelid(self.non_mn3)[0]

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
        self.non_mn1_pastelid_txid = self.nodes[self.non_mn1].tickets("register", "id", self.nonmn1_pastelid1, self.passphrase, nonmn1_address1)["txid"]
        self.nodes[self.non_mn3].tickets("register", "id", self.creator_pastelid1, self.passphrase, self.nonmn3_address1)

        self.sync_all(10,30)
        self.nodes[self.mining_node_num].generate(5)
        self.sync_all(10,30)

        self.total_copies = 10
        # Get current top MNs at Node 0
        self.creator_ticket_height = self.nodes[0].getblockcount()
        top_masternodes = self.nodes[0].masternode("top")[str(self.creator_ticket_height)]
        print(f"top mns for height {self.creator_ticket_height}:\n{top_masternodes}")
        assert_greater_than(len(top_masternodes), 3)

        json_ticket = {
            "nft_ticket_version": 1,
            "author": self.creator_pastelid1,
            "blocknum": self.creator_ticket_height,
            "block_hash": "ABCDEFG",
            "copies": self.total_copies,
            "royalty": 0.1,
            "green": True,
            "app_ticket": "HIJKLMNOP"}

        self.ticket = str_to_b64str(json.dumps(json_ticket))
        print(self.ticket)

        # create ticket signature
        ticket_signature_creator = self.nodes[self.non_mn3].pastelid("sign", self.ticket, self.creator_pastelid1, self.passphrase)["signature"]
        for n in range(self.number_of_master_nodes):
            mn = self.mn_nodes[n]
            self.mn_ticket_signatures[n] = self.nodes[n].pastelid("sign", self.ticket, mn.mnid, self.passphrase)["signature"]

        self.update_mn_indexes(0, -1, 6)
        # define signatures
        for top_mn in self.top_mns:
            top_mn.signature = self.mn_ticket_signatures[top_mn.index]
        # slice top_mns into 2 lists:
        #   1) top_mns[0, 1, 2]
        #   2) non_top_mns[3, 4, 5]
        self.non_top_mns = self.top_mns[3:6]
        self.top_mns = self.top_mns[0:3]

        self.signatures_dict = dict(
            {
                "principal": {self.creator_pastelid1: ticket_signature_creator},
                "mn2": {self.top_mns[1].pastelid: self.top_mns[1].signature},
                "mn3": {self.top_mns[2].pastelid: self.top_mns[2].signature},
            }
        )

        self.not_top_mns_signatures_dict = dict(
            {
                "principal": {self.creator_pastelid1: ticket_signature_creator},
                "mn2": {self.non_top_mns[1].pastelid: self.non_top_mns[1].signature},
                "mn3": {self.non_top_mns[2].pastelid: self.non_top_mns[2].signature},
            }
        )

        self.nodes[self.non_mn1].generate(10)

        # makefaketicket nft ticket signatures pastelID passphrase label creatorheight nStorageFee ticketPrice bChangeSignature

        tickets = {
            #  non MN with real signatures of non top 10 MNs
            "nft1-non-mn123": self.nodes[self.non_mn1].tickets("makefaketicket", "nft",
                                                              self.ticket, json.dumps(self.not_top_mns_signatures_dict), self.nonmn1_pastelid1, self.passphrase, "nft-label",
                                                              str(self.creator_ticket_height), str(self.storage_fee), "10", "0"),

            #  non MN with fake signatures of top 10 MNs
            "nft2-nonmn1-fake23": self.nodes[self.non_mn1].tickets("makefaketicket", "nft",
                                                                  self.ticket, json.dumps(self.signatures_dict), self.nonmn1_pastelid1, self.passphrase, "nft-label",
                                                                  str(self.creator_ticket_height), str(self.storage_fee), "10", "1"),  # Verb = 1 - will modify pastlelid signature to make it invalid

            #  non top 10 MN with real signatures of non top 10 MNs
            "nft3-non-top-mn1-nonmn23": self.nodes[self.non_top_mns[0].index].tickets("makefaketicket", "nft",
                                                                               self.ticket, json.dumps(self.not_top_mns_signatures_dict), self.non_top_mns[0].pastelid, self.passphrase, "nft-label",
                                                                               str(self.creator_ticket_height), str(self.storage_fee), "10", "0"),

            #  non top 10 MN with fake signatures of top 10 MNs
            "nft4-non-top-mn1-fake23": self.nodes[self.non_top_mns[0].index].tickets("makefaketicket", "nft",
                                                                          self.ticket, json.dumps(self.signatures_dict), self.non_top_mns[0].pastelid, self.passphrase, "nft-label",
                                                                          str(self.creator_ticket_height), str(self.storage_fee), "10", "1"),  # Verb = 1 - will modify pastlelid signature to make it invalid

            #  top 10 MN with real signatures of non top 10 MNs
            "nft5-top-mn1-non-top-mn23": self.nodes[self.top_mns[0].index].tickets("makefaketicket", "nft",
                                                                                self.ticket, json.dumps(self.not_top_mns_signatures_dict), self.top_mns[0].pastelid, self.passphrase, "nft-label",
                                                                                str(self.creator_ticket_height), str(self.storage_fee), "10", "0"),

            #  top 10 MN with fake signatures of top 10 MNs
            "nft6-top-mn1-fake23": self.nodes[self.top_mns[0].index].tickets("makefaketicket", "nft",
                                                                          self.ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, "nft-label",
                                                                          str(self.creator_ticket_height), str(self.storage_fee), "10", "1"),  # Verb = 1 - will modify pastlelid signature to make it invalid

            #  good signatures of top 10 MNs, bad ticket fee
            "nft-top-mn1-bad-fee": self.nodes[self.top_mns[0].index].tickets("makefaketicket", "nft",
                                                            self.ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase, "nft-label",
                                                            str(self.creator_ticket_height), str(self.storage_fee), "1", "0")
        }

        for test_desc, t in tickets.items():
            try:
                print(f'Processing fake nft-reg ticket [{test_desc}]')
                self.nodes[0].sendrawtransaction(t)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(f'{test_desc}: {self.errorString}')
                if 'messageDetails' in e.error:
                    print(f"{test_desc}: {e.error['messageDetails']}")
            assert_equal("bad-tx-invalid-ticket" in self.errorString, True)

        print("== NFT Registration ticket transaction validation tested ==")


    def fake_nftact_tnx_tests(self):
        print("== NFT Registration Activation ticket transaction validation test ==")

        self.create_nft_ticket_v1(self.non_mn3, 1, 0, False)
        ticket = self.tickets[TicketType.NFT]
        ticket_type_name = TicketType.NFT.type_name
        top_mn_node = self.nodes[self.top_mns[0].index]

        # valid ticket
        nft_reg_result = top_mn_node.tickets("register", ticket_type_name, ticket.reg_ticket_base64_encoded,
                                             json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase,
                                             "nft-label", str(self.storage_fee))
        print(f"Created valid NFT Registration ticket: {json.dumps(nft_reg_result, indent=4)}")
        
        ticket.reg_txid = nft_reg_result["txid"]
        assert_true(ticket.reg_txid, "No ticket was created")

        self.wait_for_min_confirmations()
        self.generate_and_sync_inc(10, self.non_mn1)

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
                self.creator_pastelid1, self.passphrase,
                "10", "0"),

            #       3.c.2 reject if creator's Pastel ID in the activation ticket is not matching creator's Pastel ID in the registration ticket
            # This prevent someone else to create Act Ticket providing creator's Pastel ID, but wrong signature (only creator has private key to create correct signature)
            "act2-bad-creator_sign": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                ticket.reg_txid, str(ticket.reg_height),
                str(self.storage_fee),
                self.creator_pastelid1, self.passphrase,
                "10", "1"), # Verb = 1 - will modify Act ticket signature to make it invalid (non matching creator's Pastel ID)

            #       3.c.3 reject if wrong creator ticket height
            "act3-bad-creator-height": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                ticket.reg_txid, str(ticket.reg_height),
                str(self.storage_fee),
                self.creator_pastelid1, self.passphrase,
                "10", "2"),

            #       3.c.4 reject if doesn't pay ticket registration fee (10PSL)
            "act4-bad-reg_fee": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                ticket.reg_txid, str(ticket.reg_height),
                str(self.storage_fee),
                self.creator_pastelid1, self.passphrase,
                "0", "0"),

            #       3.c.5 reject if pay correct storage fee (90% = MN1(60%)+MN2(20%)+MN3(20%)) to wrong MNs
            "act5-bad-mns-addresses": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                ticket.reg_txid, str(ticket.reg_height),
                str(self.storage_fee),
                self.creator_pastelid1, self.passphrase,
                "10", "0",
                self.mn_nodes[self.non_top_mns[0].index].mnid_reg_address, str(mn0_fee),
                self.mn_nodes[self.non_top_mns[0].index].mnid_reg_address, str(mn1_fee),
                self.mn_nodes[self.non_top_mns[0].index].mnid_reg_address, str(mn2_fee)
                ),

            #       3.c.6 reject if pay wrong storage fee (90%=MN1(60%)+MN2(20%)+MN3(20%)), but to correct MNs
            "act6-bad-mns-fee": self.nodes[self.non_mn3].tickets("makefaketicket", "act",
                ticket.reg_txid, str(ticket.reg_height),
                str(self.storage_fee),
                self.creator_pastelid1, self.passphrase,
                "10", "0",
                self.mn_nodes[self.non_top_mns[0].index].mnid_reg_address, str(mn0_fee*10),
                self.mn_nodes[self.non_top_mns[0].index].mnid_reg_address, str(mn1_fee*10),
                self.mn_nodes[self.non_top_mns[0].index].mnid_reg_address, str(mn2_fee*10)
                ),
        }

        for test_desс, t in tickets.items():
            try:
                print(f'Processing fake nft-act ticket [{test_desс}]')
                self.nodes[0].sendrawtransaction(t)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(f'{test_desс}: {self.errorString}')
                if 'messageDetails' in e.error:
                    print(f"{test_desс}: {e.error['messageDetails']}")
            assert_true("bad-tx-invalid-ticket" in self.errorString or "tx-missing-inputs" in self.errorString)

        print("== NFT Registration Activation ticket transaction validation tested ==")


    def fake_offer_tnx_tests1(self):
        print("== Offer ticket transaction validation test (for activation ticket) ==")

        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 200, "", "", False)
        time.sleep(2)
        self.sync_all(10, 30)
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10, 30)
        
        ticket = self.tickets[TicketType.NFT]

        self.nft_ticket1_act_ticket_txid = self.nodes[self.non_mn3].tickets("register", "act", ticket.reg_txid, str(ticket.reg_height),
                                                                            str(self.storage_fee),
                                                                            self.creator_pastelid1, self.passphrase)["txid"]
        assert_true(self.nft_ticket1_act_ticket_txid, "No ticket was created")

        self.sync_all(10, 30)
        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10, 30)

        tickets = {
            # 1. check Pastel ID in this ticket matches Pastel ID in the referred Activation ticket
            "offer-bad-nfts-sign": self.nodes[self.non_mn3].tickets("makefaketicket", "offer",
                                                                    self.nft_ticket1_act_ticket_txid, "100000",
                                                                    self.creator_pastelid1, self.passphrase,
                                                                    "0", "0",
                                                                    "10", "1"), # Verb = 1 - will modify Act ticket signature to make it invalid (non matchig creator's Pastel ID)
        }

        # sync mempools
        self.sync_all()
        for n, t in tickets.items():
            try:
                self.nodes[0].sendrawtransaction(t)
            except JSONRPCException as e:
                self.errorString = e.error['message']
                print(n + ": " + self.errorString)
            assert_equal("bad-tx-invalid-ticket" in self.errorString, True)

        print("== Offer ticket transaction validation tested (for activation ticket) ==")

    def fake_accept_tnx_tests(self):
        print("== Accept ticket transaction validation test ==")

        print("== Accept ticket transaction validation tested ==")

    def fake_transfer_tnx_tests(self):
        print("== Transfer ticket transaction validation test ==")

        print("== Transfer ticket transaction validation tested ==")

    def fake_offer_tnx_tests2(self):
        print("== Offer ticket transaction validation test (for transfer ticket) ==")
        # 1. check Pastel ID in this ticket matches Pastel ID in the referred Transfer ticket
        # 2. Verify the NFT is not already transferred
        #    Verify there is no already transfer ticket referring to that offer ticket
        #    Verify the number of existing transfer tickets less then number of copies in the registration ticket

        print("== Offer ticket transaction validation tested (for transfer ticket) ==")

if __name__ == '__main__':
    MasterNodeTicketsTest().main()
