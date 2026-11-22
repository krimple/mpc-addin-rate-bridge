/* driver: opens a PCM name the way MPC does (sequence recorded on a Key 61 with mpc-vst-plugins poc/alsatrace) and prints
 * one line per step. test_host.sh runs it with and without the addin preloaded.
 * Usage: driver <pcm name> <playback|capture> */
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    int stream = !strcmp(argv[2], "capture") ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;
    snd_pcm_t *p;
    int r = snd_pcm_open(&p, argv[1], stream, 0);
    printf("open %d\n", r);
    if (r < 0) return 0;
    snd_pcm_hw_params_t *hw; snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(p, hw);
    printf("access %d\n", snd_pcm_hw_params_set_access(p, hw, SND_PCM_ACCESS_RW_INTERLEAVED));
    printf("format %d\n", snd_pcm_hw_params_set_format(p, hw, SND_PCM_FORMAT_S32_LE));
    printf("rate44100 %d\n", snd_pcm_hw_params_set_rate(p, hw, 44100, 0));
    unsigned ch = 32; snd_pcm_hw_params_set_channels_near(p, hw, &ch);
    snd_pcm_uframes_t per = 192; int dir = 0; snd_pcm_hw_params_set_period_size_near(p, hw, &per, &dir);
    unsigned periods = stream ? 2 : 3; snd_pcm_hw_params_set_periods_near(p, hw, &periods, &dir);
    r = snd_pcm_hw_params(p, hw);
    printf("hw_params %d\n", r);
    if (r == 0) {
        unsigned rate = 0; snd_pcm_hw_params_get_rate(hw, &rate, &dir);
        printf("rate %u\n", rate);
        printf("prepare %d\n", snd_pcm_prepare(p));
        static int32_t buf[192 * 64];
        long n = stream ? snd_pcm_readi(p, buf, 192) : snd_pcm_writei(p, buf, 192);
        printf("io %ld\n", n);
    }
    snd_pcm_close(p);
    return 0;
}
