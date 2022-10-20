#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

import hashlib
from pkgutil import iter_modules
import string
import random
from enum import Enum
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_true,
)

# ===============================================================================================================
class TicketType(Enum):
    """ Pastel ticket types.
    TicketTypeName    | ID | Description | TypeName | TicketName | FolderName | Ticket Price
    """
    ID                      = 1, "Pastel ID", "id", "pastelid", None, 10
    MNID                    = 2, "MasterNode's Pastel ID", "mnid", "pastelid", None, 10
    NFT                     = 3, "NFT", "nft", "nft-reg", None, 10
    ACTIVATE                = 4, "NFT Activation", "act", "nft-act", "nft-act", 10
    OFFER                   = 5, "Offer", "offer", "offer", None, 10
    ACCEPT                  = 6, "Accept", "accept", "accept", None, 10
    TRANSFER                = 7, "Transfer", "transfer", "transfer", None, 10
    DOWN                    = 8, "TakeDown", "take-down", "take-down", None, 10
    ROYALTY                 = 9, "Royalty", "nft-royalty", "nft-royalty", None, 10
    USERNAME                = 10, "UserName Change", "username-change", "username-change", None, 10
    ETHERIUM_ADDRESS        = 11, "Etherium Address", "ethereum-address-change", "ethereum-address-change", None, 10
    SENSE_ACTION            = 12, "Sense Action", "action", "action-reg", "sense-result", 10
    SENSE_ACTION_ACTIVATE   = 13, "Sense Action Activation", "action-act", "action-act", "sense-act", 10
    CASCADE_ACTION          = 14, "Cascade Action", "action", "action-reg", "cascade-artifact", 10
    CASCADE_ACTION_ACTIVATE = 15, "Cascade Action Activation", "action-act", "action-act", "cascade-act", 10
    NFT_COLLECTION          = 16, "NFT Collection", "nft-collection", "nft-collection-reg", None, 10
    NFT_COLLECTION_ACTIVATE = 17, "NFT Collection Activation", "nft-collection-act", "nft-collection-act", None, 10

    def __new__(cls, *args, **kwds):
        obj = object.__new__(cls)
        obj._value_ = args[0]
        return obj

    def __init__(self, _: str, description: str, type_name: str = None, ticket_name: str = None,
        folder_name: str = None, ticket_price: int = 10):
        """ Initialize enum member.
            First parameters is ingnored since it' already set by __new__.

        Args:
            _ (str): enum value, ignored
            description (str): ticket type description. Defaults to None.
            type_name (str, optional): ticket type used in pasteld in RPC commands
            ticket_name (str, optional): ticket name used in pasteld
            folder_name (str, optional): name of the storage folder
            ticket_price (int, optional): ticket price in PSL
        """
        self._description_ = description
        self._type_name_ = type_name
        self._ticket_name_ = ticket_name
        self._folder_name_ = folder_name if folder_name else type_name
        self._ticket_price_ = ticket_price

    @property
    def description(self) -> str:
        """ Returns ticket type description.

        Returns:
            str: ticket type description
        """
        return self._description_

    @property
    def type_name(self) -> str:
        """ Returns ticket type name as used in pasteld.

        Returns:
            str: ticket type name
        """
        return self._type_name_

    @property
    def ticket_name(self) -> str:
        """ Returns ticket name as used in pasteld.

        Returns:
            str: ticket name
        """
        return self._ticket_name_

    @property
    def folder_name(self) -> str:
        """ Returns name of the storage folder for this ticket type.

        Returns:
            str: name of the storage folder
        """
        return self._folder_name_

    @property
    def ticket_price(self) -> int:
        """ Returns ticket price in PSL.

        Returns:
            int: ticket price in PSL
        """
        return self._ticket_price_


# ===============================================================================================================
class ActionType(Enum):
    """Pastel action types.
    ActionTypeName    | ID | RegTicketType | ActTicketType
    """
    SENSE   = 1, TicketType.SENSE_ACTION,   TicketType.SENSE_ACTION_ACTIVATE
    CASCADE = 2, TicketType.CASCADE_ACTION, TicketType.CASCADE_ACTION_ACTIVATE

    def __new__(cls, *args, **kwds):
        obj = object.__new__(cls)
        obj._value_ = args[0]
        return obj


    def __init__(self, _: str, reg_ticket_type: TicketType, act_ticket_type: TicketType):
        """ Initialize enum member.
            First parameters is ingnored since it' already set by __new__.

        Args:
            _ (str): enum value, ignored
            reg_ticket_type (TicketType): registration ticket type for this action
            act_ticket_type (TicketType): activation ticket type for this action
        """
        self._reg_ticket_type_ = reg_ticket_type
        self._act_ticket_type_ = act_ticket_type


    @property
    def reg_ticket_type(self) -> TicketType:
        """ Returns registration ticket type for the current action.

        Returns:
            TicketType: registration ticket type
        """
        return self._reg_ticket_type_


    @property
    def act_ticket_type(self) -> TicketType:
        """ Returns activation ticket type for the current action.

        Returns:
            TicketType: activation ticket type
        """
        return self._act_ticket_type_


def get_action_type(item_type: TicketType) -> ActionType:
    """ Get ActionType by TicketType.

    Args:
        item_type (TicketType): ticket type

    Returns:
        ActionType: returns ActionType for the given TicketType or None
    """
    if item_type == TicketType.SENSE_ACTION:
        action_type = ActionType.SENSE
    elif item_type == TicketType.CASCADE_ACTION:
        action_type = ActionType.CASCADE
    else:
        action_type = None
    return action_type


def get_activation_type(item_type: TicketType) -> TicketType:
    """Get activation type by TicketType.

    Args:
        item_type (TicketType): item TicketType

    Returns:
        TicketType: activation ticket type for the given TicketType or None
    """
    if item_type == TicketType.NFT:
        act_type = TicketType.ACTIVATE
    elif item_type == TicketType.SENSE_ACTION:
        act_type = TicketType.SENSE_ACTION_ACTIVATE
    elif item_type == TicketType.CASCADE_ACTION:
        act_type = TicketType.CASCADE_ACTION_ACTIVATE
    elif item_type == TicketType.NFT_COLLECTION:
        act_type = TicketType.NFT_COLLECTION_ACTIVATE
    return act_type


# ===============================================================================================================
class PastelTestFramework (BitcoinTestFramework):
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

