import paq, bz2, gzip, sys, pylzma, io, glob
from anime_load_various_ticket_template_files_and_executed_tickets import generate_animecoin_html_strings_for_templates_and_example_executed_tickets_func, convert_list_of_strings_into_list_of_utf_encoded_byte_objects_func
import zstd #if this fails try import zstandard as zstd
#run pip install paq
#    pip install pylzma
#    pip install zstandard


def generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_to_folder_containing_files_for_dictionary, file_matching_string):
    print('Now generating compression dictionary for ZSTD:')
    list_of_matching_input_file_paths = glob.glob(path_to_folder_containing_files_for_dictionary + file_matching_string)
    for cnt, current_input_file_path in enumerate(list_of_matching_input_file_paths):
        print('Now loading input file '+ str(cnt + 1) + ' out of ' + str(len(list_of_matching_input_file_paths)))
        with open(current_input_file_path,'rb') as f:
            if cnt == 0:
                combined_dictionary_input_data = f.read()
            else:
                new_file_data = f.read()
                combined_dictionary_input_data = combined_dictionary_input_data + new_file_data
    animecoin_zstd_compression_dictionary = zstd.ZstdCompressionDict(combined_dictionary_input_data)
    print('Done generating compression dictionary!')
    return animecoin_zstd_compression_dictionary

def compress_data_with_animecoin_zstd_func(input_data, animecoin_zstd_compression_dictionary):
    global zstd_compression_level
    zstd_compression_level = 22 #Highest (best) compression level is 22
    zstandard_animecoin_compressor = zstd.ZstdCompressor(dict_data=animecoin_zstd_compression_dictionary, level=zstd_compression_level, write_content_size=True)
    if isinstance(input_data,str):
        input_data = input_data.encode('utf-8')
    animecoin_zstd_compressed_data = zstandard_animecoin_compressor.compress(input_data)
    return animecoin_zstd_compressed_data

def decompress_data_with_animecoin_zstd_func(animecoin_zstd_compressed_data, animecoin_zstd_compression_dictionary):
    zstandard_animecoin_decompressor = zstd.ZstdDecompressor(dict_data=animecoin_zstd_compression_dictionary)
    uncompressed_data = zstandard_animecoin_decompressor.decompress(animecoin_zstd_compressed_data)
    return uncompressed_data

def compress_data_with_paq_func(input_data):
    if isinstance(input_data,str):
        input_data = input_data.encode('utf-8')
    paq_compressed_data = paq.compress(input_data)
    return paq_compressed_data

def decompress_data_with_paq_func(paq_compressed_data):
    uncompressed_data = paq.decompress(paq_compressed_data)
    return uncompressed_data

use_large_binary_file_instead_of_html_input_file = 0

if use_large_binary_file_instead_of_html_input_file:
    path_to_folder_containing_files_for_dictionary = 'C:\\animecoin\\art_block_storage\\'
    file_matching_string = '*3c2ffc31e848e9f9bbb63d9a2890d5a7c89c2110e5d502ea48d05f1c993b17e8*'
    animecoin_zstd_compression_dictionary = generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_to_folder_containing_files_for_dictionary, file_matching_string)

    block_file_path = 'C:\\animecoin\\art_block_storage\\FileHash__fbb3ad3d0e77bd707650a86f828fd8e26972a1ed98a7964bc378b2ea39cf8e7e__Block__000000012__BlockHash_314772985be4b62ea281a70ebe59ae414a34ab5ca18200ca70364ec3747688ad.block'
    with open(block_file_path,'rb') as f:
        sample_input_data = f.read()
else:
    path_to_folder_containing_files_for_dictionary = 'C:\\Users\\jeffr\\Cointel Dropbox\\Animecoin_Code\\animecoin_html_ticket_template_files\\'
    file_matching_string = '*.html'
    animecoin_zstd_compression_dictionary = generate_zstd_dictionary_from_folder_path_and_file_matching_string_func(path_to_folder_containing_files_for_dictionary, file_matching_string)
    _, example_executed_artwork_metadata_html_ticket_string = generate_animecoin_html_strings_for_templates_and_example_executed_tickets_func()
    sample_input_data = example_executed_artwork_metadata_html_ticket_string.encode('utf-8')
    #animecoin_zstd_compression_dictionary = zstd.ZstdCompressionDict(concatenated_list_of_animecoin_html_strings_for_templates_and_example_executed_tickets_byte_encoded)

print('Running each compressor and decompressor on input data...')
uncompressed_size_in_bytes = sys.getsizeof(sample_input_data)

animecoin_zstd_compressed_data = compress_data_with_animecoin_zstd_func(sample_input_data, animecoin_zstd_compression_dictionary)
animecoin_zstd_uncompressed_data = decompress_data_with_animecoin_zstd_func(animecoin_zstd_compressed_data, animecoin_zstd_compression_dictionary)
if animecoin_zstd_uncompressed_data == sample_input_data:
    print('Animecoin ZSTD Archive decompressed successfully!')

paq_compressed_data = compress_data_with_paq_func(sample_input_data)
paq_uncompressed_data = decompress_data_with_paq_func(paq_compressed_data)
if paq_uncompressed_data == sample_input_data:
    print('PAQ Archive decompressed successfully!')
    
lzma_compressed_data = pylzma.compress(sample_input_data)    
lzma_uncompressed_data =  pylzma.decompress(lzma_compressed_data)
if lzma_uncompressed_data == sample_input_data:
    print('LZMA Archive decompressed successfully!')
    
bz2_compressed_data = bz2.compress(sample_input_data)    
bz2_uncompressed_data = bz2.decompress(bz2_compressed_data)
if bz2_uncompressed_data == sample_input_data:
    print('BZ2 Archive decompressed successfully!')
    
gzip_compressed_data = gzip.compress(sample_input_data)
gzip_uncompressed_data = gzip.decompress(gzip_compressed_data)
if gzip_uncompressed_data == sample_input_data:
    print('GZIP Archive decompressed successfully!')
    
print('Done Running each compressor and decompressor on input data!')

animecoin_zstd_compressed_size_in_bytes = sys.getsizeof(animecoin_zstd_compressed_data)
paq_compressed_size_in_bytes = sys.getsizeof(paq_compressed_data)
lzma_compressed_size_in_bytes = sys.getsizeof(lzma_compressed_data)
bz2_compressed_size_in_bytes = sys.getsizeof(bz2_compressed_data)
gzip_compressed_size_in_bytes = sys.getsizeof(gzip_compressed_data)

animecoin_zstd_size_as_pct_of_bz2_size = animecoin_zstd_compressed_size_in_bytes/bz2_compressed_size_in_bytes
animecoin_zstd_size_as_pct_of_gzip_size = animecoin_zstd_compressed_size_in_bytes/gzip_compressed_size_in_bytes
animecoin_zstd_size_as_pct_of_lzma_size = animecoin_zstd_compressed_size_in_bytes/lzma_compressed_size_in_bytes
animecoin_zstd_size_as_pct_of_paq_size = animecoin_zstd_compressed_size_in_bytes/paq_compressed_size_in_bytes

print('Uncompressed size: '+str(uncompressed_size_in_bytes))
print('PAQ Compressed size: '+str(paq_compressed_size_in_bytes))
print('Animecoin ZSTD Compressed size: '+str(animecoin_zstd_compressed_size_in_bytes))
print('LZMA Compressed size: '+str(lzma_compressed_size_in_bytes))
print('BZ2 Compressed size: '+str(bz2_compressed_size_in_bytes))
print('GZIP Compressed size: '+str(gzip_compressed_size_in_bytes))
print('Animecoin ZSTD size as % of BZ2 Size: '+str(animecoin_zstd_size_as_pct_of_bz2_size))
print('Animecoin ZSTD size as % of GZIP Size: '+str(animecoin_zstd_size_as_pct_of_gzip_size))
print('Animecoin ZSTD size as % of LZMA Size: '+str(animecoin_zstd_size_as_pct_of_lzma_size))
print('Animecoin ZSTD size as % of PAQ Size: '+str(animecoin_zstd_size_as_pct_of_paq_size))

"""
    print('Now running timing benchmarks for each compression algorithm:')
    
    print('PAQ: Now compressing '+str(uncompressed_size_in_bytes)+' bytes of input data.')
    %timeit -r 3 paq_compressed_data = compress_data_with_paq_func(sample_input_data)
    
    print('Animecoin ZSTD: Now compressing '+str(uncompressed_size_in_bytes)+' bytes of input data.')
    %timeit -r 3 animecoin_zstd_compressed_data = compress_data_with_animecoin_zstd_func(sample_input_data, animecoin_zstd_compression_dictionary)
    
    print('LZMA: Now compressing '+str(uncompressed_size_in_bytes)+' bytes of input data.')
    %timeit -r 3 lzma_compressed_data = pylzma.compress(sample_input_data)
    
    print('BZ2: Now compressing '+str(uncompressed_size_in_bytes)+' bytes of input data.')
    %timeit -r 3 bz2_compressed_data = bz2.compress(sample_input_data)
    
    print('GZIP: Now compressing '+str(uncompressed_size_in_bytes)+' bytes of input data.')
    %timeit -r 3 gzip_compressed_data = gzip.compress(sample_input_data)
    
    
    print('Now running timings for each compression algorithm:')
    
    print('PAQ: Now decompressing data...')
    %timeit -r 3 paq_uncompressed_data = decompress_data_with_paq_func(paq_compressed_data)
    
    print('Animecoin ZSTD: Now decompressing data...')
    %timeit -r 3 animecoin_zstd_uncompressed_data = decompress_data_with_animecoin_zstd_func(animecoin_zstd_compressed_data, animecoin_zstd_compression_dictionary)
    
    print('LZMA: Now decompressing data...')
    %timeit -r 3 lzma_uncompressed_data =  pylzma.decompress(lzma_compressed_data)
    
    print('BZ2: Now decompressing data...')
    %timeit -r 3 bz2_uncompressed_data = bz2.decompress(bz2_compressed_data)
    
    print('GZIP: Now decompressing data...')
    %timeit -r 3 gzip_uncompressed_data = gzip.decompress(gzip_compressed_data)

"""