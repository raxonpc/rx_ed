#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>

/*** DEFINES ***/

#define RX_ED_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editor_key
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

/*** DATA ***/

typedef struct erow
{
    int size;
    char *data;
} erow;

struct editor_config
{
    struct termios orig_termios;

    int screen_cols;
    int screen_rows;

    int num_rows;
    erow row;

    int cursor_x, cursor_y;
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

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 1)
        die("tcsetattr");
}

int editor_read_key()
{
    ssize_t nread;
    char c;

    while((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if(nread == -1 && errno != EAGAIN) die("read");
    }

    if(c == '\x1b')
    {
        char seq[3];

        if(read(STDIN_FILENO, seq, 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, seq + 1, 1) != 1) return '\x1b';

        if(seq[0] == '[')
        {
            if(seq[1] >= '0' && seq[1] <= '9')
            {
                if(read(STDIN_FILENO, seq + 2, 1) != 1) return '\x1b';

                if(seq[2] == '~')
                {
                    switch (seq[1])
                    {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            }
            else
            {
                switch (seq[1])
                {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        }
        else if(seq[0] == '0')
        {
            switch(seq[1])
            {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }
        return '\x1b';
    }
    else
    {
        return c;
    }
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

/*** file i/o ***/

void editor_open()
{
    char *line = "Hello, world!";
    ssize_t linelen = 13;

    ed_cfg.row.size = linelen;
    ed_cfg.row.data = malloc(linelen + 1);

    memcpy(ed_cfg.row.data, line, linelen);
    ed_cfg.row.data[linelen] = '\0';
    ed_cfg.num_rows = 1;
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
        if(y >= ed_cfg.num_rows)
        {
            if (y == ed_cfg.screen_rows / 3)
            {
                char welcome[80] = {0};
                int welcome_ln = snprintf(welcome, sizeof(welcome), "rx_ed - version %s", RX_ED_VERSION);

                if (welcome_ln > ed_cfg.screen_cols) welcome_ln = ed_cfg.screen_cols;
                int padding = (ed_cfg.screen_cols - welcome_ln) / 2;
                if (padding)
                {
                    ab_append(ab, "~", 1);
                    --padding;
                }
                while (padding--) ab_append(ab, " ", 1);


                ab_append(ab, welcome, welcome_ln);
            }
            else
            {
                ab_append(ab, "~", 1);
            }
        }
        else
        {
            int len = ed_cfg.row.size;
            if(len > ed_cfg.screen_cols) len = ed_cfg.screen_cols;

            ab_append(ab, ed_cfg.row.data, len);
        }
        ab_append(ab, "\x1b[K", 3); // clear the line (default - to the right of the cursor)
        if(y < ed_cfg.screen_rows - 1)
        {
            ab_append(ab, "\r\n", 2);
        }
    }
}

void editor_refresh_screen()
{
    struct abuf ab = ABUF_INIT;

    ab_append(&ab, "\x1b[?25l", 6); //hide the cursor
    ab_append(&ab, "\x1b[H", 3); // reposition the cursor (default - 1;1)

    editor_draw_rows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", ed_cfg.cursor_y + 1, ed_cfg.cursor_x + 1);
    ab_append(&ab, buf, strlen(buf));

    ab_append(&ab, "\x1b[?25h", 6); // show the cursor

    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}

/*** INPUT ***/

void editor_move_cursor(int key)
{
    switch(key)
    {
        case ARROW_LEFT:
            if(ed_cfg.cursor_x != 0) --ed_cfg.cursor_x;
            break;
        case ARROW_RIGHT:
            if(ed_cfg.cursor_x != ed_cfg.screen_cols - 1) ++ed_cfg.cursor_x;
            break;
        case ARROW_UP:
            if(ed_cfg.cursor_y != 0 ) --ed_cfg.cursor_y;
            break;
        case ARROW_DOWN:
            if(ed_cfg.cursor_y != ed_cfg.screen_rows - 1) ++ed_cfg.cursor_y;
            break;
        default: break;
    }
}



void editor_process_keypress()
{
    int c = editor_read_key();

    switch(c)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);

        case PAGE_UP:
            ed_cfg.cursor_y = 0;
            break;
        case PAGE_DOWN:
            ed_cfg.cursor_y = ed_cfg.screen_rows - 1;
            break;
        case HOME_KEY:
            ed_cfg.cursor_x = 0;
            break;
        case END_KEY:
            ed_cfg.cursor_x = ed_cfg.screen_cols - 1;
            break;

        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
        case ARROW_DOWN:
            editor_move_cursor(c);
            break;
        default:
            break;
    }
}

/*** INIT ***/

void init_editor()
{
    ed_cfg.cursor_x = 0;
    ed_cfg.cursor_y = 0;

    ed_cfg.num_rows = 0;

    if(get_window_size(&ed_cfg.screen_cols, &ed_cfg.screen_rows) == -1)
        die("get_window_size");
}
int main()
{
    enable_raw_mode();
    init_editor();
    editor_open();

    while(1) {
        editor_refresh_screen();
        editor_process_keypress();
    }

    return 0;
}
