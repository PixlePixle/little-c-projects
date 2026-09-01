#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <wchar.h>
#include <locale.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define true 1
#define false 0

struct editorConfig {
    int cx, cy;
    int cellx,celly;
    struct termios initial;
    int screenRows;
    int screenCols;
};


#define ROWS 30
#define COLS 40
 

char mine[] = "#";
char square[] = "\u25A0";
char flag[] = "\u2691";
char openSquare[] = "\u25A1";
int board[ROWS][COLS] = {0};
int state[ROWS][COLS] = {0};

int playing = true;
void initBoard(){
}
int offsetX;
int offsetY;

struct editorConfig E;

void die(const char *s)
{
    
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    
    perror(s);
    exit(1);
}

void disableRawMode()
{
    if ( tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.initial)  == -1 )
        die("tcsetattr");
}

void enableRawMode()
{
    atexit(disableRawMode);
    struct termios raw;
    if ( tcgetattr(STDIN_FILENO, &E.initial) == -1 ) die ("tcgetattr");
    raw = E.initial;
    raw.c_iflag &= ~(ICRNL | IXON | ISTRIP | BRKINT | INPCK);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if( tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1 ) die("tcsetattr");
}

int getWindowSize(int *rows, int* cols)
{
    struct winsize ws;

    if( ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}


struct abuf {
    char *b;
    int len;
};
#define ABUF_INIT {NULL,0}


void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);
    if( new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}
void editorDraw(struct abuf *ab)
{
    int y;
    for(y = 0; y < E.screenRows; y++)
    {
       // if(y == 0)
       // {
       //     char temp[10] = {'\0'};
       //     snprintf(temp, sizeof(temp), "%d", numFlagged);
       //     abAppend(ab, "Mines left: ", 12);
       //     abAppend(ab, temp, sizeof(temp));
       // }
        

        if(y >= offsetY && y - offsetY < ROWS)
        {
            for(int x = 0; x < offsetX; x++)
                abAppend(ab, " ", 1);
            for(int x = 0; x < COLS; x++)
            {
                //char temp[2] = {'\0'};
                //snprintf(temp, sizeof(temp), "%d", board[y-offsetY][x]);
                
                if(board[y - offsetY][x])   
                    abAppend(ab, square, sizeof(square));
                else
                    abAppend(ab, openSquare, sizeof(openSquare));
                abAppend(ab, " ", 1);
            }
        }
        if( y == offsetY + ROWS)
        {


                
        }

        // Clears everything after the last inserted thing in row
        abAppend(ab, "\x1b[K", 3);

        if(y < E.screenRows - 1) abAppend(ab, "\r\n", 2);
    }
}

//"\x1b[999C\x1b[999B" C = Forward, B = Down


void editorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;
    // Clear Screen
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    // draw the screen
    editorDraw(&ab);


    // Moves cursor back to location, and outputs buffer to screen
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));
    
    abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}


void step()
{
    for( int y = 0; y < ROWS; y++ )
    {
        for( int x = 0; x < COLS; x++)
        {
            int neighbours = 0;
            for(int ty = y - 1; ty <= y + 1; ty++)
            {
                for(int tx = x - 1; tx <= x + 1; tx++)
                {
                    if((0 <= tx && tx < COLS) && (0 <= ty && ty < ROWS))
                        neighbours += board[ty][tx];
                }
            }
            neighbours -= board[y][x];    
            if( !( 2 == neighbours && neighbours == 3 ) ) // Dies
            {
                state[y][x] = 0;
            }
            if( neighbours == 2) // state stays same
            {
                state[y][x] = board[y][x];
            }
            if( neighbours == 3 ) // becomes alive
            {
                state[y][x] = 1;
            }
        }
    }
    //memcpy(board, state, sizeof(state[0][0]) * ROWS * COLS);
    for(int y= 0; y < ROWS; y++)
    {
        for(int x = 0; x < COLS; x++)
        {
            board[y][x] = state[y][x];
        }
    }
}

int main()
{
    enableRawMode();
    initBoard();
    setlocale(LC_ALL, "");
    E.cy = 0;
    E.cx = 0;
    E.cellx = 0;
    E.celly = 0;
    if(getWindowSize(&E.screenRows, &E.screenCols) == 1 ) die ("getWindowSize");
    offsetX = (E.screenCols - (2 * COLS))/2;
    //offsetX = 0;
    //offsetY = 0;
    offsetY = (E.screenRows - ROWS)/2;
    E.cx = offsetX;
    E.cy = offsetY;
    while( true )
    {
        editorRefreshScreen();
        char c = '\0';
        if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
        if(c == '\x1b')
        {
            char seq[3];
            if(read(STDIN_FILENO, &seq[0], 1) != 1);
            if(read(STDIN_FILENO, &seq[1], 1) != 1);
            if(seq[0] == '[')
                {
                    switch(seq[1])
                    {
                    case 'A': // Up
                        //E.cy--;
                        if (E.celly > 0)
                        {
                            E.celly--;
                            E.cy -= 1;
                        }
                        break;
                    case 'B': // Down
                        //E.cy++;
                        if (E.celly < ROWS - 1)
                        {
                            E.celly++;
                            E.cy += 1;
                        }
                        break;
                    case 'C': // Right
                        //E.cx++;
                        if (E.cellx < COLS - 1)
                        {
                            E.cellx++;
                            E.cx += 2;
                        }
                        break;
                    case 'D': // Left
                        //E.cx--;
                        if (E.cellx > 0)
                        {
                            E.cellx--;
                            E.cx -= 2;
                        }
                        break;
                    }
                    if(E.cy < 0) E.cy = 0;
                    if(E.cy >= E.screenRows) E.cy = E.screenRows -1;
                    if(E.cx < 0) E.cx = 0;
                    if(E.cx >= E.screenCols) E.cx = E.screenCols - 1;
                }
        }
        switch (c) {
            case CTRL_KEY('q'):
                write(STDOUT_FILENO, "\x1b[2J", 4);
                write(STDOUT_FILENO, "\x1b[H", 3);
                exit(0);
                break;
            case 'r':
                memset(board, 0, sizeof(board[0][0]) * ROWS * COLS);
                memset(state, 0, sizeof(state[0][0]) * ROWS * COLS);
                initBoard();
                
        }
        if( playing )
            switch (c) {
                case 'f':
                   step();
                   break;
                case ' ':
                        board[E.celly][E.cellx] ^= 1; 
                    break;
            }

    }
    return 0;
}