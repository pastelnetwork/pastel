import os, time, hashlib
from time import sleep
from tqdm import tqdm
from decimal import getcontext, Decimal
from math import floor, log

def convert_integer_to_bytes_func(input_integer):
    if input_integer == 0:
        return b''
    else: #Uses recursion:
        return convert_integer_to_bytes_func(input_integer//256) + bytes([input_integer%256])

def check_that_all_block_transactions_are_valid_func(block_transaction_data):
    if 1==1:  # This is just a placeholder function
        all_transactions_are_valid = 1
    else:
        all_transactions_are_valid = 0
    return all_transactions_are_valid

def generate_block_candidate_func(block_transaction_data, nonce_number):
    nonce = convert_integer_to_bytes_func(nonce_number)
    block_transaction_data_with_nonce = block_transaction_data + nonce
    if USE_VERBOSE:
        print('Generated new block candidate for nonce: '+ str(nonce_number))
    return block_transaction_data_with_nonce

def get_hash_list_func(block_transaction_data_with_nonce, use_verbose): #Adjusted so each has the same length
    hash_01 = hashlib.sha256(block_transaction_data_with_nonce).hexdigest() + hashlib.sha256(block_transaction_data_with_nonce).hexdigest()
    hash_02 = hashlib.sha3_256(block_transaction_data_with_nonce).hexdigest() + hashlib.sha3_256(block_transaction_data_with_nonce).hexdigest()
    hash_03 = hashlib.sha3_512(block_transaction_data_with_nonce).hexdigest()
    hash_04 = hashlib.blake2s(block_transaction_data_with_nonce).hexdigest() + hashlib.blake2s(block_transaction_data_with_nonce).hexdigest()
    hash_05 = hashlib.blake2b(block_transaction_data_with_nonce).hexdigest()
    hash_list = [hash_01, hash_02, hash_03, hash_04, hash_05]
    if use_verbose:
        list_of_hash_types = ['SHA-256 + (Repeated again in reverse)', 'SHA3-256 + (Repeated again in reverse)', 'SHA3-512', 'BLAKE2S + (Repeated again in reverse)', 'BLAKE2B']
        print('\n\nList of computed hashes:\n')
        for hash_count, current_hash in enumerate(hash_list):
            print(list_of_hash_types[hash_count]+ ': ' + current_hash)
    return hash_list

def check_score_of_hash_list_func(input_hash_list, difficulty_level, use_verbose):
    combined_hash = 0
    hash_list_score = 0
    list_of_hash_integers = list()
    for current_hash in input_hash_list:
        list_of_hash_integers.append(Decimal(int(current_hash, 16)))
    list_of_hashes_in_size_order = sorted(list_of_hash_integers)
    smallest_hash = list_of_hashes_in_size_order[0]
    second_smallest_hash = list_of_hashes_in_size_order[1]
    middle_hash = list_of_hashes_in_size_order[2]
    second_largest_hash = list_of_hashes_in_size_order[-2]
    largest_hash = list_of_hashes_in_size_order[-1]
    combined_hash_step_1 = (pow(largest_hash, 5) / pow(second_largest_hash, 3))/(Decimal(10)*largest_hash.log10()*smallest_hash.log10())
    combined_hash_step_2 = (pow(middle_hash, 5) / pow(second_smallest_hash, 3))/(Decimal(10)*second_largest_hash.log10()*second_smallest_hash.log10())
    combined_hash_step_3 = (pow(second_largest_hash, 5) / pow(smallest_hash, 3))/(Decimal(10)*largest_hash.log10()*middle_hash.log10()*smallest_hash.log10())
    combined_hash = int(floor( (combined_hash_step_1*combined_hash_step_2*combined_hash_step_3)/(Decimal(10)*max([combined_hash_step_1, combined_hash_step_2, combined_hash_step_3])) ))
    combined_hash_hex = str(hex(combined_hash)).replace('0x','')
    if combined_hash_hex[0:difficulty_level] == difficulty_level*'1':
        hash_list_score = 1
    if USE_VERBOSE:
        print('Step 1: ' + str(round(combined_hash_step_1, 30)))
        print('Step 2: ' + str(round(combined_hash_step_2, 30)))
        print('Step 3: ' + str(round(combined_hash_step_3, 30)))
        print('Combined Hash as Integer: ' + str(round(combined_hash, 30)))
        print('Combined Hash in Hex: ' + combined_hash_hex)
    return hash_list_score, combined_hash, combined_hash_hex

def mine_for_new_block_func(block_transaction_data, difficulty_level, block_count):
    start_time = time.time()
    pbar = tqdm()
    block_is_valid = 0
    nonce_number = 0
    all_transactions_are_valid = check_that_all_block_transactions_are_valid_func(block_transaction_data)
    if all_transactions_are_valid:
        while block_is_valid == 0:
            pbar.update(1)
            nonce_number = nonce_number + 1
            block_transaction_data_with_nonce = generate_block_candidate_func(block_transaction_data, nonce_number)
            current_hash_list = get_hash_list_func(block_transaction_data_with_nonce, use_verbose=USE_VERBOSE)
            current_hash_list_score, current_combined_hash, combined_hash_hex= check_score_of_hash_list_func(current_hash_list, difficulty_level, use_verbose=USE_VERBOSE)
            if nonce_number == 1:
                current_best_combined_hash = current_combined_hash
            if current_combined_hash < current_best_combined_hash:
                current_best_combined_hash = current_combined_hash
                current_best_combined_hash_hex = combined_hash_hex
                pbar.set_description('Best combined hash so far: ' + current_best_combined_hash_hex)
            if current_hash_list_score >= MIN_HASH_LIST_SCORE:
                end_time = time.time()
                block_duration_in_minutes = (end_time - start_time)/60
                try:
                    current_best_combined_hash_hex
                except NameError:
                    current_best_combined_hash_hex = hex(0)
                print('\n\n\n\n***Just mined a new block!***\n\n Block Duration: '+str(round(block_duration_in_minutes,3))+' minutes.\n\n')
                print('Block Number: ' + str(block_count))
                print('Hash of mined block: '+ current_best_combined_hash_hex)
                print('Hash as integer: '+str(int(current_best_combined_hash_hex,16))+'\n')
                print('Individual Hash Values SHA-256 (Repeated 2x), SHA3-256 (Repeated 2x), SHA3-512, BLAKE2S (Repeated 2x), and BLAKE2B, respectively:\n')
                for current_hash in current_hash_list:
                    print(current_hash)
                print('Applying the following transformations to individual hashes to generate the combined hash:\n')
                print(FORMULAS_STRING)
                print('\nCurrent Difficulty Level: ' + str(difficulty_level))
                print('\nCurrent Number of Digits of Precision: '+str(getcontext().prec))
                print('\nCurrent Difficulty Level: ' + str(difficulty_level))
                sleep(3)
                return current_hash_list, current_hash_list_score, current_combined_hash, combined_hash_hex,  block_duration_in_minutes

try:
  block_count
except NameError:
  block_count = 0

use_demo = 1
number_of_demo_blocks_to_mine = 500
USE_VERBOSE = 0
MIN_HASH_LIST_SCORE = 1
baseline_digits_of_precision = 100
getcontext().prec = baseline_digits_of_precision
current_required_number_of_digits_of_precision = baseline_digits_of_precision
number_of_digits_of_precision = baseline_digits_of_precision
final_digits_of_precision = 1000 #How many digits we want to keep track of in the decimal expansion of numbers
increase_hash_difficulty_every_k_blocks = 10
reset_numerical_precision_every_r_blocks = increase_hash_difficulty_every_k_blocks
max_increase_per_iteration = 3*log(final_digits_of_precision/baseline_digits_of_precision)/log(10) # X% increase per round, so final precision will be ~ final_digits_of_precision digits
max_difficulty_increase_per_block = 1
target_minutes_per_block_mined = 2.5
FORMULAS_STRING = """\nlist_of_hash_integers = list()
                        for current_hash in input_hash_list:
                            list_of_hash_integers.append(Decimal(int(current_hash, 16)))
                        list_of_hashes_in_size_order = sorted(list_of_hash_integers)
                        smallest_hash = list_of_hashes_in_size_order[0]
                        second_smallest_hash = list_of_hashes_in_size_order[1]
                        middle_hash = list_of_hashes_in_size_order[2]
                        second_largest_hash = list_of_hashes_in_size_order[-2]
                        largest_hash = list_of_hashes_in_size_order[-1]
                        combined_hash_step_1 = (pow(largest_hash, 5) / pow(second_largest_hash, 3))/(Decimal(10)*largest_hash.log10()*smallest_hash.log10())
                        combined_hash_step_2 = (pow(middle_hash, 5) / pow(second_smallest_hash, 3))/(Decimal(10)*second_largest_hash.log10()*second_smallest_hash.log10())
                        combined_hash_step_3 = (pow(second_largest_hash, 5) / pow(smallest_hash, 3))/(Decimal(10)*largest_hash.log10()*middle_hash.log10()*smallest_hash.log10())
                        combined_hash = int(floor( (combined_hash_step_1*combined_hash_step_2*combined_hash_step_3)/(Decimal(1/100)*max([combined_hash_step_1, combined_hash_step_2, combined_hash_step_3])) ))
                        combined_hash_hex = str(hex(combined_hash)).replace('0x','')"""

if use_demo:
    if block_count==0:
        print('WELCOME TO ANIMECOIN POW')
        print('Generating fake transaction data for block...')
        block_transaction_data = os.urandom(pow(10,5))
        difficulty_level = 1
        print('Initial Block Difficulty: '+ str(difficulty_level))
        print('Initial Number of decimal places of accuracy: '+str(baseline_digits_of_precision))
        print('Target block duration in minutes: '+str(target_minutes_per_block_mined))
        sleep(2)
    for ii in range(number_of_demo_blocks_to_mine):
        current_hash_list, current_hash_list_score, current_combined_hash, current_combined_hash_hex,  current_block_duration_in_minutes = mine_for_new_block_func(block_transaction_data, difficulty_level, block_count)
        block_count = block_count + 1
        ratio_of_actual_block_time_to_target_block_time = target_minutes_per_block_mined/current_block_duration_in_minutes
        print('Ratio of actual block time to target block time: '+ str(ratio_of_actual_block_time_to_target_block_time))
        getcontext().prec = current_required_number_of_digits_of_precision

        if ratio_of_actual_block_time_to_target_block_time > 1: #Block size was too short; increase difficulty and numerical precision:
            current_required_number_of_digits_of_precision = floor(current_required_number_of_digits_of_precision*min([(1+max_increase_per_iteration), ratio_of_actual_block_time_to_target_block_time]))
            if reset_numerical_precision_every_r_blocks%block_count==0:
                current_required_number_of_digits_of_precision = baseline_digits_of_precision
            if increase_hash_difficulty_every_k_blocks%block_count==0:
                computed_difficulty_level_increase = floor(min([ratio_of_actual_block_time_to_target_block_time, max_difficulty_increase_per_block]))
            else:
                computed_difficulty_level_increase = 0
        else: #Block size was too long; reduce difficulty and numerical precision:
            current_required_number_of_digits_of_precision = floor(current_required_number_of_digits_of_precision*max([(1 - max_increase_per_iteration), ratio_of_actual_block_time_to_target_block_time]))
            if reset_numerical_precision_every_r_blocks%block_count==0:
                current_required_number_of_digits_of_precision = baseline_digits_of_precision
            if increase_hash_difficulty_every_k_blocks%block_count==0:
                computed_difficulty_level_increase = floor(max( [(1/ratio_of_actual_block_time_to_target_block_time), -max_difficulty_increase_per_block]) )
            else:
                computed_difficulty_level_increase = 0
        getcontext().prec = current_required_number_of_digits_of_precision
        print('Increasing required numerical precision by '+ str(round(100*computed_difficulty_level_increase))+'% for next block.\n\n')
        difficulty_level = difficulty_level + computed_difficulty_level_increase

use_debug_mode = 1
if use_debug_mode:
    block_is_valid = 0
    nonce_number = 0
    block_transaction_data = os.urandom(pow(10,5))
    block_transaction_data_with_nonce = generate_block_candidate_func(block_transaction_data, nonce_number)
    current_hash_list = get_hash_list_func(block_transaction_data_with_nonce, use_verbose=USE_VERBOSE)
    input_hash_list = current_hash_list
