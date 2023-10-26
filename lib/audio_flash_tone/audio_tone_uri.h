#ifndef __AUDIO_TONEURI_H__
#define __AUDIO_TONEURI_H__

extern const char* tone_uri[];

typedef enum {
    TONE_TYPE_ALERTA1,
    TONE_TYPE_CALL_TERMINATED,
    TONE_TYPE_CALLING,
    TONE_TYPE_NO_ANSWER,
    TONE_TYPE_RING,
    TONE_TYPE_MAX,
} tone_type_t;

int get_tone_uri_num();

#endif
