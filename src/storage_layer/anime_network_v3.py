 
import sys, time, os.path, io, glob, hashlib, imghdr, random, os, sqlite3, socket, warnings, base64, json, hmac, logging, asyncio
#import bencode, crc32c, dht, krpc, tracker, utils #TinyBT
from shutil import copyfile
#from fs.memoryfs import MemoryFS
from random import randint
from math import ceil, log, floor, sqrt
from collections import defaultdict
from zipfile import ZipFile
from subprocess import check_output
from datetime import datetime
from kademlia.network import Server
from uuid import getnode as get_mac
with warnings.catch_warnings():
    warnings.filterwarnings("ignore",category=DeprecationWarning)
    from fs.copy import copy_fs
    from fs.osfs import OSFS
try:
    from tqdm import tqdm
except:
    pass
    
folder_path_of_art_folders_to_encode = 'C:\\animecoin\\art_folders_to_encode\\' #Each subfolder contains the various art files pertaining to a given art asset.
block_storage_folder_path = 'C:\\animecoin\\art_block_storage\\'
chunk_db_file_path = 'C:\\animecoin\\anime_chunkdb.sqlite'
use_integrity_scan_of_block_files_on_startup = 0

if use_integrity_scan_of_block_files_on_startup:
    if not os.path.exists(chunk_db_file_path):
        conn = sqlite3.connect(chunk_db_file_path)
        c = conn.cursor()
        local_hash_table_creation_string= """CREATE TABLE potential_local_hashes (block_hash text PRIMARY KEY, file_hash);"""
        global_hash_table_creation_string= """CREATE TABLE potential_global_hashes (
                                                block_hash text,
                                                file_hash text,
                                                peer_ip_and_port text,
                                                datetime_peer_last_seen text,
                                                PRIMARY KEY (block_hash,peer_ip_and_port)
                                                );"""
        c.execute(local_hash_table_creation_string)
        c.execute(global_hash_table_creation_string)
        conn.commit()
        
    potential_local_block_hashes_list = []
    potential_local_file_hashes_list = []
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    print('Now Verifying Block Files ('+str(len(list_of_block_file_paths))+' files found)\n')
    
    try:
        pbar = tqdm(total=len(list_of_block_file_paths))
    except:
        print('.')
        
    for current_block_file_path in list_of_block_file_paths:
        with open(current_block_file_path, 'rb') as f:
            try:
                current_block_binary_data = f.read()
            except:
                print('\nProblem reading block file!\n')
                continue
        try:
            pbar.update(1)
        except:
            pass
        hash_of_block = hashlib.sha256(current_block_binary_data).hexdigest()
        reported_block_sha256_hash = current_block_file_path.split('\\')[-1].split('__')[-1].replace('BlockHash_','').replace('.block','')
        if hash_of_block == reported_block_sha256_hash:
            reported_file_sha256_hash = current_block_file_path.split('\\')[-1].split('__')[1]
            if reported_block_sha256_hash not in potential_local_block_hashes_list:
                potential_local_block_hashes_list.append(reported_block_sha256_hash)
                potential_local_file_hashes_list.append(reported_file_sha256_hash)
        else:
            print('\nBlock '+reported_block_sha256_hash+' did not hash to the correct value-- file is corrupt! Skipping to next file...\n')
    print('\n\nDone verifying block files!\nNow writing local block metadata to SQLite database...\n')
    table_name= 'potential_local_hashes'
    id_column = 'block_hash'
    column_name = 'file_hash'
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    c.execute("""DELETE FROM potential_local_hashes""")
    for hash_cnt, current_block_hash in enumerate(potential_local_block_hashes_list):
        current_file_hash = potential_local_file_hashes_list[hash_cnt]
        sql_string = """INSERT OR IGNORE INTO potential_local_hashes (block_hash, file_hash) VALUES (\"{blockhash}\", \"{filehash}\")""".format(blockhash=current_block_hash, filehash=current_file_hash)
        c.execute(sql_string)
    conn.commit()
    print('Done writing file hash data to SQLite file!\n')
    set_of_local_potential_block_hashes = c.execute('SELECT block_hash FROM potential_local_hashes').fetchall()
    set_of_local_potential_file_hashes = c.execute('SELECT DISTINCT file_hash FROM potential_local_hashes').fetchall()
    #conn.close()
    
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    current_block_file_path = list_of_block_file_paths[0]

if 0:
    listen_connection = ('0.0.0.0', 10001)
    dht_node = dht.DHT(listen_connection)
    
    test = dht_node.get_external_connection()
    current_block_hash = potential_local_block_hashes_list[0]
    current_block_hash = '0235c92ffe80b54a486f18d041b05908eda22a52f4bafc0379f2742c1a9c4149'
    dht_node.dht_announce_peer(current_block_hash, implied_port = 10001) 
    peers = dht_node.dht_get_peers(current_block_hash, timeout = 5, retries = 2) 
    search_dict = dht_node.dht_ping(('127.0.0.1', 10001), timeout = 5) 
    node_search_results = dht_node.dht_find_node('0x169e0f5b6783e38cb60c7848a300cd39a08c3fca', timeout = 5, retries = 2) 
    
    unique_file_hashes_list = []
    for hash_cnt, current_block_hash in enumerate(potential_local_block_hashes_list):
        current_file_hash = potential_local_file_hashes_list[hash_cnt]
        dht_node.dht_announce_peer(current_block_hash, implied_port = 10001) 
        if current_file_hash not in unique_file_hashes_list:
            unique_file_hashes_list.append(current_file_hash)
    
    for hash_cnt, current_block_hash in enumerate(potential_local_block_hashes_list):
        peers = dht_node.dht_get_peers(current_block_hash, timeout = 5, retries = 2) 
    dht_node.shutdown()


if 0:
    from functools import partial
    from twisted.internet import reactor
    from twisted.internet.protocol import Protocol, Factory
    from twisted.internet.endpoints import TCP4ServerEndpoint, TCP4ClientEndpoint, connectProtocol
    from twisted.internet.error import CannotListenError
    from twisted.internet.task import LoopingCall
    
    def generate_nodeid():
        return hashlib.sha256(os.urandom(256)).hexdigest()
    
    DEFAULT_PORT = 2718
    BOOTSTRAP_LIST = ['localhost:2718', '140.82.14.38:2718']
    PING_INTERVAL = 1200.0 # 20 min = 1200.0
    nonce = lambda: generate_nodeid()
    incr_nonce = lambda env: format(int(env['nonce'], 16) + 1, 'x')
    
    class InvalidSignatureError(Exception):
        pass
    
    class InvalidNonceError(Exception):
        pass
    
    def make_envelope(msgtype, msg, nodeid):
        msg['nodeid'] = nodeid
        msg['nonce'] =  nonce()
        data = json.dumps(msg)
        sign = hmac.new(nodeid, data)
        envelope = {'data': msg,
                    'sign': sign.hexdigest(),
                    'msgtype': msgtype}
        return json.dumps(envelope)
    
    def envelope_decorator(nodeid, func):
        msgtype = func.__name__.split('_')[0]
        def inner(*args, **kwargs):
            return make_envelope(msgtype, func(*args, **kwargs), nodeid)
        return inner
    
    def create_ackhello(nodeid):
        msg = {}
        return make_envelope('ackhello', msg, nodeid)
    
    def create_hello(nodeid, version):
        msg = {'version': version}
        return make_envelope('hello', msg, nodeid)
    
    def create_ping(nodeid):
        msg = {}
        return make_envelope('ping', msg, nodeid)
    
    def create_pong(nodeid):
        msg = {}
        return make_envelope('pong', msg, nodeid)
    
    def create_getaddr(nodeid):
        msg = {}
        return make_envelope('getaddr', msg, nodeid)
    
    def create_addr(nodeid, nodes):
        msg = {'nodes': nodes}
        return make_envelope('addr', msg, nodeid)
    
    def read_envelope(message):
        return json.loads(message)
    
    def read_message(message):
        """Read and parse the message into json. Validate the signature and return envelope['data'] """
        envelope = json.loads(message)
        nodeid = str(envelope['data']['nodeid'])
        signature = str(envelope['sign'])
        msg = json.dumps(envelope['data'])
        verify_sign = hmac.new(nodeid, msg)
        if hmac.compare_digest(verify_sign.hexdigest(), signature):
            return envelope['data']
        else:
            raise InvalidSignatureError
    
    def _print(*args):
        time = datetime.now().time().isoformat()[:8]
        print(time),
        print("".join(map(str, args)))
        
    
    class AnimeProtocol(Protocol):
        def __init__(self, factory, state="GETHELLO", kind="LISTENER"):
            self.factory = factory
            self.state = state
            self.VERSION = 0
            self.remote_nodeid = None
            self.kind = kind
            self.nodeid = self.factory.nodeid
            self.lc_ping = LoopingCall(self.send_PING)
            self.message = partial(envelope_decorator, self.nodeid)
    
        def connectionMade(self):
            r_ip = self.transport.getPeer()
            h_ip = self.transport.getHost()
            self.remote_ip = r_ip.host + ":" + str(r_ip.port)
            self.host_ip = h_ip.host + ":" + str(h_ip.port)
    
        def print_peers(self):
            if len(self.factory.peers) == 0:
                _print(" [!] PEERS: No peers connected.")
            else:
                _print(" [ ] PEERS:")
                for peer in self.factory.peers:
                    addr, kind = self.factory.peers[peer][:2]
                    _print("     [*]", peer, "at", addr, kind)
    
        def write(self, line):
            self.transport.write(line + "\n")
    
        def connectionLost(self, reason):
            # NOTE: It looks like the NCProtocol instance will linger in memory since ping keeps going if we don't .stop() it.
            try: self.lc_ping.stop()
            except AssertionError: pass
    
            try:
                self.factory.peers.pop(self.remote_nodeid)
                if self.nodeid != self.remote_nodeid:
                    self.print_peers()
            except KeyError:
                if self.nodeid != self.remote_nodeid:
                    _print(" [ ] GHOST LEAVES: from", self.remote_nodeid, self.remote_ip)
    
        def dataReceived(self, data):
            for line in data.splitlines():
                line = line.strip()
                envelope = read_envelope(line)
                if self.state in ["GETHELLO", "SENTHELLO"]:
                    # Force first message to be HELLO or crash
                    if envelope['msgtype'] == 'hello':
                        self.handle_HELLO(line)
                    else:
                        _print(" [!] Ignoring", envelope['msgtype'], "in", self.state)
                else:
                    if envelope['msgtype'] == 'ping':
                        self.handle_PING(line)
                    elif envelope['msgtype'] == 'pong':
                        self.handle_PONG(line)
                    elif envelope['msgtype'] == 'addr':
                        self.handle_ADDR(line)
    
        def send_PING(self):
            _print(" [>] PING   to", self.remote_nodeid, "at", self.remote_ip)
            ping = create_ping(self.nodeid)
            self.write(ping)
    
        def handle_PING(self, ping):
            if read_message(ping):
                pong = create_pong(self.nodeid)
                self.write(pong)
    
        def send_ADDR(self):
            _print(" [>] Telling " + self.remote_nodeid + " about my peers")
            # Shouldn't this be a list and not a dict?
            peers = self.factory.peers
            listeners = [(n, peers[n][0], peers[n][1], peers[n][2])
                         for n in peers]
            addr = create_addr(self.nodeid, listeners)
            self.write(addr)
    
        def handle_ADDR(self, addr):
            try:
                nodes = read_message(addr)['nodes']
                _print(" [<] Recieved addr list from peer " + self.remote_nodeid)
                #for node in filter(lambda n: nodes[n][1] == "SEND", nodes):
                for node in nodes:
                    _print("     [*] "  + node[0] + " " + node[1])
                    if node[0] == self.nodeid:
                        _print(" [!] Not connecting to " + node[0] + ": thats me!")
                        return
                    if node[1] != "SPEAKER":
                        _print(" [ ] Not connecting to " + node[0] + ": is " + node[1])
                        return
                    if node[0] in self.factory.peers:
                        _print(" [ ] Not connecting to " + node[0]  + ": already connected")
                        return
                    _print(" [ ] Trying to connect to peer " + node[0] + " " + node[1])
                    host, port = node[0].split(":") # TODO: Use [2] and a time limit to not connect to "old" peers
                    point = TCP4ClientEndpoint(reactor, host, int(port))
                    d = connectProtocol(point, AnimeProtocol(animefactory, "SENDHELLO", "SPEAKER"))
                    d.addCallback(gotProtocol)
            except InvalidSignatureError:
                print(addr)
                _print(" [!] ERROR: Invalid addr sign ", self.remote_ip)
                self.transport.loseConnection()
    
        def handle_PONG(self, pong):
            pong = read_message(pong)
            _print(" [<] PONG from", self.remote_nodeid, "at", self.remote_ip)
            # hacky
            addr, kind = self.factory.peers[self.remote_nodeid][:2]
            self.factory.peers[self.remote_nodeid] = (addr, kind, time.time())
    
        def send_HELLO(self):
            hello = create_hello(self.nodeid, self.VERSION)
            _print(" [ ] SEND_HELLO:", self.nodeid, "to", self.remote_ip)
            self.transport.write(hello + "\n")
            self.state = "SENTHELLO"
    
        def handle_HELLO(self, hello):
            try:
                hello = read_message(hello)
                self.remote_nodeid = hello['nodeid']
                if self.remote_nodeid == self.nodeid:
                    _print(" [!] Found myself at", self.host_ip)
                    self.transport.loseConnection()
                else:
                    if self.state == "GETHELLO":
                        my_hello = create_hello(self.nodeid, self.VERSION)
                        self.transport.write(my_hello + "\n")
                    self.add_peer()
                    self.state = "READY"
                    self.print_peers()
                    #self.write(messages.create_ping(self.nodeid))
                    if self.kind == "LISTENER":
                        # The listener pings its audience
                        _print(" [ ] Starting pinger to " + self.remote_nodeid)
                        self.lc_ping.start(PING_INTERVAL, now=False)
                        # Tell new audience about my peers
                        self.send_ADDR()
            except InvalidSignatureError:
                _print(" [!] ERROR: Invalid hello sign ", self.remoteip)
                self.transport.loseConnection()
    
        def add_peer(self):
            entry = (self.remote_ip, self.kind, time.time())
            self.factory.peers[self.remote_nodeid] = entry
    
    class AnimeFactory(Factory):
        def __init__(self):
            pass
    
        def startFactory(self):
            self.peers = {}
            self.numProtocols = 0
            self.nodeid = generate_nodeid()[:10]
            _print(" [ ] NODEID:", self.nodeid)
    
        def stopFactory(self):
            pass
    
        def buildProtocol(self, addr):
            return AnimeProtocol(self, "GETHELLO", "LISTENER")
    
    def gotProtocol(p):
        # ClientFactory instead?
        p.send_HELLO()
        
    if 0:
        try:
            endpoint = TCP4ServerEndpoint(reactor,DEFAULT_PORT,interface='127.0.0.1')
            _print(" [ ] LISTEN:",'127.0.0.1:',DEFAULT_PORT)
            animefactory = AnimeFactory()
            animefactory.startFactory()
            endpoint.listen(animefactory)
        except CannotListenError:
            _print("[!] Address in use")
            raise SystemExit
        
        _print(" [ ] Trying to connect to bootstrap hosts:")
        for bootstrap in BOOTSTRAP_LIST:
            _print("     [*] ", bootstrap)
            host, port = bootstrap.split(':')
            point = TCP4ClientEndpoint(reactor, host, int(port))
            d = connectProtocol(point, AnimeProtocol(animefactory, "SENDHELLO", "SPEAKER"))
            d.addCallback(gotProtocol)
        
        reactor.run()
########################################################################################################################


def generate_node_mac_hash_id():
    return hashlib.sha256(str(int(get_mac())).encode('utf-8')).hexdigest()

def get_local_matching_blocks_from_file_hash_func(block_storage_folder_path,sha256_hash_of_desired_file):
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_desired_file+'*.block'))
    list_of_block_hashes = []
    list_of_file_hashes = []
    for current_block_file_path in list_of_block_file_paths:
        reported_file_sha256_hash = current_block_file_path.split('\\')[-1].split('__')[1]
        list_of_file_hashes.append(reported_file_sha256_hash)
        reported_block_file_sha256_hash = current_block_file_path.split('__')[-1].replace('.block','').replace('BlockHash_','')
        list_of_block_hashes.append(reported_block_file_sha256_hash)
    return list_of_block_file_paths, list_of_block_hashes, list_of_file_hashes


try:
    list_of_block_file_paths, list_of_block_hashes, list_of_file_hashes = get_local_matching_blocks_from_file_hash_func(block_storage_folder_path,example_file_hash)
    example_block_hash_1 = list_of_block_hashes[0]
    example_block_hash_2 = list_of_block_hashes[1]
    example_file_hash_1 = list_of_file_hashes[0]
    example_file_hash_2 = list_of_file_hashes[1]
    example_block_file_path_hash_1 = list_of_block_file_paths[0]
    example_block_file_path_hash_2 = list_of_block_file_paths[0]
except:
    example_block_hash_1 = 'bb739b16f1e8bb3fe51f32c11319248649c18423c0c75bcaaae68bf4cb09cde0'
    example_block_hash_2 = 'f1126b528c10e97af98e1af71894e5a7a2d1dbf9ba30c94fff1f4d494e669ced'
    example_file_hash_1 = '7eb4b448d346b5bee36bba8b2a1e4983d64ac1a57c3064e39e93110c4704ef1b'
    example_file_hash_2 = '586e0ad859248b54e7c055028126bf22bc8a4850440c9829505ba9cf8164bba1'
    
default_port = 14142 #2718
bootstrap_node1 = ('108.61.86.243', int(default_port))
bootstrap_node2 = ('140.82.14.38', int(default_port))
bootstrap_node3 = ('140.82.2.58', int(default_port))
bootstrap_node_list = [bootstrap_node1,bootstrap_node2,bootstrap_node3]
handler = logging.StreamHandler()
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
log = logging.getLogger('kademlia')
log.addHandler(handler)
log.setLevel(logging.DEBUG)   
my_node_id = generate_node_mac_hash_id()

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.connect(("8.8.8.8", 80))
my_node_ip_address = s.getsockname()[0]
s.close()

use_start_new_network = 0

if use_start_new_network:
    server = Server()
    server.listen(default_port)
    loop = asyncio.get_event_loop()
    loop.set_debug(True)
    try:
        loop.run_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.stop()
        loop.close()

conn = sqlite3.connect(chunk_db_file_path)
c = conn.cursor()
set_of_local_potential_block_hashes = c.execute('SELECT block_hash FROM potential_local_hashes').fetchall()
set_of_local_potential_file_hashes = c.execute('SELECT file_hash FROM potential_local_hashes').fetchall()

def get_blocks_for_given_file_hash_func(file_hash,c):
    blocks_for_this_file_hash = c.execute("""SELECT block_hash FROM potential_local_hashes where file_hash = ?""",[file_hash]).fetchall()
    list_of_block_hashes = []
    for current_block in blocks_for_this_file_hash:
        list_of_block_hashes.append(current_block[0])
    return list_of_block_hashes

#test_file_hash = '7eb4b448d346b5bee36bba8b2a1e4983d64ac1a57c3064e39e93110c4704ef1b'
#list_of_block_hashes = get_blocks_for_given_file_hash_func(test_file_hash,c)

use_connect_to_existing_network = 1

if use_connect_to_existing_network:
    loop = asyncio.get_event_loop()
    loop.set_debug(True)
    server = Server()
    
    try:
        server.listen(default_port)
    except:
        print('Problem!')
        
    loop.run_until_complete(server.bootstrap(bootstrap_node_list))
    
    for cnt, current_block_hash in enumerate(set_of_local_potential_block_hashes):
        current_file_hash = set_of_local_potential_file_hashes[cnt]
        try:
            print('\nFile Hash: '+current_file_hash[0]+'\nBlock Hash: '+current_block_hash[0]+'\n')
            loop.run_until_complete(server.set(current_block_hash[0],current_file_hash[0]))
        except:
            pass
    
    for current_file_hash in set_of_local_potential_file_hashes:
            try:
                print('\nFile Hash: '+current_file_hash[0]+'\n')
                loop.run_until_complete(server.set(current_file_hash[0],my_node_id))
            except:
                pass
    
    try:
        print('\nFile Hash: '+current_file_hash[0]+'\n')
        loop.run_until_complete(server.set(my_node_id,my_node_ip_address+':'+default_port))
    except:
        pass
     
    #loop = asyncio.new_event_loop()
    #asyncio.set_event_loop(loop)
    #asyncio.wait() 
    #result = loop.run_until_complete(server.get('jeffwework'))
    #future = loop.create_task(server.get(example_block_hash_1))
    #result = loop.run_until_complete(future)
    
    test_block_hash = '2755c37e3a3c9808d027348f48f889df7a5bac61cd0add15fa2d0a92da1ffb29'
    result = loop.run_until_complete(server.get(test_block_hash))
    #print(result)
    
    if 0:
        loop.call_soon_threadsafe(loop.stop)
        server.stop()
        loop.close()
