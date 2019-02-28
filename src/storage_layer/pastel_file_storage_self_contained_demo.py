import os, time, base64, hashlib, glob, random, sys, io, struct, subprocess, math, urllib
from math import ceil, floor, log, log2, sqrt
from collections import defaultdict
from struct import pack, unpack
from time import time
from fs.memoryfs import MemoryFS
from fs.copy import copy_fs
from fs.osfs import OSFS
import zstandard as zstd
import tarfile

#Note: Code is based on the Python LT implementation by Anson Rosenthal, found here: https://github.com/anrosent/LT-code

#Parameters:
use_demonstrate_luby_blocks = 1
block_redundancy_factor = 12 #How many times more blocks should we store than are required to regenerate the file?
desired_block_size_in_bytes =  1024*1000*2
root_pastel_folder_path = '/Users/jemanuel/pastel/'
prepared_final_art_zipfiles_folder_path = os.path.join(root_pastel_folder_path,'prepared_final_art_files' + os.sep)
test_art_file_name = 'Arturo_Lopez__Number_03'
folder_containing_art_image_and_metadata_files = root_pastel_folder_path + 'art_folders_to_encode' + os.sep + test_art_file_name + os.sep
path_to_folder_containing_luby_blocks = folder_containing_art_image_and_metadata_files + 'block_files' + os.sep
path_to_save_reconstructed_and_decompressed_files = root_pastel_folder_path + 'reconstructed_files' + os.sep + test_art_file_name + os.sep
percentage_of_block_files_to_randomly_delete = 0.75
percentage_of_block_files_to_randomly_corrupt = 0.10
percentage_of_each_selected_file_to_be_randomly_corrupted = 0.02
art_block_storage_folder_path = root_pastel_folder_path + 'art_block_storage'

#Various helper functions:
def get_sha256_hash_of_input_data_func(input_data_or_string):
    if isinstance(input_data_or_string, str):
        input_data_or_string = input_data_or_string.encode('utf-8')
    sha256_hash_of_input_data = hashlib.sha3_256(input_data_or_string).hexdigest()
    return sha256_hash_of_input_data
    
def compress_data_with_zstd_func(input_data, zstd_compression_level):
    zstandard_compressor = zstd.ZstdCompressor(level=zstd_compression_level, write_content_size=True)
    zstd_compressed_data = zstandard_compressor.compress(input_data)
    return zstd_compressed_data

def decompress_data_with_zstd_func(zstd_compressed_data):
    zstandard_decompressor = zstd.ZstdDecompressor()
    uncompressed_data = zstandard_decompressor.decompress(zstd_compressed_data)
    return uncompressed_data

def add_art_image_files_and_metadata_to_zstd_compressed_tar_file_func(folder_containing_art_image_and_metadata_files):
    with tarfile.open(folder_containing_art_image_and_metadata_files+ 'art_files.tar', 'w') as tar:
       tar.add(folder_containing_art_image_and_metadata_files, arcname='.')
    with open(folder_containing_art_image_and_metadata_files+'art_files.tar','rb') as f:
        tar_binary_data = f.read()
    tar_file_hash = hashlib.sha3_256(tar_binary_data).hexdigest()
    zstd_compression_level = 15
    print('Now compressing art image and metadata files with Z-Standard...')
    compressed_tar_binary_data = compress_data_with_zstd_func(tar_binary_data, zstd_compression_level)
    print('Done!')
    compressed_output_file_path = folder_containing_art_image_and_metadata_files+'art_files_compressed.zst'
    with open(compressed_output_file_path,'wb') as f:
        f.write(compressed_tar_binary_data)
    with open(compressed_output_file_path,'rb') as f:
        compressed_binary_data_test = f.read()
    compressed_file_hash = hashlib.sha3_256(compressed_binary_data_test).hexdigest()
    decompressed_data = decompress_data_with_zstd_func(compressed_binary_data_test)
    decompressed_file_hash = hashlib.sha3_256(decompressed_data).hexdigest()
    assert(decompressed_file_hash==tar_file_hash)
    return compressed_output_file_path, compressed_file_hash

#LT Coding Helper fucntions:
def gen_tau(S, K, delta):
    """The Robust part of the RSD, we precompute an array for speed"""
    pivot = floor(K/S)
    return [S/K * 1/d for d in range(1, pivot)] \
            + [S/K * log(S/delta)] \
            + [0 for d in range(pivot, K)] 

def gen_rho(K):
    """The Ideal Soliton Distribution, we precompute an array for speed"""
    return [1/K] + [1/(d*(d-1)) for d in range(2, K+1)]

def gen_mu(K, delta, c):
    """The Robust Soliton Distribution on the degree of transmitted blocks"""
    S = c * log(K/delta) * sqrt(K) 
    tau = gen_tau(S, K, delta)
    rho = gen_rho(K)
    normalizer = sum(rho) + sum(tau)
    return [(rho[d] + tau[d])/normalizer for d in range(K)]

def gen_rsd_cdf(K, delta, c):
    """The CDF of the RSD on block degree, precomputed for sampling speed"""
    mu = gen_mu(K, delta, c)
    return [sum(mu[:d+1]) for d in range(K)]

class PRNG(object):
    """A Pseudorandom Number Generator that yields samples from the set of source blocks using the RSD degree distribution described above. """
    def __init__(self, params):
        """Provide RSD parameters on construction """
        self.state = None  # Seed is set by interfacing code using set_seed
        K, delta, c = params
        self.K = K
        self.cdf = gen_rsd_cdf(K, delta, c)

    def _get_next(self):
        """Executes the next iteration of the PRNG evolution process, and returns the result"""
        PRNG_A = 16807
        PRNG_M = (1 << 31) - 1
        self.state = PRNG_A * self.state % PRNG_M
        return self.state

    def _sample_d(self):
        """Samples degree given the precomputed distributions above and the linear PRNG output """
        PRNG_M = (1 << 31) - 1
        PRNG_MAX_RAND = PRNG_M - 1
        p = self._get_next() / PRNG_MAX_RAND
        for ix, v in enumerate(self.cdf):
            if v > p:
                return ix + 1
        return ix + 1

    def set_seed(self, seed):
        """Reset the state of the PRNG to the given seed"""
        self.state = seed

    def get_src_blocks(self, seed=None):
        """Returns the indices of a set of `d` source blocks sampled from indices i = 1, ..., K-1 uniformly, where `d` is sampled from the RSD described above.     """
        if seed:
            self.state = seed
        blockseed = self.state
        d = self._sample_d()
        have = 0
        nums = set()
        while have < d:
            num = self._get_next() % self.K
            if num not in nums:
                nums.add(num)
                have += 1
        return blockseed, d, nums
  
def _split_file(f, blocksize):
    """Block file byte contents into blocksize chunks, padding last one if necessary"""
    f_bytes = f.read()
    blocks = [int.from_bytes(f_bytes[i:i+blocksize].ljust(blocksize, b'0'), sys.byteorder) 
            for i in range(0, len(f_bytes), blocksize)]
    return len(f_bytes), blocks

class CheckNode(object):
     def __init__(self, src_nodes, check):
        self.check = check
        self.src_nodes = src_nodes

class BlockGraph(object):
    """Graph on which we run Belief Propagation to resolve source node data"""
    def __init__(self, num_blocks):
        self.checks = defaultdict(list)
        self.num_blocks = num_blocks
        self.eliminated = {}

    def add_block(self, nodes, data):
        """Adds a new check node and edges between that node and all source nodes it connects, resolving all message passes that become possible as a result. """
        if len(nodes) == 1:# We can eliminate this source node
            to_eliminate = list(self.eliminate(next(iter(nodes)), data))
            while len(to_eliminate):# Recursively eliminate all nodes that can now be resolved
                other, check = to_eliminate.pop()
                to_eliminate.extend(self.eliminate(other, check))
        else:
            for node in list(nodes): # Pass messages from already-resolved source nodes
                if node in self.eliminated:
                    nodes.remove(node)
                    data ^= self.eliminated[node]
            if len(nodes) == 1: # Resolve if we are left with a single non-resolved source node
                return self.add_block(nodes, data)
            else: # Add edges for all remaining nodes to this check
                check = CheckNode(nodes, data)
                for node in nodes:
                    self.checks[node].append(check)
        return len(self.eliminated) >= self.num_blocks # Are we done yet?

    def eliminate(self, node, data):
        """Resolves a source node, passing the message to all associated checks """
        self.eliminated[node] = data # Cache resolved value
        others = self.checks[node]
        del self.checks[node]
        for check in others: # Pass messages to all associated checks
            check.check ^= data
            check.src_nodes.remove(node)
            if len(check.src_nodes) == 1: # Yield all nodes that can now be resolved
                yield (next(iter(check.src_nodes)), check.check)

def encode_file_into_luby_blocks_func(folder_containing_art_image_and_metadata_files):
    global block_redundancy_factor
    global desired_block_size_in_bytes
    file_paths_in_folder = glob.glob(folder_containing_art_image_and_metadata_files + '*')
    for current_file_path in file_paths_in_folder:
        if current_file_path.split('.')[-1] in ['zst','tar']:
            try:
                os.remove(current_file_path)
            except Exception as e:
                print('Error: '+ str(e))                
    c_constant = 0.1 #Don't touch
    delta_constant = 0.5 #Don't touch
    start_time = time()
    ramdisk_object = MemoryFS()
    c_constant = 0.1
    delta_constant = 0.5
    seed = random.randint(0, 1 << 31 - 1)
    compressed_output_file_path, compressed_file_hash = add_art_image_files_and_metadata_to_zstd_compressed_tar_file_func(folder_containing_art_image_and_metadata_files)      
    final_art_file__original_size_in_bytes = os.path.getsize(compressed_output_file_path)
    output_blocks_list = [] #Process compressed file into a stream of encoded blocks, and save those blocks as separate files in the output folder:
    print('Now encoding file ' + compressed_output_file_path + ' (' + str(round(final_art_file__original_size_in_bytes/1000000)) + 'mb)\n\n')
    total_number_of_blocks_to_generate = ceil((1.00*block_redundancy_factor*final_art_file__original_size_in_bytes) / desired_block_size_in_bytes)
    print('Total number of blocks to generate for target level of redundancy: '+str(total_number_of_blocks_to_generate))
    with open(compressed_output_file_path,'rb') as f:
        compressed_data = f.read()
    compressed_data_size_in_bytes = len(compressed_data)
    blocks = [int.from_bytes(compressed_data[ii:ii+desired_block_size_in_bytes].ljust(desired_block_size_in_bytes, b'0'), 'little') for ii in range(0, compressed_data_size_in_bytes, desired_block_size_in_bytes)]
    prng = PRNG(params=(len(blocks), delta_constant, c_constant))
    prng.set_seed(seed)
    output_blocks_list = list()
    number_of_blocks_generated = 0
    while number_of_blocks_generated < total_number_of_blocks_to_generate:
        random_seed, d, ix_samples = prng.get_src_blocks()
        block_data = 0
        for ix in ix_samples:
            block_data ^= blocks[ix]
        block_data_bytes = int.to_bytes(block_data, desired_block_size_in_bytes, 'little')
        block_data_hash = hashlib.sha3_256(block_data_bytes).digest()
        block = (compressed_data_size_in_bytes, desired_block_size_in_bytes, random_seed, block_data_hash, block_data_bytes)
        header_bit_packing_pattern_string = '<3I32s'
        bit_packing_pattern_string = header_bit_packing_pattern_string + str(desired_block_size_in_bytes) + 's'
        length_of_header_in_bytes = struct.calcsize(header_bit_packing_pattern_string)
        packed_block_data = pack(bit_packing_pattern_string, *block)
        if number_of_blocks_generated == 0: #Test that the bit-packing is working correctly:
            with io.BufferedReader(io.BytesIO(packed_block_data)) as f:
                header_data = f.read(length_of_header_in_bytes)
                #first_generated_block_raw_data = f.read(desired_block_size_in_bytes)
            compressed_input_data_size_in_bytes_test, desired_block_size_in_bytes_test, random_seed_test, block_data_hash_test = unpack(header_bit_packing_pattern_string, header_data)
            if block_data_hash_test != block_data_hash:
                print('Error! Block data hash does not match the hash reported in the block header!')
        output_blocks_list.append(packed_block_data)
        number_of_blocks_generated = number_of_blocks_generated + 1
        hash_of_block = get_sha256_hash_of_input_data_func(packed_block_data)
        output_block_file_path = 'FileHash__'+compressed_file_hash + '__Block__' + '{0:09}'.format(number_of_blocks_generated) + '__BlockHash_' + hash_of_block +'.block'
        try:
            with ramdisk_object.open(output_block_file_path,'wb') as f:
                f.write(packed_block_data)
        except Exception as e:
            print('Error: '+ str(e))
    duration_in_seconds = round(time() - start_time, 1)
    print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds! \nOriginal zip file was encoded into ' + str(number_of_blocks_generated) + ' blocks of ' + str(ceil(desired_block_size_in_bytes/1000)) + ' kilobytes each. Total size of all blocks is ~' + str(ceil((number_of_blocks_generated*desired_block_size_in_bytes)/1000000)) + ' megabytes\n')
    print('Now copying encoded files from ram disk to local storage...')
    block_storage_folder_path = folder_containing_art_image_and_metadata_files + os.sep + 'block_files'
    if not os.path.isdir(block_storage_folder_path):
        os.makedirs(block_storage_folder_path)
    filesystem_object = OSFS(block_storage_folder_path)     
    copy_fs(ramdisk_object, filesystem_object)
    print('Done!\n')
    ramdisk_object.close()
    return duration_in_seconds

def reconstruct_data_from_luby_blocks(path_to_folder_containing_luby_blocks):
    c_constant = 0.1
    delta_constant = 0.5
    list_of_luby_block_file_paths = glob.glob(path_to_folder_containing_luby_blocks + '*.block')
    list_of_luby_block_data_binaries = list()
    for current_luby_block_file_path in list_of_luby_block_file_paths:
        try:
            with open(current_luby_block_file_path,'rb') as f:
                current_luby_block_binary_data = f.read()
            list_of_luby_block_data_binaries.append(current_luby_block_binary_data)
        except Exception as e:
            print('Error: '+ str(e))
    header_bit_packing_pattern_string = '<3I32s'
    length_of_header_in_bytes = struct.calcsize(header_bit_packing_pattern_string)
    first_block_data = list_of_luby_block_data_binaries[0]
    with io.BytesIO(first_block_data) as f:
        compressed_input_data_size_in_bytes, desired_block_size_in_bytes, _, _ = unpack(header_bit_packing_pattern_string, f.read(length_of_header_in_bytes))
    minimum_number_of_blocks_required = ceil(compressed_input_data_size_in_bytes / desired_block_size_in_bytes)
    block_graph = BlockGraph(minimum_number_of_blocks_required)
    number_of_lt_blocks_found = len(list_of_luby_block_data_binaries)
    print('Found ' + str(number_of_lt_blocks_found) + ' LT blocks to use...')
    for block_count, current_packed_block_data in enumerate(list_of_luby_block_data_binaries):
        with io.BytesIO(current_packed_block_data) as f:
            header_data = f.read(length_of_header_in_bytes)
            compressed_input_data_size_in_bytes, desired_block_size_in_bytes, random_seed, reported_block_data_hash = unpack(header_bit_packing_pattern_string, header_data)
            block_data_bytes = f.read(desired_block_size_in_bytes)
            calculated_block_data_hash = hashlib.sha3_256(block_data_bytes).digest()
            if calculated_block_data_hash == reported_block_data_hash:
                if block_count == 0:
                    print('Calculated block data hash matches the reported blocks from the block header!')
                prng = PRNG(params=(minimum_number_of_blocks_required, delta_constant, c_constant))
                _, _, src_blocks = prng.get_src_blocks(seed=random_seed)
                block_data_bytes_decoded = int.from_bytes(block_data_bytes, 'little' )
                file_reconstruction_complete = block_graph.add_block(src_blocks, block_data_bytes_decoded)
                if file_reconstruction_complete:
                    break
            else:
                print('Block data hash does not match reported block hash from block header file! Skipping to next block...')
    if file_reconstruction_complete:
        print('\nDone reconstructing data from blocks! Processed a total of ' + str(block_count + 1) + ' blocks\n')
        incomplete_file = 0
    else:
        print('Warning! Processed all available LT blocks but still do not have the entire file!')
        incomplete_file = 1
    if not incomplete_file:
        with io.BytesIO() as f:
            for ix, block_bytes in enumerate(map(lambda p: int.to_bytes(p[1], desired_block_size_in_bytes, 'little'), sorted(block_graph.eliminated.items(), key=lambda p:p[0]))):
                if (ix < minimum_number_of_blocks_required - 1) or ( (compressed_input_data_size_in_bytes % desired_block_size_in_bytes) == 0 ):
                    f.write(block_bytes)
                else:
                    f.write(block_bytes[:compressed_input_data_size_in_bytes % desired_block_size_in_bytes])
                reconstructed_data = f.getvalue()
    if not incomplete_file and type(reconstructed_data) in [bytes, bytearray]:
        print('Successfully reconstructed file from LT blocks! Reconstucted file size is '+str(len(reconstructed_data))+' bytes.')
        return reconstructed_data
    else:
        print('\nError: Data reconstruction from LT blocks failed!\n\n')

def save_reconstructed_data_to_file_and_decompress_func(reconstructed_data, path_to_save_reconstructed_and_decompressed_files):
    decompressed_reconstructed_data = decompress_data_with_zstd_func(reconstructed_data)
    if not os.path.isdir(path_to_save_reconstructed_and_decompressed_files):
        os.makedirs(path_to_save_reconstructed_and_decompressed_files)
    with open(path_to_save_reconstructed_and_decompressed_files + 'art_files.tar', 'wb') as f:
        f.write(decompressed_reconstructed_data)
    with tarfile.open(path_to_save_reconstructed_and_decompressed_files+'art_files.tar') as tar:
        tar.extractall(path=path_to_save_reconstructed_and_decompressed_files)
    try:
        os.remove(path_to_save_reconstructed_and_decompressed_files+'art_files.tar')
    except Exception as e:
        print('Error: '+ str(e))

#Stress testing functions:
def randomly_delete_percentage_of_local_block_files_func(path_to_folder_containing_luby_blocks, percentage_of_block_files_to_randomly_delete):
    list_of_block_file_paths = glob.glob(path_to_folder_containing_luby_blocks+'*.block')
    number_of_deleted_blocks = 0
    print('\nNow deleting random block files...')
    for current_file_path in list_of_block_file_paths:
        if random.random() <= percentage_of_block_files_to_randomly_delete:
            try:
                os.remove(current_file_path)
                number_of_deleted_blocks = number_of_deleted_blocks + 1
            except OSError:
                pass
    return number_of_deleted_blocks
            
def randomly_corrupt_a_percentage_of_bytes_in_a_random_sample_of_block_files_func(path_to_folder_containing_luby_blocks, percentage_of_block_files_to_randomly_corrupt, percentage_of_each_selected_file_to_be_randomly_corrupted):
    list_of_block_file_paths = glob.glob(path_to_folder_containing_luby_blocks+'*.block')
    number_of_corrupted_blocks = 0
    for current_file_path in list_of_block_file_paths:
        if random.random() <= percentage_of_block_files_to_randomly_corrupt:
            number_of_corrupted_blocks = number_of_corrupted_blocks + 1
            total_bytes_of_data_in_chunk = os.path.getsize(current_file_path)
            random_bytes_to_write = 5
            number_of_bytes_to_corrupt = ceil(total_bytes_of_data_in_chunk*percentage_of_each_selected_file_to_be_randomly_corrupted)
            specific_bytes_to_corrupt = random.sample(range(1, total_bytes_of_data_in_chunk), ceil(number_of_bytes_to_corrupt/random_bytes_to_write))
            print('Now intentionally corrupting block file: ' + current_file_path.split('\\')[-1] )
            with open(current_file_path,'wb') as f:
                try:
                    for byte_index in specific_bytes_to_corrupt:
                        f.seek(byte_index)
                        f.write(os.urandom(random_bytes_to_write))
                except OSError:
                    pass
    return number_of_corrupted_blocks

def get_block_file_list_from_masternode_func(ip_address_of_masternode):
    masternode_file_server_url = 'http://'+ip_address_of_masternode+'/'
    response = urllib.request.urlopen(masternode_file_server_url)
    response_html_string = response.read()
    response_html_string_split = response_html_string.decode('utf-8').split('./')
    list_of_available_block_file_names = [x.split('</a><br>')[0] for x in response_html_string_split if 'FileHash__' in x]
    list_of_available_art_file_hashes = list(set([x.split('FileHash__')[1].split('__Block__')[0] for x in list_of_available_block_file_names]))        
    list_of_available_block_file_hashes = list(set([x.split('__BlockHash_')[1].split('.block')[0] for x in list_of_available_block_file_names]))        
    return list_of_available_block_file_names, list_of_available_art_file_hashes, list_of_available_block_file_hashes

def get_local_matching_blocks_from_art_file_hash_func(sha256_hash_of_desired_art_file=''):
    global art_block_storage_folder_path
    list_of_block_file_paths = glob.glob(os.path.join(art_block_storage_folder_path,'*'+sha256_hash_of_desired_art_file+'*.block'))
    list_of_block_hashes = []
    list_of_file_hashes = []
    for current_block_file_path in list_of_block_file_paths:
        reported_file_sha256_hash = current_block_file_path.split('\\')[-1].split('__')[1]
        list_of_file_hashes.append(reported_file_sha256_hash)
        reported_block_file_sha256_hash = current_block_file_path.split('__')[-1].replace('.block','').replace('BlockHash_','')
        list_of_block_hashes.append(reported_block_file_sha256_hash)
    return list_of_block_file_paths, list_of_block_hashes, list_of_file_hashes

def get_all_local_block_file_hashes_func():
    _, list_of_block_hashes, list_of_art_file_hashes = get_local_matching_blocks_from_art_file_hash_func()
    list_of_block_hashes = list(set(list_of_block_hashes))
    list_of_art_file_hashes = list(set(list_of_art_file_hashes))
    return list_of_art_file_hashes, list_of_block_hashes

def get_local_block_file_binary_data_func(sha256_hash_of_desired_block):
    global block_storage_folder_path
    list_of_block_file_paths = glob.glob(os.path.join(art_block_storage_folder_path,'*'+sha256_hash_of_desired_block+'*.block'))
    try:
        with open(list_of_block_file_paths[0],'rb') as f:
            block_binary_data= f.read()
            return block_binary_data
    except Exception as e:
        print('Error: '+ str(e))
     
def create_storage_challenge_func(ip_address_of_masternode):
    # ip_address_of_masternode = '149.28.34.59'
    _, _, list_of_remote_block_file_hashes = get_block_file_list_from_masternode_func(ip_address_of_masternode)
    _, list_of_local_block_file_hashes = get_all_local_block_file_hashes_func()
    list_of_block_files_available_remotely_and_locally = list(set(list_of_remote_block_file_hashes) & set(list_of_local_block_file_hashes))
    randomly_selected_block_file_hash_for_challenge = random.choice(list_of_block_files_available_remotely_and_locally)
    block_binary_data = get_local_block_file_binary_data_func(randomly_selected_block_file_hash_for_challenge)
    size_of_block_file_in_bytes = sys.getsizeof(block_binary_data)
    challenge_start_byte = random.randint(0,size_of_block_file_in_bytes)
    challenge_end_byte = random.randint(0,size_of_block_file_in_bytes)
    if challenge_start_byte > challenge_end_byte:
        tmp = challenge_end_byte
        challenge_end_byte = challenge_start_byte
        challenge_start_byte = tmp
    block_binary_data_random_segment = block_binary_data[challenge_start_byte:challenge_end_byte]
    size_of_block_file_random_segment_in_bytes = sys.getsizeof(block_binary_data_random_segment)
    assert(size_of_block_file_random_segment_in_bytes != 0)
    sha256_hash_of_block_file_random_segment = get_sha256_hash_of_input_data_func(block_binary_data_random_segment)
    return randomly_selected_block_file_hash_for_challenge, challenge_start_byte, challenge_end_byte, sha256_hash_of_block_file_random_segment

if use_demonstrate_luby_blocks:
    duration_in_seconds = encode_file_into_luby_blocks_func(folder_containing_art_image_and_metadata_files)
    number_of_deleted_blocks = randomly_delete_percentage_of_local_block_files_func(path_to_folder_containing_luby_blocks, percentage_of_block_files_to_randomly_delete)
    number_of_corrupted_blocks = randomly_corrupt_a_percentage_of_bytes_in_a_random_sample_of_block_files_func(path_to_folder_containing_luby_blocks, percentage_of_block_files_to_randomly_corrupt, percentage_of_each_selected_file_to_be_randomly_corrupted)
    reconstructed_data = reconstruct_data_from_luby_blocks(path_to_folder_containing_luby_blocks)
    save_reconstructed_data_to_file_and_decompress_func(reconstructed_data, path_to_save_reconstructed_and_decompressed_files)
    
