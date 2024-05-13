#!/usr/bin/env python3
# Copyright (c) 2018-2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

import hashlib
import string
import random
from collections import namedtuple

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_true,
)
from ticket_type import TicketType

# ===============================================================================================================
RegisterResultInfo = namedtuple('RegisterResultInfo',
    ['txid', 'key'],
    defaults=['', ''])

class PastelTestFramework (BitcoinTestFramework):
    """
    Common class for testing Pastel Framework.
    """

    passphrase = "passphrase"
    new_passphrase = "new passphrase"

    # error strings
    ERR_READ_PASTELID_FILE = "Failed to read Pastel secure container file"
    ERR_INVALID_PASS = "Passphrase is invalid"


    def __init__(self):
        super().__init__()

        # registered ticket counters
        self._ticket_counters = {}
        self._offer_counters = {}
        self._accept_counters = {}
        self._transfer_counters = {}


    def ticket_counter(self, ticket_type: TicketType) -> int:
        """Get counter for registered tickets of the given type.

        Args:
            ticket_type (TicketType): ticket type

        Returns:
            int: number of registered tickets of the given type
        """
        if ticket_type in self._ticket_counters:
            return self._ticket_counters[ticket_type]
        else:
            return 0


    def _get_add_counter(self, ticket_type: TicketType):
        counters = None
        if ticket_type == TicketType.OFFER:
            counters = self._offer_counters
        elif ticket_type == TicketType.ACCEPT:
            counters = self._accept_counters
        elif ticket_type == TicketType.TRANSFER:
            counters = self._transfer_counters
        return counters


    def offer_counter(self, item_type: TicketType) -> int:
        """Get number of offers for the given item type.

        Args:
            item_type (TicketType): item ticket type

        Returns:
            int: number of offers for the given item type
        """
        if item_type in self._offer_counters:
            return self._offer_counters[item_type]
        else:
            return 0


    def accept_counter(self, item_type: TicketType) -> int:
        """Get number of accepts for the given item type.

        Args:
            item_type (TicketType): item ticket type

        Returns:
            int: number of accepts for the given item type
        """
        if item_type in self._accept_counters:
            return self._accept_counters[item_type]
        else:
            return 0


    def transfer_counter(self, item_type: TicketType) -> int:
        """Get number of transfers for the given item type.

        Args:
            item_type (TicketType): item ticket type

        Returns:
            int: number of transfers for the given item type
        """
        if item_type in self._transfer_counters:
            return self._transfer_counters[item_type]
        else:
            return 0


    def inc_ticket_counter(self, ticket_type: TicketType, count: int = 1, item_type: TicketType = None):
        """Increase counter for the given ticket type.

        Args:
            ticket_type (TicketType): ticket type
            count (int, optional): number of registered tickets. Defaults to 1.
            item_type (TicketType, optional): item type for tickets like offer,accept and transfer.
        """
        if ticket_type in self._ticket_counters:
            self._ticket_counters[ticket_type] += count
        else:
            self._ticket_counters[ticket_type] = count
        print(f'{ticket_type} tickets: {self.ticket_counter(ticket_type)}')
        counters = None
        if item_type is not None:
            counters = self._get_add_counter(ticket_type)
        if counters is not None:
            if item_type in counters:
                counters[item_type] += count
            else:
                counters[item_type] = count
            print(f"{ticket_type} tickets for {item_type.description}: {counters[item_type]}")


    def list_all_ticket_counters(self):
        """List all ticket counters.
        """
        print('+=== Counters for registered tickets ===')
        for name, member in TicketType.__members__.items():
            print(f'|{name:>28} : {self.ticket_counter(member)}')
            counters = self._get_add_counter(member)
            if counters is not None:
                for key, value in counters.items():
                    print(f"| >> {name} - {key.description} : {value}")
        print('+=======================================')


    def create_pastelid(self, node_no = 0):
        """Create new Pastel ID and associated LegRoast keys on node node_no.

        Args:
            node_no (int, optional): node to create Pastel ID on. Defaults to 0.

        Returns:
            tuple(pastelid, legRoastKey): returns tuple with generated Pastel ID and LegRoast key
        """
        keys = self.nodes[node_no].pastelid("newkey", self.passphrase)
        assert_true(keys["pastelid"], f"No Pastel ID was created on node #{node_no}")
        assert_true(keys["legRoastKey"], f"No LegRoast public key was generated on node #{node_no}")
        return keys["pastelid"], keys["legRoastKey"]


    def get_rand_testdata(self, scope, length):
        return ''.join(random.choice(scope) for i in range(length))


    def get_random_mock_hash(self) -> str:
        letters = string.ascii_letters
        value_hashed = self.get_rand_testdata(letters, 10)
        encoded_value = value_hashed.encode()
        return hashlib.sha3_256(encoded_value).hexdigest()


    def get_random_txid(self) -> str:
        return ('%064x' % random.getrandbits(256))

