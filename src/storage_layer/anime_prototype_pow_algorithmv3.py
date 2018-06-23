import os, time, hashlib, base64, random, math, socket
from time import sleep
from math import floor
from mpmath import mp, mpf
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor

#pip install mpmath, gmpy2, tqdm
try: #For Pythonista:
    import console, speech
    console.set_font('Menlo-Regular', 7)
    speech.say('Welcome to Animecoin My ning!', 'fr_FR')
    while speech.is_speaking():
       time.sleep(0.1)
    def printc_ios_helper_func(input_string, r, g, b):
        console.set_color(r, g, b)
        print(input_string)
    def printc_ios_func(input_string, color_code):
        if color_code == 'g':
            printc_ios_helper_func(input_string, 46,204,64)
        elif color_code == 'r':
            printc_ios_helper_func(input_string, 255,65,54)
        elif color_code == 'c':
            printc_ios_helper_func(input_string, 127,219,255)
        elif color_code == 'b':
            printc_ios_helper_func(input_string, 0,116,217)
        elif color_code == 'y':
            printc_ios_helper_func(input_string, 255,220,0)
        elif color_code == 'm':
            printc_ios_helper_func(input_string, 240,18,190)
        elif color_code == 'k':
            printc_ios_helper_func(input_string, 25,25,25)
    def printrc_ios_func(input_string):
            color_code_list = ['r','g','b','c','y','m','k']
            printc(input_string, color_code_list[random.randint(0, len(color_code_list) - 1)])
    printc = printc_ios_func
    printrc = printrc_ios_func
except: #Color:
    from colorama import Fore
    def printc(input_string, color_code):
        if color_code == 'g':
            print(Fore.LIGHTGREEN_EX + input_string)
        elif color_code == 'r':
            print(Fore.LIGHTRED_EX + input_string)
        elif color_code == 'c':
            print(Fore.LIGHTCYAN_EX + input_string)
        elif color_code == 'b':
            print(Fore.LIGHTBLUE_EX + input_string)
        elif color_code == 'y':
            print(Fore.LIGHTYELLOW_EX + input_string)
        elif color_code == 'm':
            print(Fore.LIGHTMAGENTA_EX + input_string)
        elif color_code == 'k':
            print(Fore.LIGHTBLACK_EX + input_string)
        print(Fore.RESET)
    def printrc(input_string):
        color_code_list = ['r','g','b','c','y','m','k']
        printc(input_string, color_code_list[random.randint(0, len(color_code_list) - 1)])
SLEEP_PERIOD = 2 #seconds
SLOWDOWN = 0
use_verbose = 0
try:
    from tqdm import tqdm
    tqdm_import_successful = 0
except NameError:
    tqdm_import_successful = 0
LIST_OF_FUNCTION_STRINGS = [' acosh(x)',  ' acosh(x**2)',  ' acos(x)',  ' acot(x)',  ' acot(x**2)',  ' acoth(x)',  ' acsc(x)',  ' acsc(x**2)',  ' acsch(x)',  ' agm(x, x**2)',  ' airyai(x)',  ' airybi(1/x)',  ' asec(x)',  ' asech(x)',  ' asin(x)',  ' asinh(x)',  ' atan(x)',  ' atan2(x, x)',  ' atanh(x)',  ' barnesg(1/x)',  ' bernoulli(x)',  ' besseli(x, 1/x)',  ' besselk(x, 1/x)',  ' bessely(x, 1/x)',  ' beta(x, x)',  ' cbrt(x**0.1)',  ' chebyt(x, x**2)',  ' chebyu(x, x**2)',  ' chi(x)',  ' clcos(x, x)',  ' clsin(x, x)',  ' cos(x)',  ' cos(x**2)',  ' cosh(x)',  ' cot(x)',  ' csch(x)',  ' degrees(x)',  ' ellipk(x)',  ' elliprc(x, x)',  ' elliprg(x, x, x)',  ' erfc(x)',  ' exp(x)',  ' exp(x**2)',  ' expm1(x)',  ' gamma(x)',  ' hankel1(x, 2)',  ' hankel2(x, 2)',  ' harmonic(x)',  ' harmonic(x**2)',  ' hermite(x, x)',  ' hypot(x,x**2)',  ' jacobi(x, x, x, x)',  ' laguerre(x, 1/x, 1/x)',  ' lambertw(x)',  ' lambertw(x**2)',  ' legendre(x, x)',  ' ln(x)',  ' ln(x**2)',  ' log(x)',  ' log(x**2)',  ' log10(x)',  ' loggamma(x**2)',  ' powm1(x,x)',  ' power(x, 3)',  ' power(x, 4)',  ' power(x, 5)',  ' power(x, 6)',  ' power(x, 7)',  ' power(x, 8)',  ' power(x, 9)',  ' power(x, 10)',  ' power(x, 11)',  ' power(x, 12)',  ' psi(x, x**2)',  ' radians(x)',  ' riemannr(x)',  ' root(x,3)',  ' root(x,4)',  ' root(x,5)',  ' root(x,6)',  ' root(x,7)',  ' root(x,8)',  ' root(x,9)',  ' root(x,10)',  ' root(x,11)',  ' root(x,12)',  ' scorerhi(x)',  ' sec(x)',  ' sech(x)',  ' shi(x**2)',  ' shi(x)',  ' si(x**2)',  ' si(x)',  ' siegeltheta(x)',  ' sin(x)',  ' sin(x**2)',  ' sinc(x)',  ' sinh(x)',  ' sqrt(x)',  ' tanh(x)']

def get_my_local_ip_func():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    my_node_ip_address = s.getsockname()[0]
    s.close()
    return my_node_ip_address

def reverse_string_func(input_string):
    return input_string[::-1]

def convert_integer_to_bytes_func(input_integer):
    if input_integer == 0:
        return b''
    else: #Use recursion:
        return convert_integer_to_bytes_func(input_integer//256) + bytes([input_integer%256])

def get_sort_indices_func(list_of_values):
    sort_indices = sorted(range(len(list_of_values)),key=lambda x:list_of_values[x])
    return sort_indices

def sort_list_based_on_another_list_func(list_to_sort, another_list):
    sort_indices = get_sort_indices_func(another_list)
    sorted_list = [x for _,x in sorted(zip(sort_indices, list_to_sort))]
    return sorted_list

def make_text_indented_func(s, numSpaces):
    return "\n".join((numSpaces * " ") + i for i in s.splitlines())

def print_line_func():
    seconds, _ = math.modf(time.time())
    if seconds%2==0:
        printc('\n_______________________________________________________________________________________________________________________________________\n','c')
    else:
        printc('\n_______________________________________________________________________________________________________________________________________\n','y')

def print_welcome_message_func():
    print_line_func()
    welcome_message = '\n                         ╔╦╦╦═╦╗╔═╦═╦══╦═╗\n                         ║║║║╩╣╚╣═╣║║║║║╩╣\n                         ╚══╩═╩═╩═╩═╩╩╩╩═╝\n\n                               *TO*\n\n                  /\\  ._  o ._ _   _   _  _  o ._\n                 /--\\ | | | | | | (/_ (_ (_) | | |\n            _________________________________________\n\n    _/      _/      _/_/_/      _/      _/      _/_/_/_/      _/_/_/\n   _/_/  _/_/        _/        _/_/    _/      _/            _/    _/\n  _/  _/  _/        _/        _/  _/  _/      _/_/_/        _/_/_/\n _/      _/        _/        _/    _/_/      _/            _/    _/\n_/      _/      _/_/_/      _/      _/      _/_/_/_/      _/    _/\n\n                       "Hashing Since 2018"'
    printrc(make_text_indented_func(welcome_message, 12))

def print_mining_congratulations_message_func():
    print_line_func()
    random_number = random.random()
    if random_number > 0.75:
        printc(make_text_indented_func('\n\n███████╗██╗  ██╗ ██████╗███████╗██╗     ██╗     ███████╗███╗   ██╗████████╗\n██╔════╝╚██╗██╔╝██╔════╝██╔════╝██║     ██║     ██╔════╝████╗  ██║╚══██╔══╝\n█████╗   ╚███╔╝ ██║     █████╗  ██║     ██║     █████╗  ██╔██╗ ██║   ██║   \n██╔══╝   ██╔██╗ ██║     ██╔══╝  ██║     ██║     ██╔══╝  ██║╚██╗██║   ██║   \n███████╗██╔╝ ██╗╚██████╗███████╗███████╗███████╗███████╗██║ ╚████║   ██║   \n╚══════╝╚═╝  ╚═╝ ╚═════╝╚══════╝╚══════╝╚══════╝╚══════╝╚═╝  ╚═══╝   ╚═╝',random.randint(5,30)),'c')
    elif random_number > 0.55:
        printc(make_text_indented_func('\n\n╔╦╗╔═╗╔═╗╔╗╔╦╔═╗╦╔═╗╔═╗╔╗╔╔╦╗\n║║║╠═╣║ ╦║║║║╠╣ ║║  ║╣ ║║║ ║ \n╩ ╩╩ ╩╚═╝╝╚╝╩╚  ╩╚═╝╚═╝╝╚╝ ╩',random.randint(5,30)),'r')
    elif random_number > 0.25:
        printc(make_text_indented_func('\n\n   ▄████████ ███    █▄     ▄███████▄    ▄████████    ▄████████ ▀█████████▄  \n  ███    ███ ███    ███   ███    ███   ███    ███   ███    ███   ███    ███ \n  ███    █▀  ███    ███   ███    ███   ███    █▀    ███    ███   ███    ███ \n  ███        ███    ███   ███    ███  ▄███▄▄▄      ▄███▄▄▄▄██▀  ▄███▄▄▄██▀  \n▀███████████ ███    ███ ▀█████████▀  ▀▀███▀▀▀     ▀▀███▀▀▀▀▀   ▀▀███▀▀▀██▄  \n         ███ ███    ███   ███          ███    █▄  ▀███████████   ███    ██▄ \n   ▄█    ███ ███    ███   ███          ███    ███   ███    ███   ███    ███ \n ▄████████▀  ████████▀   ▄████▀        ██████████   ███    ███ ▄█████████▀  \n                                                    ███    ███',random.randint(5,30)),'m')
    elif random_number > 0.00:
        printc(make_text_indented_func("\n\n                                      ,----,                      ,----,                                                                                 \n    ,----..                         ,/   .`|                    ,/   .`|                          ,--.                                 ,--.              \n   /   /   \\                      ,`   .'  :   .--.--.        ,`   .'  :    ,---,               ,--.'|     ,---,        ,---,        ,--.'|   ,----..    \n  /   .     :           ,--,    ;    ;     /  /  /    '.    ;    ;     /   '  .' \\          ,--,:  : |   .'  .' `\\   ,`--.' |    ,--,:  : |  /   /   \\   \n .   /   ;.  \\        ,'_ /|  .'___,/    ,'  |  :  /`. /  .'___,/    ,'   /  ;    '.     ,`--.'`|  ' : ,---.'     \\  |   :  : ,`--.'`|  ' : |   :     :  \n.   ;   /  ` ;   .--. |  | :  |    :     |   ;  |  |--`   |    :     |   :  :       \\    |   :  :  | | |   |  .`\\  | :   |  ' |   :  :  | | .   |  ;. /  \n;   |  ; \\ ; | ,'_ /| :  . |  ;    |.';  ;   |  :  ;_     ;    |.';  ;   :  |   /\\   \\   :   |   \\ | : :   : |  '  | |   :  | :   |   \\ | : .   ; /--`   \n|   :  | ; | ' |  ' | |  . .  `----'  |  |    \\  \\    `.  `----'  |  |   |  :  ' ;.   :  |   : '  '; | |   ' '  ;  : '   '  ; |   : '  '; | ;   | ;  __  \n.   |  ' ' ' : |  | ' |  | |      '   :  ;     `----.   \\     '   :  ;   |  |  ;/  \\   \\ '   ' ;.    ; '   | ;  .  | |   |  | '   ' ;.    ; |   : |.' .' \n'   ;  \\; /  | :  | | :  ' ;      |   |  '     __ \\  \\  |     |   |  '   '  :  | \\  \\ ,' |   | | \\   | |   | :  |  ' '   :  ; |   | | \\   | .   | '_.' : \n \\   \\  ',  /  |  ; ' |  | '      '   :  |    /  /`--'  /     '   :  |   |  |  '  '--'   '   : |  ; .' '   : | /  ;  |   |  ' '   : |  ; .' '   ; : \\  | \n  ;   :    /   :  | : ;  ; |      ;   |.'    '--'.     /      ;   |.'    |  :  :         |   | '`--'   |   | '` ,/   '   :  | |   | '`--'   '   | '/  .' \n   \\   \\ .'    '  :  `--'   \\     '---'        `--'---'       '---'      |  | ,'         '   : |       ;   :  .'     ;   |.'  '   : |       |   :    /   \n    `---`      :  ,      .-./                                            `--''           ;   |.'       |   ,.'       '---'    ;   |.'        \\   \\ .'    \n                `--`----'                                                                '---'         '---'                  '---'           `---`",random.randint(5,30)),'y')
    printc('\n\n\nCongratulations, you just mined a valid Animecoin block!\n\n\n', 'g')
    print_ascii_art_func(random.randint(0,7))
    print_line_func()

def print_ascii_art_func(pic_number):
    list_of_ascii_art = ["\n\n                _ ___                /^^\\ /^\\  /^^\\_\n    _          _@)@) \\            ,,/ '` ~ `'~~ ', `\\.\n  _/o\\_ _ _ _/~`.`...'~\\        ./~~..,'`','',.,' '  ~:\n / `,'.~,~.~  .   , . , ~|,   ,/ .,' , ,. .. ,,.   `,  ~\\_\n( ' _' _ '_` _  '  .    , `\\_/ .' ..' '  `  `   `..  `,   \\_\n ~V~ V~ V~ V~ ~\\ `   ' .  '    , ' .,.,''`.,.''`.,.``. ',   \\_\n  _/\\ /\\ /\\ /\\_/, . ' ,   `_/~\\_ .' .,. ,, , _/~\\_ `. `. '.,  \\_\n < ~ ~ '~`'~'`, .,  .   `_: ::: \\_ '      `_/ ::: \\_ `.,' . ',  \\_\n  \\ ' `_  '`_    _    ',/ _::_::_ \\ _    _/ _::_::_ \\   `.,'.,`., \\-,-,-,_,_,\n   `'~~ `'~~ `'~~ `'~~  \\(_)(_)(_)/  `~~' \\(_)(_)(_)/ ~'`\\_.._,._,'_;_;_;_;_;",
                         "\n      .---.        .-----------\n     /     \\  __  /    ------\n    / /     \\(  )/    -----\n   //////   ' \\/ `   ---\n  //// / // :    : ---\n // /   /  /`    '--        ANIMECOIN FIDELIS\n//          //..\\\n       ====UU====UU====\n           '//||\\`\n             ''``",
                         '\n                     .\n                    / V                  / `  /\n                 <<   |\n                 /    |\n               /      |     "ANIMEEEEEEEEEEEE"\n             /        |\n           /    \\  \\ /\n          (      ) | |\n  ________|   _/_  | |\n<__________\\______)\\__)',
                         '\n                               /T /I\n                              / |/ | .-~/\n                          T\\ Y  I  |/  /  _\n         /T               | \\I  |  I  Y.-~/\n        I l   /I       T\\ |  |  l  |  T  /\n     T\\ |  \\ Y l  /T   | \\I  l   \\ `  l Y\n __  | \\l   \\l  \\I l __l  l   \\   `  _. |\n \\ ~-l  `\\   `\\  \\  \\ ~\\  \\   `. .-~   |\n  \\   ~-. "-.  `  \\  ^._ ^. "-.  /  \\   |\n.--~-._  ~-  `  _  ~-_.-"-." ._ /._ ." ./\n >--.  ~-.   ._  ~>-"    "\\   7   7   ]\n^.___~"--._    ~-{  .-~ .  `\\ Y . /    |\n <__ ~"-.  ~       /_/   \\   \\I  Y   : |\n   ^-.__           ~(_/   \\   >._:   | l______\n       ^--.,___.-~"  /_/   !  `-.~"--l_ /     ~"-.\n              (_/ .  ~(   /\'     "~"--,Y   -=b-. _)\n               (_/ .  \\  :           / l      c"~o                 \\ /    `.    .     .^   \\_.-~"~--.  )\n                 (_/ .   `  /     /       !       )/\n                  / / _.   \'.   .\':      /        \'\n                  ~(_/ .   /    _  `  .-<_\n                    /_/ . \' .-~" `.  / \\  \\          ,z=.\n                    ~( /   \'  :   | K   "-.~-.______//\n                      "-,.    l   I/ \\_    __{--->._(==.\n                       //(     \\  <    ~"~"     //\n                      /\' /\\     \\  \\     ,v=.  ((\n                    .^. / /\\     "  }__ //===-  `\n                   / / \' \'  "-.,__ {---(==-\n                 .^ \'       :  T  ~"   ll       ~ANIMECOIN~\n                / .  .  . : | :!        \\    "Pauca sed Matura"\n               (_/  /   | | j-"          ~^\n                 ~-<_(_.^-~"',
                         '\n\n  ____    _    _   _______    _____   _______              _   _   _____    _____   _   _    _____ \n / __ \\  | |  | | |__   __|  / ____| |__   __|     /\\     | \\ | | |  __ \\  |_   _| | \\ | |  / ____|\n| |  | | | |  | |    | |    | (___      | |       /  \\    |  \\| | | |  | |   | |   |  \\| | | |  __ \n| |  | | | |  | |    | |     \\___ \\     | |      / /\\ \\   | . ` | | |  | |   | |   | . ` | | | |_ |\n| |__| | | |__| |    | |     ____) |    | |     / ____ \\  | |\\  | | |__| |  _| |_  | |\\  | | |__| |\n \\____/   \\____/     |_|    |_____/     |_|    /_/    \\_\\ |_| \\_| |_____/  |_____| |_| \\_|  \\_____|',
                         "\n _____________________\n|          |\\         |\n|   |\\     | .        |\n|   | `\\___|_/        |\n|    `/\'   `\\-. -._   |\n|   .-.   ,-.. `\\._`. |\n|   | +| | + |  ;. `  |\n|   (\\,--.\\_.\'), ;    |   ----Animecoin Mining System----\n|   /\\`--\' _./\' /_.   |   ""Everyone should be able to mine.\n|   OO`m/`m/\'OO\'      |    At least until someone makes an ASIC!""\n|_____________________|                       --Ancient Proverb\n\n",
                         '\n`;-.          ___,\n  `.`\\_...._/`.-"`\n    \\        /      ,\n    /()   () \\    .\' `-._\n   |)  .    ()\\  /   _.\'\n   \\  -\'-     ,; \'. <\n    ;.__     ,;|   >\n    / ,    / ,  |.-\'.-\'\n  (_/    (_/ ,;|.<`\n    \\    ,     ;-`\n     >   \\    /\n    (_,-\'`> .\'\n         (_,\'',
                         '\n                    ,.\n                  ,_> `.   ,\';\n              ,-`\'      `\'   \'`\'._\n           ,,-) ---._   |   .---\'\'`-),.\n         ,\'      `.  \\  ;  /   _,\'     `,\n      ,--\' ____       \\   \'  ,\'    ___  `-,\n     _>   /--. `-.              .-\'.--\\   \\__\n    \'-,  (    `.  `.,`~ \\~\'-. ,\' ,\'    )    _\\\n    _<    \\     \\ ,\'  \') )   `. /     /    <,.\n ,-\'   _,  \\    ,\'    ( /      `.    /        `-,\n `-.,-\'     `.,\'       `         `.,\'  `\\    ,-\'\n  ,\'       _  /   ,,,      ,,,     \\     `-. `-._\n /-,     ,\'  ;   \' _ \\    / _ `     ; `.     `(`-\\\n  /-,        ;    (o)      (o)      ;          `\'`,\n,~-\'  ,-\'    \\     \'        `      /     \\      <_\n/-. ,\'        \\                   /       \\     ,-\'\n  \'`,     ,\'   `-/             \\-\' `.      `-. <\n   /_    /      /   (_     _)   \\    \\          `,\n     `-._;  ,\' |  .::.`-.-\' :..  |       `-.    _\\\n       _/       \\  `:: ,^. :.:\' / `.        \\,-\'\n     \'`.   ,-\'  /`-..-\'-.-`-..-\'\\            `-.\n       >_ /     ;  (\\/( \' )\\/)  ;     `-.    _<\n       ,-\'      `.  \\`-^^^-\'/  ,\'        \\ _<\n        `-,  ,\'   `. `"`"`"\' ,\'   `-.   <`\'\n          \')        `._.,,_.\'        \\ ,-\'\n           \'._        \'`\'`\'   \\       <\n              >   ,\'       ,   `-.   <`\'\n               `,/          \\      ,-`\n                `,   ,\' |   /     /\n                 \'; /   ;        (\n                  _)|   `       (\n                  `\')         .-\'\n                    <_   \\   /       ANIMECOIN\n                      \\   /\\(   "theoria cum praxi"\n                       `;/  `',
                         "\n _________________.---.______\n(_(______________(_o o_(____()\n             .___.'. .'.___.\n             \\ o    Y    o /\n              \\ \\__   __/ /\n               '.__'-'__.'",
                        ]
    selected_pic = list_of_ascii_art[pic_number]
    printrc(make_text_indented_func(selected_pic, 12) + '\n\n')
    return selected_pic

def format_text_string_func(*args):
    formatted_string = ''.join(map(str, args))
    return formatted_string

def set_numerical_precision_func(required_decimal_places_of_accuracy):
    mp.dps = required_decimal_places_of_accuracy  #For mpmath

def get_numerical_precision_func():
    return mp.dps

def check_that_all_block_transactions_are_valid_func(block_transaction_data):
    if 1 == 1:  # This is just a placeholder function
        all_transactions_are_valid = 1
    else:
        all_transactions_are_valid = 0
    return all_transactions_are_valid

def construct_difficulty_string_func(difficulty_level):
    difficulty_string = ''
    for ii in range(1,difficulty_level+1):
        difficulty_string = difficulty_string + str(ii%10)
    return difficulty_string

def generate_block_candidate_func(block_transaction_data, nonce_number, use_verbose):
    nonce = convert_integer_to_bytes_func(nonce_number)
    block_transaction_data_with_nonce = block_transaction_data + nonce
    if use_verbose:
        print('Generated new block candidate for nonce: '+ str(nonce_number))
    return block_transaction_data_with_nonce

def convert_text_string_to_integer_func(input_text_string):
    text_string_as_integer = int(base64.b16encode(input_text_string.encode('utf-8')).decode('utf-8'), 16)
    return text_string_as_integer

def get_number_of_matching_leading_characters_in_string_func(input_string_or_integer, match_string_or_integer):
    input_string = str(input_string_or_integer)
    match_string = str(match_string_or_integer)
    if len(match_string) > len(input_string):
        number_of_matching_leading_characters = len(match_string) - len(match_string.replace(input_string, ''))
    else:
        number_of_matching_leading_characters = 0
        for cnt, current_match_character in enumerate(match_string):
            current_input_character = input_string[cnt]
            if current_input_character == current_match_character:
                number_of_matching_leading_characters = number_of_matching_leading_characters + 1
            else:
                break
    return number_of_matching_leading_characters

def mpmath_function_constructor_func(function_index_number):
    list_of_function_strings = LIST_OF_FUNCTION_STRINGS
    selected_function_string = list_of_function_strings[function_index_number]
    constructed_lambda_string = 'lambda x: mp.fabs(mp.'+selected_function_string+')'
    constructed_function = eval(constructed_lambda_string)
    return constructed_function

def get_hash_list_func(block_transaction_data_with_nonce, use_verbose): #Adjusted so each has the same length
    hash_01 = hashlib.sha256(block_transaction_data_with_nonce).hexdigest() + reverse_string_func(hashlib.sha256(block_transaction_data_with_nonce).hexdigest())
    hash_02 = hashlib.sha3_256(block_transaction_data_with_nonce).hexdigest() + reverse_string_func(hashlib.sha3_256(block_transaction_data_with_nonce).hexdigest())
    hash_03 = hashlib.sha3_512(block_transaction_data_with_nonce).hexdigest()
    hash_04 = hashlib.blake2s(block_transaction_data_with_nonce).hexdigest() + reverse_string_func(hashlib.blake2s(block_transaction_data_with_nonce).hexdigest())
    hash_05 = hashlib.blake2b(block_transaction_data_with_nonce).hexdigest()
    hash_list = [hash_01, hash_02, hash_03, hash_04, hash_05]
    if use_verbose:
        list_of_hash_types = ['SHA-256 +  (Repeated again in reverse)', 'SHA3-256 + (Repeated again in reverse)', 'SHA3-512                              ', 'BLAKE2S +  (Repeated again in reverse)', 'BLAKE2B                               ']
        printc('\n\nList of computed hashes:\n','g')
        for hash_count, current_hash in enumerate(hash_list):
            printc(list_of_hash_types[hash_count]+ ':     ' + current_hash,'b')
    return hash_list

def select_list_of_mpmath_functions_from_previous_block_final_hash_func(previous_block_final_hash):
    list_of_function_strings = LIST_OF_FUNCTION_STRINGS
    previous_block_final_hash_as_string = str(previous_block_final_hash)
    length_of_hash = len(previous_block_final_hash_as_string)
    if length_of_hash%2 != 0:
        previous_block_final_hash = previous_block_final_hash*10
        previous_block_final_hash_as_string = str(previous_block_final_hash)
    previous_block_final_hash_as_string_reversed = reverse_string_func(previous_block_final_hash_as_string)
    previous_block_final_hash_as_string_reversed_leading_zeros_stripped = previous_block_final_hash_as_string_reversed.lstrip('0')
    list_of_2_digits_chunks = list()
    list_of_mpmath_function_names = list()
    number_of_functions_to_use = min([5, len(previous_block_final_hash_as_string_reversed_leading_zeros_stripped)])
    for ii in range(2*number_of_functions_to_use):
        if ii%2 == 1:
            current_2_digit_chunk = previous_block_final_hash_as_string_reversed_leading_zeros_stripped[ii-1] + previous_block_final_hash_as_string_reversed_leading_zeros_stripped[ii]
            list_of_2_digits_chunks.append(current_2_digit_chunk)
    list_of_mpmath_functions = list()
    for current_chunk in list_of_2_digits_chunks:
        corresponding_mpmath_function = mpmath_function_constructor_func(int(current_chunk))
        list_of_mpmath_functions.append(corresponding_mpmath_function)
        list_of_mpmath_function_names.append(list_of_function_strings[int(current_chunk)])
    return list_of_2_digits_chunks, list_of_mpmath_functions, list_of_mpmath_function_names, previous_block_final_hash_as_string_reversed_leading_zeros_stripped

def check_score_of_hash_list_func(input_hash_list, difficulty_level, list_of_2_digits_chunks, list_of_mpmath_functions, list_of_mpmath_function_names, previous_block_final_hash_as_string_reversed_leading_zeros_stripped, use_verbose):
    hash_is_valid = 0
    list_of_hash_integers = list()
    for current_hash in input_hash_list:
        list_of_hash_integers.append(mpf(int(current_hash, 16)))
    list_of_hashes_in_size_order = sorted(list_of_hash_integers)
    input_hash_list_sorted = sort_list_based_on_another_list_func(input_hash_list, list_of_hash_integers)
    list_of_hash_types = ['SHA-256 + (Repeated again in reverse)', 'SHA3-256 + (Repeated again in reverse)', 'SHA3-512', 'BLAKE2S + (Repeated again in reverse)', 'BLAKE2B']
    list_of_hash_types_sorted = sort_list_based_on_another_list_func(list_of_hash_types, list_of_hash_integers)
    smallest_hash = list_of_hashes_in_size_order[0]
    second_smallest_hash = list_of_hashes_in_size_order[1]
    middle_hash = list_of_hashes_in_size_order[2]
    second_largest_hash = list_of_hashes_in_size_order[-2]
    largest_hash = list_of_hashes_in_size_order[-1]
    list_of_hashes_by_name = [smallest_hash, largest_hash, middle_hash, second_smallest_hash, second_largest_hash]
    list_of_hash_name_strings = ['smallest_hash', 'largest_hash', 'middle_hash', 'second_smallest_hash', 'second_largest_hash']
    last_k_digits_to_extract_from_each_intermediate_result = difficulty_level*5
    list_of_intermediate_results_last_k_digits = list()
    if use_verbose:
        printc('Previous final hash: ' + str(previous_block_final_hash),'b')
        printc('After being reversed and removing leading zeros: '+ previous_block_final_hash_as_string_reversed_leading_zeros_stripped + '\n\n','b')
    for cnt, current_formula in enumerate(list_of_mpmath_functions):
        current_formula_name = list_of_mpmath_function_names[cnt]
        two_digit_hash_chunk = list_of_2_digits_chunks[cnt]
        hash_index = cnt%len(list_of_hashes_by_name)
        current_hash_value = list_of_hashes_by_name[hash_index]
        current_hash_value_rescaled = mp.frac(mp.log10(current_hash_value))
        current_hash_name_string = list_of_hash_name_strings[hash_index]
        current_intermediate_result = current_formula(current_hash_value_rescaled)
        current_intermediate_result_as_string = str(current_intermediate_result)
        current_intermediate_result_as_string_decimal_point_and_leading_zeros_stripped = current_intermediate_result_as_string.replace('.','').lstrip('0')
        current_intermediate_result_as_string_decimal_point_and_leading_zeros_stripped_last_k_digits = current_intermediate_result_as_string_decimal_point_and_leading_zeros_stripped[-last_k_digits_to_extract_from_each_intermediate_result:]
        list_of_intermediate_results_last_k_digits.append(int(current_intermediate_result_as_string_decimal_point_and_leading_zeros_stripped_last_k_digits))
        if use_verbose:
            printc('Current 2-digit chunk of reversed previous hash: '+str(two_digit_hash_chunk),'m')
            printc('Corresponding mathematical formula for index '+str(two_digit_hash_chunk)+' (out of 100 distinct formulas from mpmath):   y =' + current_formula_name,'r')
            printc('Current hash value to use in the formula: ' + list_of_hash_types_sorted[cnt] + ':  ' + input_hash_list_sorted[cnt] + ')','y')
            printc('Hash Value as an Integer (i.e., whole number): ' + current_hash_name_string + ' =  ' + str(int(current_hash_value)),'y')
            printc('Rescaled hash value (i.e., x = fractional_part( log(hash_value) ) in the formula): ' + str(current_hash_value_rescaled),'g')
            printc('Output from formula: (i.e., y in the formula): ' + str(current_intermediate_result),'g')
            printc('Output as string with decimal point and leading zeros removed: ' + str(current_intermediate_result_as_string_decimal_point_and_leading_zeros_stripped),'g')
            printc('Last '+str(last_k_digits_to_extract_from_each_intermediate_result)+' digits of rescaled output (these are multiplied together to produce the final hash): ' + str(current_intermediate_result_as_string_decimal_point_and_leading_zeros_stripped_last_k_digits)+'\n\n','c')
    final_hash = 1
    for current_k_digit_chunk in list_of_intermediate_results_last_k_digits:
        final_hash = final_hash*current_k_digit_chunk
    difficulty_string = construct_difficulty_string_func(difficulty_level)
    number_of_matching_leading_characters = get_number_of_matching_leading_characters_in_string_func(final_hash, difficulty_string)
    if number_of_matching_leading_characters >= difficulty_level:
        hash_is_valid = 1
    if use_verbose:
        print('\n')
        printc(make_text_indented_func('Final Hash (product of all the k-digit chunks): '+str(final_hash),5),'c')
        printc('\nDifficulty Level:  ' + str(difficulty_level),'r')
        printc('Difficulty String:  ' + difficulty_string,'r')
        if hash_is_valid:
            if difficulty_level == 1:
                digit_text = 'digit'
            else:
                digit_text = str(difficulty_level)+' digits'
            print_mining_congratulations_message_func()
            printc('\nSince the first ' + digit_text + ' of the final hash match all of the digits in the difficulty string, the hash is valid!', 'b')
        else:
            printc('\nHash is invalid, sorry!\n', 'b')
    if SLOWDOWN:
        sleep(0.1)
    return number_of_matching_leading_characters, hash_is_valid, final_hash


def generate_next_mining_attempt_func(block_transaction_data, nonce_number, previous_block_final_hash, use_verbose):
    nonce_number = nonce_number + 1
    block_transaction_data_with_nonce = generate_block_candidate_func(block_transaction_data, nonce_number, use_verbose)
    current_hash_list = get_hash_list_func(block_transaction_data_with_nonce, use_verbose)
    list_of_2_digits_chunks, list_of_mpmath_functions, list_of_mpmath_function_names, previous_block_final_hash_as_string_reversed_leading_zeros_stripped = select_list_of_mpmath_functions_from_previous_block_final_hash_func(previous_block_final_hash)
    current_number_of_matching_leading_characters, current_hash_is_valid, current_final_hash = check_score_of_hash_list_func(current_hash_list, difficulty_level, list_of_2_digits_chunks, list_of_mpmath_functions, list_of_mpmath_function_names, previous_block_final_hash_as_string_reversed_leading_zeros_stripped, use_verbose)
    return current_hash_list, nonce_number, list_of_2_digits_chunks, list_of_mpmath_functions, list_of_mpmath_function_names, previous_block_final_hash_as_string_reversed_leading_zeros_stripped, current_number_of_matching_leading_characters, current_hash_is_valid, current_final_hash

def execute_next_mining_attempt_func(block_transaction_data, nonce_number, previous_block_final_hash, use_verbose):
    current_hash_list = 0
    with ThreadPoolExecutor() as executor:
        future = executor.submit(generate_next_mining_attempt_func, block_transaction_data, nonce_number, previous_block_final_hash, use_verbose)
        outputs = future.result(timeout=5)
    if len(outputs) > 0:
        current_hash_list = outputs[0]
        nonce_number = outputs[1]
        list_of_2_digits_chunks = outputs[2]
        list_of_mpmath_functions = outputs[3]
        list_of_mpmath_function_names = outputs[4]
        previous_block_final_hash_as_string_reversed_leading_zeros_stripped = outputs[5]
        current_number_of_matching_leading_characters = outputs[6]
        current_hash_is_valid = outputs[7]
        current_final_hash = outputs[8]
        return  current_hash_list, nonce_number, list_of_2_digits_chunks, list_of_mpmath_functions, list_of_mpmath_function_names, previous_block_final_hash_as_string_reversed_leading_zeros_stripped, current_number_of_matching_leading_characters, current_hash_is_valid, current_final_hash
    else:
        if tqdm_import_successful:
            pbar = tqdm()
            pbar.set_description('There was a problem computing the candidate block, skipping to the next one!')
        else:
            printc('There was a problem computing the candidate block, skipping to the next one!', 'r')
        return current_hash_list

def get_nonce_func():
    return random.randint(1, 100000)

def get_animecoin_id_public_key_func(): #Placeholder
    my_animecoin_id_public_key = '54fda990775aac0d54d8c5330cddbd52f5e4d816dd18a6f48b07f64c08f'
    return my_animecoin_id_public_key

class solution_object(object):
  def __init__(self, block_transaction_data, block_count, difficulty_level, current_required_number_of_digits_of_precision, nonce_number, previous_block_final_hash, \
               current_hash_list, list_of_2_digits_chunks, list_of_mpmath_functions, list_of_mpmath_function_names, previous_block_final_hash_as_string_reversed_leading_zeros_stripped, \
               current_number_of_matching_leading_characters, current_hash_is_valid, current_final_hash):
          self.block_transaction_data = block_transaction_data
          self.block_count = block_count
          self.difficulty_level = difficulty_level
          self.current_required_number_of_digits_of_precision = current_required_number_of_digits_of_precision
          self.nonce_number = nonce_number
          self.previous_block_final_hash = previous_block_final_hash
          self.current_hash_list = current_hash_list
          self.list_of_2_digits_chunks = list_of_2_digits_chunks
          self.list_of_mpmath_functions = list_of_mpmath_functions
          self.list_of_mpmath_function_names = list_of_mpmath_function_names
          self.previous_block_final_hash_as_string_reversed_leading_zeros_stripped = previous_block_final_hash_as_string_reversed_leading_zeros_stripped
          self.current_number_of_matching_leading_characters = current_number_of_matching_leading_characters
          self.current_hash_is_valid = current_hash_is_valid
          self.current_final_hash = current_final_hash
          self.current_hash_list = current_hash_list
          self.date_time_solution_found = datetime.now()
          self.miner_ip_address = get_my_local_ip_func()
          self.miner_animecoin_id_public_key = get_animecoin_id_public_key_func()

def mine_for_new_block_func(block_transaction_data, difficulty_level, block_count, previous_block_final_hash, current_required_number_of_digits_of_precision, use_verbose):
    start_time = time.time()
    if tqdm_import_successful:
        pbar = tqdm()
    block_is_valid = 0
    initial_nonce = get_nonce_func()
    nonce_number = initial_nonce
    current_best_number_of_matching_leading_characters = 0
    list_of_hash_types = ['SHA-256 + (Repeated again in reverse)', 'SHA3-256 + (Repeated again in reverse)', 'SHA3-512', 'BLAKE2S + (Repeated again in reverse)', 'BLAKE2B']
    all_transactions_are_valid = check_that_all_block_transactions_are_valid_func(block_transaction_data)
    if all_transactions_are_valid:
        while block_is_valid == 0:
            if SLOWDOWN:
                sleep(0.3)
            if tqdm_import_successful:
                pbar.update(1)
            try:
                current_hash_list, nonce_number, list_of_2_digits_chunks, list_of_mpmath_functions, list_of_mpmath_function_names, previous_block_final_hash_as_string_reversed_leading_zeros_stripped, \
                    current_number_of_matching_leading_characters, current_hash_is_valid, current_final_hash = execute_next_mining_attempt_func(block_transaction_data, nonce_number, previous_block_final_hash, use_verbose)
            except:
                current_hash_list = 0
            if (time.time() - start_time) > (2.5*60):
                break
            if current_hash_list != 0:
                if nonce_number == 1:
                    current_best_number_of_matching_leading_characters = current_number_of_matching_leading_characters
                    current_best_final_hash = current_final_hash
                if current_best_number_of_matching_leading_characters < current_number_of_matching_leading_characters:
                    current_best_number_of_matching_leading_characters = current_number_of_matching_leading_characters
                    current_best_final_hash = current_final_hash
                    difficulty_string = construct_difficulty_string_func(difficulty_level)
                    if current_number_of_matching_leading_characters == 1:
                        digit_text = ' digit'
                    else:
                        digit_text = ' digits'
                    update_string = 'Best Nonce found so far ('+str(nonce_number)+') generates a final hash that matches ' + str(current_number_of_matching_leading_characters) + digit_text + ' out of the required '+str(difficulty_level)+' in the difficulty string "' + difficulty_string+'"; Best final hash: ' + str(current_final_hash)
                    if tqdm_import_successful:
                        pbar.set_description(update_string)
                    else:
                        printc(update_string,'y')
                    block_duration_in_minutes = (time.time() - start_time)/60
                    packaged_solution_object = solution_object(block_transaction_data, block_count, difficulty_level, current_required_number_of_digits_of_precision, nonce_number, previous_block_final_hash, current_hash_list, \
                                           list_of_2_digits_chunks, list_of_mpmath_functions, list_of_mpmath_function_names, previous_block_final_hash_as_string_reversed_leading_zeros_stripped, \
                                           current_number_of_matching_leading_characters, current_hash_is_valid, current_final_hash)
                if current_hash_is_valid:
                    block_is_valid = 1
                    try:
                        current_best_final_hash
                    except NameError:
                        current_best_final_hash = 0
                    if current_best_final_hash != 0:
                        print_line_func()
                        printc('\n***Just mined a new block!***\n\nBlock Duration: ' + str(round(block_duration_in_minutes, 3)) + ' minutes.\n', 'c')
                        printc('\nBlock Number: ' + str(block_count), 'c')
                        printc('\nWinning Nonce Number: ' + str(nonce_number), 'c')
                        printc('\nFinal Hash of mined block as Integer: '+ str(current_final_hash), 'y')
                        printc('\nNumber of Digits in Final Hash Integer: ' + str(len(str((current_final_hash)))), 'y')
                        print_line_func()
                        sleep(SLEEP_PERIOD*0.5)
                        current_number_of_matching_leading_characters, current_hash_is_valid, current_final_hash = check_score_of_hash_list_func(current_hash_list, difficulty_level, list_of_2_digits_chunks, list_of_mpmath_functions, list_of_mpmath_function_names, previous_block_final_hash_as_string_reversed_leading_zeros_stripped, use_verbose=1)
                        printc('\n\nList of computed hashes:\n','b')
                        for hash_count, current_hash in enumerate(current_hash_list):
                            printc(make_text_indented_func(list_of_hash_types[hash_count], 5)+ ':', 'y')
                            printc(make_text_indented_func(str(current_hash), 10),'k')
                        printc('\n\nCurrent Difficulty Level: ' + str(difficulty_level), 'r')
                        printc('\nCurrent Number of Digits of Precision: '+str(get_numerical_precision_func()), 'r')
                        printc('\nSleeping for ' + str(SLEEP_PERIOD) + ' seconds...', 'r')
                        print_line_func()
                        sleep(SLEEP_PERIOD*0.5)
    return current_number_of_matching_leading_characters, current_final_hash, nonce_number, block_duration_in_minutes, packaged_solution_object

try:
    block_count
except NameError:
    block_count = 0
use_demo = 1
number_of_demo_blocks_to_mine = 100
baseline_digits_of_precision = 128
maximum_digits_of_precision = 1000
set_numerical_precision_func(baseline_digits_of_precision)
current_required_number_of_digits_of_precision = baseline_digits_of_precision
adjust_hash_difficulty_every_k_blocks = 100
reset_numerical_precision_every_r_blocks = adjust_hash_difficulty_every_k_blocks
max_difficulty_change_per_block = 1
initial_difficulty_level = 4
target_block_duration_in_minutes = 2.5

use_debug_mode = 1
if use_debug_mode:
    block_is_valid = 0
    nonce_number = 0
    block_transaction_data = os.urandom(pow(10, 6))
    block_transaction_data_with_nonce = generate_block_candidate_func(block_transaction_data, nonce_number, use_verbose=1)
    current_hash_list = get_hash_list_func(block_transaction_data_with_nonce, use_verbose=1)
    input_hash_list = current_hash_list

if use_demo:
    if block_count == 0:
        print_welcome_message_func()
        print('\n\n')
        print_ascii_art_func(random.randint(0, 7))
        print('\nGenerating fake transaction data for block...')
        block_transaction_data = os.urandom(10**5)
        print('\nInitial Block Difficulty: '+ str(initial_difficulty_level))
        print('\nInitial Number of decimal places of accuracy: '+str(baseline_digits_of_precision))
        print('\nTarget block duration in minutes: '+str(target_block_duration_in_minutes))
        print_line_func()
        difficulty_level = initial_difficulty_level
        difficulty_adjustment_countdown = adjust_hash_difficulty_every_k_blocks
        numerical_precision_reset_countdown = reset_numerical_precision_every_r_blocks
        previous_block_final_hash = int(hashlib.sha3_512(b'anime').hexdigest(), 16)
    for ii in range(number_of_demo_blocks_to_mine):
        if SLOWDOWN:
            sleep(2)
        printc('Current Block Count: '+str(block_count),'b')
        printc('\nTarget block duration in seconds: '+str(round(target_block_duration_in_minutes*60)),'g')
        current_number_of_matching_leading_characters, current_final_hash, nonce_number, current_block_duration_in_minutes, packaged_solution_object = mine_for_new_block_func(block_transaction_data, difficulty_level, block_count, previous_block_final_hash, current_required_number_of_digits_of_precision, use_verbose)
        previous_block_final_hash = current_final_hash
        blocks_until_next_difficulty_level_increase = adjust_hash_difficulty_every_k_blocks - ii%adjust_hash_difficulty_every_k_blocks
        set_numerical_precision_func(current_required_number_of_digits_of_precision)
        ratio_of_actual_block_time_to_target_block_time = current_block_duration_in_minutes/target_block_duration_in_minutes
        printc('Ratio of actual block time to target block time: '+ str(round(ratio_of_actual_block_time_to_target_block_time,4)),'g')
        difficulty_adjustment_countdown = difficulty_adjustment_countdown - 1
        numerical_precision_reset_countdown = numerical_precision_reset_countdown - 1
        if ratio_of_actual_block_time_to_target_block_time <= 1:
            if numerical_precision_reset_countdown == 0:
                printc('\nResetting the required number of digits of precision from the current level of ' + str(current_required_number_of_digits_of_precision) + ' back to down to the baseline level of '+str(baseline_digits_of_precision),'m')
                current_required_number_of_digits_of_precision = baseline_digits_of_precision
                numerical_precision_reset_countdown = reset_numerical_precision_every_r_blocks
            else:
                printc('\nThe ratio of the last block time to the target block time is less than 1.0, so we need to make the work harder so that the blocks are mined more slowly.\nWe begin with the number of digits of numerical precision required:','g')
                precision_increase = floor(maximum_digits_of_precision/adjust_hash_difficulty_every_k_blocks/10)
                new_required_number_of_digits_of_precision = min([maximum_digits_of_precision, current_required_number_of_digits_of_precision + precision_increase])
                precision_percentage_increase = new_required_number_of_digits_of_precision/current_required_number_of_digits_of_precision - 1
                printc('\nNow increasing numerical precision by '+str(round(100*precision_percentage_increase, 3))+'% to '+ str(new_required_number_of_digits_of_precision) +'\n','y')
                print_line_func()
                current_required_number_of_digits_of_precision = new_required_number_of_digits_of_precision
            if difficulty_adjustment_countdown == 0:
                computed_difficulty_level_change = min([floor(1/ratio_of_actual_block_time_to_target_block_time), max_difficulty_change_per_block])
                print_line_func()
                printc('\n\n***Now increasing the difficulty level by ' + str(round(computed_difficulty_level_change, 3)) + ', from '+str(difficulty_level)+' to ' + str(difficulty_level + round(computed_difficulty_level_change,3)),'g')
                print_line_func()
                difficulty_level = difficulty_level + computed_difficulty_level_change
                difficulty_adjustment_countdown = adjust_hash_difficulty_every_k_blocks
            else:
                printc('\nThere are '+str(blocks_until_next_difficulty_level_increase)+' more blocks remaining before the next difficulty level adjustment.\n','g')
        else:
            if numerical_precision_reset_countdown == 0:
                printc('\nResetting the required number of digits of precision from the current level of ' + str(current_required_number_of_digits_of_precision) + ' back to down to the baseline level of '+str(baseline_digits_of_precision),'m')
                current_required_number_of_digits_of_precision = baseline_digits_of_precision
                numerical_precision_reset_countdown = reset_numerical_precision_every_r_blocks
            else:
                printc('\nThe ratio of the last block time to the target block time is greater than 1.0, so we need to make the work easier so that the blocks are mined more quickly. We begin with the number of digits of numerical precision required:\n','g')
                precision_decrease = floor(maximum_digits_of_precision/adjust_hash_difficulty_every_k_blocks/10)
                new_required_number_of_digits_of_precision = max([baseline_digits_of_precision, current_required_number_of_digits_of_precision - precision_decrease])
                precision_percentage_decrease = new_required_number_of_digits_of_precision/current_required_number_of_digits_of_precision - 1
                printc('\nNow decreasing numerical precision by '+str(round(100*precision_percentage_decrease,3))+'% to '+ str(new_required_number_of_digits_of_precision) +'\n','y')
                print_line_func()
                current_required_number_of_digits_of_precision = new_required_number_of_digits_of_precision
            if difficulty_adjustment_countdown == 0:
                computed_difficulty_level_change = -min([floor(1/ratio_of_actual_block_time_to_target_block_time), max_difficulty_change_per_block])
                print_line_func()
                printc('\n\n***Now decreasing the difficulty level by ' + str(round(-computed_difficulty_level_change, 3)) + ', from '+str(difficulty_level)+' to ' + str(difficulty_level + round(computed_difficulty_level_change,3)),'g')
                print_line_func()
                difficulty_level = difficulty_level + computed_difficulty_level_change
                difficulty_adjustment_countdown = adjust_hash_difficulty_every_k_blocks
            else:
                printc('\nThere are '+str(blocks_until_next_difficulty_level_increase)+' more blocks remaining before the next difficulty level adjustment.\n','y')
        set_numerical_precision_func(current_required_number_of_digits_of_precision)
        block_count = block_count + 1
