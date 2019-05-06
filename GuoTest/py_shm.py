 #!s script dumps the content of a shared memory block  
 # used by Linux/Cdorked.A into a file named httpd_cdorked_config.bin  
 # when the machine is infected.  
 #  
 # Some of the data is encrypted. If your server is infected and you  
 # would like to help, please send the httpd_cdorked_config.bin  
 # to our lab for analysis. Thanks!  
 #  
 # Marc-Etienne M.Léveillé <leve...@eset.com>  
 #  
   
from ctypes import *  
   
SHM_SIZE = 1920*1080*3*16+32 
SHM_KEY = 67483  
   
OUTFILE="httpd_cdorked_config.bin"  
   
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
     
shmid = shmget(SHM_KEY, SHM_SIZE, 0o666)  
if shmid < 0:  
    print ("System not infected")  
else:   
    addr = shmat(shmid, None, 0)
    #memset(addr+2, 0x0, 4)
    #a=28
    #cop = c_int(a)
    #memmove(addr+2,byref(cop), 4)
    #f = file(OUTFILE, 'wb')  
    f=open(OUTFILE, 'wb')  
    f.write(string_at(addr,SHM_SIZE)) 
    print(1+int.from_bytes(string_at(addr + 2, 4), byteorder='little', signed=True))
    f.close()  
    print(addr, type(addr))
print ("Dumped %d bytes in %s" % (SHM_SIZE, OUTFILE))  
