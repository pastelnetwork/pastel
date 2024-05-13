#!/usr/bin/env python3
# Copyright (c) 2018-2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
from enum import Enum
from typing import Optional

# switch to use either:
#       sell|buy|trade (False)
#   or
#       offer|accept|transfer (True)
USE_OAT = True

# ===============================================================================================================
class TicketType(Enum):
    """ Pastel ticket types.
    TicketTypeName          | ID | Description | TypeName | TicketName | FolderName | Ticket Price
    """
    ID                      = 1, "Pastel ID", "id", "pastelid", None, 10
    MNID                    = 2, "MasterNode's Pastel ID", "mnid", "pastelid", None, 10
    NFT                     = 3, "NFT", "nft", "nft-reg", None, 10
    ACTIVATE                = 4, "NFT Activation", "act", "nft-act", "nft-act", 10
    OFFER                   = 5, "Offer", "offer" if USE_OAT else "sell", "offer" if USE_OAT else "sell", None, 10
    ACCEPT                  = 6, "Accept", "accept" if USE_OAT else "buy", "accept" if USE_OAT else "buy", None, 10
    TRANSFER                = 7, "Transfer", "transfer" if USE_OAT else "trade", "transfer" if USE_OAT else "trade", None, 10
    DOWN                    = 8, "TakeDown", "take-down", "take-down", None, 10
    ROYALTY                 = 9, "Royalty", "nft-royalty", "nft-royalty", None, 10
    USERNAME                = 10, "UserName Change", "username-change", "username-change", None, 10
    ETHEREUM_ADDRESS        = 11, "Ethereum Address", "ethereum-address-change", "ethereum-address-change", None, 10
    SENSE_ACTION            = 12, "Sense Action", "action", "action-reg", "sense-result", 10
    SENSE_ACTION_ACTIVATE   = 13, "Sense Action Activation", "action-act", "action-act", "sense-act", 10
    CASCADE_ACTION          = 14, "Cascade Action", "action", "action-reg", "cascade-artifact", 10
    CASCADE_ACTION_ACTIVATE = 15, "Cascade Action Activation", "action-act", "action-act", "cascade-act", 10
    COLLECTION              = 16, "Collection", "collection", "collection-reg", None, 10
    COLLECTION_ACTIVATE     = 17, "Collection Activation", "collection-act", "collection-act", None, 10
    CONTRACT                = 18, "Contract", "contract", "contract", None, 10

    def __new__(cls, *args, **kwds):
        obj = object.__new__(cls)
        obj._value_ = args[0]
        return obj

    def __init__(self, _: int, description: str, type_name: Optional[str], ticket_name: Optional[str],
        folder_name: Optional[str], ticket_price: int = 10):
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


    def __str__(self):
        return self.name


    @property
    def description(self) -> str:
        """ Returns ticket type description.

        Returns:
            str: ticket type description
        """
        return self._description_


    @property
    def type_name(self) -> Optional[str]:
        """ Returns ticket type name as used in pasteld.

        Returns:
            str: ticket type name
        """
        return self._type_name_


    @property
    def ticket_name(self) -> Optional[str]:
        """ Returns ticket name as used in pasteld.

        Returns:
            str: ticket name
        """
        return self._ticket_name_


    @property
    def folder_name(self) -> Optional[str]:
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


    def __init__(self, _: int, reg_ticket_type: TicketType, act_ticket_type: TicketType):
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
        raise ValueError(f"Unsupported item type: {item_type}")
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
    elif item_type == TicketType.COLLECTION:
        act_type = TicketType.COLLECTION_ACTIVATE
    else:
        raise ValueError(f"Unsupported item type: {item_type}")
    return act_type


# ===============================================================================================================
class CollectionItemType(Enum):
    """Collection item types.
    CollectionItemType | ID | TypeDescription | TypeName | RegTicketType | ActTicketType
    """
    NFT   = 1, "NFT",    "nft",   TicketType.NFT,          TicketType.ACTIVATE
    SENSE = 2, "Sense",  "sense", TicketType.SENSE_ACTION, TicketType.SENSE_ACTION_ACTIVATE

    def __new__(cls, *args, **kwds):
        obj = object.__new__(cls)
        obj._value_ = args[0]
        return obj

    def __init__(self, _: int, type_description: str, type_name: str, reg_ticket_type: TicketType, act_ticket_type: TicketType):
        """ Initialize enum member.
            First parameters is ingnored since it' already set by __new__.

        Args:
            _ (str): enum value, ignored
            type_description (str): collection item type description
            type_name (str): collection item type name
            reg_ticket_type (TicketType): registration ticket type for this collection item
            act_ticket_type (TicketType): activation ticket type for this collection item
        """
        self._type_description_ = type_description
        self._type_name_ = type_name
        self._reg_ticket_type_ = reg_ticket_type
        self._act_ticket_type_ = act_ticket_type

    @property
    def type_description(self) -> str:
        """ Returns collection item type name.

        Returns:
            str: collection item type name
        """
        return self._type_description_
    
    
    @property
    def type_name(self) -> str:
        """ Returns collection item type name.

        Returns:
            str: collection item type name
        """
        return self._type_name_
    
    
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

