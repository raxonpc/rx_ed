#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/ioctl.h>

/*** DEFINES ***/
#define CTRL_KEY(k) ((k) & 0x1f)

/*** DATA ***/
struct editor_config
{
    struct termios orig_termios;
    int screen_cols;
    int screen_rows;
};

struct editor_config ed_cfg;

/*** TERMINAL ***/
void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disable_raw_mode()
{
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &ed_cfg.orig_termios) == -1)
        die("tcsetattr");
}

void enable_raw_mode()
{
    if(tcgetattr(STDIN_FILENO, &ed_cfg.orig_termios) == -1)
        die("tcgetattr");
    atexit(disable_raw_mode);

    struct termios raw = ed_cfg.orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 20;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 1)
        die("tcsetattr");
}

char editor_read_key()
{
    ssize_t nread;
    char c;

    while((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

int get_cursor_position(int *cols, int *rows)
{
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i++] == 'R') break;
    }
    buf[i] = '\0';

    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int get_window_size(int *cols, int *rows)
{
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;

        return get_cursor_position(cols, rows);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** APPEND BUFFER ***/

struct abuf
{
    char *b;
    int len;
};

#define ABUF_INIT { NULL, 0 };

void ab_append(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);

    if(new == NULL) return;
    memcpy(new + ab->len, s, len);
    ab->b = new;
    ab->len += len;
}

void ab_free(struct abuf *ab)
{
    free(ab->b);
}



/*** OUTPUT ***/

void editor_draw_rows(struct abuf *ab)
{
    for(int y = 0; y < ed_cfg.screen_rows; ++y)
    {
        ab_append(ab, "~", 1);

        if(y < ed_cfg.screen_rows - 1)
        {
            ab_append(ab, "\r\n", 2);
        }
    }
}

void editor_refresh_screen()
{
    struct abuf ab = ABUF_INIT;

    ab_append(&ab, "\x1b[2J", 4);
    ab_append(&ab, "\x1b[H", 3);

    editor_draw_rows(&ab);

    ab_append(&ab, "\x1b[H", 3);

    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}

/*** INPUT ***/

void editor_process_keypress()
{
    char c;

    c = editor_read_key();

    switch(c)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
        default:
            break;
    }
}

/*** INIT ***/

void init_editor()
{
    if(get_window_size(&ed_cfg.screen_cols, &ed_cfg.screen_rows) == -1)
        die("get_window_size");
}
int main()
{
    enable_raw_mode();
    init_editor();

    while(1) {
        editor_refresh_screen();
        editor_process_keypress();
    }

    return 0;
}
