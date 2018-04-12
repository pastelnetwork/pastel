import time
import sys
import os.path
import io
import fileinput
import argparse
import glob
import hashlib
import struct
import imghdr
import random
from PIL import Image, ImageFilter
from struct import pack, unpack, error
from random import randint
from math import ceil, log, floor, sqrt
from collections import defaultdict
from zipfile import ZipFile

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
                
                
def turn_art_image_folder_into_encoded_block_files_func(input_art_folder_path, desired_block_size_in_bytes, block_redundancy_factor):
    #First, scan the input folder and add each valid image file to a single zip file:
    start_time = time.time()
    blocks_output_folder = input_art_folder_path + 'output_blocks\\'
    file_base_name = input_art_folder_path.split('\\')[-2].replace(' ','_').lower()
    zip_file_path = input_art_folder_path + file_base_name + '.zip'

    art_input_file_paths =  glob.glob(input_art_folder_path+'*.jpg') + glob.glob(input_art_folder_path+'*.png') + glob.glob(input_art_folder_path+'*.bmp') + glob.glob(input_art_folder_path+'*.gif')
    with ZipFile(zip_file_path,'w') as myzip:
        for current_file_path in art_input_file_paths:
            if (imghdr.what(current_file_path) == 'gif') or (imghdr.what(current_file_path) == 'jpeg') or (imghdr.what(current_file_path) == 'png') or (imghdr.what(current_file_path) == 'bmp'):
                myzip.write(current_file_path,arcname=current_file_path.split('\\')[-1])
    with open(zip_file_path,'rb') as f:
        final_art_file = f.read()
        art_zipfile_hash = hashlib.sha256(final_art_file).hexdigest()
    final_art_file__original_size_in_bytes = os.path.getsize(zip_file_path)
    output_blocks_list = []
    DEFAULT_C = 0.1
    DEFAULT_DELTA = 0.5
    seed = randint(0, 1 << 31 - 1)
    #Process ZIP file into a stream of encoded blocks, and save those blocks as separate files in the output folder:
    print('Now encoding file ' + zip_file_path + ', (' + str(round(final_art_file__original_size_in_bytes/1000000)) + 'mb); File Hash is: '+ art_zipfile_hash)
    total_number_of_blocks_required = ceil((block_redundancy_factor*final_art_file__original_size_in_bytes) / desired_block_size_in_bytes)
    
    with open(zip_file_path,'rb') as f:
        f_bytes = f.read()
        filesize = len(f_bytes)
        #Block file byte contents into blocksize chunks, padding last one if necessary
        blocks = [int.from_bytes(f_bytes[ii:ii+desired_block_size_in_bytes].ljust(desired_block_size_in_bytes, b'0'), sys.byteorder) for ii in range(0, len(f_bytes), desired_block_size_in_bytes)]
        K = len(blocks) # init stream vars
        prng = PRNG(params=(K, DEFAULT_DELTA, DEFAULT_C))
        prng.set_seed(seed)
        
        number_of_blocks_generated = 0# block generation loop
        while number_of_blocks_generated <= total_number_of_blocks_required:
            blockseed, d, ix_samples = prng.get_src_blocks()
            block_data = 0
            for ix in ix_samples:
                block_data ^= blocks[ix]
            block = (filesize, desired_block_size_in_bytes, blockseed, int.to_bytes(block_data, desired_block_size_in_bytes, sys.byteorder)) # Generate blocks of XORed data in network byte order
            number_of_blocks_generated = number_of_blocks_generated + 1
            if (number_of_blocks_generated % 100) == 0:
                print('Generated '+str(number_of_blocks_generated)+' blocks so far')
            packed_block_data = pack('!III%ss'%desired_block_size_in_bytes, *block)
            output_blocks_list.append(packed_block_data)
            output_block_file_path = blocks_output_folder + 'File_Hash__' + art_zipfile_hash + '___Block_' + '{0:010}'.format(number_of_blocks_generated) + '.block'
            if not os.path.exists(blocks_output_folder):
                try:
                    os.makedirs(blocks_output_folder)
                except Exception as e:
                    print('Error: '+ str(e)) 
            try:
                with open(output_block_file_path,'wb') as f:
                    f.write(packed_block_data)
            except Exception as e:
                print('Error: '+ str(e)) 
        duration_in_seconds = round(time.time() - start_time, 1)
        print('Finished processing in '+str(duration_in_seconds) + ' seconds! Original zip file was encoded into ' + str(number_of_blocks_generated) + ' blocks of ' + str(ceil(desired_block_size_in_bytes/1000)) + ' kilobytes each. Total size of all blocks is ~' + str(ceil((number_of_blocks_generated*desired_block_size_in_bytes)/1000000)) + ' megabytes')
    return output_blocks_list
    

def decode_folder_of_block_files_into_original_zip_file_func(block_files_folder_path,decoded_file_destination_folder_path):
    #First, scan the blocks folder:
    start_time = time.time()
    list_of_block_file_paths = glob.glob(block_files_folder_path+'*.block')
    reported_file_sha256_hash = list_of_block_file_paths[0].split('\\')[-1].split('__')[1]
    print('Found '+str(len(list_of_block_file_paths))+' block files in folder! The SHA256 hash of the original zip file is reported to be: '+reported_file_sha256_hash)
    decoded_file_destination_file_path = decoded_file_destination_folder_path + 'Reconstructed_File_with_SHA256_Hash_of__' + reported_file_sha256_hash + '.zip'
    c = 0.1 #Implementation of a sampler for the Robust Soliton Distribution.
    delta = 0.5
    seed = randint(0, 1 << 31 - 1)
    
    for block_count, current_block_file_path in enumerate(list_of_block_file_paths):
        with open(current_block_file_path,'rb') as f:
            packed_block_data = f.read()
        input_stream = io.BufferedReader(io.BytesIO(packed_block_data),buffer_size=1000000)
        header = unpack('!III', input_stream.read(12))
        filesize = header[0]
        blocksize = header[1]
        blockseed = header[2]
        block = int.from_bytes(input_stream.read(blocksize), 'big')
        number_of_blocks_required = ceil(filesize/blocksize)
    
        if block_count==0:
            block_graph = BlockGraph(number_of_blocks_required)
    
        if (block_count % 100) == 0:
            print('Now decoding block file ' + current_block_file_path)
    
        prng = PRNG(params=(number_of_blocks_required, delta, c))
        _, _, src_blocks = prng.get_src_blocks(seed = blockseed)
    
        file_reconstruction_complete = block_graph.add_block(src_blocks, block)
          
        if file_reconstruction_complete:
            print('Done building file! Processed a total of '+str(block_count)+' blocks')
            break

    if not os.path.isfile(decoded_file_destination_file_path):
        with open(decoded_file_destination_file_path,'wb') as f: 
            for ix, block_bytes in enumerate(map(lambda p: int.to_bytes(p[1], blocksize, 'big'), sorted(block_graph.eliminated.items(), key = lambda p:p[0]))):
                if ix < number_of_blocks_required - 1 or filesize % blocksize == 0:
                    f.write(block_bytes)
                else:
                    f.write(block_bytes[:filesize%blocksize])
        
    with open(decoded_file_destination_file_path,'rb') as f:
        reconstructed_file = f.read()
        reconstructed_file_hash = hashlib.sha256(reconstructed_file).hexdigest()
        if reported_file_sha256_hash == reconstructed_file_hash:
            completed_successfully = 1
            print('The SHA256 hash of the reconstructed file matches the reported file hash-- file is valid!')
        else:
            completed_successfully = 0
            print('Problem! The SHA256 hash of the reconstructed file does NOT match the expected hash! File is not valid.')
    duration_in_seconds = round(time.time() - start_time, 1)
    print('Finished processing in '+str(duration_in_seconds) + ' seconds!')
    return completed_successfully

print('Welcome! This is a demo of the file storage system that is being used for the Animecoin project to store art files in a decentralized, robust way.')    
#Input Parameters:
block_redundancy_factor = 8 #How many times more blocks should we store than are required to regenerate the file?
desired_block_size_in_bytes = 100000
sample_art_folder = 'C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\Sample_Art_Folder\\' #contains the various art files
print('First, we begin by taking a folder of files that contains image files. In this case, we will use a folder than contains two image files.')
print('We will now iterate through these files, adding each to a zip file.')
output_blocks_list = turn_art_image_folder_into_encoded_block_files_func(sample_art_folder, desired_block_size_in_bytes, block_redundancy_factor)
print('Great, we just finished turning the file into a bunch of "fungible" blocks! If you check the output folder now, you will see a collection of the resulting files.')
sys.exit(0)


print('Now we can see the purpose of all this. Suppose something really bad happens, and that most of the master nodes hosting these files disappear.')
print('On top of that, suppose that many of the remaining nodes also lose some of the file chunks to corruption or disk failure. Still, we will be able to reconstruct the file.')
list_of_block_file_paths = glob.glob(sample_art_folder + 'output_blocks\\*.block')
percentage_of_block_files_to_randomly_delete = 0.80
number_of_deleted_blocks = 0
for current_file_path in list_of_block_file_paths:
    if random.random() <= percentage_of_block_files_to_randomly_delete:
        current_file_path
        try:
            os.remove(current_file_path)
            number_of_deleted_blocks = number_of_deleted_blocks + 1
        except OSError:
            pass
print('Just deleted '+str(number_of_deleted_blocks) +' of the generated blocks, or '+ str(round(100*number_of_deleted_blocks/len(list_of_block_file_paths),2))+'% of the blocks')


print('Now let\'s try to reconstruct the original file despite this *random* loss of most of the block files...')
sample_block_files_folder = 'C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\Sample_Art_Folder\\output_blocks\\' #contains the various art files
block_files_folder_path = sample_block_files_folder
decoded_file_destination_folder_path = sample_art_folder

completed_successfully = decode_folder_of_block_files_into_original_zip_file_func(block_files_folder_path,decoded_file_destination_folder_path)
if completed_successfully:
    print('Boom, we\'ve done it!')





