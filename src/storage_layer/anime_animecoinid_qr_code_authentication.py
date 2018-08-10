import io, base64, math, subprocess, time
import pyqrcode
from anime_animecoin_id_self_contained_demo import animecoin_id_keypair_generation_func
from PIL import Image, ImageFont, ImageDraw, ImageOps
import pyzbar.pyzbar as pyzbar
import cv2
import imageio
import moviepy.editor as mp

#Requirements: OpenCV 
#mac: brew install opencv
#linux: sudo apt-get install python-opencv
# pip install imageio moviepy opencv-python pyzbar pyqrcode Pillow


def generate_qr_code_from_animecoin_id_privkey_func(animecoin_id_private_key_b16_encoded):
    assert(isinstance(animecoin_id_private_key_b16_encoded , str))
    assert(len(animecoin_id_private_key_b16_encoded)==2084)
    key_character_set = 'ABCDEF1234567890'
    assert([(x in key_character_set) for x in animecoin_id_private_key_b16_encoded])
    print('Generating Video of QR Codes now...')
    number_of_key_chunks = 25
    key_chunk_size = int(math.ceil(2084/number_of_key_chunks))
    key_chunks = [str(int(math.ceil(ii/key_chunk_size)+1)) + ' ' + str(number_of_key_chunks) + ' ' + animecoin_id_private_key_b16_encoded[ii:ii+key_chunk_size] for ii in range(0, len(animecoin_id_private_key_b16_encoded), key_chunk_size)]
    key_chunks_duplicated = [key_chunks[0]]*10 + key_chunks*5
    qr_error_correcting_level = 'L' # L, M, Q, H
    qr_encoding_type = 'alphanumeric'
    qr_scale_factor = 12
    large_font_size = 18
    start_x = 50
    list_of_qr_code_pil_objects = list()
    for cnt, current_key_chunk in enumerate(key_chunks_duplicated):
        current_key_chunk_qr_code = pyqrcode.create(current_key_chunk, error=qr_error_correcting_level, version=4, mode=qr_encoding_type)
        current_key_chunk_qr_code_png_string = current_key_chunk_qr_code.png_as_base64_str(scale=qr_scale_factor)
        current_key_chunk_qr_code_png_data = base64.b64decode(current_key_chunk_qr_code_png_string)
        pil_qr_code_image = Image.open(io.BytesIO(current_key_chunk_qr_code_png_data))
        img_width, img_height = pil_qr_code_image.size #getting the base image's size
        if pil_qr_code_image.mode != 'RGB':
            pil_qr_code_image = pil_qr_code_image.convert("RGB")
        pil_qr_code_image = ImageOps.expand(pil_qr_code_image, border=(500,150,500,0))
        drawing_context = ImageDraw.Draw(pil_qr_code_image)
        large_font = ImageFont.truetype('FreeSans.ttf', large_font_size)
        larger_font = ImageFont.truetype('FreeSans.ttf', large_font_size*2)
        warning_message_1 = "Warning! Once you close this window,  these QR codes will be lost! The video of QR codes below represents your Animecoin ID Private Key-- make sure it stays secret!"
        warning_message_2 = "Since your smartphone is likely more secure than your computer, we suggest saving the video to your phone using Google Drive, Dropbox, or iCloud."
        warning_message_3 = "This assumes you have secured your phone using 2-factor authentication on iCloud, Dropbox, Google Photos, etc. If not, do this first!"
        warning_message_4 = "Then, you will be able to unlock your Animecoin wallet by holding up your phone to your computer's web cam while the video plays on your phone, which is convenient and secure."
        current_chunk_number = int(current_key_chunk.split(' ')[0])
        label_message = "QR CODE " + str(current_chunk_number) + ' of ' + str(number_of_key_chunks)
        drawing_context.text((start_x, 1*large_font_size*1.5), warning_message_1,(255,255,255), font=large_font) 
        drawing_context.text((start_x, 2*large_font_size*1.5), warning_message_2,(255,255,255), font=large_font) 
        drawing_context.text((start_x, 3*large_font_size*1.5), warning_message_3,(255,255,255), font=large_font) 
        drawing_context.text((start_x, 4*large_font_size*1.5), warning_message_4,(255,255,255), font=large_font) 
        drawing_context.text((start_x, 5*large_font_size*3), label_message,(255,255,255), font=larger_font)
        output = io.BytesIO()
        pil_qr_code_image.save(output, format='GIF')
        hex_data = output.getvalue()
        list_of_qr_code_pil_objects.append(imageio.imread(hex_data))
        if cnt%3==0:
            print(str(round(100*cnt/len(key_chunks_duplicated),1))+'% complete')
    output_filename_string = 'animated_qr_code_privkey'
    print('Generating output video...')
    imageio.mimsave(output_filename_string+'.gif', list_of_qr_code_pil_objects, duration=0.13)
    clip = mp.VideoFileClip(output_filename_string+'.gif')
    clip.write_videofile(output_filename_string+'.mp4')
    print('Done!')
    time.sleep(2)
    subprocess.call(['open', output_filename_string+'.mp4'])

def check_if_parsed_data_is_complete_func(list_of_decoded_data):
    list_of_successful_chunks = list()
    list_of_successful_chunk_numbers = list()
    for current_data in list_of_decoded_data:
        current_data_split = current_data.split(' ')
        current_chunk_number = int(current_data_split[0])
        total_number_of_chunks = int(current_data_split[1])
        current_chunk_values = current_data_split[-1]
        if current_chunk_values not in list_of_successful_chunks:
            list_of_successful_chunks.append(current_chunk_values)
            list_of_successful_chunk_numbers.append(current_chunk_number)
    sort_indices = [i[0] for i in sorted(enumerate(list_of_successful_chunk_numbers), key=lambda x:x[1])]
    list_of_successful_chunks = [list_of_successful_chunks[i] for i in sort_indices]
    list_of_successful_chunk_numbers = [list_of_successful_chunk_numbers[i] for i in sort_indices]
    if len(list_of_successful_chunks) == total_number_of_chunks:
        print('Data Complete!')
        complete = 1
    else:
        complete = 0
    return complete, list_of_successful_chunks, list_of_successful_chunk_numbers
    
def get_webcam_qr_code_data_func():
    capture = cv2.VideoCapture(0)
    list_of_decoded_data = list()
    remaining_chunks = 1
    print("Now waiting to scan QR code video! Start playing the video on your phone and hold the phone screen close to your computer's webcam!")
    while remaining_chunks > 0:
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break
        ret, frame = capture.read()
        cv2.namedWindow('ImageWindowName', cv2.WINDOW_NORMAL)
        cv2.imshow('Current', frame)
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        im = Image.fromarray(gray)
        decodedObjects = pyzbar.decode(im)
        for obj in decodedObjects:
            data_value = obj.data.decode('utf-8')
            if data_value not in list_of_decoded_data:
                number_of_key_chunks = int(data_value.split(' ')[1])
                print('Data : ' + str(data_value) + '\n')
                list_of_decoded_data.append(data_value)
            remaining_chunks = number_of_key_chunks - len(list_of_decoded_data)
            print(str(remaining_chunks) +' chunks remaining!')
        if len(list_of_decoded_data) > 0:
            complete, list_of_successful_chunks, list_of_successful_chunk_numbers = check_if_parsed_data_is_complete_func(list_of_decoded_data)
    reconstructed_key_string = ''.join(list_of_successful_chunks)
    assert(len(reconstructed_key_string)==2084)
    print('Successfully reconstructed key!')
    return reconstructed_key_string
        

use_demonstrate_qr_key_video_generation = 0
if use_demonstrate_qr_key_video_generation:
    animecoin_id_private_key_b16_encoded, animecoin_id_public_key_b16_encoded = animecoin_id_keypair_generation_func()
    print('Animecoin ID Private Key:\n' + animecoin_id_private_key_b16_encoded)
    generate_qr_code_from_animecoin_id_privkey_func(animecoin_id_private_key_b16_encoded)


use_demonstrate_qr_key_recognition = 1
if use_demonstrate_qr_key_recognition:
    reconstructed_key_string = get_webcam_qr_code_data_func()
    
    if 0:
        assert(reconstructed_key_string==animecoin_id_private_key_b16_encoded)
    
    
    
        
