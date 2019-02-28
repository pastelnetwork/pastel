import sys, os.path, io, glob, hashlib, random, os, sqlite3, warnings, shutil
warnings.simplefilter(action='ignore', category=FutureWarning)
warnings.filterwarnings('ignore',category=DeprecationWarning)
from struct import pack, unpack
from fs.memoryfs import MemoryFS
from fs.copy import copy_fs
from fs.osfs import OSFS
from random import randint
from math import ceil, floor, sqrt, log
from collections import defaultdict
from tqdm import tqdm
from time import time
from pastel_utility_functions_v1 import regenerate_sqlite_chunk_database_func
#Requirements: pip install tqdm, fs, numpy
 
###############################################################################################################
# Functions:
###############################################################################################################

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

def get_local_block_file_header_data_func(sha256_hash_of_desired_block):
    global block_storage_folder_path
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_desired_block+'*.block'))
    if len(list_of_block_file_paths) > 0:
        try:
            with open(list_of_block_file_paths[0],'rb') as f:
                block_binary_data= f.read()
            input_stream = io.BufferedReader(io.BytesIO(block_binary_data),buffer_size=1000000)
            header = unpack('!III', input_stream.read(12))
            filesize = header[0]
            blocksize = header[1]
            number_of_blocks_required = ceil(filesize/blocksize)
            return filesize, blocksize, number_of_blocks_required
        except Exception as e:
            print('Error: '+ str(e))     
    else:
        print('Don\'t have that file locally!')
        filesize = 0
        blocksize = 0
        number_of_blocks_required = 0
        return filesize, blocksize, number_of_blocks_required
    
def encode_final_art_zipfile_into_luby_transform_blocks_func(sha256_hash_of_art_file):
    global block_storage_folder_path
    global block_redundancy_factor
    global desired_block_size_in_bytes
    global prepared_final_art_zipfiles_folder_path
    start_time = time()
    ramdisk_object = MemoryFS()
    filesystem_object = OSFS(block_storage_folder_path)     
    c_constant = 0.1
    delta_constant = 0.5
    seed = randint(0, 1 << 31 - 1)
    path_to_final_artwork_zipfile_including_metadata = glob.glob(prepared_final_art_zipfiles_folder_path+'*'+sha256_hash_of_art_file+'*')[0]    
    final_art_file__original_size_in_bytes = os.path.getsize(path_to_final_artwork_zipfile_including_metadata)
    output_blocks_list = [] #Process ZIP file into a stream of encoded blocks, and save those blocks as separate files in the output folder:
    print('Now encoding file ' + os.path.split(path_to_final_artwork_zipfile_including_metadata)[-1] + ' (' + str(round(final_art_file__original_size_in_bytes/1000000)) + 'mb)\n\n')
    total_number_of_blocks_to_generate = ceil((1.00*block_redundancy_factor*final_art_file__original_size_in_bytes) / desired_block_size_in_bytes)
    print('Total number of blocks to generate for target level of redundancy: '+str(total_number_of_blocks_to_generate))
    pbar = tqdm(total=total_number_of_blocks_to_generate)
    with open(path_to_final_artwork_zipfile_including_metadata,'rb') as f:
        f_bytes = f.read()
    filesize = len(f_bytes) 
    art_zipfile_hash = hashlib.sha256(f_bytes).hexdigest()
    if art_zipfile_hash == sha256_hash_of_art_file: #Convert file byte contents into blocksize chunks, padding last one if necessary:
        blocks = [int.from_bytes(f_bytes[ii:ii+desired_block_size_in_bytes].ljust(desired_block_size_in_bytes, b'0'), sys.byteorder) for ii in range(0, len(f_bytes), desired_block_size_in_bytes)]
        number_of_blocks = len(blocks)
        print('The length of the blocks list: '+str(number_of_blocks))
        prng = PRNG(params=(number_of_blocks, delta_constant, c_constant))
        prng.set_seed(seed)
        number_of_blocks_generated = 0# block generation loop
        while number_of_blocks_generated <= total_number_of_blocks_to_generate:
            update_skip = 1
            if (number_of_blocks_generated % update_skip) == 0:
                pbar.update(update_skip)
            blockseed, d, ix_samples = prng.get_src_blocks()
            block_data = 0
            for ix in ix_samples:
                block_data ^= blocks[ix]
            block = (filesize, desired_block_size_in_bytes, blockseed, int.to_bytes(block_data, desired_block_size_in_bytes, sys.byteorder)) # Generate blocks of XORed data in network byte order
            number_of_blocks_generated = number_of_blocks_generated + 1
            packed_block_data = pack('!III%ss'%desired_block_size_in_bytes, *block)
            output_blocks_list.append(packed_block_data)
            hash_of_block = hashlib.sha256(packed_block_data).hexdigest()
            output_block_file_path = 'FileHash__'+art_zipfile_hash + '__Block__' + '{0:09}'.format(number_of_blocks_generated) + '__BlockHash_' + hash_of_block +'.block'
            try:
                with ramdisk_object.open(output_block_file_path,'wb') as f:
                    f.write(packed_block_data)
            except Exception as e:
                print('Error: '+ str(e))
        duration_in_seconds = round(time() - start_time, 1)
        print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds! \nOriginal zip file was encoded into ' + str(number_of_blocks_generated) + ' blocks of ' + str(ceil(desired_block_size_in_bytes/1000)) + ' kilobytes each. Total size of all blocks is ~' + str(ceil((number_of_blocks_generated*desired_block_size_in_bytes)/1000000)) + ' megabytes\n')
        print('Now copying encoded files from ram disk to local storage...')
        copy_fs(ramdisk_object,filesystem_object)
        print('Done!\n')
        ramdisk_object.close()
        return duration_in_seconds

def decode_block_files_into_art_zipfile_func(sha256_hash_of_art_file):
    global block_storage_folder_path
    global reconstructed_files_destination_folder_path
    global prepared_final_art_zipfiles_folder_path
    start_time = time()
    reconstructed_file_destination_file_path = os.path.join(reconstructed_files_destination_folder_path,'Final_Art_Zipfile_Hash__' + sha256_hash_of_art_file + '.zip')
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_art_file+'*.block'))
    reported_file_sha256_hash = list_of_block_file_paths[0].split(os.sep)[-1].split('__')[1]
    print('\nFound '+str(len(list_of_block_file_paths))+' block files in folder! The SHA256 hash of the original zip file is reported to be: '+reported_file_sha256_hash+'\n')
    c_constant = 0.1
    delta_constant = 0.5
    block_graph = BlockGraph(len(list_of_block_file_paths))
    for block_count, current_block_file_path in enumerate(list_of_block_file_paths):
        with open(current_block_file_path,'rb') as f:
            packed_block_data = f.read()
        hash_of_block = hashlib.sha256(packed_block_data).hexdigest()
        reported_hash_of_block = current_block_file_path.split('__')[-1].replace('.block','').replace('BlockHash_','')
        if hash_of_block == reported_hash_of_block:
            pass #Block hash matches reported hash, so block is not corrupted
        else:
            print('\nError, the block hash does NOT match the reported hash, so this block is corrupted! Skipping to next block...\n')
            continue
        input_stream = io.BufferedReader(io.BytesIO(packed_block_data)) #,buffer_size=1000000
        header = unpack('!III', input_stream.read(12))
        filesize = header[0]
        blocksize = header[1]
        blockseed = header[2]
        number_of_blocks_required = ceil(filesize/blocksize)
        if block_count == 0:
            print('\nA total of '+str(number_of_blocks_required)+' blocks are required!\n')
        block = int.from_bytes(input_stream.read(blocksize),'big')
        input_stream.close
        if (block_count % 1) == 0:
            name_parts_list = current_block_file_path.split(os.sep)[-1].split('_')
            parsed_block_hash = name_parts_list[-1].replace('.block','')
            parsed_block_number = name_parts_list[6]
            parsed_file_hash = name_parts_list[2]
            print('\nNow decoding:\nBlock Number: ' + parsed_block_number + '\nFile Hash: ' + parsed_file_hash + '\nBlock Hash: '+ parsed_block_hash)
        prng = PRNG(params=(number_of_blocks_required, delta_constant, c_constant))
        _, _, src_blocks = prng.get_src_blocks(seed = blockseed)
        file_reconstruction_complete = block_graph.add_block(src_blocks, block)
        if file_reconstruction_complete:
            print('\nDone building file! Processed a total of '+str(block_count)+' blocks\n')
            break
        with open(reconstructed_file_destination_file_path,'wb') as f: 
            for ix, block_bytes in enumerate(map(lambda p: int.to_bytes(p[1], blocksize, 'big'), sorted(block_graph.eliminated.items(), key = lambda p:p[0]))):
                if ix < number_of_blocks_required - 1 or filesize % blocksize == 0:
                    f.write(block_bytes)
                else:
                    f.write(block_bytes[:filesize%blocksize])
    try:
        with open(reconstructed_file_destination_file_path,'rb') as f:
            reconstructed_file = f.read()
            reconstructed_file_hash = hashlib.sha256(reconstructed_file).hexdigest()
            if reported_file_sha256_hash == reconstructed_file_hash:
                completed_successfully = 1
                print('\nThe SHA256 hash of the reconstructed file matches the reported file hash-- file is valid! Now copying to prepared final art zipfiles folder...\n')
                shutil.copy(reconstructed_file_destination_file_path, prepared_final_art_zipfiles_folder_path)
                print('Done!')
            else:
                completed_successfully = 0
                print('\nProblem! The SHA256 hash of the reconstructed file does NOT match the expected hash! File is not valid.\n')
    except Exception as e:
        print('Error: '+ str(e))
    duration_in_seconds = round(time() - start_time, 1)
    print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds!')
    return completed_successfully

def refresh_block_storage_folder_and_check_block_integrity_func(use_verify_integrity=0):
    global chunk_db_file_path
    global block_storage_folder_path
    if not os.path.exists(chunk_db_file_path):
       regenerate_sqlite_chunk_database_func()
    potential_local_block_hashes_list = []
    potential_local_file_hashes_list = []
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    if use_verify_integrity:
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
            reported_block_sha256_hash = current_block_file_path.split(os.sep)[-1].split('__')[-1].replace('BlockHash_','').replace('.block','')
            if hash_of_block == reported_block_sha256_hash:
                reported_file_sha256_hash = current_block_file_path.split(os.sep)[-1].split('__')[1]
                if reported_block_sha256_hash not in potential_local_block_hashes_list:
                    potential_local_block_hashes_list.append(reported_block_sha256_hash)
                    potential_local_file_hashes_list.append(reported_file_sha256_hash)
            else:
                print('\nBlock '+reported_block_sha256_hash+' did not hash to the correct value-- file is corrupt! Skipping to next file...\n')
        print('\nDone verifying block files!\nNow writing local block metadata to SQLite database...\n')
    else:
        for current_block_file_path in list_of_block_file_paths:
            reported_block_sha256_hash = current_block_file_path.split(os.sep)[-1].split('__')[-1].replace('BlockHash_','').replace('.block','')
            reported_file_sha256_hash = current_block_file_path.split(os.sep)[-1].split('__')[1]
            if reported_block_sha256_hash not in potential_local_block_hashes_list:
                potential_local_block_hashes_list.append(reported_block_sha256_hash)
                potential_local_file_hashes_list.append(reported_file_sha256_hash)
    conn = sqlite3.connect(chunk_db_file_path)
    c = conn.cursor()
    c.execute("""DELETE FROM potential_local_hashes""")
    for hash_cnt, current_block_hash in enumerate(potential_local_block_hashes_list):
        current_file_hash = potential_local_file_hashes_list[hash_cnt]
        sql_string = """INSERT OR IGNORE INTO potential_local_hashes (block_hash, file_hash) VALUES (\"{blockhash}\", \"{filehash}\")""".format(blockhash=current_block_hash, filehash=current_file_hash)
        c.execute(sql_string)
    conn.commit()
    conn.close()
    #print('Done writing file hash data to SQLite file!\n')  
    return potential_local_block_hashes_list, potential_local_file_hashes_list

#Stress testing functions:
def randomly_delete_percentage_of_local_block_files_func(percentage_of_block_files_to_randomly_delete):
    global block_storage_folder_path
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    number_of_deleted_blocks = 0
    pbar = tqdm(total=ceil(percentage_of_block_files_to_randomly_delete*len(list_of_block_file_paths)))
    print('\nNow deleting random block files!\n')
    for current_file_path in list_of_block_file_paths:
        if random.random() <= percentage_of_block_files_to_randomly_delete:
            try:
                os.remove(current_file_path)
                number_of_deleted_blocks = number_of_deleted_blocks + 1
                pbar.update(1)
            except OSError:
                pass
    return number_of_deleted_blocks
            
def randomly_corrupt_a_percentage_of_bytes_in_a_random_sample_of_block_files_func(percentage_of_block_files_to_randomly_corrupt,percentage_of_each_selected_file_to_be_randomly_corrupted):
    global block_storage_folder_path
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    number_of_corrupted_blocks = 0
    for current_file_path in list_of_block_file_paths:
        if random.random() <= percentage_of_block_files_to_randomly_corrupt:
            number_of_corrupted_blocks = number_of_corrupted_blocks + 1
            total_bytes_of_data_in_chunk = os.path.getsize(current_file_path)
            random_bytes_to_write = 5
            number_of_bytes_to_corrupt = ceil(total_bytes_of_data_in_chunk*percentage_of_each_selected_file_to_be_randomly_corrupted)
            specific_bytes_to_corrupt = random.sample(range(1, total_bytes_of_data_in_chunk), ceil(number_of_bytes_to_corrupt/random_bytes_to_write))
            print('\n\nNow intentionally corrupting block file: ' + current_file_path.split('\\')[-1] )
            with open(current_file_path,'wb') as f:
                try:
                    for byte_index in specific_bytes_to_corrupt:
                        f.seek(byte_index)
                        f.write(os.urandom(random_bytes_to_write))
                except OSError:
                    pass
    return number_of_corrupted_blocks