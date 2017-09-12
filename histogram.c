/*

RUN:
    make
    ./f

ADJUST LEVELS:
    alsamixer -c 1
    sudo alsactl store

SPEAKER TEST:
    speaker-test -c2 -D plughw:1,0

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include <alsa/asoundlib.h>
#include "mailbox.h"
#include "gpu_fft.h"

#include "VG/openvg.h"
#include "VG/vgu.h"
#include "fontinfo.h"
#include "shapes.h"

#include <wiringSerial.h>


#define NUM_BINS       26
#define BAR_WIDTH      30
#define BAR_SPACING    5
#define GRAPH_ORIGIN_X 100
#define GRAPH_ORIGIN_Y 100

#define PI 3.1415926535


void serial_send(int fd, char bar_num, int brightness) {
  char header = 255 - bar_num;
  if (brightness < 0) brightness = 0;
  if (brightness > 255-8) brightness = 255-8;
  char value = brightness;
  serialPutchar(fd, header);
  serialPutchar(fd, value);
}

float map(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int main () {

    float bins[NUM_BINS];    // a reduced histogram of the FFT output data
    float lights_unscaled[8];     // floats (unscaled) for each light
    float lights_scaled[8];       // float between 0.0 and 1.0
    float lights_smoothed[8];       // float between 0.0 and 1.0
    int output_levels[8];
    memset(lights_smoothed, 0, sizeof(float) * 8);

    float lights_min[8] = { 10000,  2000,     0,     0,     0,     0,     0,     0};
    float lights_max[8] = { 30000, 25000, 20000, 20000, 17000, 12000, 10000, 15000};

    int auto_adjust = 1;
    if (auto_adjust) {
        for (int i = 0; i < 8; i++) lights_min[i] = 9999999;
        for (int i = 0; i < 8; i++) lights_max[i] = 0;
    }

    // gpu-fft varaibles
    int log2_N = 11;
    int N = 2048;
    int mb = mbox_open();
    struct GPU_FFT *fft;

    // alsa variables
    int buffer_frames = N;
    unsigned int rate = 44100;  // supports 44100, 32000, 11025, 8000, 5512
    char *device = "plughw:1,0";
    int err;
    char *buffer;
    snd_pcm_t *capture_handle;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;

    // build hanning window
    float hanning_window[N];
    for (int n = 0; n < N; n++) {
        hanning_window[n] = 0.5 * (1 - cos(2.0*PI*n/(N-1)));
    }

    // serial setup
    int arduino = serialOpen("/dev/ttyACM0", 115200);

    // /********* SETUP  *********/
    if ((err = snd_pcm_open (&capture_handle, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf (stderr, "cannot open audio device %s (%s)\n", device, snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
        fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n", snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_hw_params_any (capture_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n", snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf (stderr, "cannot set access type (%s)\n", snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_hw_params_set_format (capture_handle, hw_params, format)) < 0) {
        fprintf (stderr, "cannot set sample format (%s)\n", snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &rate, 0)) < 0) {
        fprintf (stderr, "cannot set sample rate (%s)\n", snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_hw_params_set_channels (capture_handle, hw_params, 2)) < 0) {
        fprintf (stderr, "cannot set channel count (%s)\n", snd_strerror (err));
        exit (1);
    }
    if ((err = snd_pcm_hw_params (capture_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot set parameters (%s)\n", snd_strerror (err));
        exit (1);
    }
    snd_pcm_hw_params_free (hw_params);
    if ((err = snd_pcm_prepare (capture_handle)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use (%s)\n", snd_strerror (err));
        exit (1);
    }

    printf("setup complete!\n");




    /********* MAIN  *********/

    buffer = malloc(buffer_frames * snd_pcm_format_width(format) / 8 * 2);

//    for (int iteration = 0; iteration < 10000; iteration++) {
    while(1) {
        snd_pcm_reset(capture_handle);

        // get a buffer's with of audio from the microphone via ALSA
        if ((err = snd_pcm_readi (capture_handle, buffer, buffer_frames)) != buffer_frames) {
            fprintf (stderr, "read from audio interface failed (%s)\n", snd_strerror (err));
            exit (1);
        }

        gpu_fft_prepare(mb, log2_N, GPU_FFT_FWD, 1, &fft);

        // copy audio buffer data into FFT->in
        for (int i = 0; i < N; i++) {
            fft->in[i].re = buffer[i] * hanning_window[i];
            // fft->in[i].re = buffer[i];
            fft->in[i].im = 0;
        }

        // run the fft on the GPU
        gpu_fft_execute(fft);

        // copy first half of FFT output into bars (reduce # of bins)
        for (int i = 0; i < NUM_BINS; i++) {
            float magnitude = sqrt(pow(fft->out[i].re, 2) + pow(fft->out[i].im, 2));
            bins[i] = (magnitude);
        }

        // Videocore memory lost if not freed !
        gpu_fft_release(fft);

        // each light corresponds to three bins. don't use the first two bins.
        lights_unscaled[0] = (bins[2] + bins[3] + bins[4]);
        lights_unscaled[1] = (bins[5] + bins[6] + bins[7]);
        lights_unscaled[2] = (bins[8] + bins[9] + bins[10]);
        lights_unscaled[3] = (bins[11] + bins[12] + bins[13]);
        lights_unscaled[4] = (bins[14] + bins[15] + bins[16]);
        lights_unscaled[5] = (bins[17] + bins[18] + bins[19]);
        lights_unscaled[6] = (bins[20] + bins[21] + bins[22]);
        lights_unscaled[7] = (bins[23] + bins[24] + bins[25]);

        // update max/mins
        if (auto_adjust) {
            for (int i = 0; i < 8; i++) {
                // if (lights_unscaled[i] < lights_min[i] || lights_unscaled[i] > lights_max[i]) {
                //     for (int i = 0; i < 8; i++) {
                //         fprintf (stdout, "[%7.f %7.f] ", lights_min[i], lights_max[i]);
                //     }
                //     fprintf(stdout, "\n");
                // }
                lights_min[i] = fmin(lights_min[i], lights_unscaled[i]);
                lights_max[i] = fmax(lights_max[i], lights_unscaled[i]);
            }
        }

        // slowly restore max/mins over time
        for (int i = 0; i < 8; i++) {
                float min = lights_min[i];
                float max = lights_max[i];

                min = min + 0.00001 * (max - min);
                max = max - 0.0001 * (max - min);

                lights_min[i] = min;
                lights_max[i] = max;
            }

        // scale light values to [0...1] range
        for (int i = 0; i < 8; i++) {
            float scaled_val = map(lights_unscaled[i], lights_min[i], lights_max[i], -0.2, 1.5);
            float clipped_val = fmax(fmin((scaled_val), 1.0), 0.0);
            lights_scaled[i] = clipped_val;
        }

        for (int i = 0; i < 8; i++) {
            if (lights_scaled[i] > lights_smoothed[i]) {
                lights_smoothed[i] = 0.5 * lights_smoothed[i] + 0.5 * lights_scaled[i];
            } else {
                lights_smoothed[i] = 0.97 * lights_smoothed[i] + 0.03 * lights_scaled[i];
            }
        }

        // for each light, calculate the output value
        for (int i = 0; i < 8; i++) {
            output_levels[i] = lights_smoothed[i] * 255;
        }

        // send each output level to the arduino
        for (int i = 0; i < 8; i++) {
            serial_send(arduino, i, output_levels[i]);
        }

    }





    /********* FINISH *********/


    finish();  // Graphics cleanup

    serialClose(arduino) ;

    free(buffer);
    snd_pcm_close (capture_handle);
    fprintf(stdout, "audio interface closed\n");
    exit (0);

}
