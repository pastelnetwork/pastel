import os

def convert_list_of_strings_into_list_of_utf_encoded_byte_objects_func(list_of_strings):
    list_of_utf_encoded_byte_objects = []
    for current_string in list_of_strings:
        if isinstance(current_string,str):
            list_of_utf_encoded_byte_objects.append(current_string.encode('utf-8'))
        else:
            print('Error! Invalid string given as input.')
    return list_of_utf_encoded_byte_objects    
 
def concatenate_list_of_strings_func(list_of_strings):
    concatenated_string = ''
    for current_string in list_of_strings:
        concatenated_string = concatenated_string + current_string + os.linesep
    return concatenated_string

def generate_animecoin_html_strings_for_templates_and_example_executed_tickets_func():
    artwork_metadata_html_template_string = """
    <!doctype html>
    <html lang=en>
    <head>
    <meta charset=utf-8>
    <style> body { font-family: 'Sailec', Vardana, sans-serif;} table {border-collapse: collapse; border: 3px solid rgb(200,200,200); letter-spacing: 1px; font-size: 0.8rem;}td, th {border: 1px solid rgb(190,190,190); padding: 20px 20px;}</style>
    <title>Animecoin Artwork Metadata</font></title>
    <body>
    <table align="left" summary="Animecoin Artwork Metadata">
    <caption><font size="7" color="blue">Animecoin Artwork Metadata</font></caption>
    <tbody>
    <tr> <th align="left">Animecoin Metadata Format Version Number:</th> <td>ANIME_METADATA_FORMAT_VERSION_NUMBER</td> </tr>
    <tr> <th align="left">Date-Time when Artwork was Signed by Artist:</th> <td>ANIME_DATE_TIME_ARTWORK_SIGNED</td> </tr>
    <tr> <th align="left">Current Bitcoin Blockchain Block Number (for validation purposes):</th> <td>ANIME_CURRENT_BITCOIN_BLOCK_NUMBER</td></tr>
    <tr> <th align="left">Current Bitcoin Blockchain Block Hash (for validation purposes):</th> <td>ANIME_CURRENT_BITCOIN_BLOCK_HASH</td></tr>
    <tr> <th align="left">Artist's Animecoin ID Public Key:</th> <td>ANIME_ARTIST_ANIMECOIN_ID_PUBLIC_KEY</td> </tr>
    <tr> <th align="left">Artist's Receiving ANIME Blockchain Address (Note: this is where the registered art assets are sent by the Registering Masternode once the assets are created):</th> <td>ANIME_ARTIST_RECEIVING_BLOCKCHAIN_ADDRESS</td> </tr>
    <tr> <th align="left">Total Number of Unique Copies of Artwork to Create (Note: This cannot be changed later!):</th> <td>ANIME_TOTAL_NUMBER_OF_UNIQUE_COPIES_OF_ARTWORK_TO_CREATE</td></tr>
    <tr> <th align="left">Artist's Name (Note: any user can select any name; what gives security against  impersonation is the Artist's Digital Signature):</th> <td>ANIME_ARTIST_NAME</td> </tr>
    <tr> <th align="left">Title of Artwork:</th> <td>ANIME_ARTWORK_TITLE</td></tr>
    <tr> <th align="left">Artwork Series Name:</th> <td>ANIME_SERIES_NAME</td> </tr>
    <tr> <th align="left">Artist's Website:</th> <td>ANIME_ARTIST_WEBSITE</td> </tr>
    <tr> <th align="left">Artist's Statement about Artwork:</th> <td>ANIME_ARTIST_STATEMENT</td></tr>
    <tr> <th align="left">Combined Size of Artwork Image Files in Megabytes:</th> <td>ANIME_COMBINED_IMAGE_SIZE_IN_MB</td></tr>
    <tr> <th align="left">Current ANIME Mining Difficulty Rate:</th> <td>ANIME_CURRENT_ANIME_MINING_DIFFICULTY_RATE</td></tr>
    <tr> <th align="left">Average ANIME Mining Difficulty Rate for First 20,000 Blocks:</th> <td>ANIME_AVG_ANIME_MINING_DIFFICULTY_RATE_FIRST_20K_BLOCKS</td></tr>
    <tr> <th align="left">ANIME Mining Difficulty Adjustment Ratio:</th> <td>ANIME_MINING_DIFFICULTY_ADJUSTMENT_RATIO</td></tr>
    <tr> <th align="left">Required Registration Fee in ANIME Before Difficulty Adjustment:</th> <td>ANIME_REQUIRED_REGISTRATION_FEE_IN_ANIME_PRE_ADJUSTMENT</td></tr>
    <tr> <th align="left">Required Registration Fee in ANIME After Difficulty Adjustment:</th> <td>ANIME_REQUIRED_REGISTRATION_FEE_IN_ANIME_POST_ADJUSTMENT</td></tr>
    <tr> <th align="left">Forfeitable Deposit in ANIME to Initiate Registration Process (total amount risked by Artist if artwork can't be registered; 10% of the full Registration Fee after Difficulty Adjustment):</th> <td>ANIME_ARTISTS_FORFEITABLE_DEPOSIT_IN_ANIME_TO_INITIATE_REGISTRATION</td></tr>
    <tr> <th align="left">Combined Artwork Image and Metadata String (Note: this is a text string that fully describes all the essential aspects of the Artwork):</th> <td>ANIME_COMBINED_ARTWORK_IMAGE_AND_METADATA_STRING</td></tr>
    <tr> <th align="left">Combined Artwork Image and Metadata Hash:</th> <td>ANIME_COMBINED_ARTWORK_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Artist's Animecoin ID Digital Signature on the Combined Artwork Image and Metadata Hash:</th> <td>ANIME_ARTIST_SIGNATURE_ON_COMBINED_ARTWORK_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Registering Masternode's ANIME Collateral Blockchain Address:</th> <td>ANIME_REGISTERING_MASTERNODE_ANIME_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Registering Masternode's Animecoin ID Public Key:</th> <td>ANIME_REGISTERING_MASTERNODE_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Registering Masternode's IP Address:</th> <td>ANIME_REGISTERING_MASTERNODE_IP_ADDRESS</td></tr>
    <tr> <th align="left">Registering Masternode's Collateral Address Digital Signature on the Combined Image and Metadata Hash:</th> <td>ANIME_REGISTERING_MASTERNODE_COLLATERAL_SIGNATURE_ON_COMBINED_ARTWORK_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Registering Masternode's Animecoin ID Digital Signature on the Combined Image and Metadata Hash:</th> <td>ANIME_REGISTERING_MASTERNODE_ANIMECOIN_ID_SIGNATURE_ON_COMBINED_ARTWORK_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Registering Masternode's Animecoin Blockchain Address for Receiving Registration Fee:</th> <td>ANIME_REGISTERING_MASTERNODE_ANIMECOIN_BLOCKCHAIN_ADDRESS_FOR_RECEIVING_REGISTRATION_FEE</td></tr>
    <tr> <th align="left">Date-Time when Registering Masternode Processed Artwork:</th> <td>ANIME_DATE_TIME_REGISTERING_MASTERNODE_PROCESSED_ARTWORK</td> </tr>
    <tr> <th align="left">Confirming Masternode 1's ANIME Collateral Blockchain Address:</th> <td>ANIME_CONFIRMING_MN_1_ANIME_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Animecoin ID Public Key:</th> <td>ANIME_CONFIRMING_MN_1_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Confirming Masternode 1's IP Address:</th> <td>ANIME_CONFIRMING_MN_1_IP_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Collateral Address Digital Signature on the Combined Image and Metadata Hash:</th> <td>ANIME_CONFIRMING_MN_1_COLLATERAL_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Animecoin ID Digital Signature on the Combined Image and Metadata Hash:</th> <td>ANIME_CONFIRMING_MN_1_ANIMECOIN_ID_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Animecoin Blockchain Address for Receiving Share of Registration Fee (paid by the Registering Maternode):</th> <td>ANIME_CONFIRMING_MN_1_ANIMECOIN_BLOCKCHAIN_ADDRESS_FOR_RECEIVING_SHARE_OF_ARTWORK_REGISTRATION_FEE</td></tr>
    <tr> <th align="left">Date-Time when Confirming Masternode 1 Processed Artwork:</th> <td>ANIME_DATE_TIME_CONFIRMING_MN_1_PROCESSED_ARTWORK</td> </tr>
    <tr> <th align="left">Confirming Masternode 2's ANIME Collateral Blockchain Address:</th> <td>ANIME_CONFIRMING_MN_2_ANIME_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Animecoin ID Public Key:</th> <td>ANIME_CONFIRMING_MN_2_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Confirming Masternode 2's IP Address:</th> <td>ANIME_CONFIRMING_MN_2_IP_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Collateral Address Digital Signature on the Combined Image and Metadata Hash:</th> <td>ANIME_CONFIRMING_MN_2_COLLATERAL_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Animecoin ID Digital Signature on the Combined Image and Metadata Hash:</th> <td>ANIME_CONFIRMING_MN_2_ANIMECOIN_ID_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Animecoin Blockchain Address for Receiving Share of Registration Fee (paid by the Registering Maternode):</th> <td>ANIME_CONFIRMING_MN_2_ANIMECOIN_BLOCKCHAIN_ADDRESS_FOR_RECEIVING_SHARE_OF_ARTWORK_REGISTRATION_FEE</td></tr>
    <tr> <th align="left">Date-Time when Confirming Masternode 2 Processed Artwork:</th> <td>ANIME_DATE_TIME_CONFIRMING_MN_2_PROCESSED_ARTWORK</td> </tr>
    <tr> <th align="left">ANIME Blockchain Address containing this Artwork Metadata File in Compressed Form:</th> <td>ANIME_ARTWORK_METADATA_FILE_BLOCKCHAIN_STORAGE_ADDRESS</td></tr>
    <tr> <th align="left">Animecoin Blockchain File Storage File Compression Method Description String:</th> <td>ANIME_BLOCKCHAIN_FILE_STORAGE_FILE_COMPRESSION_METHOD_DESCRIPTION_STRING</td></tr>
    </tbody>
    </table>
    
    <br> <br>
    <div style = "clear:both;"></div>
    <br> <br>
    
    <table align="left" summary="Included Images and their SHA256 Hashes">
    <caption><font size="6" color="blue">Included Images and their SHA256 Hashes:</font></caption>
    <tbody> ANIME_IMAGE_FILEPATHS_AND_HASH_DATA </tbody>
    </table>
    
    </body>
    </html>
    """

    trade_ticket_html_template_string = """
    <!doctype html>
    <html lang=en>
    <head>
    <meta charset=utf-8>
    <style> body { font-family: 'Sailec', Vardana, sans-serif;} table {border-collapse: collapse; border: 3px solid rgb(200,200,200); letter-spacing: 1px; font-size: 0.8rem;}td, th {border: 1px solid rgb(190,190,190); padding: 25px 25px;}</style>
    <title>Animecoin Masternode Decentralized Exchange</font></title>
    </head>
    <body>
    <table align="left" summary="Animecoin Trade Ticket">
    <caption><font size="7" color="blue">Animecoin Trade Ticket</font></caption>
    <tbody>
    <tr> <th align="left">Animecoin Trade Ticket Format Version Number:</th> <td>ANIME_TRADE_TICKET_FORMAT_VERSION_NUMBER</td> </tr>
    <tr> <th align="left">Date-Time when Trader Submitted Trade Ticket:</th> <td>ANIME_DATETIME_TRADER_SUBMITTED_ANIME_TICKET</td> </tr>
    <tr> <th align="left">Current Bitcoin Blockchain Block Number (for validation purposes):</th> <td>ANIME_CURRENT_BITCOIN_BLOCK_NUMBER</td></tr>
    <tr> <th align="left">Current Bitcoin Blockchain Block Hash (for validation purposes):</th> <td>ANIME_CURRENT_BITCOIN_BLOCK_HASH</td></tr>
    <tr> <th align="left">Submitting Trader's Animecoin ID Public Key::</th> <td>ANIME_TRADER_ANIMECOIN_PUBLIC_ID_PUBKEY</td></tr>
    <tr> <th align="left">Submitting Trader's Animecoin Blockchain Address:</th> <td>ANIME_TRADER_ANIME_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Desired Trade Type (Buy or Sell):</th> <td>ANIME_TRADE_TYPE_BUY_OR_SELL</td> </tr>
    <tr> <th align="left">Traded Artwork's Combined Image and Metadata String:</th> <td>ANIME_TRADED_ARTWORK_COMBINED_IMAGE_AND_METADATA_STRING</td></tr>
    <tr> <th align="left">Traded Artwork's Combined Image and Metadata Hash:</th> <td>ANIME_TRADED_ARTWORK_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Artist's Animecoin Public ID Signature on the Traded Artwork's Combined Image and Metadata Hash:</th> <td>ANIME_ARTIST_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Number of Unique Copies of Traded Artwork to Buy or Sell (Trade Quantity):</th> <td>ANIME_NUMBER_OF_UNIQUE_COPIES_OF_ARTWORK_TO_BUY_OR_SELL</td></tr>
    <tr> <th align="left">Price (measured in ANIME coins) at which the Submitting Trader is willing to Buy or Sell the Traded Artwork (Trade Price):</th> <td>ANIME_DESIRED_PRICE_IN_ANIME_TERMS</td></tr>
    <tr> <th align="left">Total Amount of ANIME to Pay or Receive in Trade (Trade Price times Trade Quantity):</th> <td>ANIME_TOTAL_TRADE_AMOUNT_IN_ANIME_TERMS</td></tr>
    <tr> <th align="left">Total Amount of ANIME that Submitting Trader Agrees to Pay Assigned Masternode Broker as a Commission for Completing the Trade (Trade Commission):</th> <td>ANIME_TOTAL_TRADE_COMMISSION_IN_ANIME_TERMS</td></tr>
    <tr> <th align="left">Trade Ticket Details String:</th> <td>ANIME_TRADE_TICKET_DETAILS_STRING</td></tr>
    <tr> <th align="left">Trade Ticket Details Hash:</th> <td>ANIME_TRADE_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Submitting Trader's Animecoin Public ID Signature on Trade Ticket Details Hash:</th> <td>ANIME_TRADER_SIGNATURE_ON_TRADE_TICKET</td></tr>
    <tr> <th align="left">Assigned Masternode Broker's IP Address:</th> <td>ANIME_ASSIGNED_BROKER_IP_ADDRESS</td> </tr>
    <tr> <th align="left">Assigned Masternode Broker's Animecoin ID Public Key:</th> <td>ANIME_ASSIGNED_BROKER_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Assigned Masternode Broker's ANIME Collateral Blockchain Address:</th> <td>ANIME_ASSIGNED_BROKER_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Assigned Masternode Broker's Collateral Address Digital Signature on Trade Ticket Details Hash:</th> <td>ANIME_ASSIGNED_BROKER_COLLATERAL_SIGNATURE_ON_TRADE_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Assigned Masternode Broker's Animecoin Public ID Digital Signature on Trade Ticket Details Hash:</th> <td>ANIME_ASSIGNED_BROKER_ANIMECOIN_ID_SIGNATURE_ON_TRADE_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Assigned Masternode Broker's Animecoin Blockchain Address:</th> <td>ANIME_ASSIGNED_BROKER_ANIME_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Assigned Masternode Broker's Animecoin Blockchain Address for Receiving Trade Commision:</th> <td>ANIME_ASSIGNED_BROKER_ANIMECOIN_BLOCKCHAIN_ADDRESS_FOR_RECEIVING_COMMISSION</td></tr>
    <tr> <th align="left">Date-Time when Assigned Masternode Broker Processed Trade Ticket:</th> <td>ANIME_DATE_TIME_ASSIGNED_BROKER_PROCESSED_TRADE_TICKET</td> </tr>
    <tr> <th align="left">Confirming Masternode Broker 1's ANIME Collateral Blockchain Address:</th> <td>ANIME_CONFIRMING_MN_BROKER_1_ANIME_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode Broker 1's Animecoin ID Public Key:</th> <td>ANIME_CONFIRMING_MN_BROKER_1_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Confirming Masternode Broker 1's IP Address:</th> <td>ANIME_CONFIRMING_MN_BROKER_1_IP_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode Broker 1's Collateral Address Digital Signature on the Combined Image and Metadata Hash:</th> <td>ANIME_CONFIRMING_MN_BROKER_1_COLLATERAL_SIGNATURE_ON_TRADE_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode Broker 1's Animecoin Public ID Digital Signature on the Combined Image and Metadata Hash:</th> <td>ANIME_CONFIRMING_MN_BROKER_1_ANIMECOIN_ID_SIGNATURE_ON_TRADE_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode Broker 1's Animecoin Blockchain Address for Receiving Share of Trade Commision (paid by the Assigned Maternode Broker):</th> <td>ANIME_CONFIRMING_MN_BROKER_1_ANIMECOIN_BLOCKCHAIN_ADDRESS_FOR_RECEIVING_SHARE_OF_TRADE_COMMISSION</td></tr>
    <tr> <th align="left">Date-Time when Confirming Masternode Broker 1 Processed Trade Ticket:</th> <td>ANIME_DATE_TIME_CONFIRMING_MN_BROKER_1_PROCESSED_TRADE_TICKET</td> </tr>
    <tr> <th align="left">Confirming Masternode Broker 2's ANIME Collateral Blockchain Address:</th> <td>ANIME_CONFIRMING_MN_BROKER_2_ANIME_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode Broker 2's Animecoin ID Public Key:</th> <td>ANIME_CONFIRMING_MN_BROKER_2_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Confirming Masternode Broker 2's IP Address:</th> <td>ANIME_CONFIRMING_MN_BROKER_2_IP_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode Broker 2's Collateral Address Digital Signature on the Combined Image and Metadata Hash:</th> <td>ANIME_CONFIRMING_MN_BROKER_2_COLLATERAL_SIGNATURE_ON_TRADE_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode Broker 2's Animecoin Public ID Digital Signature on the Combined Image and Metadata Hash:</th> <td>ANIME_CONFIRMING_MN_BROKER_2_ANIMECOIN_ID_SIGNATURE_ON_TRADE_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode Broker 2's Animecoin Blockchain Address for Receiving Share of Trade Commision (paid by the Assigned Maternode Broker):</th> <td>ANIME_CONFIRMING_MN_BROKER_2_ANIMECOIN_BLOCKCHAIN_ADDRESS_FOR_RECEIVING_SHARE_OF_TRADE_COMMISSION</td></tr>
    <tr> <th align="left">Date-Time when Confirming Masternode Broker 2 Processed Trade Ticket:</th> <td>ANIME_DATE_TIME_CONFIRMING_MN_BROKER_2_PROCESSED_TRADE_TICKET</td> </tr>
    <tr> <th align="left">ANIME Blockchain Address containing Traded Artwork's Metadata file in Compressed Form:</th> <td>ANIME_ARTWORK_METADATA_FILE_BLOCKCHAIN_STORAGE_ADDRESS</td></tr>
    <tr> <th align="left">ANIME Blockchain Address containing this Trade Ticket File in Compressed Form:</th> <td>ANIME_TRADE_TICKET_FILE_BLOCKCHAIN_STORAGE_ADDRESS</td></tr>
    <tr> <th align="left">Animecoin Blockchain File Storage File Compression Method Description String:</th> <td>ANIME_BLOCKCHAIN_FILE_STORAGE_FILE_COMPRESSION_METHOD_DESCRIPTION_STRING</td></tr>
    </tbody>
    </table>
    
    </body>
    </html>
    """

    treasury_fund_payment_request_voting_ticket_html_template_string = """
    <!doctype html>
    <html lang=en>
    <head>
    <meta charset=utf-8>
    <style> body { font-family: 'Sailec', Vardana, sans-serif;} table {border-collapse: collapse; border: 3px solid rgb(200,200,200); letter-spacing: 1px; font-size: 0.8rem;}td, th {border: 1px solid rgb(190,190,190); padding: 25px 25px;}</style>
    <title>Animecoin Treasury Fund Payment Request Voting Ticket</font></title>
    </head>
    <body>
    <table align="left" summary="Animecoin Treasury Fund Payment Request Voting Ticket">
    <caption><font size="7" color="blue">Animecoin Treasury Fund Payment Request Voting Ticket</font></caption>
    <tbody>
    <tr> <th align="left">Animecoin Treasury Fund Payment Request Voting Ticket Format Version Number:</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_FORMAT_VERSION_NUMBER</td> </tr>
    <tr> <th align="left">Date-Time when Requesting Masternode Submitted Payment Request Voting Ticket:</th> <td>ANIME_DATETIME_REQUESTING_MASTERNODE_SUBMITTED_PAYMENT_REQUEST_VOTING_TICKET</td> </tr>
    <tr> <th align="left">Current Animecoin Blockchain Block Number (Note: this is also known as the Block Height):</th> <td>ANIME_CURRENT_BITCOIN_BLOCK_NUMBER</td></tr>
    <tr> <th align="left">Current Animecoin Blockchain Block Hash:</th> <td>ANIME_CURRENT_BITCOIN_BLOCK_HASH</td></tr>
    <tr> <th align="left">Duration of Treasury Fund Payment Request Voting, Measured in Animecoin Blocks (Note: there is a 2.5 minute duration per ANIME block):</th> <td>ANIME_DURATION_OF_TREASURY_FUND_PAYMENT_REQUEST_VOTING_MEASURED_IN_NUMBER_OF_ANIME_BLOCKS</td></tr>
    <tr> <th align="left">Implied Animecoin Blockchain Block Number When Voting Period Ends (Note: if the treasury fund payment request voting ticket does not have the required number of valid Masternode signatures by this ANIME block number, then the request is automatically invalidated):</th> <td>ANIME_IMPLIED_ANIMECOIN_BLOCK_NUMBER_WHEN_TREASURY_FUND_PAYMENT_REQUEST_VOTING_PERIOD_ENDS</td></tr>
    <tr> <th align="left">Current Number of Valid Animecoin Masternodes (Note: this number includes all nodes that have activated with a valid ANIME collateral address containing sufficient ANIME to run a masternode):</th> <td>ANIME_CURRENT_NUMBER_OF_VALID_ANIMECOIN_MASTERNODES</td></tr>
    <tr> <th align="left">Current Number of Valid Animecoin Masternodes with Voting Rights (Note: this number excludes Masternodes that have been operating for fewer than 4,000 blocks and Masternodes that are currently being penalized for provable dishonest acts such as stealing registration fees or trading commissions):</th> <td>ANIME_CURRENT_NUMBER_OF_VALID_ANIMECOIN_MASTERNODES_WITH_VOTING_RIGHTS</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request - Quorum Required as a Percentage of the Current Number of Valid Masternodes with Voting Rights:</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_QUORUM_REQUIRED_AS_A_PERCENTAGE_OF_THE_CURRENT_NUMBER_OF_VALID_MASTERNODES_WITH_VOTING_RIGHTS</td></tr>
    <tr> <th align="left">Implied Number of Voting Masternodes Needed to Consistute a Quorum for Treasury Fund Payment Request Voting (Note: this minimum number of Masternodes must sign their vote on the treasury fund payment request ticket, otherwise the vote is considered invalid and the payment will not be sent):</th> <td>ANIME_IMPLIED_NUMBER_OF_VOTING_MASTERNODES_NEEDED_TO_CONSTITUTE_A_TREASURY_FUND_PAYMENT_REQUEST_QUORUM</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request - Required Percentage of all Valid Voting Masternodes to Validate  Treasury Fund Payment Request:</th> <td>ANIME_REQUIRED_PERCENTAGE_OF_THE_CURRENT_NUMBER_OF_VALID_MASTERNODES_WITH_VOTING_RIGHTS_TO_VALIDATE_TREASURY_FUND_PAYMENT_REQUEST</td></tr>
    <tr> <th align="left">Implied Number of Voting Masternodes Needed to Validate a Treasury Fund Payment Request (Note: this is the number of valid Masternode votes required to force all nodes to require new valid mined blocks to include the specified payment in the next coinbase transactions until the total amount has been paid):</th> <td>ANIME_IMPLIED_NUMBER_OF_VOTING_MASTERNODES_NEEDED_TO_VALIDATE_A_TREASURY_FUND_PAYMENT_REQUEST</td></tr>
    <tr> <th align="left">Current Bitcoin Blockchain Block Number (for validation purposes):</th> <td>ANIME_CURRENT_BITCOIN_BLOCK_NUMBER</td></tr>
    <tr> <th align="left">Current Bitcoin Blockchain Block Hash (for validation purposes):</th> <td>ANIME_CURRENT_BITCOIN_BLOCK_HASH</td></tr>
    <tr> <th align="left">Requesting Masternode's IP Address:</th> <td>ANIME_REQUESTING_MASTERNODE_IP_ADDRESS</td> </tr>
    <tr> <th align="left">Requesting Masternode's Animecoin ID Public Key:</th> <td>ANIME_REQUESTING_MASTERNODE_ANIMECOIN_PUBLIC_ID_PUBKEY</td></tr>
    <tr> <th align="left">Requesting Masternode's ANIME Collateral Blockchain Address:</th> <td>ANIME_REQUESTING_MASTERNODE_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Specified Animecoin Blockchain Address for Receiving the Requested Payment from the Animecoin Treasury Fund:</th> <td>ANIME_SPECIFIED_ANIMECOIN_BLOCKCHAIN_ADDRESS_FOR_RECEIVING_TREASURY_FUND_PAYMENT</td> </tr>
    <tr> <th align="left">Requested Payment Amount (measured in ANIME) to be Sent by the Animecoin Treasury Fund to the Specified ANIME Blockchain Address:</th> <td>ANIME_REQUESTED_TREASURY_FUND_PAYMENT_AMOUNT_IN_ANIME_TERMS</td></tr>
    <tr> <th align="left">Explanation of the Purpose of the Requested Funds:</th> <td>ANIME_EXPLANATION_OF_PURPOSE_OF_TREASURY_FUND_PAYMENT_REQUEST</td></tr>
    <tr> <th align="left">URL Link to a Single Supporting Documentation File (e.g., a PDF file linked from Dropbox; the hash of this document file will be validated against the reported hash in this ticket, so if the file contents changes subsequent to the submission of the trading ticket, the hashes will not match, making the documentation less reliable):</th> <td>ANIME_URL_TO_SUPPORTING_DOCUMENTATION_FILE_FOR_TREASURY_FUND_PAYMENT_REQUEST</td></tr>
    <tr> <th align="left">SHA256 Hash of Supporting Document File:</th> <td>ANIME_SHA256_HASH_OF_TREASURY_FUND_PAYMENT_REQUEST_LINKED_SUPPORTING_DOCUMENT_FILE</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Software Development (TRUE/FALSE; Note: you should include a link to the code you have written or propose to develop in the Explanation field):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_CATEGORY_SOFTWARE_DEVELOPMENT</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Exchange Listing Fees (TRUE/FALSE; Note: you should include a link to the proposed exchange website in the Explanation field, and also explain why the community should support listing ANIME on this particular exchange. You should compare the listing fees on your proposed exchange to the listing fees on other crypto exchang sites, and also compare the average volume on that exchange for other currency pairs):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_CATEGORY_EXCHANGE_LISTING_FEES</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Marketing Activities (TRUE/FALSE; Note: you should include a link to the proposed marketing plan in the Explanation field):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_CATEGORY_MARKETING_ACTIVITIES</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Anime Community Outreach (TRUE/FALSE; Note: you should include a link to the proposed anime community outreach plan in the Explanation field):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_CATEGORY_ANIME_COMMUNITY_OUTREACH</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Purchase of ANIME Rare Artwork as a Patron (TRUE/FALSE; Note: you should include a link to the ANIME art asset you are suggesting the Treasury Fund purchase for philanthropic reasons in the Explanation field, as well as your reasoning for why this asset and/or artist is worthy of community support):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_CATEGORY_RARE_ART_PURCHASE_AS_A_PATRON</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Blockchain Address to Receive the Rare Artwork Purchased as a Patron (Note: while the ANIME Artwork token can be sent to any valid ANIME address, ANIME blockchain addresses that are publicly and demonstrably linked to a Non-Profit Foundation such as a museum or artist collective are preferred; for example, the museum can put its ANIME blockchain address on its website or twitter page.):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_CATEGORY_RARE_ART_PURCHASE_AS_A_PATRON_RECEIVING_ANIME_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Philanthropic Grant of ANIME Coins to a Deserving Artist (TRUE/FALSE; Note: you should include a link to a website or document describing the receiving artst and why the community should support that artist; encouraged reasons include the quality of their artwork, financial hardship, and being a good public spokesperson for the project; while the ANIME coins can be sent to any valid ANIME address, addresses that are publicly and demonstrably linked to the Deserving artist are preferred-- for example, on the artist's public Instagram/Facebook/Twitter/Deviantart pages.):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_PHILANTHROPIC_GRANT_OF_ANIME_COINS_TO_A_DESERVING_ARTIST</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Grant of ANIME Coins in Return for Influencer Marketing (TRUE/FALSE; Note: you should include a link to a website or document describing the influencer why the community should pay them; encouraged reasons include: a large and engaged audience on YouTube or other services; a clean reputation with no widespread accusations of scamminess or dishonesty;  a deep demonstrated understanding of crypto, technology, or art/anime):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_CATEGORY_INFLUENCER_MARKETING</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Transfer to Rare Artwork Market Making Liquidity Bot (TRUE/FALSE; Note: you should include a link to source code for your proposed market making bot in the Explanation field; the purpose of the bot is to increase the liquidity on the ANIME decentralized art exchange; all operational details of the bot must be publicly disclosed by the bot developer, and the bot developer is permitted to remove 10% of any generated profits as compensation, with the remaining profit used to increase the capital available to the bot.):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_CATEGORY_RARE_ARTWORK_MARKET_MAKING_LIQUIDITY_BOT</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Transfer to ANIME Coin Market Making Liquidity Bot (TRUE/FALSE; Note: you should include a link to source code for your proposed market making bot in the Explanation field as well as describe which exchanges the bot will be active on; the purpose of the bot is to increase the liquidity on various ANIME exchange sites, in both BTC and other currency pairs; all operational details of the bot must be publicly disclosed by the bot developer, and the bot developer is permitted to remove 10% of any generated profits as compensation, with the remaining profit used to increase the capital available to the bot.):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_CATEGORY_ANIME_COIN_MARKET_MAKING_LIQUIDITY_BOT</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Category: Animecoin Purchase of ANIME Coins for the Purpose of Destroying Them (TRUE/FALSE; Note: any ANIME coins used for this purpose will be sent to a provably un-spendable ANIME Blockchain Address; the purpose of this is to reduce the number of circulating ANIME coins, which increases the value of the remaining coins, similar to how a share buyback works in the stock market.):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_CATEGORY_SOFTWARE_DEVELOPMENT</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Voting Ticket Details String:</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_STRING</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Voting Ticket Details Hash:</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Requesting Masternode's ANIME Collateral Address Digital Signature on Treasury Fund Payment Request Voting Ticket Details Hash:</th> <td>ANIME_REQUESTING_MASTERNODE_COLLATERAL_SIGNATURE_ON_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Requesting Masternode's Animecoin Public ID Digital Signature on Treasury Fund Payment Request Voting Ticket Details Hash:</th> <td>ANIME_REQUESTING_MASTERNODE_ANIMECOIN_ID_SIGNATURE_ON_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 1's ANIME Collateral Blockchain Address:</th> <td>ANIME_CONFIRMING_MN_1_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Animecoin ID Public Key:</th> <td>ANIME_CONFIRMING_MN_1_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Confirming Masternode 1's IP Address:</th> <td>ANIME_CONFIRMING_MN_1_IP_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Collateral Address Digital Signature on Treasury Fund Payment Request Voting Ticket Details Hash:</th> <td>ANIME_CONFIRMING_MN_1_COLLATERAL_SIGNATURE_ON_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Animecoin Public ID Digital Signature on Treasury Fund Payment Request Voting Ticket Details Hash:</th> <td>ANIME_CONFIRMING_MN_1_ANIMECOIN_ID_SIGNATURE_ON_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Date-Time when Confirming Masternode 1 Signed the Treasury Fund Payment Request Voting Ticket Details Hash:</th> <td>ANIME_DATE_TIME_CONFIRMING_MN_1_SIGNED_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH</td> </tr>
    <tr> <th align="left">Confirming Masternode 2's ANIME Collateral Blockchain Address:</th> <td>ANIME_CONFIRMING_MN_2_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Animecoin ID Public Key:</th> <td>ANIME_CONFIRMING_MN_2_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Confirming Masternode 2's IP Address:</th> <td>ANIME_CONFIRMING_MN_2_IP_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Collateral Address Digital Signature on Treasury Fund Payment Request Voting Ticket Details Hash:</th> <td>ANIME_CONFIRMING_MN_2_COLLATERAL_SIGNATURE_ON_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Animecoin Public ID Digital Signature on Treasury Fund Payment Request Voting Ticket Details Hash:</th> <td>ANIME_CONFIRMING_MN_2_ANIMECOIN_ID_SIGNATURE_ON_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Date-Time when Confirming Masternode 2 Signed the Treasury Fund Payment Request Voting Ticket Details Hash:</th> <td>ANIME_DATE_TIME_CONFIRMING_MN_2_SIGNED_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH</td> </tr>
    <tr> <th align="left">Treasury Fund Payment Request Voting Ticket Details Hash with the String "PAYMENT APPROVED" Appended to the End (Voting Masternodes must sign this string to vote FOR the approval of the specified payment request):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH_WITH_APPROVAL</td></tr>
    <tr> <th align="left">Treasury Fund Payment Request Voting Ticket Details Hash with the String "PAYMENT DENIED" Appended to the End (Voting Masternodes must sign this string to vote AGAINST the approval of the specified payment request):</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_DETAILS_HASH_WITH_DENIAL</td></tr>
    <tr> <th align="left">ANIME Blockchain Address containing this Treasury Fund Payment Request Voting Ticket File in Compressed Form:</th> <td>ANIME_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET_FILE_BLOCKCHAIN_STORAGE_ADDRESS</td></tr>
    <tr> <th align="left">Animecoin Blockchain File Storage File Compression Method Description String:</th> <td>ANIME_BLOCKCHAIN_FILE_STORAGE_FILE_COMPRESSION_METHOD_DESCRIPTION_STRING</td></tr>
    <tbody>
    </table>
    
    <br> <br>
    <div style = "clear:both;"></div>
    <br> <br>
    
    <table align="left" summary="Included Masternode Public Keys and the Corresponding Signatures on the Executed Voting Ticket String">
    <caption><font size="4" color="blue">Included Masternode Public Keys and the Corresponding Signatures on the Executed Voting Ticket String</font></caption>
    <caption><font size="2" color="blue">i.e., each voting masternode signs the hash of the payment request ticket details with the text "PAYMENT APPROVED" or "PAYMENT_DENIED" appended to it.</font></caption>
    <caption><font size="2" color="blue">This is done using 1) the digital signature associated with the voting Masternode's Animecoin ID, and 2) the digital signature associated with the voting Masternode's ANIME Collateral Address.</font></caption>
    <tbody> ANIME_MASTERNODE_PUBLIC_KEY_AND_SIGNATURE_STRING_BLOCK_FOR_TREASURY_FUND_PAYMENT_REQUEST_VOTING_TICKET </tbody>
    </table>
    
    </body>
    </html>
    """
    
    offending_content_takedown_request_voting_ticket_html_template_string = """
    <!doctype html>
    <html lang=en>
    <head>
    <meta charset=utf-8>
    <style> body { font-family: 'Sailec', Vardana, sans-serif;} table {border-collapse: collapse; border: 3px solid rgb(200,200,200); letter-spacing: 1px; font-size: 0.8rem;}td, th {border: 1px solid rgb(190,190,190); padding: 25px 25px;}</style>
    <title>Animecoin Offending Content Takedown Request Voting Ticket</font></title>
    </head>
    <body>
    <table align="left" summary="Animecoin Offending Content Takedown Request Voting Ticket">
    <caption><font size="7" color="blue">Animecoin Offending Content Takedown Request Voting Ticket</font></caption>
    <tbody>
    <tr> <th align="left">Animecoin Offending Content Takedown Request Voting Ticket Format Version Number:</th> <td>ANIME_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_FORMAT_VERSION_NUMBER</td> </tr>
    <tr> <th align="left">Date-Time when Requesting Masternode Submitted Offending Content Takedown Request Voting Ticket:</th> <td>ANIME_DATETIME_REQUESTING_MASTERNODE_SUBMITTED_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET</td> </tr>
    <tr> <th align="left">Current Animecoin Blockchain Block Number (Note: this is also known as the Block Height):</th> <td>ANIME_CURRENT_ANIMECOIN_BLOCK_NUMBER</td></tr>
    <tr> <th align="left">Current Animecoin Blockchain Block Hash:</th> <td>ANIME_CURRENT_ANIMECOIN_BLOCK_HASH</td></tr>
    <tr> <th align="left">Duration of Takedown Request Voting, Measured in Animecoin Blocks (Note: there is a 2.5 minute duration per ANIME block):</th> <td>ANIME_DURATION_OF_TAKEDOWN_REQUEST_VOTING_MEASURED_IN_NUMBER_OF_ANIME_BLOCKS</td></tr>
    <tr> <th align="left">Implied Animecoin Blockchain Block Number When Voting Period Ends (Note: if the takedown request voting ticket does not have the required number of valid Masternode signatures by this ANIME block number, then the request is automatically invalidated):</th> <td>ANIME_IMPLIED_ANIMECOIN_BLOCK_NUMBER_WHEN_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_PERIOD_ENDS</td></tr>
    <tr> <th align="left">Current Number of Valid Animecoin Masternodes (Note: this number includes all nodes that have activated with a valid ANIME collateral address containing sufficient ANIME to run a masternode):</th> <td>ANIME_CURRENT_NUMBER_OF_VALID_ANIMECOIN_MASTERNODES</td></tr>
    <tr> <th align="left">Current Number of Valid Animecoin Masternodes with Voting Rights (Note: this number excludes Masternodes that have been operating for fewer than 4,000 blocks and Masternodes that are currently being penalized for provable dishonest acts such as stealing registration fees or trading commissions):</th> <td>ANIME_CURRENT_NUMBER_OF_VALID_ANIMECOIN_MASTERNODES_WITH_VOTING_RIGHTS</td></tr>
    <tr> <th align="left">Offending Content Takedown Request - Quorum Required as a Percentage of the Current Number of Valid Masternodes with Voting Rights:</th> <td>ANIME_OFFENDING_CONTENT_TAKEDOWN_REQUEST_QUORUM_REQUIRED_AS_A_PERCENTAGE_OF_THE_CURRENT_NUMBER_OF_VALID_MASTERNODES_WITH_VOTING_RIGHTS</td></tr>
    <tr> <th align="left">Implied Number of Voting Masternodes Needed to Consistute a Quorum for Offending Content Takedown Request Voting (Note: this minimum number of Masternodes must sign their vote on the content takedown request ticket, otherwise the vote is considered invalid and the offending content will not be deregistered):</th> <td>ANIME_IMPLIED_NUMBER_OF_VOTING_MASTERNODES_NEEDED_TO_CONSTITUTE_AN_OFFENDING_CONTENT_TAKEDOWN_REQUEST_QUORUM</td></tr>
    <tr> <th align="left">Offending Content Takedown Request - Required Percentage of all Valid Voting Masternodes to Validate an Offending Content Takedown Request:</th> <td>ANIME_REQUIRED_PERCENTAGE_OF_THE_CURRENT_NUMBER_OF_VALID_MASTERNODES_WITH_VOTING_RIGHTS_TO_VALIDATE_AN_OFFENDING_CONTENT_TAKEDOWN_REQUEST</td></tr>
    <tr> <th align="left">Implied Number of Voting Masternodes Needed to Validate Offending Content Takedown Request (Note: this is the number of valid Masternode votes required to force all nodes to reject the specified offending artwork):</th> <td>ANIME_IMPLIED_NUMBER_OF_VOTING_MASTERNODES_NEEDED_TO_VALIDATE_AN_OFFENDING_CONTENT_TAKEDOWN_REQUEST</td></tr>
    <tr> <th align="left">Current Bitcoin Blockchain Block Number (for validation purposes):</th> <td>ANIME_CURRENT_BITCOIN_BLOCK_NUMBER</td></tr>
    <tr> <th align="left">Current Bitcoin Blockchain Block Hash (for validation purposes):</th> <td>ANIME_CURRENT_BITCOIN_BLOCK_HASH</td></tr>
    <tr> <th align="left">Requesting Masternode's IP Address:</th> <td>ANIME_REQUESTING_MASTERNODE_IP_ADDRESS</td> </tr>
    <tr> <th align="left">Requesting Masternode's Animecoin ID Public Key:</th> <td>ANIME_REQUESTING_MASTERNODE_ANIMECOIN_PUBLIC_ID_PUBKEY</td></tr>
    <tr> <th align="left">Requesting Masternode's ANIME Collateral Blockchain Address:</th> <td>ANIME_REQUESTING_MASTERNODE_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Offending Artwork's Combined Image and Metadata Hash:</th> <td>ANIME_OFFENDING_CONTENT_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">ANIME Blockchain Address containing Offending Artwork's Metadata File in Compressed Form:</th> <td>ANIME_OFFENDING_CONTENT_ARTWORK_METADATA_FILE_BLOCKCHAIN_STORAGE_ADDRESS</td></tr>
    <tr> <th align="left">Explanation for Why Offending Artwork Should be De-Registered:</th> <td>ANIME_EXPLANATION_FOR_WHY_OFFENDING_CONTENT_SHOULD_BE_DEREGISTERED</td></tr>
    <tr> <th align="left">Takedown Request Category: Offending Artwork is NSFW (TRUE/FALSE; Note: this excludes drawings and only applies to actual photos):</th> <td>ANIME_TAKEDOWN_REQUEST_CATEGORY_NSFW</td></tr>
    <tr> <th align="left">Takedown Request Category: Offending Artwork is a Dupe (TRUE/FALSE; Note: this means that the Offending Artwork was not an original artwork created for the Animecoin System):</th> <td>ANIME_TAKEDOWN_REQUEST_CATEGORY_DUPE</td></tr>
    <tr> <th align="left">Takedown Request Category: Offending Artwork Violates Copyright Laws (TRUE/FALSE; Note: this should only be used if the copyright owner has complained publicly about the infringement; any such supporting documentation should be included in the takedown request ticket):</th> <td>ANIME_TAKEDOWN_REQUEST_CATEGORY_COPYRIGHT_INFRINGEMENT</td></tr>
    <tr> <th align="left">Takedown Request Category: Some Other Reason (TRUE/FALSE; Note: this category should only be used when the 3 other provided categories do not apply):</th> <td>ANIME_TAKEDOWN_REQUEST_CATEGORY_ALL_OTHER_REASONS</td></tr>
    <tr> <th align="left">URL Link to a Single Supporting Documentation File that Justifies the Takedown Request (e.g., a PDF file linked from Dropbox; the hash of this document file will be validated against the reported hash in this ticket, so if the file contents changes subsequent to the submission of the trading ticket, the hashes will not match, making the documentation less reliable):</th> <td>ANIME_URL_TO_SUPPORTING_DOCUMENTATION_FILE_FOR_OFFENDING_CONTENT_TAKEDOWN_REQUEST</td></tr>
    <tr> <th align="left">SHA256 Hash of Supporting Document File:</th> <td>ANIME_SHA256_HASH_OF_OFFENDING_CONTENT_TAKEDOWN_REQUEST_SUPPORTING_DOCUMENT</td></tr>
    <tr> <th align="left">Offending Content Takedown Request Voting Ticket Details String:</th> <td>ANIME_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_STRING</td></tr>
    <tr> <th align="left">Offending Content Takedown Request Voting Ticket Details Hash:</th> <td>ANIME_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Requesting Masternode's ANIME Collateral Address Digital Signature on Offending Content Takedown Request Voting Ticket Details Hash:</th> <td>ANIME_REQUESTING_MASTERNODE_COLLATERAL_SIGNATURE_ON_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Requesting Masternode's Animecoin Public ID Digital Signature on Offending Content Takedown Request Voting Ticket Details Hash:</th> <td>ANIME_REQUESTING_MASTERNODE_ANIMECOIN_ID_SIGNATURE_ON_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 1's ANIME Collateral Blockchain Address:</th> <td>ANIME_CONFIRMING_MN_1_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Animecoin ID Public Key:</th> <td>ANIME_CONFIRMING_MN_1_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Confirming Masternode 1's IP Address:</th> <td>ANIME_CONFIRMING_MN_1_IP_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Collateral Address Digital Signature on Offending Content Takedown Request Voting Ticket Details Hash:</th> <td>ANIME_CONFIRMING_MN_1_COLLATERAL_SIGNATURE_ON_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Animecoin Public ID Digital Signature on Offending Content Takedown Request Voting Ticket Details Hash:</th> <td>ANIME_CONFIRMING_MN_1_ANIMECOIN_ID_SIGNATURE_ON_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Date-Time when Confirming Masternode 1 Signed the Offending Content Takedown Request Voting Ticket Details Hash:</th> <td>ANIME_DATE_TIME_CONFIRMING_MN_1_SIGNED_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH</td> </tr>
    <tr> <th align="left">Confirming Masternode 2's ANIME Collateral Blockchain Address:</th> <td>ANIME_CONFIRMING_MN_2_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Animecoin ID Public Key:</th> <td>ANIME_CONFIRMING_MN_2_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Confirming Masternode 2's IP Address:</th> <td>ANIME_CONFIRMING_MN_2_IP_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Collateral Address Digital Signature on Offending Content Takedown Request Voting Ticket Details Hash:</th> <td>ANIME_CONFIRMING_MN_2_COLLATERAL_SIGNATURE_ON_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Animecoin Public ID Digital Signature on Offending Content Takedown Request Voting Ticket Details Hash:</th> <td>ANIME_CONFIRMING_MN_2_ANIMECOIN_ID_SIGNATURE_ON_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH</td></tr>
    <tr> <th align="left">Date-Time when Confirming Masternode 2 Signed the Offending Content Takedown Request Voting Ticket Details Hash:</th> <td>ANIME_DATE_TIME_CONFIRMING_MN_2_SIGNED_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH</td> </tr>
    <tr> <th align="left">Offending Content Takedown Request Voting Ticket Details Hash with the String "TAKEDOWN APPROVED" Appended to the End (Voting Masternodes must sign this string to vote FOR the approval of the specified takedown request):</th> <td>ANIME_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH_WITH_APPROVAL</td></tr>
    <tr> <th align="left">Offending Content Takedown Request Voting Ticket Details Hash with the String "TAKEDOWN DENIED" Appended to the End (Voting Masternodes must sign this string to vote AGAINST the approval of the specified takedown request):</th> <td>ANIME_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_DETAILS_HASH_WITH_DENIAL</td></tr>
    <tr> <th align="left">ANIME Blockchain Address containing this Treasury Fund Payment Request Voting Ticket File in Compressed Form:</th> <td>ANIME_OFFENDING_CONTENT_TAKEDOWN_REQUEST_VOTING_TICKET_FILE_BLOCKCHAIN_STORAGE_ADDRESS</td></tr>
    <tr> <th align="left">Animecoin Blockchain File Storage File Compression Method Description String:</th> <td>ANIME_BLOCKCHAIN_FILE_STORAGE_FILE_COMPRESSION_METHOD_DESCRIPTION_STRING</td></tr>
    <tbody>
    </table>
    
    <br> <br>
    <div style = "clear:both;"></div>
    <br> <br>
    
    <table align="left" summary="Included Masternode Public Keys and the Corresponding Signatures on the Executed Voting Ticket String">
    <caption><font size="4" color="blue">Included Masternode Public Keys and the Corresponding Signatures on the Executed Voting Ticket String</font></caption>
    <caption><font size="2" color="blue">i.e., each voting masternode signs the hash of the offending content takedown request ticket details with the text "TAKEDOWN APPROVED" or "TAKEDOWN DENIED" appended to it.</font></caption>
    <caption><font size="2" color="blue">This is done using 1) the digital signature associated with the voting Masternode's Animecoin ID, and 2) the digital signature associated with the voting Masternode's ANIME Collateral Address.</font></caption>
    <tbody> ANIME_MASTERNODE_PUBLIC_KEY_AND_SIGNATURE_STRING_BLOCK_FOR_OFFENDING_CONTENT_TAKEDOWN_VOTING_TICKET </tbody>
    </table>
    
    </body>
    </html>
    """
    
    example_executed_artwork_metadata_html_ticket_string = """
    <!doctype html>
    <html lang=en>
    <head>
    <meta charset=utf-8>
    <style> body { font-family: 'Sailec', Vardana, sans-serif;} table {border-collapse: collapse; border: 3px solid rgb(200,200,200); letter-spacing: 1px; font-size: 0.8rem;}td, th {border: 1px solid rgb(190,190,190); padding: 20px 20px;}</style>
    <title>Animecoin Artwork Metadata</font></title>
    <body>
    <table align="left" summary="Animecoin Artwork Metadata">
    <caption><font size="7" color="blue">Animecoin Artwork Metadata</font></caption>
    <tbody>
    <tr> <th align="left">ANIME Metadata Format Version Number:</th> <td>1.00</td> </tr>
    <tr> <th align="left">Artist's Animecoin ID Public Key:</th> <td>-----BEGIN RSA PUBLIC KEY-----
    MEgCQQCUCohW62EXu6hIJDOK96CC3pFu+iPWvHnd1BiFZBjWwMUf0sMhUtpqGDbk
    5m1lLIp75ApY+L8yGs5mmmm9qn7FAgMBAAE=
    -----END RSA PUBLIC KEY-----
    </td> </tr>
    <tr> <th align="left">Artist's Receiving ANIME Blockchain Address:</th> <td>AfyXTrtHaimN6YDFCnWqMssRU3QPJjhT77</td> </tr>
    <tr> <th align="left">Combined Image and Metadata String:</th> <td>7b8053598a1b19136df077c56a3b37128d6a83a0829225bd5c85b53db7e7d969e3ff01ba6f4d5630f5da3003f541c765da74cf43134b2e514f36434d6eb209d01.00Arturo Lopez-----BEGIN RSA PUBLIC KEY-----
    MEgCQQCUCohW62EXu6hIJDOK96CC3pFu+iPWvHnd1BiFZBjWwMUf0sMhUtpqGDbk
    5m1lLIp75ApY+L8yGs5mmmm9qn7FAgMBAAE=
    -----END RSA PUBLIC KEY-----
    AfyXTrtHaimN6YDFCnWqMssRU3QPJjhT77Girl with a Bitcoin Earring100Animecoin Initial Release Crew: The Great Works Collectionhttp://www.anime-coin.comThis work is a reference to astronomers in ancient times, using tools like astrolabes.1293.513523161000000000000000000219c40536fba8f956486e984fd1984fa05cf66a42d5056</td></tr>
    <tr> <th align="left">Combined Image and Metadata Hash:</th> <td>2af9e0030301b3b54b861aee10f1c4f045d23404a05acee051211b74787cd850</td></tr>
    <tr> <th align="left">Artist's Digital Signature on the Combined Image and Metadata Hash (Base64 Encoded):</th> <td>PFd9ayXoabw7e4VdFLuj1KQdmvcgPZJZOA14G2kUI2xPXKJTONUlByK0NpmywFi9xFo1y4ngHsJvdmYkuDGfwQ==</td></tr>
    <tr> <th align="left">Total Number of Unique Copies of Artwork:</th> <td>100</td></tr>
    <tr> <th align="left">Date-Time Artwork was Signed by Artist:</th> <td>2018-05-17 18:37:41:054473</td> </tr>
    <tr> <th align="left">Artist's Name:</th> <td>Arturo Lopez</td> </tr>
    <tr> <th align="left">Artwork Title:</th> <td>Girl with a Bitcoin Earring</td></tr>
    <tr> <th align="left">Artwork Series Name:</th> <td>Animecoin Initial Release Crew: The Great Works Collection</td> </tr>
    <tr> <th align="left">Artist's Website:</th> <td>http://www.anime-coin.com</td> </tr>
    <tr> <th align="left">Artist's Statement about Artwork:</th> <td>This work is a reference to astronomers in ancient times, using tools like astrolabes.</td></tr>
    <tr> <th align="left">Combined Size of Artwork Image Files in Megabytes:</th> <td>37.742208000000005</td></tr>
    <tr> <th align="left">Current Bitcoin Blockchain Block Number:</th> <td>523161</td></tr>
    <tr> <th align="left">Current Bitcoin Blockchain Block Hash:</th> <td>000000000000000000219c40536fba8f956486e984fd1984fa05cf66a42d5056</td></tr>
    <tr> <th align="left">Current ANIME Mining Difficulty Rate:</th> <td>350</td></tr>
    <tr> <th align="left">Average ANIME Mining Difficulty Rate for First 20,000 Blocks:</th> <td>100</td></tr>
    <tr> <th align="left">ANIME Mining Difficulty Adjustment Ratio:</th> <td>3.5</td></tr>
    <tr> <th align="left">Required Registration Fee in ANIME Coins Before Difficulty Adjustment:</th> <td>453</td></tr>
    <tr> <th align="left">Required Registration Fee in ANIME Coins After Difficulty Adjustment:</th> <td>129</td></tr>
    <tr> <th align="left">Forfeitable Deposit in ANIME to Initiate Registration Process:</th> <td>13</td></tr>
    <tr> <th align="left">Registering Masternode Collateral ANIME Blockchain Address:</th> <td>AdaAvFegJbBdH4fB8AiWw8Z4Ek75Xsja6i</td></tr>
    <tr> <th align="left">Registering Masternode Animecoin ID Public Key:</th> <td>-----BEGIN RSA PUBLIC KEY-----
    MEgCQQDJt52qSffb8ps23ZC/dMg0/sOLOk47WlQL20YxhP3hUsR3u4qcArF/YSUz
    ghOIZViQ0B4luUdSrSok3L5iXczPAgMBAAE=
    -----END RSA PUBLIC KEY-----
    </td></tr>
    <tr> <th align="left">Registering Masternode IP Address:</th> <td>10.59.30.116</td></tr>
    <tr> <th align="left">Registering Masternode's Collateral Address Digital Signature on the Combined Image and Metadata Hash (Base64 Encoded):</th> <td>ePDSX40XB6Fi+rZ++sZ05K8mVyNhcu2p8OdNg+IOJNG+7MR4tSwgGv35lo26OCwsxHE3b03KcNdW6jR9IgC97Q==</td></tr>
    <tr> <th align="left">Registering Masternode's Animecoin Public ID Digital Signature on the Combined Image and Metadata Hash (Base64 Encoded):</th> <td>WE+yd1Bbs9SDrSiz43/Heqw70LH7GsynzQU9u6xmgQsHP8AesPgrlPkE4UEyIjLDCnNCx0EcXi61NCVI7fYXxQ==</td></tr>
    <tr> <th align="left">Date-Time Masternode Registered Artwork:</th> <td>2018-05-17 18:37:41:355316</td> </tr>
    <tr> <th align="left">Confirming Masternode 1 Collateral ANIME Blockchain Address:</th> <td>ANIME_CONFIRMING_MN_1_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 1 Animecoin ID Public Key:</th> <td>ANIME_CONFIRMING_MN_1_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Confirming Masternode 1 IP Address:</th> <td>ANIME_CONFIRMING_MN_1_IP_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Collateral Address Digital Signature on the Combined Image and Metadata Hash (Base64 Encoded):</th> <td>ANIME_CONFIRMING_MN_1_COLLATERAL_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 1's Animecoin Public ID Digital Signature on the Combined Image and Metadata Hash (Base64 Encoded):</th> <td>ANIME_CONFIRMING_MN_1_ANIMECOIN_ID_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Date-Time Confirming Masternode 1 Attested to Artwork:</th> <td>ANIME_DATE_TIME_CONFIRMING_MN_1_ATTESTED_TO_ARTWORK</td> </tr>
    <tr> <th align="left">Confirming Masternode 2 Collateral ANIME Blockchain Address:</th> <td>ANIME_CONFIRMING_MN_2_COLLATERAL_BLOCKCHAIN_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 2 Animecoin ID Public Key:</th> <td>ANIME_CONFIRMING_MN_2_ANIMECOIN_ID_PUBLIC_KEY</td></tr>
    <tr> <th align="left">Confirming Masternode 2 IP Address:</th> <td>ANIME_CONFIRMING_MN_2_IP_ADDRESS</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Collateral Address Digital Signature on the Combined Image and Metadata Hash (Base64 Encoded):</th> <td>ANIME_CONFIRMING_MN_2_COLLATERAL_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Confirming Masternode 2's Animecoin Public ID Digital Signature on the Combined Image and Metadata Hash (Base64 Encoded):</th> <td>ANIME_CONFIRMING_MN_2_ANIMECOIN_ID_SIGNATURE_ON_COMBINED_IMAGE_AND_METADATA_HASH</td></tr>
    <tr> <th align="left">Date-Time Confirming Masternode 2 Attested to Artwork:</th> <td>ANIME_DATE_TIME_CONFIRMING_MN_2_ATTESTED_TO_ARTWORK</td> </tr>
    </tbody>
    </table>
    
    <br> <br>
    <div style = "clear:both;"></div>
    <br> <br>
    
    <table align="left" summary="Included Images and their SHA256 Hashes">
    <caption><font size="6" color="blue">Included Images and their SHA256 Hashes:</font></caption>
    <tbody> <tr> <th align="left"><img src="./Arturo_Lopez__Number_02.png" alt="7b8053598a1b19136df077c56a3b37128d6a83a0829225bd5c85b53db7e7d969" style="width:1000px;height:866px;"></th> <td>7b8053598a1b19136df077c56a3b37128d6a83a0829225bd5c85b53db7e7d969</td> </tr>
    
    <tr> <th align="left"><img src="./Arturo_Lopez__Number_02__With_Background.png" alt="e3ff01ba6f4d5630f5da3003f541c765da74cf43134b2e514f36434d6eb209d0" style="width:1000px;height:866px;"></th> <td>e3ff01ba6f4d5630f5da3003f541c765da74cf43134b2e514f36434d6eb209d0</td> </tr>
    
     </tbody>
    </table>
    
    </body>
    </html>
    """
    list_of_animecoin_html_strings_for_templates_and_example_executed_tickets = [artwork_metadata_html_template_string, trade_ticket_html_template_string, treasury_fund_payment_request_voting_ticket_html_template_string, offending_content_takedown_request_voting_ticket_html_template_string]
    list_of_animecoin_html_strings_for_templates_and_example_executed_tickets_byte_encoded = convert_list_of_strings_into_list_of_utf_encoded_byte_objects_func(list_of_animecoin_html_strings_for_templates_and_example_executed_tickets)
    concatenated_list_of_animecoin_html_strings_for_templates_and_example_executed_tickets = concatenate_list_of_strings_func(list_of_animecoin_html_strings_for_templates_and_example_executed_tickets).replace(os.linesep,'')
    concatenated_list_of_animecoin_html_strings_for_templates_and_example_executed_tickets_byte_encoded = concatenated_list_of_animecoin_html_strings_for_templates_and_example_executed_tickets.encode('utf-8')
    return concatenated_list_of_animecoin_html_strings_for_templates_and_example_executed_tickets_byte_encoded, example_executed_artwork_metadata_html_ticket_string