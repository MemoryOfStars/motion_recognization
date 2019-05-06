import cv2
import os
import _thread
from ctypes import *
import numpy as np
import struct
import sys
from sys import platform

dir_path = os.path.dirname(os.path.realpath(__file__))
sys.path.append('../../../python');
from openpose import pyopenpose as op


YUV_SIZE     = int(1920*1080*3/2)
RGB_SIZE     = 1920*1080*4
SHM_LEN      = 8
SHM_SIZE_YUV = YUV_SIZE*SHM_LEN + 32
SHM_SIZE_RGB = RGB_SIZE*SHM_LEN + 32
SHM_KEY_YUV  = 67483
SHM_KEY_RGB  = 67484

try:  
    rt = CDLL('librt.so')  
except:  
    rt = CDLL('librt.so.1')  

shmget = rt.shmget  
shmget.argtypes = [c_int, c_size_t, c_int]  
shmget.restype = c_int  
shmat = rt.shmat  
shmat.argtypes = [c_int, POINTER(c_void_p), c_int]  
shmat.restype = c_void_p


def enqueue(rgbData):
    shmid = shmget(SHM_KEY_RGB, c_size_t(SHM_SIZE_RGB), 0o666)  
    if shmid < 0:  
        print ("System not infected")
        return -1
    else:   
        addr = shmat(shmid, None, 0)
    head = int.from_bytes(string_at(addr, 4), byteorder='little', signed=True)
    tail = int.from_bytes(string_at(addr + 16, 4), byteorder='little', signed=True)
    
    free_entries = (SHM_LEN - 1 + tail - head)%SHM_LEN
    if free_entries < 1:
        print('RGB ring fulfilled')
        return -1
    memmove(addr + 32 + head*RGB_SIZE, rgbData, RGB_SIZE)
    
    head = (head + 1)%SHM_LEN
    memmove(addr, byref(c_int(head)), 4)
    return 0

def dequeue():
    shmid = shmget(SHM_KEY_YUV, c_size_t(SHM_SIZE_YUV), 0o666)  
    if shmid < 0:  
        print ("System not infected")
        return None
    else:   
        addr = shmat(shmid, None, 0)    
    head = int.from_bytes(string_at(addr, 4), byteorder='little', signed=True)
    tail = int.from_bytes(string_at(addr + 16, 4), byteorder='little', signed=True)
    
    if head == tail:
        print('ring empty')
        return None
    result = create_string_buffer(YUV_SIZE)
    memmove(result, string_at(addr + 32 + tail*YUV_SIZE, YUV_SIZE), YUV_SIZE)
    
    tail = (tail+1)%SHM_LEN
    memmove(addr + 16, byref(c_int(tail)), 4)
    return result

def analyze():
    params = dict()
    params["model_folder"] = "../../../../models/"


    opWrapper = op.WrapperPython()
    opWrapper.configure(params)
    opWrapper.start()
    datum = op.Datum()
    
    while True:
        yuv_data_raw = dequeue()
        while yuv_data_raw == None:
            yuv_data_raw = dequeue()
        yuv_data = struct.unpack('c'*1620*1920, yuv_data_raw)
        yuv_img = np.array(list(map(ord, yuv_data)), dtype=np.uint8).reshape(1620,1920)
        print(str(yuv_img.shape))
        
        imageToProcess = cv2.cvtColor(yuv_img, cv2.COLOR_YUV2RGB_I420)
        print('cvt to rgb end')
        datum.cvInputData = imageToProcess
        print('start analyze')
        opWrapper.emplaceAndPop([datum])
        print('analyze end')
        rgba_data = cv2.cvtColor(datum.cvOutputData, cv2.COLOR_RGB2RGBA)
        print('cvt to rgba end')
        rgba_bin = rgba_data.reshape(1920*1080*4).ctypes.data
        print('start enqueue')
        while enqueue(rgba_bin) == -1:
            pass


if __name__ == '__main__':

    _thread.start_new_thread(analyze, ())
    while True:
        pass
