#!/usr/bin/env python3

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_true, start_node
from test_framework.authproxy import JSONRPCException

class SignOfflineTest (BitcoinTestFramework):
    
    def __init__(self):
        super().__init__()
        self.num_nodes = 2
        self.setup_clean_chain = True
    
    def setup_network(self):
        self.nodes = [ start_node(0, self.options.tmpdir, ["-nuparams=76b809bb:10"]) ]
        self.is_network_split = False
        self.sync_all()

    # Tests
    def run_test(self):
        print("Mining blocks...")
        self.nodes[0].generate(101)

        offline_node = start_node(1, self.options.tmpdir, ["-maxconnections=0", "-nuparams=76b809bb:10"])
        self.nodes.append(offline_node)

        assert_equal(0, len(offline_node.getpeerinfo())) # make sure node 1 has no peers

        taddr = self.nodes[0].getnewaddress()

        tx = self.nodes[0].listunspent()[0]
        txid = tx['txid']
        scriptpubkey = tx['scriptPubKey']
        privkeys = [self.nodes[0].dumpprivkey(tx['address'])]

        create_inputs = [{'txid': txid, 'vout': 0}]
        sign_inputs = [{'txid': txid, 'vout': 0, 'scriptPubKey': scriptpubkey, 'amount': self._reward}]

        create_hex = self.nodes[0].createrawtransaction(create_inputs, {taddr: self._reward-self._fee})

        # An offline regtest node does not rely on the approx release height of the software
        # to determine the consensus rules to be used for signing.
        try:
            signed_tx = offline_node.signrawtransaction(create_hex, sign_inputs, privkeys)
            self.nodes[0].sendrawtransaction(signed_tx['hex'])
            assert(False)
        except JSONRPCException:
            pass

        # Passing in the consensus branch id resolves the issue for offline regtest nodes.
        signed_tx = offline_node.signrawtransaction(create_hex, sign_inputs, privkeys, "ALL", "76b809bb")

        # If we return the transaction hash, then we have have not thrown an error (success)
        online_tx_hash = self.nodes[0].sendrawtransaction(signed_tx['hex'])
        assert_true(len(online_tx_hash) > 0)

if __name__ == '__main__':
    SignOfflineTest().main()