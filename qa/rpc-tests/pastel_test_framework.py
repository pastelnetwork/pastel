#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_true, 
)
import hashlib
import string
import random
from enum import (
    Enum, 
    auto
)
from test_framework.authproxy import JSONRPCException
from decimal import Decimal, getcontext
getcontext().prec = 16

# Pastel ticket types
class TicketType(Enum):
    ID = auto()
    MNID = auto()
    NFT = auto()
    ACTIVATE = auto()
    SELL = auto()
    BUY = auto()
    TRADE = auto()
    DOWN = auto()
    ROYALTY = auto()
    USERNAME = auto()
    ETHERIUM_ADDRESS = auto()
    ACTION = auto()
    ACTION_ACTIVATE = auto()
    NFT_COLLECTION = auto()
    NFT_COLLECTION_ACTIVATE = auto()

class PastelTestFramework (BitcoinTestFramework):
    passphrase = "passphrase"
    new_passphrase = "new passphrase"

    # error strings
    ERR_READ_PASTELID_FILE = "Failed to read Pastel secure container file"
    ERR_INVALID_PASS = "Passphrase is invalid"

    def __init__(self):
        super().__init__()

        # registered ticket counters
        self._ticket_counters = dict()

    def ticket_counter(self, ticket_type) -> int:
        if ticket_type in self._ticket_counters:
            return self._ticket_counters[ticket_type]
        else:
            return 0

    def inc_ticket_counter(self, ticket_type, count = 1):
        if ticket_type in self._ticket_counters:
            self._ticket_counters[ticket_type] += count
        else:
            self._ticket_counters[ticket_type] = count
        print(f'{ticket_type} tickets: {self.ticket_counter(ticket_type)}')

    def list_all_ticket_counters(self):
        print('+=== Counters for registered tickets ===')
        for name, member in TicketType.__members__.items():
            print(f'|{name:>24} : {self.ticket_counter(member)}')
        print('+=======================================')

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

    def get_random_txid(self):
        return ('%064x' % random.getrandbits(256))
