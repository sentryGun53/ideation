S = hex/shader_256.hex \
    hex/shader_512.hex \
    hex/shader_1k.hex \
    hex/shader_2k.hex \
    hex/shader_4k.hex \
    hex/shader_8k.hex \
    hex/shader_16k.hex \
    hex/shader_32k.hex \
    hex/shader_64k.hex \
    hex/shader_128k.hex \
    hex/shader_256k.hex \
    hex/shader_512k.hex \
    hex/shader_1024k.hex \
    hex/shader_2048k.hex \
    hex/shader_4096k.hex

C = mailbox.c gpu_fft.c gpu_fft_base.c gpu_fft_twiddles.c gpu_fft_shaders.c histogram.c

H = gpu_fft.h mailbox.h

F = -lshapes -lrt -lm -ldl -lasound -lwiringPi -I/opt/vc/include -I.


histogram_make: $(S) $(C) $(H)
	gcc -Wall -std=gnu99 -o h $(F) $(C)


clean:
	rm -f *.bin

