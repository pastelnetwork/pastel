#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import math

from test_framework.util import assert_equal, assert_equals, assert_greater_than, \
    assert_true, assert_raises, assert_raises_message, assert_shows_help, start_nodes
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException

from decimal import Decimal, getcontext
getcontext().prec = 16

# error strings
ERR_READ_PASTELID_FILE = "Failed to read Pastel secure container file"

class SecureContainerTest(BitcoinTestFramework):
    passphrase = "passphrase"
    new_passphrase = "new passphrase"
    pastelid1 = None
    id1_lrkey = None
    pastelid2 = None

    def __init__(self):
        super().__init__()
        self.num_nodes = 1

    def setup_network(self):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir,
                                 extra_args=[[ '-debug' ]])
        self.is_network_split = False

    # create new PastelID and associated LegRoast keys on node node_no
    # returns PastelID
    def create_pastelid(self, LegRoastKeyVar = None):
        keys = self.nodes[0].pastelid("newkey", self.passphrase)
        NewPastelID = keys["pastelid"]
        assert_true(NewPastelID, f"No PastelID was created")
        if LegRoastKeyVar:
            LegRoastKeyVar = keys["legroast"]
            assert_true(LegRoastKeyVar, f"No LegRoast public key was generated")
        return NewPastelID

    def run_test(self):
        print("pastelid help")
        assert_shows_help(self.nodes[0].pastelid)

        print("pastelid newkey")
        assert_shows_help(self.nodes[0].pastelid, "newkey")
        self.pastelid1 = self.create_pastelid(self.id1_lrkey)
        self.pastelid2 = self.create_pastelid()

        # fail if empty passphrase
        try:
            self.nodes[0].pastelid("newkey", "")
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("passphrase for new key cannot be empty" in self.errorString, True)

        # List all internally stored PastelID and keys
        print("pastelid list")
        id_list = self.nodes[0].pastelid("list")
        id_list = dict((key+str(i), val) for i, k in enumerate(id_list) for key, val in k.items())
        assert_true(self.pastelid1 in id_list.values(), f"PastelID {self.pastelid1} not in the list")
        assert_true(self.pastelid2 in id_list.values(), f"PastelID {self.pastelid2} not in the list")

        print("pastelid passwd")
        assert_shows_help(self.nodes[0].pastelid, "passwd")
        text_to_sign = "my text to sign"
        # check that signing with existing passphrase works
        signature = self.nodes[0].pastelid("sign", text_to_sign, self.pastelid1, self.passphrase)["signature"]
        assert_true(signature, "Cannot sign text using existing passphrase. No ed448 signature was created")
        # verify signature
        result = self.nodes[0].pastelid("verify", text_to_sign, signature, self.pastelid1)["verification"]
        assert_equal(result, "OK")

        # missing new passphrase
        assert_raises(JSONRPCException, self.nodes[0].pastelid, "passwd", self.pastelid1, self.passphrase)
        # empty new passphrase
        assert_raises_message(JSONRPCException, "cannot be empty", self.nodes[0].pastelid, "passwd", self.pastelid1, self.passphrase, "")
        # empty Pastel ID
        assert_raises_message(JSONRPCException, "cannot be empty", self.nodes[0].pastelid, "passwd", "", self.passphrase, self.new_passphrase)
        # empty passphrase
        assert_raises_message(JSONRPCException, "cannot be empty", self.nodes[0].pastelid, "passwd", self.pastelid1, "", self.new_passphrase)
        # change passphrase
        result = self.nodes[0].pastelid("passwd", self.pastelid1, self.passphrase, self.new_passphrase)["result"]
        assert_equal(result, "successful")
        # try to sign text using old passphrase
        assert_raises_message(JSONRPCException, "Failed to decrypt", self.nodes[0].pastelid, "sign", text_to_sign, self.pastelid1, self.passphrase)
        # signing using new passphrase should work
        signature = self.nodes[0].pastelid("sign", text_to_sign, self.pastelid1, self.new_passphrase)["signature"]
        assert_true(signature, "Cannot sign text using existing passphrase. No ed448 signature was created")
        # verify signature
        result = self.nodes[0].pastelid("verify", text_to_sign, signature, self.pastelid1)["verification"]
        assert_equal(result, "OK")

if __name__ == '__main__':
    SecureContainerTest().main()