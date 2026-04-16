#ifndef SOUND_H
#define SOUND_H

/* =============================================================================
 * sound.h  —  Sound Effects / Audio Output Module
 * =============================================================================
 * OS MODULE: I/O Management (Audio output)
 *   Generates square-wave WAV files in /tmp and plays them via afplay (macOS).
 *   No <math.h> — only integer arithmetic for waveform generation.
 *
 * RULES COMPLIANCE:
 *   - <stdio.h>  : allowed (file I/O for WAV generation)
 *   - <unistd.h> : Hardware Abstraction Exception (fork/exec for playback)
 *   - No <math.h>, no <string.h>
 * =============================================================================
 */

/* Sound type constants */
#define SND_MOVE 0
#define SND_ROTATE 1
#define SND_DROP 2
#define SND_CLEAR 3
#define SND_GAMEOVER 4
#define SND_MUSIC 5
#define SND_COUNT 6

/* Lifecycle */
void sound_init(void);    /* generate WAV files in /tmp        */
void sound_cleanup(void); /* remove temp files, kill processes */

/* Playback */
void sound_play(int type);    /* play a one-shot sound effect      */
void sound_music_start(void); /* start looping background music    */
void sound_music_stop(void);  /* stop background music             */

#endif /* SOUND_H */
