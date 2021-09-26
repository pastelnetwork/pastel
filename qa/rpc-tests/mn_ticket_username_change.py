#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
from test_framework.util import (
    initialize_chain_clean,
    assert_equal,
    assert_equals,
    assert_greater_than,
    assert_true,
    assert_raises,
    assert_raises_message,
    assert_raises_rpc,
    assert_shows_help,
)
from pastel_test_framework import PastelTestFramework
from test_framework.authproxy import JSONRPCException
import test_framework.rpc_consts as rpc

from decimal import Decimal, getcontext
getcontext().prec = 16

# error strings
ERR_READ_PASTELID_FILE = "Failed to read Pastel secure container file"

class UserNameChangeTest(PastelTestFramework):
    """
    Test Pastel change-username tickets
    """

    USERNAME_CHANGE_FEE_FIRST_TIME = 100
    USERNAME_CHANGE_FEE_SECOND_TIME = 5000
    CHANGE_DISABLED_BLOCK_COUNT = 10

    n1_pastelid1 = None
    n1_pastelid2 = None
    n2_pastelid = None
    n3_pastelid = None
    node1_balancer = None

    def list_username_tickets(self, nExpectedCount):
        result = self.nodes[0].tickets("list", "username")
        nTicketCount = len(result) if result else 0
        print(f"username-change tickets: {nTicketCount}")
        assert_equal(nExpectedCount, nTicketCount, f"Should have only {nExpectedCount} username-change ticket(s)")


    def check_username_change_ticket(self, txid, username, pastelid, fee):
        result = self.nodes[0].tickets("get", txid)
        tkt = result["ticket"]
        assert_equal(tkt["type"], "username-change", "invalid ticket type")
        assert_equal(tkt["username"], username, "invalid ticket username")
        assert_equal(tkt["pastelID"], pastelid, "invalid ticket pastelid")
        assert_equal(tkt["fee"], fee, "invalid ticket fee")
        print(f">> username '{username}'' is registered to {pastelid}, fee {fee}")


    def check_balance(self, delta):
        newbalance = self.nodes[1].getbalance()
        assert_equal(self.node1_balance - delta, newbalance, "node balance is not matching")
        self.node1_balance = newbalance


    def validate_username(self, username, errmsg, isBad = True):
        result = self.nodes[1].tickets("tools", "validateusername", username)
        print(result)
        assert_equal(result["isBad"], isBad, f"username [{username}] validation failed")
        assert_true(errmsg in result["validationError"], f"username [{username}] error message validation failed")


    def run_test(self):
        print("---- UserName-Change Ticket tests STARTED ----")
        print(" - Creating Pastel IDs")
        self.n1_pastelid1 = self.create_pastelid(1)
        self.n1_pastelid2 = self.create_pastelid(1)
        self.n2_pastelid = self.create_pastelid(2)
        self.n3_pastelid = self.create_pastelid(3)
        print(
            f"node1: pastelid1={self.n1_pastelid1}\n"
            f"node1: pastelid2={self.n1_pastelid2}\n"
            f"node2: pastelid={self.n2_pastelid}\n"
            f"node3: pastelid={self.n3_pastelid}"
        )

        coinbase_addr = []
        addr = []
        # step over coinbase address
        for i in range(self.num_nodes):
            coinbase_addr.append(self.nodes[i].getnewaddress())
            addr.append(self.nodes[i].getnewaddress())
        self.generate_and_sync_inc(1)

        # send all utxos from node #3 to addr[0] to make empty balance
        self.nodes[3].sendtoaddress(addr[0], self.nodes[3].getbalance(), "empty node3", "test", True)
        self.generate_and_sync_inc(1)

        print(" - Username registration")
        assert_shows_help(self.nodes[0].tickets, "register", "username")

        # valid user names
        username1 = "Morpheus"
        username2 = "Trinity"
        username3 = "Anderson"
        username4 = "AgentSmith"

        self.list_username_tickets(0)

        # missing pastel id
        assert_raises_rpc(rpc.RPC_INVALID_PARAMETER, "JSONRPC error", self.nodes[1].tickets, "register", "username", "neo")
        # missing passphrase
        assert_raises_rpc(rpc.RPC_INVALID_PARAMETER, "JSONRPC error", self.nodes[1].tickets, "register", "username", "neo", self.n1_pastelid1)
        # too short username < 4 chars
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Invalid size of username", self.nodes[1].tickets, "register", "username", "neo", self.n1_pastelid1, self.passphrase)
        # too long username > 12 chars
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Invalid size of username", self.nodes[1].tickets, "register", "username", "ThomasAnderson", self.n1_pastelid1, self.passphrase)
        # invalid start character in username - should start with the letter A-Z,a-z
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Invalid username", self.nodes[1].tickets, "register", "username", "1Morpheus", self.n1_pastelid1, self.passphrase)
        # invalid character in username - should contain only letters [A-Z,a-z] or digits [0-9]
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Invalid username", self.nodes[1].tickets, "register", "username", "Agent-Smith", self.n1_pastelid1, self.passphrase)
        # bad username
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Invalid username", self.nodes[1].tickets, "register", "username", "stupid", self.n1_pastelid1, self.passphrase)

        self.node1_balance = self.nodes[1].getbalance()
        # register first time username 'Morpheus' for n1_pastelid1 - keep it in mempool only
        result = self.nodes[1].tickets("register", "username", username1, self.n1_pastelid1, self.passphrase)
        print(result)
        txid = result["txid"]
        assert_true(txid, "No username-change ticket was created")

        # try to register the same username - mempool transaction exists with the same username
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "same username in the memory pool", self.nodes[1].tickets, "register", "username", username1, self.n1_pastelid1, self.passphrase)
        # try to register another username with the same pastel id - mempool transaction exists with the same pastel id
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "same PastelID in the memory pool", self.nodes[1].tickets, "register", "username", username2, self.n1_pastelid1, self.passphrase)
        # commit transaction for username1 change
        self.sync_all()
        self.generate_and_sync_inc(1)
        self.list_username_tickets(1)
        self.check_username_change_ticket(txid, username1, self.n1_pastelid1, self.USERNAME_CHANGE_FEE_FIRST_TIME)
        self.check_balance(self.USERNAME_CHANGE_FEE_FIRST_TIME)

        # now let's try to register same username again - username is already registered in the blockchain
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is already registered in blockchain", self.nodes[1].tickets, "register", "username", username1, self.n1_pastelid1, self.passphrase)
        # try to register same username with n2_pastelid2 on node2
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is already registered in blockchain", self.nodes[2].tickets, "register", "username", username1, self.n2_pastelid, self.passphrase)
        # try to register same username with n1_pastelid2 on node1
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is already registered in blockchain", self.nodes[1].tickets, "register", "username", username1, self.n1_pastelid2, self.passphrase)
        # using invalid pastelid - node2 using n1_pastelid
        assert_raises_rpc(rpc.RPC_MISC_ERROR, ERR_READ_PASTELID_FILE, self.nodes[2].tickets, "register", "username", username2, self.n1_pastelid1, self.passphrase)
        # using invalid passphrase 
        assert_raises_rpc(rpc.RPC_MISC_ERROR, ERR_READ_PASTELID_FILE, self.nodes[1].tickets, "register", "username", username2, self.n1_pastelid1, self.new_passphrase)

        # second username change too early - less than 10 blocks for regtest
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Already changed in last", self.nodes[1].tickets, "register", "username", username2, self.n1_pastelid1, self.passphrase)
        self.generate_and_sync_inc(self.CHANGE_DISABLED_BLOCK_COUNT)

        node1_balance = self.nodes[1].getbalance()
        # register second time username 'Trinity' for n1_pastelid1
        result = self.nodes[1].tickets("register", "username", username2, self.n1_pastelid1, self.passphrase)
        print(result)
        txid2 = result["txid"]
        assert_true(txid2, "No username-change ticket was created")

        # 'Morpheus' username registered to n1_pastelid1 is not active now (ticket transaction is in the mempool, but not yet in blockchain)
        # n1_pastelid2 can use this username now - first username change for this pastelid
        result = self.nodes[1].tickets("register", "username", username1, self.n1_pastelid2, self.passphrase)
        print(result)
        txid1 = result["txid"]
        assert_true(txid1, "No username-change ticket was created")
        # commit transactions for both username1 & username2 changes
        self.sync_all()
        self.generate_and_sync_inc(1)

        # check txid1,txid2 transactions
        self.list_username_tickets(2)
        self.check_username_change_ticket(txid1, username1, self.n1_pastelid2, self.USERNAME_CHANGE_FEE_FIRST_TIME)
        self.check_username_change_ticket(txid2, username2, self.n1_pastelid1, self.USERNAME_CHANGE_FEE_SECOND_TIME)
        self.check_balance(self.USERNAME_CHANGE_FEE_FIRST_TIME + self.USERNAME_CHANGE_FEE_SECOND_TIME)

        # not enough coins on node3
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Not enough coins", self.nodes[3].tickets, "register", "username", username3, self.n3_pastelid, self.passphrase)

        # send coins to node3, enough to change username one time
        self.nodes[0].sendtoaddress(addr[3], self.USERNAME_CHANGE_FEE_FIRST_TIME + 1)
        self.generate_and_sync_inc(1)
        result = self.nodes[3].tickets("register", "username", username3, self.n3_pastelid, self.passphrase)
        print(result)
        txid3 = result["txid"]
        assert_true(txid3, "No username-change ticket was created")
        self.sync_all()
        self.generate_and_sync_inc(1)
        # check txid3 transaction
        self.list_username_tickets(3)
        self.check_username_change_ticket(txid3, username3, self.n3_pastelid, self.USERNAME_CHANGE_FEE_FIRST_TIME)
        self.generate_and_sync_inc(self.CHANGE_DISABLED_BLOCK_COUNT)
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Not enough coins", self.nodes[3].tickets, "register", "username", username4, self.n3_pastelid, self.passphrase)

        print(" - UserName validation")
        # too short
        self.validate_username("neo", "Invalid size of username") 
        # too long
        self.validate_username("ThomasAnderson", "Invalid size of username")
        # should start with a letter
        self.validate_username("1Oracle", "Invalid username")
        # invalid character
        self.validate_username("Agent-Smith", "Invalid username")
        # bad word
        self.validate_username("ugly", "Invalid username")
        self.validate_username("aStuPiD", "Invalid username")
        # already registered
        self.validate_username(username1, "already registered")
        self.validate_username("Cypher", "", False)

        print("---- UserName-Change Ticket tests FINISHED ----")

if __name__ == '__main__':
    UserNameChangeTest().main()