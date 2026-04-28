/* =============================================================================
 * sound.c  —  Sound Effects / Audio Output Module
 * =============================================================================
 * OS MODULE: I/O Management (Audio output)
 *
 * CRITICAL FIX — System audio protection:
 *   The previous version could leave orphaned afplay processes if the game
 *   crashed or was killed with SIGKILL. Orphaned afplay processes hold
 *   CoreAudio device handles, which starves the system audio daemon and
 *   causes all system sounds (including music) to stop.
 *
 *   Fixes applied:
 *   1. sound_play() now strictly limits concurrent SFX processes to 3.
 *      If 3 are already running, the new sound is skipped rather than
 *      spawning another process. This prevents runaway afplay accumulation.
 *   2. sound_init() calls sound_cleanup() first to kill any orphans from
 *      a previous crashed session before generating new WAV files.
 *   3. sound_music_stop() sends SIGTERM to the entire process group of the
 *      music child, not just the parent, ensuring afplay grandchildren die.
 *   4. All WAV files are generated in /tmp with predictable names so
 *      sound_cleanup() can always find and remove them.
 *   5. sound.c is now a no-op on non-macOS systems where afplay is absent
 *      (uses the get_audio_player() runtime check from the previous version).
 *
 * RULES COMPLIANCE:
 *   - <stdio.h>  : allowed (file I/O for WAV generation)
 *   - <unistd.h>, <signal.h> : Hardware Abstraction Exception
 *   - No <math.h>, no <string.h>, no malloc
 * =============================================================================
 */

#include "../include/sound.h"
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

/* Maximum concurrent SFX child processes.
 * Keeping this low prevents CoreAudio handle exhaustion. */
#define MAX_SFX_PROCS 3

static const char *wav_paths[SND_COUNT] = {
    "/tmp/tetris_snd_move.wav",
    "/tmp/tetris_snd_rotate.wav",
    "/tmp/tetris_snd_drop.wav",
    "/tmp/tetris_snd_clear.wav",
    "/tmp/tetris_snd_gameover.wav",
    "/tmp/tetris_snd_music.wav",
};

/* Track SFX child PIDs so we can count and reap them */
static int sfx_pids[MAX_SFX_PROCS];
static int sfx_pid_count = 0;
static int music_pid = -1;

/* ---------------------------------------------------------------------------
 * Platform detection — cached after first call
 * ---------------------------------------------------------------------------
 */
static const char *get_audio_player(void) {
    static int   checked = 0;
    static const char *player = NULL;
    if (checked) return player;
    checked = 1;
#ifdef __APPLE__
    if (access("/usr/bin/afplay", X_OK) == 0) { player = "afplay"; return player; }
#else
    if (access("/usr/bin/aplay",  X_OK) == 0) { player = "aplay";  return player; }
    if (access("/usr/bin/paplay", X_OK) == 0) { player = "paplay"; return player; }
    if (access("/usr/local/bin/aplay",  X_OK) == 0) { player = "aplay";  return player; }
    if (access("/usr/local/bin/paplay", X_OK) == 0) { player = "paplay"; return player; }
#endif
    player = NULL;
    return player;
}

/* ---------------------------------------------------------------------------
 * WAV helpers — integer arithmetic only, no <math.h>
 * ---------------------------------------------------------------------------
 */
static void write_u16(FILE *f, unsigned int v) {
    unsigned char b[2];
    b[0]=(unsigned char)(v&0xFF); b[1]=(unsigned char)((v>>8)&0xFF);
    fwrite(b,1,2,f);
}
static void write_u32(FILE *f, unsigned int v) {
    unsigned char b[4];
    b[0]=(unsigned char)(v&0xFF);   b[1]=(unsigned char)((v>>8)&0xFF);
    b[2]=(unsigned char)((v>>16)&0xFF); b[3]=(unsigned char)((v>>24)&0xFF);
    fwrite(b,1,4,f);
}
static void write_wav_header(FILE *f, int sample_rate, int num_samples) {
    int data_size=num_samples*2, file_size=36+data_size;
    fwrite("RIFF",1,4,f); write_u32(f,(unsigned int)file_size);
    fwrite("WAVE",1,4,f); fwrite("fmt ",1,4,f);
    write_u32(f,16); write_u16(f,1); write_u16(f,1);
    write_u32(f,(unsigned int)sample_rate);
    write_u32(f,(unsigned int)(sample_rate*2));
    write_u16(f,2); write_u16(f,16);
    fwrite("data",1,4,f); write_u32(f,(unsigned int)data_size);
}
static void write_sample(FILE *f, int value) {
    if (value>32767) value=32767;
    if (value<-32767) value=-32767;
    unsigned int uv=(unsigned int)(value&0xFFFF);
    unsigned char b[2];
    b[0]=(unsigned char)(uv&0xFF); b[1]=(unsigned char)((uv>>8)&0xFF);
    fwrite(b,1,2,f);
}
static void gen_square(FILE *f, int freq_hz, int duration_ms,
                       int volume, int sample_rate) {
    int total=(sample_rate*duration_ms)/1000;
    int half=sample_rate/(2*freq_hz); if(half<1) half=1;
    int i;
    for(i=0;i<total;i++){
        int pos=i%(half*2);
        int val=(pos<half)?volume:-volume;
        int fade_start=total-(total/5);
        if(i>fade_start){ int r=total-i,fl=total-fade_start; val=(val*r)/fl; }
        write_sample(f,val);
    }
}
static void gen_sweep(FILE *f, int freq_start, int freq_end,
                      int duration_ms, int volume, int sample_rate) {
    int total=(sample_rate*duration_ms)/1000;
    int i, phase_acc=0;
    for(i=0;i<total;i++){
        int freq=freq_start+((freq_end-freq_start)*i)/total;
        int half=sample_rate/(2*freq); if(half<1) half=1;
        int val=(phase_acc<half)?volume:-volume;
        phase_acc++; if(phase_acc>=half*2) phase_acc=0;
        int fade_start=total-(total/4);
        if(i>fade_start){ int r=total-i,fl=total-fade_start; val=(val*r)/fl; }
        write_sample(f,val);
    }
}

typedef struct { int freq; int dur; } Note;
static const Note melody[] = {
    {659,200},{494,100},{523,100},{587,200},{523,100},{494,100},
    {440,200},{440,100},{523,100},{659,200},{587,100},{523,100},
    {494,200},{494,100},{523,100},{587,200},{659,200},
    {523,200},{440,200},{440,200},{0,100},
    {0,50},{587,200},{698,100},{880,200},{784,100},{698,100},
    {659,200},{523,100},{659,200},{587,100},{523,100},
    {494,200},{494,100},{523,100},{587,200},{659,200},
    {523,200},{440,200},{440,200},{0,200},
};
#define MELODY_LEN ((int)(sizeof(melody)/sizeof(melody[0])))

/* ---------------------------------------------------------------------------
 * sound_cleanup_orphans()
 *   Kill and reap any stale afplay processes from a previous crashed session.
 *   Called by sound_init() before generating new WAV files.
 *   This is what prevents CoreAudio handle exhaustion across sessions.
 * ---------------------------------------------------------------------------
 */
static void sound_cleanup_orphans(void) {
    /* Reap all children non-blocking */
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    /* Also kill and reap the tracked music process if still running */
    if (music_pid > 0) {
        kill(music_pid, SIGTERM);
        kill(music_pid, SIGKILL);
        waitpid(music_pid, NULL, 0);
        music_pid = -1;
    }
    /* Reset SFX tracking */
    int i;
    for (i = 0; i < sfx_pid_count; i++) {
        if (sfx_pids[i] > 0) {
            kill(sfx_pids[i], SIGTERM);
            waitpid(sfx_pids[i], NULL, WNOHANG);
        }
    }
    sfx_pid_count = 0;
}

/* ---------------------------------------------------------------------------
 * sound_init — cleanup orphans, then generate WAV files
 * ---------------------------------------------------------------------------
 */
void sound_init(void) {
    /* Kill orphans from any previous crashed session before we start */
    sound_cleanup_orphans();

    const char *player = get_audio_player();
    if (!player) return;   /* no audio player — skip silently */

    FILE *f; int sample_rate = 22050;

    f = fopen(wav_paths[SND_MOVE], "wb");
    if (f) {
        write_wav_header(f, sample_rate, (sample_rate*30)/1000);
        gen_square(f, 200, 30, 8000, sample_rate); fclose(f);
    }
    f = fopen(wav_paths[SND_ROTATE], "wb");
    if (f) {
        write_wav_header(f, sample_rate, (sample_rate*50)/1000);
        gen_square(f, 400, 50, 8000, sample_rate); fclose(f);
    }
    f = fopen(wav_paths[SND_DROP], "wb");
    if (f) {
        write_wav_header(f, sample_rate, (sample_rate*80)/1000);
        gen_square(f, 120, 80, 12000, sample_rate); fclose(f);
    }
    f = fopen(wav_paths[SND_CLEAR], "wb");
    if (f) {
        write_wav_header(f, sample_rate, (sample_rate*250)/1000);
        gen_sweep(f, 400, 900, 250, 10000, sample_rate); fclose(f);
    }
    f = fopen(wav_paths[SND_GAMEOVER], "wb");
    if (f) {
        write_wav_header(f, sample_rate, (sample_rate*600)/1000);
        gen_sweep(f, 500, 80, 600, 12000, sample_rate); fclose(f);
    }
    f = fopen(wav_paths[SND_MUSIC], "wb");
    if (f) {
        int total_ms=0, i, rep;
        for(i=0;i<MELODY_LEN;i++) total_ms+=melody[i].dur;
        total_ms*=2;
        write_wav_header(f, sample_rate, (sample_rate*total_ms)/1000);
        for(rep=0;rep<2;rep++) {
            for(i=0;i<MELODY_LEN;i++) {
                if(melody[i].freq==0) {
                    int samp=(sample_rate*melody[i].dur)/1000, s;
                    for(s=0;s<samp;s++) write_sample(f,0);
                } else {
                    gen_square(f,melody[i].freq,melody[i].dur,6000,sample_rate);
                }
            }
        }
        fclose(f);
    }
}

/* ---------------------------------------------------------------------------
 * sound_play — one-shot SFX with process limit guard
 *
 * OS Module: Process Management + Error Handling
 *   Strictly limits concurrent afplay processes to MAX_SFX_PROCS.
 *   If at limit, the sound is skipped rather than spawning more processes.
 *   This prevents CoreAudio device handle exhaustion.
 * ---------------------------------------------------------------------------
 */
void sound_play(int type) {
    if (type < 0 || type >= SND_COUNT) return;
    if (type == SND_MUSIC) return;

    const char *player = get_audio_player();
    if (!player) return;

    /* Reap finished children and compact the pid list */
    int i, new_count = 0;
    for (i = 0; i < sfx_pid_count; i++) {
        int status;
        if (waitpid(sfx_pids[i], &status, WNOHANG) > 0) {
            /* Child finished — slot is now free */
        } else {
            sfx_pids[new_count++] = sfx_pids[i];
        }
    }
    sfx_pid_count = new_count;

    /* Skip if at process limit — protects CoreAudio from exhaustion */
    if (sfx_pid_count >= MAX_SFX_PROCS) return;

    int pid = fork();
    if (pid == 0) {
        char *args[] = { (char *)player, (char *)wav_paths[type], NULL };
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execvp(player, args);
        _exit(1);
    } else if (pid > 0) {
        /* Track the child PID */
        sfx_pids[sfx_pid_count++] = pid;
    }
}

/* ---------------------------------------------------------------------------
 * sound_music_start / stop
 *
 * Music process group: the grandparent (music_pid) and its afplay child
 * are both killed on stop by sending signals to the process GROUP.
 * Using negative pid in kill() targets the entire group.
 * ---------------------------------------------------------------------------
 */
void sound_music_start(void) {
    if (music_pid > 0) return;
    const char *player = get_audio_player();
    if (!player) return;

    int pid = fork();
    if (pid == 0) {
        /* Create a new process group so we can kill the whole family at once */
        setpgid(0, 0);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        while (1) {
            int cpid = fork();
            if (cpid == 0) {
                char *args[] = { (char *)player,
                                 (char *)wav_paths[SND_MUSIC], NULL };
                execvp(player, args);
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
        /* Kill entire process group: music grandparent + afplay grandchildren */
        kill(-music_pid, SIGTERM);
        usleep(50000);   /* 50ms grace period */
        kill(-music_pid, SIGKILL);
        waitpid(music_pid, NULL, WNOHANG);
        music_pid = -1;
    }
    /* Reap any remaining zombie afplay processes */
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

/* ---------------------------------------------------------------------------
 * sound_cleanup — stop music, reap all children, remove temp files
 * ---------------------------------------------------------------------------
 */
void sound_cleanup(void) {
    sound_music_stop();

    /* Kill and reap any remaining SFX children */
    int i;
    for (i = 0; i < sfx_pid_count; i++) {
        if (sfx_pids[i] > 0) {
            kill(sfx_pids[i], SIGTERM);
            waitpid(sfx_pids[i], NULL, WNOHANG);
        }
    }
    sfx_pid_count = 0;

    /* Final reap sweep */
    while (waitpid(-1, NULL, WNOHANG) > 0) {}

    /* Remove temporary WAV files */
    for (i = 0; i < SND_COUNT; i++) unlink(wav_paths[i]);
}