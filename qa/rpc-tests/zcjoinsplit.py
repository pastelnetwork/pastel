#!/usr/bin/env python

#
# Test joinsplit semantics
#

import sys; assert sys.version_info < (3,), ur"This script does not run under Python 3. Please use Python 2.7.x."

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, start_node, \
    gather_inputs

from decimal import Decimal, getcontext
getcontext().prec = 16

class JoinSplitTest(BitcoinTestFramework):
    def setup_network(self):
        print("Seting up network "+self.options.tmpdir)
        self.nodes = []
        self.is_network_split = False
        self.nodes.append(start_node(0, self.options.tmpdir))

    def run_test(self):
        print "On REGTEST... \n\treward is {} per block\n\t100 blocks to maturity".format(self._reward)
        zckeypair = self.nodes[0].zcrawkeygen()
        zcsecretkey = zckeypair["zcsecretkey"]
        zcaddress = zckeypair["zcaddress"]

        amount = self._reward*4
        amount_less = amount - self._fee
        (total_in, inputs) = gather_inputs(self.nodes[0], amount )
        protect_tx = self.nodes[0].createrawtransaction(inputs, {})
        joinsplit_result = self.nodes[0].zcrawjoinsplit(protect_tx, {}, {zcaddress:amount_less}, amount_less, 0)

        receive_result = self.nodes[0].zcrawreceive(zcsecretkey, joinsplit_result["encryptednote1"])
        assert_equal(receive_result["exists"], False)

        protect_tx = self.nodes[0].signrawtransaction(joinsplit_result["rawtxn"])
        self.nodes[0].sendrawtransaction(protect_tx["hex"])
        self.nodes[0].generate(1)

        receive_result = self.nodes[0].zcrawreceive(zcsecretkey, joinsplit_result["encryptednote1"])
        assert_equal(receive_result["exists"], True)

        # The pure joinsplit we create should be mined in the next block
        # despite other transactions being in the mempool.
        addrtest = self.nodes[0].getnewaddress()
        for xx in range(0,10):
            self.nodes[0].generate(1)
            for x in range(0,50):
                self.nodes[0].sendtoaddress(addrtest, self._fee);

        amount_less2 = amount_less - self._fee
        joinsplit_tx = self.nodes[0].createrawtransaction([], {})
        joinsplit_result = self.nodes[0].zcrawjoinsplit(joinsplit_tx, {receive_result["note"] : zcsecretkey}, {zcaddress: amount_less2}, 0, self._fee)

        self.nodes[0].sendrawtransaction(joinsplit_result["rawtxn"])
        self.nodes[0].generate(1)

        print "Done!"
        receive_result = self.nodes[0].zcrawreceive(zcsecretkey, joinsplit_result["encryptednote1"])
        assert_equal(receive_result["exists"], True)

if __name__ == '__main__':
    JoinSplitTest().main()
