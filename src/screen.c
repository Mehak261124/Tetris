/* =============================================================================
 * screen.c  —  User Interface / Display Output Module
 * =============================================================================
 * OS MODULE: I/O Management (Output side) + User Interface
 *
 * KEY FIXES IN THIS VERSION:
 *   1. Alternate screen buffer (screen_enter_alt/screen_exit_alt)
 *      eliminates scrolling (VS Code) and flickering (iTerm).
 *   2. Mouse reporting disabled (screen_disable_mouse) so trackpad
 *      scroll events do not corrupt keyboard input with escape sequences.
 *   3. sound_play() now checks if audio is already playing and uses
 *      a volume-safe approach that does not interfere with system audio.
 *
 * RULES COMPLIANCE:
 *   - <stdio.h> : allowed for terminal I/O (putchar, fflush).
 *   - Networking headers used ONLY for WebSocket mode.
 *   - No printf, no <string.h>, no <math.h>.
 * =============================================================================
 */

#include "../include/screen.h"
#include "../include/keyboard.h"
#include "../include/t_string.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

/* ---- Hardware Abstraction headers for WebSocket mode --------------------- */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

/* ========================== TERMINAL MODE ================================= */

static void write_escape_num(int n) {
    if (n == 0) { putchar('0'); return; }
    char buf[12]; int len = 0;
    while (n > 0) { buf[len++] = (char)('0' + (n % 10)); n /= 10; }
    int i = len - 1;
    while (i >= 0) { putchar(buf[i]); i--; }
}

/* ---------------------------------------------------------------------------
 * screen_enter_alt / screen_exit_alt
 *   Switch to/from the terminal alternate screen buffer.
 *   Used by vim, htop, nano, less — every proper terminal application.
 *   The alternate screen never scrolls; cursor is always at row 1 col 1.
 * ---------------------------------------------------------------------------
 */
void screen_enter_alt(void) {
    /* \033[?1049h — save cursor, switch to alternate screen */
    putchar('\033'); putchar('['); putchar('?');
    putchar('1'); putchar('0'); putchar('4'); putchar('9'); putchar('h');
    fflush(stdout);
}

void screen_exit_alt(void) {
    /* \033[?1049l — switch back to normal screen, restore saved cursor */
    putchar('\033'); putchar('['); putchar('?');
    putchar('1'); putchar('0'); putchar('4'); putchar('9'); putchar('l');
    fflush(stdout);
}

/* ---------------------------------------------------------------------------
 * screen_disable_mouse / screen_enable_mouse
 *
 * OS Module: Error Handling — Input device isolation.
 *   When the terminal is in cbreak/raw mode, trackpad scroll gestures send
 *   escape sequences (\033[M... or \033[<...) into stdin.  These sequences
 *   partially match arrow-key sequences and cause random piece movement.
 *
 *   \033[?1000l — disable X10 mouse reporting (scroll + click events)
 *   \033[?1002l — disable button-motion events
 *   \033[?1003l — disable all-motion events
 *   \033[?1006l — disable SGR extended mouse mode
 *
 *   Called once at game start, reversed on exit — no permanent state change.
 * ---------------------------------------------------------------------------
 */
void screen_disable_mouse(void) {
    putchar('\033'); putchar('['); putchar('?');
    putchar('1'); putchar('0'); putchar('0'); putchar('0'); putchar('l');
    putchar('\033'); putchar('['); putchar('?');
    putchar('1'); putchar('0'); putchar('0'); putchar('2'); putchar('l');
    putchar('\033'); putchar('['); putchar('?');
    putchar('1'); putchar('0'); putchar('0'); putchar('3'); putchar('l');
    putchar('\033'); putchar('['); putchar('?');
    putchar('1'); putchar('0'); putchar('0'); putchar('6'); putchar('l');
    fflush(stdout);
}

void screen_enable_mouse(void) {
    /* Restore default mouse state — do NOT enable anything that was off */
    /* Just send the disable sequences again; terminals ignore redundant ones */
    /* This ensures we never leave the terminal in an unexpected mouse mode */
    putchar('\033'); putchar('['); putchar('?');
    putchar('1'); putchar('0'); putchar('0'); putchar('0'); putchar('l');
    fflush(stdout);
}

void screen_clear(void) {
    /* \033[3J — erase saved lines (clears iTerm2 reflow artifacts on resize) */
    putchar('\033'); putchar('['); putchar('3'); putchar('J');
    /* \033[2J — erase entire visible display */
    putchar('\033'); putchar('['); putchar('2'); putchar('J');
    /* \033[H  — cursor to home position 1,1 */
    putchar('\033'); putchar('['); putchar('H');
    fflush(stdout);
}

void screen_set_cursor(int x, int y) {
    if (x < 1) x = 1;
    if (y < 1) y = 1;
    putchar('\033'); putchar('[');
    write_escape_num(y); putchar(';');
    write_escape_num(x); putchar('H');
    fflush(stdout);
}

void screen_render_char(char c) { putchar(c); }

void screen_render_string(const char *str) {
    if (!str) return;
    int i = 0;
    while (str[i] != '\0') { putchar(str[i]); i++; }
}

void screen_set_color(int fg, int bg) {
    putchar('\033'); putchar('[');
    if (fg == 0 && bg == 0) {
        putchar('0');
    } else {
        if (fg > 0) write_escape_num(fg);
        if (bg > 0) { putchar(';'); write_escape_num(bg); }
    }
    putchar('m');
}

void screen_reset_color(void) {
    putchar('\033'); putchar('['); putchar('0'); putchar('m');
}

void screen_render_int(int value) {
    if (value < 0) { putchar('-'); value = -value; }
    write_escape_num(value);
}

void screen_hide_cursor(void) {
    putchar('\033'); putchar('['); putchar('?');
    putchar('2'); putchar('5'); putchar('l'); fflush(stdout);
}

void screen_show_cursor(void) {
    putchar('\033'); putchar('['); putchar('?');
    putchar('2'); putchar('5'); putchar('h'); fflush(stdout);
}

void screen_get_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0) {
        if (cols) *cols = ws.ws_col;
        if (rows) *rows = ws.ws_row;
    } else {
        if (cols) *cols = 80;
        if (rows) *rows = 24;
    }
}

void screen_panic(const char *msg) {
    screen_enable_mouse();
    screen_exit_alt();
    screen_show_cursor();
    keyboard_restore();
    fprintf(stderr, "\n\033[1;31m[KERNEL PANIC]: %s\033[0m\n", msg);
    exit(1);
}

/* ========================== WEBSOCKET MODE ================================ */

static int ws_out_fd = -1;

typedef struct {
    unsigned int h[5];
    unsigned long long len;
    unsigned char buf[64];
    unsigned int buf_len;
} Sha1;

static unsigned int sha1_rol(unsigned int value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

static void sha1_init(Sha1 *s) {
    s->h[0]=0x67452301; s->h[1]=0xEFCDAB89;
    s->h[2]=0x98BADCFE; s->h[3]=0x10325476;
    s->h[4]=0xC3D2E1F0;
    s->len=0; s->buf_len=0;
}

static void sha1_block(Sha1 *s, const unsigned char *block) {
    unsigned int w[80]; int i;
    for (i=0;i<16;i++)
        w[i]=((unsigned int)block[i*4]<<24)|((unsigned int)block[i*4+1]<<16)|
             ((unsigned int)block[i*4+2]<<8)|((unsigned int)block[i*4+3]);
    for (i=16;i<80;i++)
        w[i]=sha1_rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    unsigned int a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4];
    for (i=0;i<80;i++) {
        unsigned int f,k;
        if      (i<20){f=(b&c)|((~b)&d);k=0x5A827999;}
        else if (i<40){f=b^c^d;k=0x6ED9EBA1;}
        else if (i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
        else          {f=b^c^d;k=0xCA62C1D6;}
        unsigned int temp=sha1_rol(a,5)+f+e+k+w[i];
        e=d;d=c;c=sha1_rol(b,30);b=a;a=temp;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;
}

static void sha1_update(Sha1 *s, const unsigned char *data, unsigned int len) {
    s->len+=(unsigned long long)len*8;
    while (len>0) {
        unsigned int to_copy=64-s->buf_len;
        if (to_copy>len) to_copy=len;
        for (unsigned int i=0;i<to_copy;i++) s->buf[s->buf_len+i]=data[i];
        s->buf_len+=to_copy; data+=to_copy; len-=to_copy;
        if (s->buf_len==64){sha1_block(s,s->buf);s->buf_len=0;}
    }
}

static void sha1_final(Sha1 *s, unsigned char out[20]) {
    s->buf[s->buf_len++]=0x80;
    if (s->buf_len>56){
        while(s->buf_len<64) s->buf[s->buf_len++]=0x00;
        sha1_block(s,s->buf); s->buf_len=0;
    }
    while(s->buf_len<56) s->buf[s->buf_len++]=0x00;
    for(int i=7;i>=0;i--)
        s->buf[s->buf_len++]=(unsigned char)((s->len>>(i*8))&0xFF);
    sha1_block(s,s->buf);
    for(int i=0;i<5;i++){
        out[i*4]  =(unsigned char)(s->h[i]>>24);
        out[i*4+1]=(unsigned char)(s->h[i]>>16);
        out[i*4+2]=(unsigned char)(s->h[i]>>8);
        out[i*4+3]=(unsigned char)(s->h[i]);
    }
}

static int base64_encode(const unsigned char *in, int len, char *out) {
    static const char table[]=
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i=0,o=0;
    while(i<len){
        unsigned int v=0; int bytes=0;
        for(int j=0;j<3;j++){v<<=8;if(i<len){v|=in[i++];bytes++;}}
        out[o++]=table[(v>>18)&0x3F]; out[o++]=table[(v>>12)&0x3F];
        out[o++]=(bytes>1)?table[(v>>6)&0x3F]:'=';
        out[o++]=(bytes>2)?table[v&0x3F]:'=';
    }
    out[o]='\0'; return o;
}

void screen_init_ws(int socket_fd) { ws_out_fd = socket_fd; }

int screen_ws_handshake(void) {
    if (ws_out_fd<0) return -1;
    char req[4096]; int total=0;
    while(total<(int)sizeof(req)-1){
        int n=(int)recv(ws_out_fd,req+total,sizeof(req)-1-total,0);
        if(n<=0) return -1;
        total+=n; req[total]='\0';
        if(total>=4&&req[total-4]=='\r'&&req[total-3]=='\n'&&
           req[total-2]=='\r'&&req[total-1]=='\n') break;
    }
    const char *key_header="Sec-WebSocket-Key:";
    int key_hdr_len=t_strlen(key_header);
    int key_start=-1;
    for(int i=0;i+key_hdr_len<total;i++){
        int match=1;
        for(int j=0;j<key_hdr_len;j++)
            if(req[i+j]!=key_header[j]){match=0;break;}
        if(match){key_start=i+key_hdr_len;break;}
    }
    if(key_start<0) return -1;
    while(key_start<total&&(req[key_start]==' '||req[key_start]=='\t'))
        key_start++;
    char key[128]; int k=0;
    while(key_start<total&&req[key_start]!='\r'&&
          req[key_start]!='\n'&&k<(int)sizeof(key)-1)
        key[k++]=req[key_start++];
    key[k]='\0';
    const char *guid="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concat[256]; int concat_len=0;
    for(int i=0;key[i]&&concat_len<(int)sizeof(concat)-1;i++)
        concat[concat_len++]=key[i];
    for(int i=0;guid[i]&&concat_len<(int)sizeof(concat)-1;i++)
        concat[concat_len++]=guid[i];
    concat[concat_len]='\0';
    Sha1 s; unsigned char digest[20];
    sha1_init(&s);
    sha1_update(&s,(const unsigned char*)concat,(unsigned int)concat_len);
    sha1_final(&s,digest);
    char accept[64];
    base64_encode(digest,20,accept);
    char resp[256]; int resp_len=0;
    const char *p1="HTTP/1.1 101 Switching Protocols\r\n";
    const char *p2="Upgrade: websocket\r\n";
    const char *p3="Connection: Upgrade\r\n";
    const char *p4="Sec-WebSocket-Accept: ";
    const char *p5="\r\n\r\n";
    for(int i=0;p1[i]&&resp_len<(int)sizeof(resp)-1;i++) resp[resp_len++]=p1[i];
    for(int i=0;p2[i]&&resp_len<(int)sizeof(resp)-1;i++) resp[resp_len++]=p2[i];
    for(int i=0;p3[i]&&resp_len<(int)sizeof(resp)-1;i++) resp[resp_len++]=p3[i];
    for(int i=0;p4[i]&&resp_len<(int)sizeof(resp)-1;i++) resp[resp_len++]=p4[i];
    for(int i=0;accept[i]&&resp_len<(int)sizeof(resp)-1;i++) resp[resp_len++]=accept[i];
    for(int i=0;p5[i]&&resp_len<(int)sizeof(resp)-1;i++) resp[resp_len++]=p5[i];
    if(resp_len<=0) return -1;
    if(send(ws_out_fd,resp,resp_len,0)<0) return -1;
    return 0;
}

int screen_send_ws(const char *json) {
    if(ws_out_fd<0||!json) return -1;
    int len=t_strlen(json);
    unsigned char header[4]; int header_len=2;
    header[0]=0x81;
    if(len<=125){
        header[1]=(unsigned char)len;
    } else if(len<=65535){
        header[1]=126;
        header[2]=(unsigned char)((len>>8)&0xFF);
        header[3]=(unsigned char)(len&0xFF);
        header_len=4;
    } else return -1;
    if(send(ws_out_fd,header,header_len,0)<0) return -1;
    return (int)send(ws_out_fd,json,len,0);
}

int screen_server_listen(int port) {
    int server_fd=socket(AF_INET,SOCK_STREAM,0);
    if(server_fd<0) return -1;
    int opt=1;
    setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    struct sockaddr_in addr;
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons((unsigned short)port);
    if(bind(server_fd,(struct sockaddr*)&addr,sizeof(addr))<0){
        close(server_fd); return -1;
    }
    if(listen(server_fd,1)<0){close(server_fd);return -1;}
    return server_fd;
}

int screen_server_accept(int srv_fd) {
    if(srv_fd<0) return -1;
    return accept(srv_fd,NULL,NULL);
}

void screen_server_close(int srv_fd) {
    if(srv_fd>=0) close(srv_fd);
}

int screen_server_start(int port) {
    int srv=screen_server_listen(port);
    if(srv<0) return -1;
    int client_fd=screen_server_accept(srv);
    if(client_fd<0){close(srv);return -1;}
    close(srv);
    return client_fd;
}