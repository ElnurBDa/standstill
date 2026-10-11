// Procedural audio for STANDSTILL — all sound synthesized from math, no files.
#ifndef STANDSTILL_AUDIO_H
#define STANDSTILL_AUDIO_H
int  audio_init(void);
void audio_shutdown(void);
void audio_shoot(void);
void audio_hit(void);
void audio_death(void);
void audio_whoosh(void);        // time-freeze / unfreeze swell
void audio_set_time(float t);   // 0 = frozen .. 1 = flowing (modulates ambient drone)
#endif
