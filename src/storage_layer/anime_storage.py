import time
import sys
import os.path
import io
import glob
import hashlib
import imghdr
import random
import os
import sqlite3
import warnings
import base64
import json
from struct import pack, unpack, error
from shutil import copyfile
from fs.memoryfs import MemoryFS
from random import randint
from math import ceil, log, floor, sqrt
from collections import defaultdict
from zipfile import ZipFile
from tqdm import tqdm
from subprocess import check_output
#Requirements:
# pip install tqdm, fs

def user_data_dir(appname=None):
    """Return path to the user data directory for this application"""
    if sys.platform == 'win32':
        path = os.getenv('LOCALAPPDATA', os.path.normpath(os.path.expanduser('~/AppData/Local/')))
        if appname:
            path = os.path.join(path, appname, appname)
    elif sys.platform == 'darwin':
        path = os.path.expanduser('~/Library/Application Support/')
        if appname:
            path = os.path.join(path, appname)
    else:
        path = os.getenv('XDG_DATA_HOME', os.path.expanduser('~/.local/share/'))
        if appname:
            path = os.path.join(path, appname)
    return path

with warnings.catch_warnings():
    warnings.filterwarnings("ignore",category=DeprecationWarning)
    from fs.copy import copy_fs
    from fs.osfs import OSFS

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
    """Block file byte contents into blocksize chunks, padding last one if necessary
    """
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
        # We can eliminate this source node
        if len(nodes) == 1:
            to_eliminate = list(self.eliminate(next(iter(nodes)), data))
            # Recursively eliminate all nodes that can now be resolved
            while len(to_eliminate):
                other, check = to_eliminate.pop()
                to_eliminate.extend(self.eliminate(other, check))
        else:
            # Pass messages from already-resolved source nodes
            for node in list(nodes):
                if node in self.eliminated:
                    nodes.remove(node)
                    data ^= self.eliminated[node]

            # Resolve if we are left with a single non-resolved source node
            if len(nodes) == 1:
                return self.add_block(nodes, data)
            else:
                # Add edges for all remaining nodes to this check
                check = CheckNode(nodes, data)
                for node in nodes:
                    self.checks[node].append(check)
        # Are we done yet?
        return len(self.eliminated) >= self.num_blocks

    def eliminate(self, node, data):
        """Resolves a source node, passing the message to all associated checks """
        # Cache resolved value
        self.eliminated[node] = data
        others = self.checks[node]
        del self.checks[node]
        # Pass messages to all associated checks
        for check in others:
            check.check ^= data
            check.src_nodes.remove(node)
            # Yield all nodes that can now be resolved
            if len(check.src_nodes) == 1:
                yield (next(iter(check.src_nodes)), check.check)
               
                
def turn_art_image_folder_into_encoded_block_files_func(folder_path_of_art_folders_to_encode, block_storage_folder_path, desired_block_size_in_bytes, block_redundancy_factor):
    #First, scan the input folder and add each valid image file to a single zip file:\
    overall_start_time = time.time()
    list_of_art_sub_folder_paths = glob.glob(folder_path_of_art_folders_to_encode + '*\\')
    if not os.path.exists(block_storage_folder_path):
        try:
            os.makedirs(block_storage_folder_path)
        except Exception as e:
                print('Error: '+ str(e)) 
    ramdisk_object = MemoryFS()
    print('\nNow removing any existing remaining zip files...')
    zip_file_paths = glob.glob(folder_path_of_art_folders_to_encode+'\\**\\*.zip')
    for current_zip_file_path in zip_file_paths:
        try:
            os.remove(current_zip_file_path)
        except Exception as e:
            print('Error: '+ str(e))
    print('Done removing old zip files!\n')
    
    for current_art_folder_path in list_of_art_sub_folder_paths:
        start_time = time.time()
        file_base_name = current_art_folder_path.split('\\')[-2].replace(' ','_').lower()
        zip_file_path = os.path.join(current_art_folder_path,file_base_name+'.zip')
        art_input_file_paths =  glob.glob(current_art_folder_path+'*.jpg') + glob.glob(current_art_folder_path+'*.png') + glob.glob(current_art_folder_path+'*.bmp') + glob.glob(current_art_folder_path+'*.gif')
        with ZipFile(zip_file_path,'w') as myzip:
            for current_file_path in art_input_file_paths:
                if (imghdr.what(current_file_path) == 'gif') or (imghdr.what(current_file_path) == 'jpeg') or (imghdr.what(current_file_path) == 'png') or (imghdr.what(current_file_path) == 'bmp'):
                    myzip.write(current_file_path,arcname=current_file_path.split('\\')[-1])
        with open(zip_file_path,'rb') as f:
            final_art_file = f.read()
            art_zipfile_hash = hashlib.sha256(final_art_file).hexdigest()
        new_zip_file_path = os.path.join(current_art_folder_path,art_zipfile_hash + '.zip')
        copyfile(zip_file_path,new_zip_file_path)
        zip_file_path = new_zip_file_path
        final_art_file__original_size_in_bytes = os.path.getsize(zip_file_path)
        output_blocks_list = []
        DEFAULT_C = 0.1
        DEFAULT_DELTA = 0.5
        seed = randint(0, 1 << 31 - 1)
        #Process ZIP file into a stream of encoded blocks, and save those blocks as separate files in the output folder:
        print('Now encoding file ' + zip_file_path + ', (' + str(round(final_art_file__original_size_in_bytes/1000000)) + 'mb);\nFile Hash is: '+ art_zipfile_hash+'\n\n')
        total_number_of_blocks_required = ceil((1.00*block_redundancy_factor*final_art_file__original_size_in_bytes) / desired_block_size_in_bytes)
        pbar = tqdm(total=total_number_of_blocks_required)
        with open(zip_file_path,'rb') as f:
            f_bytes = f.read()
        filesize = len(f_bytes)
        #Convert file byte contents into blocksize chunks, padding last one if necessary
        blocks = [int.from_bytes(f_bytes[ii:ii+desired_block_size_in_bytes].ljust(desired_block_size_in_bytes, b'0'), sys.byteorder) for ii in range(0, len(f_bytes), desired_block_size_in_bytes)]
        K = len(blocks) # init stream vars
        prng = PRNG(params=(K, DEFAULT_DELTA, DEFAULT_C))
        prng.set_seed(seed)
        number_of_blocks_generated = 0# block generation loop
        while number_of_blocks_generated <= total_number_of_blocks_required:
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
        duration_in_seconds = round(time.time() - start_time, 1)
        print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds! \nOriginal zip file was encoded into ' + str(number_of_blocks_generated) + ' blocks of ' + str(ceil(desired_block_size_in_bytes/1000)) + ' kilobytes each. Total size of all blocks is ~' + str(ceil((number_of_blocks_generated*desired_block_size_in_bytes)/1000000)) + ' megabytes\n')
            
    print('Now copying encoded files from ram disk to local storage...')
    if not os.path.exists(block_storage_folder_path):
        try:
            os.makedirs(block_storage_folder_path)
        except Exception as e:
            print('Error: '+ str(e))
    filesystem_object = OSFS(block_storage_folder_path)     
    copy_fs(ramdisk_object,filesystem_object)
    total_duration_in_seconds = round(time.time() - overall_start_time, 1)
    print('Now removing zip files...')
    zip_file_paths = glob.glob(folder_path_of_art_folders_to_encode+'\\**\\*.zip')
    for current_zip_file_path in zip_file_paths:
        try:
            os.remove(current_zip_file_path)
        except Exception as e:
            print('Error: '+ str(e))
    print('Completed deleting zip files!')
    print('All Done! The entire process took '+ str(round(total_duration_in_seconds/60,2))+' minutes!\n')
    ramdisk_object.close()
    return total_duration_in_seconds

def decode_folder_of_block_files_into_original_zip_file_func(sha256_hash_of_desired_file, block_storage_folder_path, decoded_file_destination_folder_path):
    #First, scan the blocks folder:
    start_time = time.time()
    list_of_block_file_paths = glob.glob(os.path.join(block_storage_folder_path,'*'+sha256_hash_of_desired_file+'*.block'))
    reported_file_sha256_hash = list_of_block_file_paths[0].split('\\')[-1].split('__')[1]
    print('\nFound '+str(len(list_of_block_file_paths))+' block files in folder! The SHA256 hash of the original zip file is reported to be: '+reported_file_sha256_hash+'\n')
    decoded_file_destination_file_path = decoded_file_destination_folder_path + 'Reconstructed_File_with_SHA256_Hash_of__' + reported_file_sha256_hash + '.zip'
    c = 0.1
    delta = 0.5
    block_graph = BlockGraph(len(list_of_block_file_paths))

    for block_count, current_block_file_path in enumerate(list_of_block_file_paths):
        with open(current_block_file_path,'rb') as f:
            packed_block_data = f.read()
        
        hash_of_block = hashlib.sha256(packed_block_data).hexdigest()
        reported_hash_of_block = current_block_file_path.split('__')[-1].replace('.block','').replace('BlockHash_','')
        
        if hash_of_block == reported_hash_of_block:
            pass
            #print('Block hash matches reported hash, so block is not corrupted!')
        else:
            print('\nError, the block hash does NOT match the reported hash, so this block is corrupted! Skipping to next block...\n')
            continue
        
        input_stream = io.BufferedReader(io.BytesIO(packed_block_data),buffer_size=1000000)
        header = unpack('!III', input_stream.read(12))
        filesize = header[0]
        blocksize = header[1]
        blockseed = header[2]
        block = int.from_bytes(input_stream.read(blocksize), 'big')
        number_of_blocks_required = ceil(filesize/blocksize)
        if (block_count % 1) == 0:
            name_parts_list = current_block_file_path.split('\\')[-1].split('_')
            parsed_block_hash = name_parts_list[-1].replace('.block','')
            parsed_block_number = name_parts_list[6].replace('.block','')
            parsed_file_hash = name_parts_list[3].replace('.block','')
            print('\nNow decoding:\nBlock Number: ' + parsed_block_number + '\nFile Hash: ' + parsed_file_hash + '\nBlock Hash: '+ parsed_block_hash)
            
        prng = PRNG(params=(number_of_blocks_required, delta, c))
        _, _, src_blocks = prng.get_src_blocks(seed = blockseed)
    
        file_reconstruction_complete = block_graph.add_block(src_blocks, block)
          
        if file_reconstruction_complete:
            print('\nDone building file! Processed a total of '+str(block_count)+' blocks\n')
            break

    if os.path.isfile(decoded_file_destination_file_path):
        print('\nA file with that name exists already; deleting this first before attempting to write new file!\n')
        try:
            os.remove(decoded_file_destination_file_path)
        except:
            print('Error removing file!')
    else:
        with open(decoded_file_destination_file_path,'wb') as f: 
            for ix, block_bytes in enumerate(map(lambda p: int.to_bytes(p[1], blocksize, 'big'), sorted(block_graph.eliminated.items(), key = lambda p:p[0]))):
                if ix < number_of_blocks_required - 1 or filesize % blocksize == 0:
                    f.write(block_bytes)
                else:
                    f.write(block_bytes[:filesize%blocksize])
    
    try:
        with open(decoded_file_destination_file_path,'rb') as f:
            reconstructed_file = f.read()
            reconstructed_file_hash = hashlib.sha256(reconstructed_file).hexdigest()
            if reported_file_sha256_hash == reconstructed_file_hash:
                completed_successfully = 1
                print('\nThe SHA256 hash of the reconstructed file matches the reported file hash-- file is valid!\n')
            else:
                completed_successfully = 0
                print('\nProblem! The SHA256 hash of the reconstructed file does NOT match the expected hash! File is not valid.\n')
    except Exception as e:
        print('Error: '+ str(e))
    
    duration_in_seconds = round(time.time() - start_time, 1)
    print('\n\nFinished processing in '+str(duration_in_seconds) + ' seconds!')
    return completed_successfully





########################################################################################################################################
#Input Parameters:
use_demo_mode = 1
use_stress_test = 1
use_reconstruct_files = 1
block_redundancy_factor = 10 #How many times more blocks should we store than are required to regenerate the file?
desired_block_size_in_bytes = 1024*1000*2
percentage_of_block_files_to_randomly_delete = 0.75
use_random_corruption = 0 
percentage_of_block_files_to_randomly_corrupt = 0.05
folder_path_of_art_folders_to_encode = 'C:\\animecoin\\art_folders_to_encode\\' #Each subfolder contains the various art files pertaining to a given art asset.
block_storage_folder_path = 'C:\\animecoin\\art_block_storage\\'
chunk_db_file_path = 'C:\\animecoin\\anime_chunkdb.sqlite'
decoded_file_destination_folder_path = 'C:\\animecoin\\reconstructed_files\\'

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
    
if use_demo_mode:
    print('\nWelcome! This is a demo of the file storage system that is being used for the Animecoin project to store art files in a decentralized, robust way.')    
    print('\n\nFirst, we begin by taking a bunch of folders; each one contains one or images representing a single digital artwork to be registered.')
    print('\nWe will now iterate through these files, adding each to a zip file, so that every artwork has a corresponding zip file. Then we will encode each of these zip files into a collection of chunks as follows:\n\n')
    #sys.exit(0)

list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
if len(list_of_block_file_paths) > 0:
    print('\nDeleting all the previously generated block files...')
    try:
        check_output('rmdir /S /Q '+ block_storage_folder_path, shell=True)
        print('Done!\n')
    except:
        print('.')

run_time = turn_art_image_folder_into_encoded_block_files_func(folder_path_of_art_folders_to_encode, block_storage_folder_path, desired_block_size_in_bytes, block_redundancy_factor)
print('Total Run Time: '+str(run_time)+' seconds')
       


list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
potential_local_file_hashes_list = []
potential_local_block_hashes_list = []
list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
print('Now Verifying Block Files ('+str(len(list_of_block_file_paths))+' files found)\n')
pbar = tqdm(total=len(list_of_block_file_paths))

for current_block_file_path in list_of_block_file_paths:
    with open(current_block_file_path, 'rb') as f:
        try:
            current_block_binary_data = f.read()
        except:
            print('\nProblem reading block file!\n')
            continue
    pbar.update(1)
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
conn.close()

if use_stress_test: #Check how robust system is to lost/corrupted blocks:
    if use_demo_mode:
        print('\n\nGreat, we just finished turning the file into a bunch of "fungible" blocks! If you check the output folder now, you will see a collection of the resulting files.')
        print('Now we can see the purpose of all this. Suppose something really bad happens, and that most of the master nodes hosting these files disappear.')
        print('On top of that, suppose that many of the remaining nodes also lose some of the file chunks to corruption or disk failure. Still, we will be able to reconstruct the file.')
        #sys.exit(0)
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
    if use_demo_mode:
        print('\n\nJust deleted '+str(number_of_deleted_blocks) +' of the generated blocks, or '+ str(round(100*number_of_deleted_blocks/len(list_of_block_file_paths),2))+'% of the blocks')
        #sys.exit(0)
    if use_random_corruption:
        percentage_of_affected_file_to_be_randomly_corrupted = 0.01
        number_of_deleted_blocks = 0
        list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
        for current_file_path in list_of_block_file_paths:
            if random.random() <= percentage_of_block_files_to_randomly_corrupt:
                total_bytes_of_data_in_chunk = os.path.getsize(current_file_path)
                random_bytes_to_write = 5
                number_of_bytes_to_corrupt = ceil(total_bytes_of_data_in_chunk*percentage_of_affected_file_to_be_randomly_corrupted)
                specific_bytes_to_corrupt = random.sample(range(1, total_bytes_of_data_in_chunk), ceil(number_of_bytes_to_corrupt/random_bytes_to_write))
                print('\n\nNow intentionally corrupting block file: ' + current_file_path.split('\\')[-1] )
                with open(current_file_path,'wb') as f:
                    try:
                        for byte_index in specific_bytes_to_corrupt:
                            f.seek(byte_index)
                            f.write(os.urandom(random_bytes_to_write))
                        os.remove(current_file_path)
                        number_of_deleted_blocks = number_of_deleted_blocks + 1
                    except OSError:
                        pass
        print('\n\nNow let\'s try to reconstruct the original file despite this *random* loss of most of the block files...')

use_reset_system_for_demo = 0 
if use_reset_system_for_demo:
    if len(list_of_block_file_paths) > 0:
        print('\nDeleting all the previously generated block files...')
        try:
            check_output('rmdir /S /Q '+ block_storage_folder_path, shell=True)
            print('Done!\n')
        except:
            print('.')
    
    previous_reconstructed_zip_file_paths = glob.glob(decoded_file_destination_folder_path+'*.zip')
    for current_existing_zip_file_path in previous_reconstructed_zip_file_paths:
        try:
            os.remove(current_existing_zip_file_path)
        except:
            pass

if use_reconstruct_files:
    list_of_block_file_paths = glob.glob(block_storage_folder_path+'*.block')
    available_art_file_hashes = [p.split('\\')[-1].split('__')[1] for p in list_of_block_file_paths]
    #Delete previous existing reconstructed zip files so we don't have any problems:

        
    if not os.path.exists(decoded_file_destination_folder_path):
        try:
            os.makedirs(decoded_file_destination_folder_path)
        except Exception as e:
            print('Error: '+ str(e)) 
    
    for current_file_path in list_of_block_file_paths:
        reported_file_sha256_hash = current_file_path.split('\\')[-1].split('__')[1]
        available_art_file_hashes.append(reported_file_sha256_hash)
    available_art_file_hashes = list(set(available_art_file_hashes))
    failed_file_hash_list = []
    for current_file_hash in available_art_file_hashes:
        print('Now reconstructing file with SHA256 Hash of: ' + current_file_hash)
        completed_successfully = decode_folder_of_block_files_into_original_zip_file_func(current_file_hash,block_storage_folder_path,decoded_file_destination_folder_path)
        if not completed_successfully:
            failed_file_hash_list.append(current_file_hash)
        if completed_successfully:
            print('\nBoom, we\'ve done it!')
    
    number_of_failed_files = len(failed_file_hash_list)
    if number_of_failed_files == 0:
        print('All files reconstructed successfully!')
    else:
        print('Some files were NOT successfully reconstructed! '+str(number_of_failed_files)+' Files had errors:\n ')
        for current_hash in failed_file_hash_list:
            print(current_hash+'\n')

