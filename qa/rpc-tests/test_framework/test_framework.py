#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2018-2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

# Base class for RPC testing
from typing import List
import logging
import os
import sys
import shutil
import tempfile
import traceback
import argparse
from decimal import Decimal, getcontext

from .authproxy import JSONRPCException
from .util import (
    initialize_chain,
    start_nodes,
    connect_nodes_bi,
    sync_blocks,
    sync_mempools,
    stop_nodes,
    wait_pastelds,
    assert_equal,
    check_json_precision,
    initialize_chain_clean,
)

getcontext().prec = 16

node_id_0: int = 0
node_id_1: int = 1
node_id_2: int = 2
node_id_3: int = 3

class BitcoinTestFramework(object):
    _coin       = Decimal('100000')
    _maxmoney   = Decimal('21000000000')

    # Block reward in PSL
    _reward     = Decimal('6250')
    _reward00   = Decimal('6250.00')
    _highfee    = Decimal('1')
    _fee        = Decimal('0.1')
    _fee00      = Decimal('0.10')

    _null           = Decimal("0.00000")
    _patoshi         = Decimal('0.00001')
    _2patoshi        = Decimal('0.00002')
    _10patoshi       = Decimal('0.0001')
    _100patoshi      = Decimal('0.001')
    _1000patoshi     = Decimal('0.01')
    _10000patoshi    = Decimal('0.1')
    _1ani           = Decimal('1.0')


    def __init__(self):
        self.num_nodes = 4
        self.setup_clean_chain = False
        self.nodes = []
        self.is_network_split = False

    def run_test(self):
        for node in self.nodes:
            assert_equal(node.getblockcount(), 200)
            assert_equal(node.getbalance(), 25*self._reward)

    def add_options(self, parser):
        pass

    def setup_chain(self):
        print(f'Initializing test directory {self.options.tmpdir}')
        if self.setup_clean_chain:
            initialize_chain_clean(self.options.tmpdir, self.num_nodes)
        else:
            initialize_chain(self.options.tmpdir)

    def setup_nodes(self):
        return start_nodes(self.num_nodes, self.options.tmpdir)

    def setup_network(self, split = False):
        self.nodes = self.setup_nodes()

        # Connect the nodes as a "chain".  This allows us
        # to split the network between nodes 1 and 2 to get
        # two halves that can work on competing chains.

        # If we joined network halves, connect the nodes from the joint
        # on outward.  This ensures that chains are properly reorganised.
        if not split:
            connect_nodes_bi(self.nodes, 1, 2)
            max_node_no = 3 if self.num_nodes >= 4 else 2
            sync_blocks(self.nodes[1:max_node_no])
            sync_mempools(self.nodes[1:max_node_no])

        connect_nodes_bi(self.nodes, 0, 1)
        if self.num_nodes > 3:
            connect_nodes_bi(self.nodes, 2, 3)
        self.is_network_split = split
        self.sync_all()

    def split_network(self):
        """
        Split the network of four nodes into nodes 0/1 and 2/3.
        """
        assert not self.is_network_split
        stop_nodes(self.nodes)
        wait_pastelds()
        self.setup_network(True)

    def sync_all(self, wait: int = 1, stop_after: int = -1):
        """Synchronize all nodes (blocks and mempools).

        Args:
            wait (int, optional): wait in secs between block count/mempool checks. Defaults t  1 sec.
            stop_after (int, optional): _description_. Defaults to -1.
        """
        if self.is_network_split:
            sync_blocks(self.nodes[:2])
            sync_blocks(self.nodes[2:])
            sync_mempools(self.nodes[:2])
            sync_mempools(self.nodes[2:])
        else:
            sync_blocks(self.nodes, wait, stop_after)
            sync_mempools(self.nodes, wait, stop_after)

    def join_network(self):
        """
        Join the (previously split) network halves together.
        """
        assert self.is_network_split
        stop_nodes(self.nodes)
        wait_pastelds()
        self.setup_network(False)

    def generate_and_sync(self, new_height, node_id = 0) -> List[str]:
        """ Generate blocks up to new_height on node #node_id, sync all nodes.
        
        Args:
            new_height (int): height to generate blocks up to
            node_id (int, optional): node number to generate blocks on. Defaults to 0.
            
        Returns:
            List[str]: list of hashes for generated blocks
        """
        current_height = self.nodes[node_id].getblockcount()
        assert(new_height > current_height)
        list_of_block_hashes = self.nodes[node_id].generate(new_height - current_height)
        self.sync_all()
        assert_equal(new_height, self.nodes[node_id].getblockcount())
        return list_of_block_hashes


    def generate_and_sync_inc(self, nblocks = 1, node_id = 0, mnid: str = None) -> List[str]:
        """Generate nblocks on node #node_id, sync all nodes.

        Args:
            nblocks (int, optional): number of blocks to generate. Defaults to 1.
            node_id (int, optional): node number to generate blocks on. Defaults to 0.
            
        Returns:
            List[str]: list of hashes for generated blocks
        """
        current_height = self.nodes[node_id].getblockcount()
        self.sync_all()
        if mnid:
            list_of_block_hashes = self.nodes[node_id].generate(nblocks, mnid)
        else:
            list_of_block_hashes = self.nodes[node_id].generate(nblocks)
        self.sync_all()
        assert_equal(current_height + nblocks, self.nodes[node_id].getblockcount())
        return list_of_block_hashes


    def main(self):

        parser = argparse.ArgumentParser(usage="%prog [options]")
        parser.add_argument("--nocleanup", dest="nocleanup", default=False, action="store_true",
                          help="Leave pastelds and test.* datadir on exit or error")
        parser.add_argument("--noshutdown", dest="noshutdown", default=False, action="store_true",
                          help="Don't stop pastelds after the test execution")
        parser.add_argument("--srcdir", dest="srcdir", default="../../src",
                          help="Source directory containing pasteld/pastel-cli (default: %default)")
        parser.add_argument("--tmpdir", dest="tmpdir", default=tempfile.mkdtemp(prefix="test"),
                          help="Root directory for datadirs")
        parser.add_argument("--tracerpc", dest="trace_rpc", default=False, action="store_true",
                          help="Print out all RPC calls as they are made")
        self.add_options(parser)
        self.options = parser.parse_args()

        if self.options.trace_rpc:
            logging.basicConfig(level=logging.DEBUG, datefmt='%H:%M:%S', format='%(asctime)s.%(msecs)03d [%(name)s:%(levelname)s] %(message)s')

        os.environ['PATH'] = self.options.srcdir+":"+os.environ['PATH']

        check_json_precision()

        success = False
        try:
            if not os.path.isdir(self.options.tmpdir):
                os.makedirs(self.options.tmpdir)
            self.setup_chain()
            self.setup_network()
            self.run_test()
            success = True
        except JSONRPCException as e:
            print("JSONRPC error: "+e.error['message'])
            traceback.print_tb(sys.exc_info()[2])
        except AssertionError as e:
            print("Assertion failed: " + str(e))
            traceback.print_tb(sys.exc_info()[2])
        except KeyError as e:
            print("key not found: "+ str(e))
            traceback.print_tb(sys.exc_info()[2])
        except Exception as e:
            print("Unexpected exception caught during testing: "+str(e))
            traceback.print_tb(sys.exc_info()[2])
        except KeyboardInterrupt as e:
            print("Exiting after " + repr(e))

        if not self.options.noshutdown:
            print("Stopping nodes")
            stop_nodes(self.nodes)
            wait_pastelds()
        else:
            print("Note: pasteld nodes were not stopped and may still be running")

        if not self.options.nocleanup and not self.options.noshutdown:
            print("Cleaning up")
            shutil.rmtree(self.options.tmpdir)

        if success:
            print("<<< TEST SUCCEDED >>>")
            sys.exit(0)
        else:
            print("<<< !!! TEST FAILED !!! >>>")

            sys.exit(1)


# Test framework for doing p2p comparison testing, which sets up some pasteld
# binaries:
# 1 binary: test binary
# 2 binaries: 1 test binary, 1 ref binary
# n>2 binaries: 1 test binary, n-1 ref binaries

class ComparisonTestFramework(BitcoinTestFramework):

    # Can override the num_nodes variable to indicate how many nodes to run.
    def __init__(self):
        super().__init__()
        self.num_nodes = 2
        self.setup_clean_chain = True

    def add_options(self, parser):
        parser.add_argument("--testbinary", dest="testbinary",
                          default=os.getenv("PASTELD", "pasteld"),
                          help="pasteld binary to test")
        parser.add_argument("--refbinary", dest="refbinary",
                          default=os.getenv("PASTELD", "pasteld"),
                          help="pasteld binary to use for reference nodes (if any)")

    def setup_network(self):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir,
            extra_args=[['-debug', '-whitelist=127.0.0.1']] * self.num_nodes,
            binary=[self.options.testbinary] +
            [self.options.refbinary]*(self.num_nodes-1))
