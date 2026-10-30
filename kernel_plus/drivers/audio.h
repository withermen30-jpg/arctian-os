#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

int      audio_init(void);

int      audio_present(void);
int      audio_play_wav(const void *data, uint32_t len);
void     audio_stop(void);
int      audio_is_playing(void);
int      audio_set_volume(int percent);
int      audio_volume(void);
void     audio_service(void);
const char *audio_last_error(void);
uint32_t audio_sample_rate(void);

#endif
