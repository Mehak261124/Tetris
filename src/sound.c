/* =============================================================================
 * sound.c  —  Sound Effects / Audio Output Module
 * =============================================================================
 * OS MODULE: I/O Management (Audio output)
 *   Generates square-wave WAV files and plays them via macOS afplay.
 *   All waveform maths use integer arithmetic — no <math.h>.
 *
 * RULES COMPLIANCE:
 *   - <stdio.h>  : allowed (file I/O for WAV generation)
 *   - <unistd.h>, <signal.h> : Hardware Abstraction Exception
 *   - No <math.h>, no <string.h>, no malloc
 * =============================================================================
 */

#include "../include/sound.h"
#include <signal.h>   /* kill() — stop background music process            */
#include <stdio.h>    /* FILE, fopen, fwrite, fclose — WAV file generation */
#include <sys/wait.h> /* waitpid WNOHANG — reap zombie child processes    */
#include <unistd.h>   /* fork(), execvp(), _exit() — audio playback        */

/* ---------------------------------------------------------------------------
 */
/* WAV file paths (generated at init) */
/* ---------------------------------------------------------------------------
 */
static const char *wav_paths[SND_COUNT] = {
    "/tmp/tetris_snd_move.wav",     "/tmp/tetris_snd_rotate.wav",
    "/tmp/tetris_snd_drop.wav",     "/tmp/tetris_snd_clear.wav",
    "/tmp/tetris_snd_gameover.wav", "/tmp/tetris_snd_music.wav",
};

static int music_pid = -1; /* PID of background music child process */

/* ---------------------------------------------------------------------------
 */
/* WAV generation helpers — all integer math, no <math.h> */
/* ---------------------------------------------------------------------------
 */

/* Write a 16-bit little-endian value */
static void write_u16(FILE *f, unsigned int v) {
  unsigned char b[2];
  b[0] = (unsigned char)(v & 0xFF);
  b[1] = (unsigned char)((v >> 8) & 0xFF);
  fwrite(b, 1, 2, f);
}

/* Write a 32-bit little-endian value */
static void write_u32(FILE *f, unsigned int v) {
  unsigned char b[4];
  b[0] = (unsigned char)(v & 0xFF);
  b[1] = (unsigned char)((v >> 8) & 0xFF);
  b[2] = (unsigned char)((v >> 16) & 0xFF);
  b[3] = (unsigned char)((v >> 24) & 0xFF);
  fwrite(b, 1, 4, f);
}

/* Write RIFF WAV header for mono 16-bit PCM */
static void write_wav_header(FILE *f, int sample_rate, int num_samples) {
  int data_size = num_samples * 2; /* 16-bit = 2 bytes per sample */
  int file_size = 36 + data_size;

  /* RIFF header */
  fwrite("RIFF", 1, 4, f);
  write_u32(f, (unsigned int)file_size);
  fwrite("WAVE", 1, 4, f);

  /* fmt sub-chunk */
  fwrite("fmt ", 1, 4, f);
  write_u32(f, 16);                              /* sub-chunk size   */
  write_u16(f, 1);                               /* PCM format       */
  write_u16(f, 1);                               /* mono             */
  write_u32(f, (unsigned int)sample_rate);       /* sample rate      */
  write_u32(f, (unsigned int)(sample_rate * 2)); /* byte rate        */
  write_u16(f, 2);                               /* block align      */
  write_u16(f, 16);                              /* bits per sample  */

  /* data sub-chunk */
  fwrite("data", 1, 4, f);
  write_u32(f, (unsigned int)data_size);
}

/* Write a 16-bit sample (little-endian signed) */
static void write_sample(FILE *f, int value) {
  /* Clamp to 16-bit range */
  if (value > 32767)
    value = 32767;
  if (value < -32767)
    value = -32767;
  unsigned int uv = (unsigned int)(value & 0xFFFF);
  unsigned char b[2];
  b[0] = (unsigned char)(uv & 0xFF);
  b[1] = (unsigned char)((uv >> 8) & 0xFF);
  fwrite(b, 1, 2, f);
}

/* Generate a square wave tone
 * freq_hz: frequency in Hz
 * duration_ms: duration in milliseconds
 * volume: amplitude (0-32767)
 * sample_rate: samples per second
 */
static void gen_square(FILE *f, int freq_hz, int duration_ms, int volume,
                       int sample_rate) {
  int total_samples = (sample_rate * duration_ms) / 1000;
  int half_period = sample_rate / (2 * freq_hz);
  if (half_period < 1)
    half_period = 1;

  int i;
  for (i = 0; i < total_samples; i++) {
    int pos_in_period = i % (half_period * 2);
    int val = (pos_in_period < half_period) ? volume : -volume;

    /* Apply a simple fade-out envelope in the last 20% */
    int fade_start = total_samples - (total_samples / 5);
    if (i > fade_start) {
      int remaining = total_samples - i;
      int fade_len = total_samples - fade_start;
      val = (val * remaining) / fade_len;
    }
    write_sample(f, val);
  }
}

/* Generate a frequency sweep (for clear/gameover sounds) */
static void gen_sweep(FILE *f, int freq_start, int freq_end, int duration_ms,
                      int volume, int sample_rate) {
  int total_samples = (sample_rate * duration_ms) / 1000;
  int i;
  int phase_acc = 0;

  for (i = 0; i < total_samples; i++) {
    /* Linear interpolation of frequency */
    int freq = freq_start + ((freq_end - freq_start) * i) / total_samples;
    int half_period = sample_rate / (2 * freq);
    if (half_period < 1)
      half_period = 1;

    int val = (phase_acc < half_period) ? volume : -volume;
    phase_acc++;
    if (phase_acc >= half_period * 2)
      phase_acc = 0;

    /* Fade out in last 25% */
    int fade_start = total_samples - (total_samples / 4);
    if (i > fade_start) {
      int remaining = total_samples - i;
      int fade_len = total_samples - fade_start;
      val = (val * remaining) / fade_len;
    }
    write_sample(f, val);
  }
}

/* ---------------------------------------------------------------------------
 */
/* Tetris melody — note frequencies (integer Hz, no math.h) */
/* Classic "Korobeiniki" simplified into square-wave notes */
/* ---------------------------------------------------------------------------
 */

/* Note: durations in ms, frequencies in Hz */
typedef struct {
  int freq;
  int dur;
} Note;

/* Simplified Tetris theme (Korobeiniki) — fits in a short loop */
static const Note melody[] = {
    /* Bar 1 */
    {659, 200},
    {494, 100},
    {523, 100},
    {587, 200},
    {523, 100},
    {494, 100},
    /* Bar 2 */
    {440, 200},
    {440, 100},
    {523, 100},
    {659, 200},
    {587, 100},
    {523, 100},
    /* Bar 3 */
    {494, 200},
    {494, 100},
    {523, 100},
    {587, 200},
    {659, 200},
    /* Bar 4 */
    {523, 200},
    {440, 200},
    {440, 200},
    {0, 100},
    /* Bar 5 */
    {0, 50},
    {587, 200},
    {698, 100},
    {880, 200},
    {784, 100},
    {698, 100},
    /* Bar 6 */
    {659, 200},
    {523, 100},
    {659, 200},
    {587, 100},
    {523, 100},
    /* Bar 7 */
    {494, 200},
    {494, 100},
    {523, 100},
    {587, 200},
    {659, 200},
    /* Bar 8 */
    {523, 200},
    {440, 200},
    {440, 200},
    {0, 200},
};
#define MELODY_LEN ((int)(sizeof(melody) / sizeof(melody[0])))

/* ---------------------------------------------------------------------------
 */
/* sound_init — generate all WAV files */
/* ---------------------------------------------------------------------------
 */
void sound_init(void) {
  FILE *f;
  int sample_rate = 22050;

  /* SND_MOVE: short low click */
  f = fopen(wav_paths[SND_MOVE], "wb");
  if (f) {
    int samples = (sample_rate * 30) / 1000;
    write_wav_header(f, sample_rate, samples);
    gen_square(f, 200, 30, 8000, sample_rate);
    fclose(f);
  }

  /* SND_ROTATE: higher pitch click */
  f = fopen(wav_paths[SND_ROTATE], "wb");
  if (f) {
    int samples = (sample_rate * 50) / 1000;
    write_wav_header(f, sample_rate, samples);
    gen_square(f, 400, 50, 8000, sample_rate);
    fclose(f);
  }

  /* SND_DROP: low thud */
  f = fopen(wav_paths[SND_DROP], "wb");
  if (f) {
    int samples = (sample_rate * 80) / 1000;
    write_wav_header(f, sample_rate, samples);
    gen_square(f, 120, 80, 12000, sample_rate);
    fclose(f);
  }

  /* SND_CLEAR: upward sweep */
  f = fopen(wav_paths[SND_CLEAR], "wb");
  if (f) {
    int samples = (sample_rate * 250) / 1000;
    write_wav_header(f, sample_rate, samples);
    gen_sweep(f, 400, 900, 250, 10000, sample_rate);
    fclose(f);
  }

  /* SND_GAMEOVER: descending sweep */
  f = fopen(wav_paths[SND_GAMEOVER], "wb");
  if (f) {
    int samples = (sample_rate * 600) / 1000;
    write_wav_header(f, sample_rate, samples);
    gen_sweep(f, 500, 80, 600, 12000, sample_rate);
    fclose(f);
  }

  /* SND_MUSIC: Tetris theme melody */
  f = fopen(wav_paths[SND_MUSIC], "wb");
  if (f) {
    /* Calculate total samples for 2 repeats of the melody */
    int total_ms = 0;
    int i, rep;
    for (i = 0; i < MELODY_LEN; i++)
      total_ms += melody[i].dur;
    total_ms *= 2; /* 2 repeats */
    int total_samples = (sample_rate * total_ms) / 1000;

    write_wav_header(f, sample_rate, total_samples);

    for (rep = 0; rep < 2; rep++) {
      for (i = 0; i < MELODY_LEN; i++) {
        if (melody[i].freq == 0) {
          /* Rest: silence */
          int samp = (sample_rate * melody[i].dur) / 1000;
          int s;
          for (s = 0; s < samp; s++)
            write_sample(f, 0);
        } else {
          gen_square(f, melody[i].freq, melody[i].dur, 6000, sample_rate);
        }
      }
    }
    fclose(f);
  }
}

/* ---------------------------------------------------------------------------
 */
/* sound_play — fork + afplay for one-shot sound */
/* ---------------------------------------------------------------------------
 */
void sound_play(int type) {
  if (type < 0 || type >= SND_COUNT)
    return;
  if (type == SND_MUSIC)
    return; /* use sound_music_start instead */

  /* Reap any finished child processes to avoid zombies */
  while (waitpid(-1, NULL, WNOHANG) > 0) {
  }

  int pid = fork();
  if (pid == 0) {
    /* Child process */
    char *args[] = {"afplay", (char *)wav_paths[type], NULL};
    /* Redirect stdout/stderr to /dev/null */
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    execvp("afplay", args);
    _exit(1); /* exec failed */
  }
  /* Parent: don't wait — fire and forget */
}

/* ---------------------------------------------------------------------------
 */
/* sound_music_start / stop — looping background music */
/* ---------------------------------------------------------------------------
 */
void sound_music_start(void) {
  if (music_pid > 0)
    return; /* already playing */

  int pid = fork();
  if (pid == 0) {
    /* Child: loop afplay forever */
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    while (1) {
      int cpid = fork();
      if (cpid == 0) {
        char *args[] = {"afplay", (char *)wav_paths[SND_MUSIC], NULL};
        execvp("afplay", args);
        _exit(1);
      }
      int status;
      waitpid(cpid, &status, 0);
    }
    _exit(0);
  }
  music_pid = pid;
}

void sound_music_stop(void) {
  if (music_pid > 0) {
    kill(music_pid, SIGTERM);
    /* Also kill any grandchild afplay processes in the group */
    kill(music_pid, SIGKILL);
    waitpid(music_pid, NULL, 0);
    music_pid = -1;
  }
}

/* ---------------------------------------------------------------------------
 */
/* sound_cleanup — remove temp files, stop music */
/* ---------------------------------------------------------------------------
 */
void sound_cleanup(void) {
  sound_music_stop();

  /* Reap all child processes */
  while (waitpid(-1, NULL, WNOHANG) > 0) {
  }

  /* Remove temp WAV files */
  int i;
  for (i = 0; i < SND_COUNT; i++) {
    unlink(wav_paths[i]);
  }
}
