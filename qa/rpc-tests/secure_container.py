#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import math
import base64

from test_framework.util import assert_equal, assert_equals, assert_greater_than, \
    assert_true, assert_raises, assert_raises_message, assert_shows_help, start_nodes
from pastel_test_framework import PastelTestFramework
from test_framework.authproxy import JSONRPCException

from decimal import Decimal, getcontext
getcontext().prec = 16

class SecureContainerTest(PastelTestFramework):
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

    def run_test(self):
        print("---- Pastel ID tests STARTED ----")
        print(" -pastelid help")
        assert_shows_help(self.nodes[0].pastelid)

        print(" -pastelid newkey")
        assert_shows_help(self.nodes[0].pastelid, "newkey")
        self.pastelid1 = self.create_pastelid(0, self.id1_lrkey)
        self.pastelid2 = self.create_pastelid()

        # fail if empty passphrase
        try:
            self.nodes[0].pastelid("newkey", "")
        except JSONRPCException as e:
            self.errorString = e.error['message']
            print(self.errorString)
        assert_equal("passphrase for new key cannot be empty" in self.errorString, True)

        # List all internally stored PastelID and keys
        print(" -pastelid list")
        id_list = self.nodes[0].pastelid("list")
        id_list = dict((key+str(i), val) for i, k in enumerate(id_list) for key, val in k.items())
        assert_true(self.pastelid1 in id_list.values(), f"PastelID {self.pastelid1} not in the list")
        assert_true(self.pastelid2 in id_list.values(), f"PastelID {self.pastelid2} not in the list")

        text_to_sign = "my text to sign"
        # Sign "text" with the internally stored private key associated with the PastelID
        # check that signing with existing passphrase works, default algorithm - EdDSA448
        signature = self.nodes[0].pastelid("sign", text_to_sign, self.pastelid1, self.passphrase)["signature"]
        assert_true(signature, "Cannot sign text using existing passphrase. No ed448 signature was created")
        assert_equal(len(base64.b64decode(signature)), 114)
        # Verify text"'s "signature" (EdDSA448) with the PastelID
        result = self.nodes[0].pastelid("verify", text_to_sign, signature, self.pastelid1)["verification"]
        assert_equal(result, "OK")

        print(" -pastelid sign & verify")
        # Sign with no errors using encoded LegRoast public key
        # returns base64 encoded signature
        lr_signature = self.nodes[0].pastelid("sign", text_to_sign, self.pastelid1, self.passphrase, "legroast")["signature"]
        assert_true(lr_signature, "Cannot sign text using existing passphrase. No LegRoast signature was created")
        assert_equal(len(base64.b64decode(lr_signature)), 14272)
        # Verify text"'s "signature" (LegRoast) with the PastelID
        result = self.nodes[0].pastelid("verify", text_to_sign, lr_signature, self.pastelid1, "legroast")["verification"]
        assert_equal(result, "OK")

        # Fail to verify EdDSA448 signature with the different key (PastelID2)
        result = self.nodes[0].pastelid("verify", text_to_sign, signature, self.pastelid2)["verification"]
        assert_equal(result, "Failed")
        # Fail to verify LegRoast signature with the different key (PastelID2)
        result = self.nodes[0].pastelid("verify", text_to_sign, lr_signature, self.pastelid2, "legroast")["verification"]
        assert_equal(result, "Failed")

        # Fail to verify modified text (ed448 signature)
        text_to_sign_modified = 'X' + text_to_sign[1:]
        result = self.nodes[0].pastelid("verify", text_to_sign_modified, signature, self.pastelid1)["verification"]
        assert_equal(result, "Failed")

        # Fail to verify modified text (LegRoast signature)
        result = self.nodes[0].pastelid("verify", text_to_sign_modified, lr_signature, self.pastelid1, "legroast")["verification"]
        assert_equal(result, "Failed")

        print(" -pastelid passwd")
        assert_shows_help(self.nodes[0].pastelid, "passwd")
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
        print("----- Pastel ID tests FINISHED -----")

if __name__ == '__main__':
    SecureContainerTest().main()