#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_true, 
)
import hashlib
import string
import random
from test_framework.authproxy import JSONRPCException
from decimal import Decimal, getcontext
getcontext().prec = 16

class PastelTestFramework (BitcoinTestFramework):
    passphrase = "passphrase"
    new_passphrase = "new passphrase"

    # create new PastelID and associated LegRoast keys on node node_no
    # returns PastelID
    def create_pastelid(self, node_no = 0):
        keys = self.nodes[node_no].pastelid("newkey", self.passphrase)
        assert_true(keys["pastelid"], f"No PastelID was created on node #{node_no}")
        assert_true(keys["legRoastKey"], f"No LegRoast public key was generated on node #{node_no}")
        return keys["pastelid"], keys["legRoastKey"]

    def get_rand_testdata(self, scope, length):
        return ''.join(random.choice(scope) for i in range(length))

    def get_random_mock_hash(self):
        letters = string.ascii_letters
        value_hashed = self.get_rand_testdata(letters, 10)
        encoded_value = value_hashed.encode()
        return hashlib.sha3_256(encoded_value).hexdigest()
