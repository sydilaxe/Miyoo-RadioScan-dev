/* Background internet-radio player: HTTP(S)/ICY -> MP3 decode -> ring buffer -> resampled float output.
 * Each Stream is independent (own thread, decoder and buffer), so two can run at once. */
#ifndef STREAM_H
#define STREAM_H

typedef enum { ST_IDLE, ST_CONNECTING, ST_BUFFERING, ST_PLAYING, ST_ERROR } StreamState;

typedef struct Stream Stream;

Stream *stream_create(int out_rate);
void stream_destroy(Stream *s);
void stream_play(Stream *s, const char *url);
void stream_stop(Stream *s);

/* Audio thread: fill `frames` interleaved stereo floats. Always fills (silence when starved). */
void stream_read(Stream *s, float *out, int frames);

StreamState stream_state(Stream *s);
void stream_status(Stream *s, char *buf, int n);   /* human-readable status line */
void stream_title(Stream *s, char *buf, int n);    /* ICY StreamTitle (now playing) */
int stream_src_rate(Stream *s);
int stream_bitrate(Stream *s);
float stream_buffer_seconds(Stream *s);

#endif
