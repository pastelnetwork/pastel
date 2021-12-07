#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .


#
# Helpful routines for regression testing
#

import os
import sys
from binascii import hexlify, unhexlify
from base64 import b64encode
from decimal import Decimal, ROUND_DOWN
from pathlib import Path
import json
import http.client
import random
import shutil
import subprocess
import time
import re
from .authproxy import AuthServiceProxy, JSONRPCException

SPROUT_BRANCH_ID = 0x00000000
OVERWINTER_BRANCH_ID = 0x5BA81B19
SAPLING_BRANCH_ID = 0x76B809BB

def p2p_port(n):
    return 11000 + n + os.getpid()%999
def rpc_port(n):
    return 12000 + n + os.getpid()%999

def check_json_precision():
    """Make sure json library being used does not lose precision converting PASTEL values"""
    n = Decimal("20000000.00003")
    patoshis = int(json.loads(json.dumps(float(n)))*1.0e5)
    if patoshis != 2000000000003:
        raise RuntimeError("JSON encode/decode loses precision")

def bytes_to_hex_str(byte_str):
    return hexlify(byte_str).decode('ascii')

def hex_str_to_bytes(hex_str):
    return unhexlify(hex_str.encode('ascii'))

def str_to_b64str(string):
    return b64encode(string.encode('utf-8')).decode('ascii')

def sync_blocks(rpc_connections, wait=1, stop_after=-1):
    """
    Wait until everybody has the same block count
    """
    print("Waiting for blocks to sync (wait interval=%d sec each, max tries=%d" %(wait, stop_after))
    count = 0
    while True:
        counts = [ x.getblockcount() for x in rpc_connections ]
        count += 1
        if counts == [ counts[0] ]*len(counts):
            break
        if stop_after != -1 and count > stop_after:
            break
        print("loop = " + str(count))
        time.sleep(wait)

def sync_mempools(rpc_connections, wait=1, stop_after=-1):
    """
    Wait until everybody has the same transactions in their memory
    pools
    """
    print("Waiting for mempools to sync (wait interval=%d sec each, max tries=%d" %(wait, stop_after))
    count = 0
    while True:
        pool = set(rpc_connections[0].getrawmempool())
        num_match = 1
        count += 1
        for i in range(1, len(rpc_connections)):
            if set(rpc_connections[i].getrawmempool()) == pool:
                num_match = num_match+1
        if num_match == len(rpc_connections):
            break
        if stop_after != -1 and count > stop_after:
            break
        print("loop = " + str(count))
        time.sleep(wait)

pasteld_processes = {}

def initialize_datadir(dirname, n):
    datadir = os.path.join(dirname, "node"+str(n))
    if not os.path.isdir(datadir):
        os.makedirs(datadir)
    with open(os.path.join(datadir, "pastel.conf"), 'w', encoding='utf8') as f:
        f.write("regtest=1\n")
        f.write("showmetrics=0\n")
        f.write("rpcuser=rt\n")
        f.write("rpcpassword=rt\n")
        f.write("port="+str(p2p_port(n))+"\n")
        f.write("rpcport="+str(rpc_port(n))+"\n")
        f.write("listenonion=0\n")
    return datadir

def find_pasteld_binary():
    """
    Find pasteld binary in:
        - PASTELD environment variable
        - current working directory
        - current script directory
        - ../../../src
    """
    isWindows = os.name == "nt"
    fname = "pasteld.exe" if isWindows else "pasteld"
    filepath = os.getenv("PASTELD")
    if filepath:
        return filepath
    filepath = Path(os.getcwd()) / fname
    if filepath.exists():
        return str(filepath)
    scriptPath = Path(__file__).parent.absolute()
    filepath = scriptPath / fname
    if filepath.exists():
        return str(filepath)
    filepath = scriptPath.parents[2] / "src" / fname
    if filepath.exists():
        return str(filepath)
    return fname

def find_pastelcli_binary():
    """
    Find pastel-cli binary in:
        - PASTELDCLI environment variable
        - current working directory
        - current script directory
        - ../../../src
    """
    isWindows = os.name == "nt"
    fname = "pastel-cli.exe" if isWindows else "pastel-cli"
    filepath = os.getenv("PASTELDCLI")
    if filepath:
        return filepath
    filepath = Path(os.getcwd()) / fname
    if filepath.exists():
        return str(filepath)
    scriptPath = Path(__file__).parent.absolute()
    filepath = scriptPath / fname
    if filepath.exists():
        return str(filepath)
    filepath = scriptPath.parents[2] / "src" / fname
    if filepath.exists():
        return str(filepath)
    return fname


def initialize_chain(test_dir):
    """
    Create (or copy from cache) a 200-block-long chain and
    4 wallets.
    """
    if not Path("cache", "node0").exists():
        print("Rebuilding cache...")
        isWindows = os.name == "nt"
        if not isWindows:
            devnull = open("/dev/null", "w+")
        # Create cache directories, run pasteld:
        for i in range(4):
            datadir=initialize_datadir("cache", i)
            args = [ find_pasteld_binary(), "-keypool=1", "-datadir="+datadir, "-discover=0" ]
            args.extend([
                '-nuparams=5ba81b19:1', # Overwinter
                '-nuparams=76b809bb:1', # Sapling
            ])
            if i > 0:
                args.append("-connect=127.0.0.1:"+str(p2p_port(0)))
            pasteld_processes[i] = subprocess.Popen(args)
            if os.getenv("PYTHON_DEBUG", ""):
                print("initialize_chain: pasteld started, calling pastel-cli -rpcwait getblockcount")
            subprocess.check_call([ find_pastelcli_binary(), "-datadir="+datadir,
                                    "-rpcwait", "getblockcount"], stdout=devnull)
            if os.getenv("PYTHON_DEBUG", ""):
                print("initialize_chain: pastel-cli -rpcwait getblockcount completed")
        devnull.close()
        rpcs = []
        for i in range(4):
            try:
                url = "http://rt:rt@127.0.0.1:%d"%(rpc_port(i),)
                rpcs.append(AuthServiceProxy(url))
            except:
                sys.stderr.write("Error connecting to "+url+"\n")
                sys.exit(1)

        # Create a 200-block-long chain; each of the 4 nodes
        # gets 25 mature blocks and 25 immature.
        # blocks are created with timestamps 10 minutes apart, starting
        # at 25th Dec 2020 - 1608854400
        block_time = 1608854400
        for i in range(2):
            for peer in range(4):
                for j in range(25):
                    set_node_times(rpcs, block_time)
                    rpcs[peer].generate(1)
                    # block_time += 10*60
                    block_time += 2*60
                # Must sync before next peer starts generating blocks
                sync_blocks(rpcs)

        # Shut them down, and clean up cache directories:
        stop_nodes(rpcs)
        wait_pastelds()
        for i in range(4):
            os.remove(log_filename("cache", i, "debug.log"))
            os.remove(log_filename("cache", i, "db.log"))
            os.remove(log_filename("cache", i, "peers.dat"))
            os.remove(log_filename("cache", i, "fee_estimates.dat"))

        print("Finished Rebuilding cache...")

    for i in range(4):
        from_dir = os.path.join("cache", "node"+str(i))
        to_dir = os.path.join(test_dir,  "node"+str(i))
        shutil.copytree(from_dir, to_dir)
        initialize_datadir(test_dir, i) # Overwrite port/rpcport in pastel.conf

def initialize_chain_clean(test_dir, num_nodes):
    """
    Create an empty blockchain and num_nodes wallets.
    Useful if a test case wants complete control over initialization.
    """
    for i in range(num_nodes):
        initialize_datadir(test_dir, i)


def _rpchost_to_args(rpchost):
    '''Convert optional IP:port spec to rpcconnect/rpcport args'''
    if rpchost is None:
        return []

    match = re.match('(\[[0-9a-fA-f:]+\]|[^:]+)(?::([0-9]+))?$', rpchost)
    if not match:
        raise ValueError('Invalid RPC host spec ' + rpchost)

    rpcconnect = match.group(1)
    rpcport = match.group(2)

    if rpcconnect.startswith('['): # remove IPv6 [...] wrapping
        rpcconnect = rpcconnect[1:-1]

    rv = ['-rpcconnect=' + rpcconnect]
    if rpcport:
        rv += ['-rpcport=' + rpcport]
    return rv

def start_node(i, dirname, extra_args=None, rpchost=None, timewait=None, binary=None):
    """
    Start a pasteld and return RPC connection to it
    """
    datadir = str(Path(dirname, "node" +str(i)).absolute())
    if binary is None:   
        binary = find_pasteld_binary()

    args = [ binary, "-datadir="+datadir, "-keypool=1", "-discover=0", "-rest" ]
    args.extend([
        '-nuparams=5ba81b19:1', # Overwinter
        '-nuparams=76b809bb:1', # Sapling
    ])
    if extra_args is not None: args.extend(extra_args)
    pasteld_processes[i] = subprocess.Popen(args)
    devnull = open("/dev/null", "w+")
    if os.getenv("PYTHON_DEBUG", ""):
        print("start_node: pasteld started, calling pastel-cli -rpcwait getblockcount")
    subprocess.check_call([ find_pastelcli_binary(), "-datadir="+datadir] +
                          _rpchost_to_args(rpchost)  +
                          ["-rpcwait", "getblockcount"], stdout=devnull)
    if os.getenv("PYTHON_DEBUG", ""):
        print("start_node: calling pastel-cli -rpcwait getblockcount returned")
    devnull.close()
    url = "http://rt:rt@%s:%d" % (rpchost or '127.0.0.1', rpc_port(i))
    if timewait is not None:
        proxy = AuthServiceProxy(url, timeout=timewait)
    else:
        proxy = AuthServiceProxy(url)
    proxy.url = url # store URL on proxy for info
    return proxy

def start_nodes(num_nodes, dirname, extra_args=None, rpchost=None, binary=None):
    """
    Start multiple pastelds, return RPC connections to them
    """
    if extra_args is None: extra_args = [ None for i in range(num_nodes) ]
    if binary is None: binary = [ None for i in range(num_nodes) ]
    return [ start_node(i, dirname, extra_args[i], rpchost, binary=binary[i]) for i in range(num_nodes) ]

def log_filename(dirname, n_node, logname):
    return os.path.join(dirname, "node"+str(n_node), "regtest", logname)

def check_node(i):
    pasteld_processes[i].poll()
    return pasteld_processes[i].returncode

def stop_node(node, i):
    try:
        node.stop()
    except http.client.CannotSendRequest as e:
        print("WARN: Unable to stop node: " + repr(e))
    pasteld_processes[i].wait()
    del pasteld_processes[i]

def stop_nodes(nodes):
    for node in nodes:
        try:
            node.stop()
        except http.client.CannotSendRequest as e:
            print("WARN: Unable to stop node: " + repr(e))
    del nodes[:] # Emptying array closes connections as a side effect

def set_node_times(nodes, t):
    for node in nodes:
        node.setmocktime(t)

def wait_pastelds():
    # Wait for all pastelds to cleanly exit
    for pasteld in list(pasteld_processes.values()):
        pasteld.wait()
    pasteld_processes.clear()

def connect_nodes(from_connection, node_num):
    ip_port = "127.0.0.1:"+str(p2p_port(node_num))
    from_connection.addnode(ip_port, "onetry")
    # poll until version handshake complete to avoid race conditions
    # with transaction relaying
    while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
        time.sleep(0.1)

def connect_nodes_bi(nodes, a, b):
    connect_nodes(nodes[a], b)
    connect_nodes(nodes[b], a)

def find_output(node, txid, amount):
    """
    Return index to output of txid with value amount
    Raises exception if there is none.
    """
    txdata = node.getrawtransaction(txid, 1)
    for i in range(len(txdata["vout"])):
        if txdata["vout"][i]["value"] == amount:
            return i
    raise RuntimeError("find_output txid %s : %s not found"%(txid,str(amount)))


def gather_inputs(from_node, amount_needed, confirmations_required=1):
    """
    Return a random set of unspent txouts that are enough to pay amount_needed
    """
    assert(confirmations_required >=0)
    utxo = from_node.listunspent(confirmations_required)
    random.shuffle(utxo)
    inputs = []
    total_in = Decimal("0.00000")
    while total_in < amount_needed and len(utxo) > 0:
        t = utxo.pop()
        total_in += t["amount"]
        inputs.append({ "txid" : t["txid"], "vout" : t["vout"], "address" : t["address"] } )
    if total_in < amount_needed:
        raise RuntimeError("Insufficient funds: need %d, have %d"%(amount_needed, total_in))
    return (total_in, inputs)

def make_change(from_node, amount_in, amount_out, fee):
    """
    Create change output(s), return them
    """
    outputs = {}
    amount = amount_out+fee
    change = amount_in - amount
    if change > amount*2:
        # Create an extra change output to break up big inputs
        change_address = from_node.getnewaddress()
        # Split change in two, being careful of rounding:
        outputs[change_address] = Decimal(change/2).quantize(Decimal('0.00001'), rounding=ROUND_DOWN)
        change = amount_in - amount - outputs[change_address]
    if change > 0:
        outputs[from_node.getnewaddress()] = change
    return outputs

def send_zeropri_transaction(from_node, to_node, amount, fee):
    """
    Create&broadcast a zero-priority transaction.
    Returns (txid, hex-encoded-txdata)
    Ensures transaction is zero-priority by first creating a send-to-self,
    then using its output
    """

    # Create a send-to-self with confirmed inputs:
    self_address = from_node.getnewaddress()
    (total_in, inputs) = gather_inputs(from_node, amount+fee*2)
    outputs = make_change(from_node, total_in, amount+fee, fee)
    outputs[self_address] = float(amount+fee)

    self_rawtx = from_node.createrawtransaction(inputs, outputs)
    self_signresult = from_node.signrawtransaction(self_rawtx)
    self_txid = from_node.sendrawtransaction(self_signresult["hex"], True)

    vout = find_output(from_node, self_txid, amount+fee)
    # Now immediately spend the output to create a 1-input, 1-output
    # zero-priority transaction:
    inputs = [ { "txid" : self_txid, "vout" : vout } ]
    outputs = { to_node.getnewaddress() : float(amount) }

    rawtx = from_node.createrawtransaction(inputs, outputs)
    signresult = from_node.signrawtransaction(rawtx)
    txid = from_node.sendrawtransaction(signresult["hex"], True)

    return (txid, signresult["hex"])

def random_zeropri_transaction(nodes, amount, min_fee, fee_increment, fee_variants):
    """
    Create a random zero-priority transaction.
    Returns (txid, hex-encoded-transaction-data, fee)
    """
    from_node = random.choice(nodes)
    to_node = random.choice(nodes)
    fee = min_fee + fee_increment*random.randint(0,fee_variants)
    (txid, txhex) = send_zeropri_transaction(from_node, to_node, amount, fee)
    return (txid, txhex, fee)

def random_transaction(nodes, amount, min_fee, fee_increment, fee_variants):
    """
    Create a random transaction.
    Returns (txid, hex-encoded-transaction-data, fee)
    """
    from_node = random.choice(nodes)
    to_node = random.choice(nodes)
    fee = min_fee + fee_increment*random.randint(0,fee_variants)

    (total_in, inputs) = gather_inputs(from_node, amount+fee)
    outputs = make_change(from_node, total_in, amount, fee)
    outputs[to_node.getnewaddress()] = float(amount)

    rawtx = from_node.createrawtransaction(inputs, outputs)
    signresult = from_node.signrawtransaction(rawtx)
    txid = from_node.sendrawtransaction(signresult["hex"], True)

    return (txid, signresult["hex"], fee)

def assert_equal(expected, actual, message=""):
    if expected != actual:
        if message:
            message = "; %s" % message 
        raise AssertionError("(left == right)%s\n  left: <%s>\n right: <%s>" % (message, str(expected), str(actual)))

def assert_equals(expected, actual, message=""):
    find_item = False
    for item in expected:
        if item == actual:
            find_item = True
    if find_item != True:
        if message:
                message = "; %s" % message
        raise AssertionError("Non of the (list) expected values are equal(left == right)%s\n  left: <%s>\n right: <%s>" % (message, str(expected), str(actual)))

def assert_true(condition, message = ""):
    if not condition:
        raise AssertionError(message)
        
def assert_false(condition, message = ""):
    assert_true(not condition, message)

def assert_greater_than(thing1, thing2):
    if thing1 <= thing2:
        raise AssertionError("%s <= %s"%(str(thing1),str(thing2)))

def assert_raises(exc, func, *args, **kwargs):
    assert_raises_message(exc, None, func, *args, **kwargs)

def assert_raises_rpc(code, expected_error_substring, func, *args, **kwargs):
    """
    Asserts that func throws and that the exception contains 'errstr' in its message and
    exception code matches.
    """
    try:
        func(*args, **kwargs)
    except JSONRPCException as e:
        if e.error['message'] is None:
            err = e.error['error']
            if code and code != err['code']:
                raise AssertionError(f"Invalid JSONRPCException code {err['code']}, expected {code} in {repr(e)}")
            if expected_error_substring and expected_error_substring not in str(e):
                raise AssertionError(f"Invalid JSONRPCException message: Couldn't find {repr(expected_error_substring)} in {repr(e)}")
        print(f"RPC Exception received: {e!r}.\nExpected substring: [{expected_error_substring}]");
    except Exception as e:
        raise AssertionError("Unexpected exception raised: "+type(e).__name__)
    else:
        err_info = " and ".join([ 
            f"the error code {code}" if code else "", 
            f"error message containing [{expected_error_substring}]" if expected_error_substring else ""])
        if err_info:
            raise AssertionError(f"No exception raised, but expected with {err_info}")
        raise AssertionError("No exception raised")

def assert_raises_message(ExceptionType, errstr, func, *args, **kwargs):
    """
    Asserts that func throws and that the exception contains 'errstr'
    in its message.
    """
    try:
        func(*args, **kwargs)
    except ExceptionType as e:
        if errstr is not None and errstr not in str(e):
            raise AssertionError(f"Invalid exception string: Couldn't find {repr(errstr)} in {repr(e)}")
    except Exception as e:
        raise AssertionError("Unexpected exception raised: "+type(e).__name__)
    else:
        raise AssertionError("No exception raised")

def fail(message=""):
    raise AssertionError(message)

def assert_shows_help(func, *args, **kwargs):
    """
    Asserts that func returns json with the help message
    """
    try:
        func(*args, **kwargs)
    except JSONRPCException as e:
        if e.error['message'] is None:
            err = e.error['error']
            if err is None or err['code'] != -8 or err['message'] is None:
                raise AssertionError(f"help message is missing in {repr(e)}")
    else:
        raise AssertionError("No exception raised")

# Returns an async operation result
def wait_and_assert_operationid_status_result(node, myopid, in_status='success', in_errormsg=None, timeout=300):
    print(f'waiting for async operation {myopid}')
    result = None
    for _ in range(1, timeout):
        results = node.z_getoperationresult([myopid])
        if len(results) > 0:
            result = results[0]
            break
        time.sleep(1)

    assert_true(result is not None, "timeout occurred")
    status = result['status']

    debug = os.getenv("PYTHON_DEBUG", "")
    if debug:
        print(f'...returned status: {status}')

    errormsg = None
    if status == "failed":
        errormsg = result['error']['message']
        if debug:
            print(f'...returned error: {errormsg}')
        assert_equal(in_errormsg, errormsg)

    assert_equal(in_status, status, f"Operation returned mismatched status. Error Message: {errormsg}")

    return result


# Returns txid if operation was a success or None
def wait_and_assert_operationid_status(node, myopid, in_status='success', in_errormsg=None, timeout=300):
    result = wait_and_assert_operationid_status_result(node, myopid, in_status, in_errormsg, timeout)
    if result['status'] == "success":
        return result['result']['txid']
    else:
        return None

# Find a coinbase address on the node, filtering by the number of UTXOs it has.
# If no filter is provided, returns the coinbase address on the node containing
# the greatest number of spendable UTXOs.
# The default cached chain has one address per coinbase output.
def get_coinbase_address(node, expected_utxos=None):
    addrs = [utxo['address'] for utxo in node.listunspent() if utxo['generated']]
    assert(len(set(addrs)) > 0)

    if expected_utxos is None:
        addrs = [(addrs.count(a), a) for a in set(addrs)]
        return sorted(addrs, reverse=True)[0][1]

    addrs = [a for a in set(addrs) if addrs.count(a) == expected_utxos]
    assert(len(addrs) > 0)
    return addrs[0]

def check_node_log(self, node_number, line_to_check, stop_node = True):
    print("Checking node " + str(node_number) + " logs")
    if stop_node:
        self.nodes[node_number].stop()
        pasteld_processes[node_number].wait()
    logpath = self.options.tmpdir + "/node" + str(node_number) + "/regtest/debug.log"
    with open(logpath, "r", encoding="utf8") as myfile:
        logdata = myfile.readlines()
    for (n, logline) in enumerate(logdata):
        if line_to_check in logline:
            return n
    raise AssertionError(repr(line_to_check) + " not found")

def nuparams(branch_id, height):
    return '-nuparams=%x:%d' % (branch_id, height)
