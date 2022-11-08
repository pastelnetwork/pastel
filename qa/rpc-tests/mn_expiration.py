#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
import math
import json
import time
from decimal import getcontext
from test_framework.util import (
    assert_equal,
    assert_raises_rpc,
    assert_true,
    assert_false,
    assert_greater_than,
    initialize_chain_clean
)
import test_framework.rpc_consts as rpc
from pastel_test_framework import (
    TicketType
)
from mn_common import (
    MasterNodeCommon
)

getcontext().prec = 16

class MasterNodeTicketsTest(MasterNodeCommon):

    def __init__(self):
        super().__init__()
        
        self.number_of_master_nodes = 12
        self.number_of_cold_nodes = self.number_of_master_nodes - 1
        self.number_of_simple_nodes = 6

        self.non_active_mn = self.number_of_master_nodes - 1

        self.non_mn1 = self.number_of_master_nodes           # mining node - will have coins #13
        self.non_mn2 = self.number_of_master_nodes + 1       # hot node - will have collateral for all active MN #14
        self.non_mn3 = self.number_of_master_nodes + 2       # will not have coins by default #15
        self.non_mn4 = self.number_of_master_nodes + 3       # will not have coins by default #16
        self.non_mn5 = self.number_of_master_nodes + 4       # will not have coins by default #17
        self.non_mn6 = self.number_of_master_nodes + 5       # will not have coins by default #18

        self.mining_node_num = self.number_of_master_nodes   # same as non_mn1
        self.hot_node_num = self.number_of_master_nodes + 1  # same as non_mn2
        
        self.errorString = ""
        self.is_network_split = False
        self.nodes = []
        self.storage_fee = 100

        self.nonmn3_pastelid1 = None
        self.nonmn4_pastelid1 = None
        self.nonmn5_pastelid1 = None
        self.nonmn6_pastelid1 = None

        self.nonmn3_address1 = None
        self.nonmn4_address1 = None
        self.nonmn5_address1 = None
        self.nonmn6_address1 = None
        self.creator_pastelid1 = None
        self.ticket_principal_signature = None

        self.total_copies = 2
        self.tickets[TicketType.NFT].item_price = 1000


    def setup_chain(self):
        print(f"Initializing test directory {self.options.tmpdir}")
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)


    def setup_network(self, split=False):
        self.setup_masternodes_network(self.mining_node_num, self.hot_node_num, "masternode,mnpayments,governance,compress")
        self.inc_ticket_counter(TicketType.MNID, self.number_of_cold_nodes)


    def run_test(self):
        self.initialize()

        self.pose_ban_tests()
        self.ticket_expiration_tests()

    # ===============================================================================================================
    def initialize(self):
        print("== Initialize nodes ==")

        self.generate_and_sync_inc(10, self.mining_node_num)

        # generate pastelIDs & send coins
        self.creator_pastelid1 = self.create_pastelid(self.non_mn3)[0]
        print(f'Creator Pastel ID: {self.creator_pastelid1}')
        self.nonmn3_pastelid1 = self.create_pastelid(self.non_mn3)[0]
        self.nonmn3_address1 = self.nodes[self.non_mn3].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn3_address1, 1000, "", "", False)

        self.nonmn4_pastelid1 = self.create_pastelid(self.non_mn4)[0]
        self.nonmn4_address1 = self.nodes[self.non_mn4].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn4_address1, 100, "", "", False)

        self.nonmn5_pastelid1 = self.create_pastelid(self.non_mn5)[0]
        self.nonmn5_address1 = self.nodes[self.non_mn5].getnewaddress()
        self.nodes[self.mining_node_num].sendtoaddress(self.nonmn5_address1, 100, "", "", False)
        self.generate_and_sync_inc(1, self.mining_node_num)

        # register Pastel IDs
        register_ids = [
            ( self.non_mn3, self.creator_pastelid1, self.nonmn3_address1 ),
            ( self.non_mn3, self.nonmn3_pastelid1, self.nonmn3_address1 ),
            ( self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1 ),
            ( self.non_mn5, self.nonmn5_pastelid1, self.nonmn5_address1 )
        ]
        for idreg_info in register_ids:
            self.nodes[idreg_info[0]].tickets("register", "id", idreg_info[1], self.passphrase, idreg_info[2])
            self.generate_and_sync_inc(1, self.mining_node_num)
        self.inc_ticket_counter(TicketType.ID, len(register_ids))
        self.__wait_for_ticket_tnx(5)


    def pose_ban_tests(self):
        """ Test PoSe (Proof-of-Service) API.
            Use this API on mn2 to ban mn5.
        """
        print("Test PoSe ban score API")
        # get current PoSe ban score
        mn2 = self.mn_nodes[2]
        mn5 = self.mn_nodes[5]
        out = self.nodes[mn2.index].masternode("pose-ban-score", "get", mn5.collateral_txid, mn5.collateral_index)
        pose_ban_score = out["pose-ban-score"]
        assert_equal(0, pose_ban_score)
        assert_false(out["pose-banned"])
        
        current_height = self.nodes[mn2.index].getblockcount()
        print(f"current height - {current_height}")
        # now increment PoSe ban score 5 times for mn5
        pose_ban_height = 0
        for i in range(5):
            out = self.nodes[mn2.index].masternode("pose-ban-score", "increment", mn5.collateral_txid, mn5.collateral_index)
            pose_ban_score = out["pose-ban-score"]
            print(f"mn5 PoSe ban score: {pose_ban_score}")
            assert_equal(i + 1, pose_ban_score)
            if i == 4:
                assert_true(out["pose-banned"])
                pose_ban_height = out["pose-ban-height"]
                print(f"mn5 banned by PoSe until height {pose_ban_height}")
            else:
                assert_false(out["pose-banned"])
        assert_greater_than(pose_ban_height, current_height)
        for _ in range(current_height, pose_ban_height + 1):
            self.generate_and_sync_inc(1, self.mining_node_num)
            time.sleep(2)
        time.sleep(10)
        # check PoSe ban score again
        out = self.nodes[mn2.index].masternode("pose-ban-score", "get", mn5.collateral_txid, mn5.collateral_index)
        pose_ban_score = out["pose-ban-score"]
        assert_equal(4, pose_ban_score)
        assert_false(out["pose-banned"])
            

    def ticket_expiration_tests(self):
        self.register_nft_reg_ticket("nft-label")
        self.__wait_for_confirmation(self.non_mn3)

        self.register_nft_act_ticket()
        self.__wait_for_confirmation(self.non_mn3)

        ticket = self.tickets[TicketType.NFT]
        self.register_offer_ticket()
        offer_ticket1_txid = ticket.offer_txid
        print(f"offer ticket 1 txid {offer_ticket1_txid}")
        self.__wait_for_confirmation(self.non_mn3)

        if (self.total_copies == 1):
            # fail if not enough copies to offer
            assert_raises_rpc(rpc.RPC_MISC_ERROR,
                "Invalid Offer ticket - copy number [2] cannot exceed the total number of available copies [1] or be 0",
                self.register_offer_ticket)

        # fail as the replace copy can be created after 5 days
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"Can only replace Offer ticket after 5 days, txid - [{offer_ticket1_txid}] copyNumber [1]",
            self.register_offer_ticket, 1)
        
        chunk_size = 10
        chunk_count = int(300/chunk_size)
        print(f"Generate 300 blocks that is > 5 days. 1 chunk is {chunk_size} blocks, total {chunk_count} chunks")
        for ind in range(chunk_count):
            print(f"chunk - {ind + 1}")
            self.generate_and_sync_inc(chunk_size, self.mining_node_num)
            time.sleep(5)

        self.register_offer_ticket(1)
        offer_ticket2_txid = ticket.offer_txid
        print(f"offer ticket 2 txid {offer_ticket2_txid}")

        offer_ticket3_txid = None
        if self.total_copies > 1:
            self.register_offer_ticket()
            offer_ticket3_txid = ticket.offer_txid
            print(f"offer ticket 3 txid {offer_ticket3_txid}")

            offer_ticket1_1 = self.nodes[self.non_mn3].tickets("find", "offer", ticket.act_txid + ":2")
            assert_equal(offer_ticket1_1['ticket']['type'], "offer")
            assert_equal(offer_ticket1_1['ticket']['item_txid'], ticket.act_txid)
            assert_equal(offer_ticket1_1["ticket"]["copy_number"], 2)

            offer_ticket1_2 = self.nodes[self.non_mn3].tickets("get", offer_ticket3_txid)
            assert_equal(offer_ticket1_2["ticket"]["item_txid"], offer_ticket1_1["ticket"]["item_txid"])
            assert_equal(offer_ticket1_2["ticket"]["copy_number"], offer_ticket1_1["ticket"]["copy_number"])

        self.__send_coins_to_accept(self.non_mn4, self.nonmn4_address1, 5)

        if offer_ticket2_txid:
            # fail if old offer ticket has been replaced
            ticket.offer_txid = offer_ticket1_txid
            assert_raises_rpc(rpc.RPC_MISC_ERROR,
               f"This Offer ticket has been replaced with another ticket, txid - [{offer_ticket2_txid}], copyNumber [1]",
               self.register_accept_ticket, self.non_mn4, self.nonmn4_pastelid1)

        ticket.offer_txid = offer_ticket2_txid
        self.register_accept_ticket(self.non_mn4, self.nonmn4_pastelid1)
        accept_ticket1_txid = ticket.accept_txid

        # fail if there is another Accept ticket1 created < 1 hour ago
        ticket.offer_txid = offer_ticket2_txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"Accept ticket [{accept_ticket1_txid}] already exists and is not yet 1h old for this Offer ticket [{offer_ticket2_txid}]",
            self.register_accept_ticket, self.non_mn4, self.nonmn4_pastelid1)

        print("Generate 30 blocks that is > 1 hour. 1 chunk is 10 blocks")
        for ind in range(3):
            print(f"chunk - {ind + 1}")
            self.generate_and_sync_inc(10, self.mining_node_num)
            time.sleep(5)

        self.register_accept_ticket(self.non_mn4, self.nonmn4_pastelid1)
        accept_ticket2_txid = ticket.accept_txid

        # fail if there is another Accept ticket2 created < 1 hour ago
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"Accept ticket [{accept_ticket2_txid}] already exists and is not yet 1h old for this Offer ticket [{offer_ticket2_txid}]",
            self.register_accept_ticket, self.non_mn4, self.nonmn4_pastelid1)

        print("Generate 30 blocks that is > 1 hour. 1 chunk is 10 blocks")
        for ind in range (3):
            print(f"chunk - {ind + 1}")
            self.generate_and_sync_inc(10, self.mining_node_num)
            time.sleep(5)

        self.register_accept_ticket(self.non_mn4, self.nonmn4_pastelid1)
        accept_ticket3_txid = ticket.accept_txid

        # fail if old accept ticket1 has been replaced
        ticket.offer_txid = offer_ticket2_txid
        ticket.accept_txid = accept_ticket1_txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"This Accept ticket has been replaced with another ticket, txid - [{accept_ticket3_txid}]",
            self.register_transfer_ticket,
            self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1)

        # fail if old accept ticket2 has been replaced
        ticket.offer_txid = offer_ticket2_txid
        ticket.accept_txid = accept_ticket2_txid
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"This Accept ticket has been replaced with another ticket, txid - [{accept_ticket3_txid}]",
            self.register_transfer_ticket, 
            self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1)

        ticket.offer_txid = offer_ticket2_txid
        ticket.accept_txid = accept_ticket3_txid
        self.register_transfer_ticket(self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1)

        # fail if there is another Transfer ticket referring to that offer ticket
        assert_raises_rpc(rpc.RPC_MISC_ERROR,
            f"Transfer ticket already exists for the Offer ticket with this txid [{offer_ticket2_txid}]",
            self.register_transfer_ticket,
            self.non_mn3, self.non_mn4, self.nonmn4_pastelid1, self.nonmn4_address1)

        if self.total_copies == 1:
            assert_raises_rpc(rpc.RPC_MISC_ERROR,
                f"The NFT you are trying to offer - from NFT Registration ticket [{ticket.act_txid}]"
                " - is already offered - there are already [1] offered copies, "
                "but only [1] copies were available",
                self.register_offer_ticket, 1)
        elif self.total_copies == 2:
            # fail if not enough copies to offer
            assert_raises_rpc(rpc.RPC_MISC_ERROR,
                "Invalid Offer ticket - copy number [3] cannot exceed the total number of available copies [2] or be 0",
                self.register_offer_ticket)

            assert_raises_rpc(rpc.RPC_MISC_ERROR,
                f"Cannot replace Offer ticket - it has been already transferred, txid - [{ticket.offer_txid}], copyNumber [1]",
                self.register_offer_ticket, 1)

            # fail as the replace copy can be created after 5 days
            assert_raises_rpc(rpc.RPC_MISC_ERROR,
                f"Can only replace Offer ticket after 5 days, txid - [{offer_ticket3_txid}] copyNumber [2]",
                self.register_offer_ticket, 2)


    def register_nft_reg_ticket(self, label):
        print("== Create the NFT registration ticket ==")

        self.create_nft_ticket_v1(self.non_mn3, self.total_copies, self.royalty, self.is_green)
        ticket = self.tickets[TicketType.NFT]
        
        top_mn_node = self.nodes[self.top_mns[0].index]
        ticket.reg_txid = top_mn_node.tickets("register", "nft",
            ticket.reg_ticket, json.dumps(self.signatures_dict), self.top_mns[0].pastelid, self.passphrase,
            label, str(self.storage_fee))["txid"]
        print(ticket.reg_txid)
        assert_true(ticket.reg_txid, "No NFT registration ticket was created")

        self.__wait_for_ticket_tnx()
        print(top_mn_node.getblockcount())


    # ===============================================================================================================
    def register_nft_act_ticket(self):
        print("== Create the nft activation ticket ==")

        ticket = self.tickets[TicketType.NFT]
        ticket.act_txid = self.nodes[self.non_mn3].tickets("register", "act", ticket.reg_txid,
                                             str(ticket.reg_height), str(self.storage_fee),
                                             self.creator_pastelid1, self.passphrase)["txid"]
        assert_true(ticket.act_txid, "No NFT Registration ticket was created")
        self.__wait_for_ticket_tnx()


    # ===============================================================================================================
    def register_offer_ticket(self, copy_number: int = 0):
        print(f"== Create Offer ticket (copy number: {copy_number})==")

        ticket = self.tickets[TicketType.NFT]
        ticket.offer_txid = self.nodes[self.non_mn3].tickets("register", "offer", ticket.act_txid, str(ticket.item_price),
                                             ticket.reg_pastelid, self.passphrase, 0, 0, copy_number)["txid"]
        assert_true(ticket.offer_txid, "No Offer ticket was created")
        self.__wait_for_ticket_tnx()


    # ===============================================================================================================
    def __send_coins_to_accept(self, node, address, num):
        nft_price = self.tickets[TicketType.NFT].item_price
        cover_price = nft_price + max(10, int(nft_price / 100)) + 5

        self.nodes[self.mining_node_num].sendtoaddress(address, num * cover_price, "", "", False)
        self.__wait_for_confirmation(node)


    def register_accept_ticket(self, acceptor_node, acceptor_pastelid1):
        print("== Create Accept ticket ==")

        ticket = self.tickets[TicketType.NFT]
        ticket.accept_txid = self.nodes[acceptor_node].tickets("register", "accept", ticket.offer_txid, str(ticket.item_price),
                                           acceptor_pastelid1, self.passphrase)["txid"]
        assert_true(ticket.accept_txid, "No Accept ticket was created")
        self.__wait_for_ticket_tnx()


    # ===============================================================================================================
    def register_transfer_ticket(self, offerer_node, acceptor_node, acceptor_pastelid1, acceptor_address1):
        print("== Create Transfer ticket ==")

        ticket = self.tickets[TicketType.NFT]
        transfer_ticket = self.tickets[TicketType.TRANSFER]
        nft_price = ticket.item_price
        cover_price = nft_price + 10

        # sends coins back, keep 1 PSL to cover transaction fee
        self.__send_coins_back(self.non_mn4)
        self.__wait_for_sync_all10()

        acceptor_coins_before = self.nodes[acceptor_node].getbalance()
        print(acceptor_coins_before)
        assert_true(acceptor_coins_before > 0 and acceptor_coins_before < 1)

        self.nodes[self.mining_node_num].sendtoaddress(acceptor_address1, cover_price, "", "", False)
        self.__wait_for_confirmation(acceptor_node)

        acceptor_coins_before = self.nodes[acceptor_node].getbalance()
        print(acceptor_coins_before)

        offerer_pastel_id = self.nodes[offerer_node].tickets("get", ticket.offer_txid)["ticket"]["pastelID"]
        print(offerer_pastel_id)
        offerer_address = self.nodes[offerer_node].tickets("find", "id", offerer_pastel_id)["ticket"]["address"]
        print(offerer_address)
        offerer_coins_before = self.nodes[offerer_node].getreceivedbyaddress(offerer_address)

        # consolidate funds into single address
        consaddress = self.nodes[acceptor_node].getnewaddress()
        self.nodes[acceptor_node].sendtoaddress(consaddress, acceptor_coins_before, "", "", True)

        ticket.transfer_txid = self.nodes[acceptor_node].tickets("register", "transfer", ticket.offer_txid, ticket.accept_txid,
                                           acceptor_pastelid1, self.passphrase)["txid"]
        assert_true(ticket.transfer_txid, "No Transfer ticket was created")
        self.__wait_for_ticket_tnx(10)

        # check correct amount of change and correct amount spent
        acceptor_coins_after = self.nodes[acceptor_node].getbalance()
        print(f"acceptor: {acceptor_coins_before} -> {acceptor_coins_after}")
        # ticket cost is Transfer ticket price, NFT cost is nft_price
        assert_true(math.isclose(acceptor_coins_after, acceptor_coins_before - transfer_ticket.ticket_price - nft_price, rel_tol=0.005))

        # check that current owner gets correct amount
        offerer_coins_after = self.nodes[offerer_node].getreceivedbyaddress(offerer_address)
        print(f"offerer: {offerer_coins_before} -> {offerer_coins_after}")
        offerer_coins_expected_to_receive = nft_price
        if self.is_green: # green fee is 2% of item price
            offerer_coins_expected_to_receive -= round(nft_price / 50)
        print(f"Current owner is expected to receive {offerer_coins_expected_to_receive} PSL")
        assert_true(math.isclose(offerer_coins_after - offerer_coins_before, offerer_coins_expected_to_receive, rel_tol=0.005))

        # from another node - get ticket transaction and check
        #   - there are 2 possible outputs to current owner
        transfer_ticket_hash = self.nodes[0].getrawtransaction(ticket.transfer_txid)
        transfer_ticket_tx = self.nodes[0].decoderawtransaction(transfer_ticket_hash)
        offerer_amount = 0
        multi_fee = 0

        for v in transfer_ticket_tx["vout"]:
            if v["scriptPubKey"]["type"] == "multisig":
                multi_fee += v["value"]
            if v["scriptPubKey"]["type"] == "pubkeyhash":
                amount = v["value"]
                print(f"transfer transaction pubkeyhash vout - {amount}")
                if v["scriptPubKey"]["addresses"][0] == offerer_address:
                    offerer_amount += amount
                    print(f"transfer transaction to current owner's address - {amount}")
        print(f"transfer transaction multisig fee_amount - {multi_fee}")
        assert_true(math.isclose(offerer_amount, offerer_coins_expected_to_receive, rel_tol=0.005))
        assert_equal(multi_fee, self.tickets[TicketType.ID].ticket_price)


    def __send_coins_back(self, node):
        coins_after = self.nodes[node].getbalance()
        if coins_after > 1:
            mining_node_address1 = self.nodes[self.mining_node_num].getnewaddress()
            self.nodes[node].sendtoaddress(mining_node_address1, coins_after - 1, "", "", False)


    def __wait_for_ticket_tnx(self, blocks: int = 5):
        time.sleep(10)
        for _ in range(blocks):
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
        self.generate_and_sync_inc(10, self.mining_node_num)
        time.sleep(2)
        print(f"block count - {self.nodes[node].getblockcount()}")


if __name__ == '__main__':
    MasterNodeTicketsTest().main()
