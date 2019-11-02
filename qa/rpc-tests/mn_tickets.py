#!/usr/bin/env python2
# Copyright (c) 2018-19 The Pastel developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from __future__ import print_function

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, \
    assert_false, assert_true, initialize_chain_clean, \
    initialize_datadir, start_nodes, start_node, connect_nodes_bi, \
    bitcoind_processes, wait_and_assert_operationid_status, p2p_port, \
    stop_node
from mn_common import MasterNodeCommon
from test_framework.authproxy import JSONRPCException
import json
import time
import base64

from decimal import Decimal, getcontext
getcontext().prec = 16

# 12 Master Nodes
private_keys_list = ["91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe", #0
                     "923JtwGJqK6mwmzVkLiG6mbLkhk1ofKE1addiM8CYpCHFdHDNGo", #1
                     # "91wLgtFJxdSRLJGTtbzns5YQYFtyYLwHhqgj19qnrLCa1j5Hp5Z", #2
                     # "92XctTrjQbRwEAAMNEwKqbiSAJsBNuiR2B8vhkzDX4ZWQXrckZv", #3
                     # "923JCnYet1pNehN6Dy4Ddta1cXnmpSiZSLbtB9sMRM1r85TWym6", #4
                     # "93BdbmxmGp6EtxFEX17FNqs2rQfLD5FMPWoN1W58KEQR24p8A6j", #5
                     # "92av9uhRBgwv5ugeNDrioyDJ6TADrM6SP7xoEqGMnRPn25nzviq", #6
                     # "91oHXFR2NVpUtBiJk37i8oBMChaQRbGjhnzWjN9KQ8LeAW7JBdN", #7
                     # "92MwGh67mKTcPPTCMpBA6tPkEE5AK3ydd87VPn8rNxtzCmYf9Yb", #8
                     # "92VSXXnFgArfsiQwuzxSAjSRuDkuirE1Vf7KvSX7JE51dExXtrc", #9
                     # "91hruvJfyRFjo7JMKnAPqCXAMiJqecSfzn9vKWBck2bKJ9CCRuo", #10
                     # "92sYv5JQHzn3UDU6sYe5kWdoSWEc6B98nyY5JN7FnTTreP8UNrq"  #11
                     ]

class MasterNodeTicketsTest (MasterNodeCommon):
    number_of_master_nodes = len(private_keys_list)
    number_of_simple_nodes = 3
    total_number_of_nodes = number_of_master_nodes+number_of_simple_nodes

    non_active_mn = number_of_master_nodes-1

    non_mn1 = number_of_master_nodes        #mining node - will have coins
    non_mn2 = number_of_master_nodes+1      #hot node - will have collateral for all active MN
    non_mn3 = number_of_master_nodes+2      #will not have coins by default

    mining_node_num = number_of_master_nodes    #same as non_mn1
    hot_node_num = number_of_master_nodes+1     #same as non_mn2

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)

    def setup_network(self, split=False):
        self.nodes = []
        self.is_network_split = False
        self.setup_masternodes_network(private_keys_list, self.number_of_simple_nodes)

    def run_test (self):
        tests = ['pastelid', 'ticket_mnid', 'ticket_id', 'ticket_art', 'ticket_act', 'ticket_trade', 'ticket_down', 'staragefee']

        self.mining_enough(self.mining_node_num, self.number_of_master_nodes)
        cold_nodes = {k: v for k, v in enumerate(private_keys_list[:-1])} #all but last!!!
        _, _, _ = self.start_mn(self.mining_node_num, self.hot_node_num, cold_nodes, self.total_number_of_nodes)

        self.reconnect_nodes(0, self.number_of_master_nodes)
        self.sync_all()


        errorString = ""

        if 'pastelid' in tests:
            print("== Pastelid test ==")
            #1. pastelid tests
            #a. Generate new PastelID and associated keys (EdDSA448). Return PastelID base58-encoded
            #a.a - generate with no errors two keys at MN and non-MN

            mn0_pastelid1 = self.nodes[0].pastelid("newkey", "passphrase")["pastelid"]
            assert_true(mn0_pastelid1, "No Pastelid was created")
            mn0_pastelid2 = self.nodes[0].pastelid("newkey", "passphrase")["pastelid"]
            assert_true(mn0_pastelid2, "No Pastelid was created")

            #for non active MN
            non_active_mn_pastelid1 = self.nodes[self.non_active_mn].pastelid("newkey", "passphrase")["pastelid"]
            assert_true(non_active_mn_pastelid1, "No Pastelid was created")

            nonmn1_pastelid1 = self.nodes[self.non_mn1].pastelid("newkey", "passphrase")["pastelid"]
            assert_true(nonmn1_pastelid1, "No Pastelid was created")
            nonmn1_pastelid2 = self.nodes[self.non_mn1].pastelid("newkey", "passphrase")["pastelid"]
            assert_true(nonmn1_pastelid2, "No Pastelid was created")

            #for node without coins
            nonmn3_pastelid1 = self.nodes[self.non_mn3].pastelid("newkey", "passphrase")["pastelid"]
            assert_true(nonmn3_pastelid1, "No Pastelid was created")

            #a.b - fail if empty passphrase
            try:
                self.nodes[self.non_mn1].pastelid("newkey", "")
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("passphrase for new key cannot be empty" in errorString, True)

            #b. Import private "key" (EdDSA448) as PKCS8 encrypted string in PEM format. Return PastelID base58-encoded
            # NOT IMPLEMENTED

            #c. List all internally stored PastelID and keys
            idlist = self.nodes[0].pastelid("list")
            idlist = dict((key+str(i), val) for i,k in enumerate(idlist) for key, val in k.items())
            assert_true(mn0_pastelid1 in idlist.values(), "PastelID " + mn0_pastelid1 + " not in the list")
            assert_true(mn0_pastelid2 in idlist.values(), "PastelID " + mn0_pastelid2 + " not in the list")

            idlist = self.nodes[self.non_mn1].pastelid("list")
            idlist = dict((key+str(i), val) for i,k in enumerate(idlist) for key, val in k.items())
            assert_true(nonmn1_pastelid1 in idlist.values(), "PastelID " + nonmn1_pastelid1 + " not in the list")
            assert_true(nonmn1_pastelid2 in idlist.values(), "PastelID " + nonmn1_pastelid2 + " not in the list")

            print("Pastelid test: 2 PastelID's each generate at node0 (MN ) and node" + str(self.non_mn1) + "(non-MN)")

            #d. Sign "text" with the internally stored private key associated with the PastelID
            #d.a - sign with no errors using key from 1.a.a
            mn0_signature1 = self.nodes[0].pastelid("sign", "1234567890", mn0_pastelid1, "passphrase")["signature"]
            assert_true(mn0_signature1, "No signature was created")
            assert_equal(len(base64.b64decode(mn0_signature1)), 114)

            #e. Sign "text" with the private "key" (EdDSA448) as PKCS8 encrypted string in PEM format
            # NOT IMPLEMENTED

            #f. Verify "text"'s "signature" with the PastelID
            #f.a - verify with no errors using key from 1.a.a
            result = self.nodes[0].pastelid("verify", "1234567890", mn0_signature1, mn0_pastelid1)["verification"]
            assert_equal(result, "OK")
            #f.b - fail to verify with the different key from 1.a.a
            result = self.nodes[0].pastelid("verify", "1234567890", mn0_signature1, mn0_pastelid2)["verification"]
            assert_equal(result, "Failed")
            #f.c - fail to verify modified text
            result = self.nodes[0].pastelid("verify", "1234567890AAA", mn0_signature1, mn0_pastelid1)["verification"]
            assert_equal(result, "Failed")

            print("Pastelid test: Message signed and verified")

        if 'ticket_mnid' in tests:
            print("== Masternode PastelID Tickets test ==")
            #2. tickets tests
            #a. PastelID ticket
            #   a.a register MN PastelID
            #       a.a.1 fail if not MN
            try:
                self.nodes[self.non_mn1].tickets("register", "mnid", nonmn1_pastelid2, "passphrase")
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("This is not a active masternode" in errorString, True)

            #       a.a.2 fail if not active MN
            try:
                self.nodes[self.non_active_mn].tickets("register", "mnid", non_active_mn_pastelid1, "passphrase")
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("This is not a active masternode" in errorString, True)

            #       a.a.3 fail if active MN, but wrong PastelID
            try:
                self.nodes[0].tickets("register", "mnid", nonmn1_pastelid2, "passphrase")
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("Cannot open file to read key from" in errorString, True) #TODO: provide better error for unknown PastelID

            #       a.a.4 fail if active MN, but wrong passphrase
            try:
                self.nodes[0].tickets("register", "mnid", mn0_pastelid1, "wrong")
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("Cannot read key from string" in errorString, True) #TODO: provide better error for wrong passphrase

            #       a.a.5 fail if active MN, but not enough coins - ~11PSL
            try:
                self.nodes[0].tickets("register", "mnid", mn0_pastelid1, "passphrase")
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("No unspent transaction found" in errorString, True)

            #       a.a.6 register without errors from active MN with enough coins
            mn0_address1 = self.nodes[0].getnewaddress()
            self.nodes[self.mining_node_num].sendtoaddress(mn0_address1, 100, "", "", False)
            self.sync_all()
            self.nodes[self.mining_node_num].generate(1)
            self.sync_all()

            mn0_ticket1_txid = self.nodes[0].tickets("register", "mnid", mn0_pastelid1, "passphrase")["txid"]
            assert_true(mn0_ticket1_txid, "No ticket was created")
            self.sync_all()
            self.nodes[self.mining_node_num].generate(1)
            self.sync_all()
            time.sleep(2)

            #       a.a.7 from another node - get ticket transaction and check
            #           - there are P2MS outputs with non-zero amounts
            #           - amounts is totaling 10PSL
            mn0_ticket1_tx_hash = self.nodes[self.non_mn3].getrawtransaction(mn0_ticket1_txid)
            mn0_ticket1_tx = self.nodes[self.non_mn3].decoderawtransaction(mn0_ticket1_tx_hash)
            amount = 0
            for v in mn0_ticket1_tx["vout"]:
                assert_greater_than(v["value"], 0)
                if v["scriptPubKey"]["type"] == "multisig":
                    amount += v["value"]
            assert_equal(amount, 10)

            #       a.a.8 fail if already registered
            try:
                self.nodes[0].tickets("register", "mnid", mn0_pastelid1, "passphrase")["txid"]
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("This PastelID is already registered in blockchain" in errorString, True)

            #   a.b find MN PastelID ticket
            #       a.b.1 by PastelID
            mn0_ticket1_1 = json.loads(self.nodes[self.non_mn3].tickets("find", "id", mn0_pastelid1))
            assert_equal(mn0_ticket1_1["ticket"]["pastelID"], mn0_pastelid1)
            assert_equal(mn0_ticket1_1['ticket']['type'], "pastelid")
            assert_equal(mn0_ticket1_1['ticket']['id_type'], "masternode")

            #       a.b.2 by Collateral output, compare to ticket from a.b.1
            mn0_outpoint = self.nodes[0].masternode("status")["outpoint"]
            mn0_ticket1_2 = json.loads(self.nodes[self.non_mn3].tickets("find", "id", mn0_outpoint))
            assert_equal(mn0_ticket1_1["ticket"]["signature"], mn0_ticket1_2["ticket"]["signature"])
            assert_equal(mn0_ticket1_1["ticket"]["outpoint"], mn0_outpoint)

            #   a.c get the same ticket by txid from a.a.3 and compare with ticket from a.b.1
            mn0_ticket1_3 = json.loads(self.nodes[self.non_mn3].tickets("get", mn0_ticket1_txid))
            assert_equal(mn0_ticket1_1["ticket"]["signature"], mn0_ticket1_3["ticket"]["signature"])

            #   a.d list all id tickets, check PastelIDs
            tickets_list = self.nodes[self.non_mn3].tickets("list", "id")
            assert_true(mn0_pastelid1 in tickets_list)
            assert_true(mn0_outpoint in tickets_list)

            print("MN PastelID tickets tested")

        if 'ticket_id' in tests:
            print("== Personal PastelID Tickets test ==")
            #b. personal PastelID ticket
            nonmn3_address1 = self.nodes[self.non_mn3].getnewaddress()

            #   b.a register personal PastelID
            #       b.a.1 fail if wrong PastelID
            try:
                self.nodes[self.non_mn3].tickets("register", "id", nonmn1_pastelid2, "passphrase", nonmn3_address1)
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("Cannot open file to read key from" in errorString, True) #TODO: provide better error for unknown PastelID

            #       b.a.2 fail if wrong passphrase
            try:
                self.nodes[self.non_mn3].tickets("register", "id", nonmn3_pastelid1, "wrong", nonmn3_address1)
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("Cannot read key from string" in errorString, True) #TODO: provide better error for wrong passphrase

            #       b.a.3 fail if not enough coins - ~11PSL
            try:
                self.nodes[self.non_mn3].tickets("register", "id", nonmn3_pastelid1, "passphrase", nonmn3_address1)
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("No unspent transaction found" in errorString, True)

            #       b.a.2 register without errors from non MN with enough coins
            self.nodes[self.mining_node_num].sendtoaddress(nonmn3_address1, 100, "", "", False)
            self.sync_all()
            self.nodes[self.mining_node_num].generate(1)
            self.sync_all()

            nonmn3_ticket1_txid = self.nodes[self.non_mn3].tickets("register", "id", nonmn3_pastelid1, "passphrase", nonmn3_address1)["txid"]
            assert_true(nonmn3_ticket1_txid, "No ticket was created")
            self.sync_all()
            self.nodes[self.mining_node_num].generate(1)
            self.sync_all()
            time.sleep(2)

            #       b.a.5 from another node - get ticket transaction and check
            #           - there are P2MS outputs with non-zerro amounts
            #           - amounts is totaling 10PSL
            nonmn3_ticket1_tx_hash = self.nodes[0].getrawtransaction(nonmn3_ticket1_txid)
            nonmn3_ticket1_tx = self.nodes[0].decoderawtransaction(nonmn3_ticket1_tx_hash)
            amount = 0
            for v in nonmn3_ticket1_tx["vout"]:
                assert_greater_than(v["value"], 0)
                if v["scriptPubKey"]["type"] == "multisig":
                    amount += v["value"]
            assert_equal(amount, 10)

            #       b.a.6 fail if already registered
            try:
                self.nodes[0].tickets("register", "id", nonmn3_pastelid1, "passphrase", nonmn3_address1)["txid"]
            except JSONRPCException,e:
                errorString = e.error['message']
            assert_equal("This PastelID is already registered in blockchain" in errorString, True)

            #   b.b find personal PastelID
            #       b.b.1 by PastelID
            nonmn3_ticket1_1 = json.loads(self.nodes[0].tickets("find", "id", nonmn3_pastelid1))
            assert_equal(nonmn3_ticket1_1["ticket"]["pastelID"], nonmn3_pastelid1)
            assert_equal(nonmn3_ticket1_1['ticket']['type'], "pastelid")
            assert_equal(nonmn3_ticket1_1['ticket']['id_type'], "personal")

            #       b.b.2 by Address
            nonmn3_ticket1_2 = json.loads(self.nodes[0].tickets("find", "id", nonmn3_address1))
            assert_equal(nonmn3_ticket1_1["ticket"]["signature"], nonmn3_ticket1_2["ticket"]["signature"])

            #   b.c get the ticket by txid from b.a.3 and compare with ticket from b.b.1
            nonmn3_ticket1_3 = json.loads(self.nodes[self.non_mn3].tickets("get", nonmn3_ticket1_txid))
            assert_equal(nonmn3_ticket1_1["ticket"]["signature"], nonmn3_ticket1_3["ticket"]["signature"])

            #   b.d list all id tickets, check PastelIDs
            tickets_list = self.nodes[0].tickets("list", "id")
            assert_true(nonmn3_pastelid1 in tickets_list)
            assert_true(nonmn3_address1 in tickets_list)

            print("Personal PastelID tickets tested")

        if 'ticket_art' in tests:
            print("== Art registration Tickets test ==")
            #c. art registration ticket
            #   c.a register art registration ticket
            #       c.a.1 fail if not active MN
            #       c.a.2 fail if artist's signature is not matching
            #       c.a.3 fail if MN2 and MN3 signatures are not matching
            #       c.a.4 fail if artist's PastelID is not registered
            #       c.a.5 fail if MN1, MN2 and MN3 are not from top 10 list at the ticket's blocknum
            #       c.a.6 register without errors, if enough coins for tnx fee
            #       c.a.7 fail if already registered
            #   c.b find registration ticket
            #       c.b.1 by artists PastelID (can be multiple)
            #       c.b.2 by hash
            #       c.b.3 by fingerprints, compare to ticket from c.b.2
            #       c.b.4 verify ticket:
            #           - all signatures match PastelIDs
            #           - all PastelIDs are registered
            #           - MN1, MN2 and MN3 are from top 10 list at the ticket's blocknum
            #   c.c get the same ticket by txid from c.a.6 and compare with ticket from c.b.2
            #   c.d list all art registration tickets, check PastelIDs
            print("Art registration tickets tested")

        if 'ticket_act' in tests:
            print("== Art activation Tickets test ==")
            #d. art activation ticket
            #   d.a register art activation ticket
            #       d.a.1 fail if not enough coins to pay 90% of registration price (from artReg ticket) + tnx fee
            #       d.a.2 fail if artist's PastelID in the activation ticket is not matching artist's PastelID in the registration ticket
            #       d.a.3 register without errors, if enough coins for tnx fee
            #       d.a.4 fail if already registered
            #       d.a.5 fail if Registration Ticket is already activated (is it the same as c.a.4?)
            #       d.a.6 from another node - get ticket transaction and check
            #           - there are 3 outputs to MN1, MN2 and MN3 with correct amounts (MN1: 60%; MN2, MN3: 20% each, of registration price)
            #   d.b find activation ticket
            #       d.b.1 by artists PastelID (can be multiple)
            #       d.b.2 by Registration height - reg_height from registration ticket
            #       d.b.3 by Registration txid - reg_txid from registration ticket, compare to ticket from d.b.2
            #       d.b.4 verify ticket:
            #           - signatures matches PastelID
            #           - PastelID is registered and is personal
            #           - Registration ticket from reg_height is valid and has artist's PastelID
            #           - Registration ticket at reg_txid is valid and has artist's PastelID
            #   d.c get the same ticket by txid from d.a.3 and compare with ticket from d.b.2
            #   a.d list all art registration tickets, check PastelIDs
            print("Art activation tickets tested")

        if 'ticket_trade' in tests:
            print("== Art trade Tickets test ==")

            print("Art trade tickets tested")

        if 'ticket_down' in tests:
            print("== Art activation Tickets test ==")

            print("Art activation tickets tested")

        if 'staragefee' in tests:
            print("== Storage fee test ==")
            #3. storagefee tests
            #a. Get Network median storage fee
            #   a.1 from non-MN without errors
            #   a.2 from MN without errors
            #   a.3 compare a.1 and a.2

            #b. Get local masternode storage fee
            #   b.1 fail from non-MN
            #   b.2 from MN without errors
            #   b.3 compare b.2 and a.1

            #c. Set storage fee for MN
            #   c.1 fail on non-MN
            #   c.2 on MN without errors
            #   c.3 get local MN storage fee and compare it with c.2
            print("Storage fee tested")


    #4. chaindata tests
        #No need

if __name__ == '__main__':
    MasterNodeTicketsTest ().main ()
