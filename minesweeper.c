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

typedef enum {
    FLAG_VISIBLE = 1 << 0,
    FLAG_FLAGGED = 1 << 1,
    FLAG_MINE    = 1 << 2
} STATE_FLAGS;

#define ROWS 25
#define COLS 25
#define MINES 50 

char mine[] = "#";
char square[] = "\u25A0";
char flag[] = "\u2691";
char openSquare[] = "\u25A1";
int board[ROWS][COLS] = {0};
int state[ROWS][COLS] = {0};
int numFlagged = MINES;
int playing = true;
char winMessage[] = "You won! r to restart";
char endMessage[] = "You hit a mine. r to restart";
int numCleared = 0;
int gameWon = 0; // 0 to indeterminate, 1 for win, 2 for loss
void initBoard(){
    numFlagged = MINES;
    numCleared = 0;
    gameWon = 0;
    srand(time(NULL));
    for(int i = 0; i < MINES; i++)
    {
        int randomY = (rand() % ROWS);
        int randomX = (rand() % COLS);
        if(board[randomY][randomX] >= 9)
        {
            i--;
            continue;
        }
        
        board[randomY][randomX] += 9;
        state[randomY][randomX] |= FLAG_MINE;
        for(int y = randomY - 1; y <= randomY + 1; y++)
        {
            for(int x = randomX - 1; x <= randomX + 1; x++)
            {
                if((0 <= x && x < COLS) && (0 <= y && y < ROWS))
                    board[y][x]++;
            }
        }
    }
    playing = true;
}

int offsetX;
int offsetY;

void recursiveClear(int y, int x)
{
    if( !(state[y][x] & FLAG_VISIBLE))
    {
        state[y][x] |= FLAG_VISIBLE;
        numCleared++;
        if(state[y][x] & FLAG_MINE)
        {
            playing = false;
            gameWon = 2;
        }
        if(board[y][x] == 0)
        {
            for(int ty = y - 1; ty <= y + 1; ty++)
            {
                for(int tx = x - 1; tx <= x + 1; tx++)
                {
                    if((0 <= tx && tx < COLS) && (0 <= ty && ty < ROWS))
                        recursiveClear(ty, tx);
                }
            }
        }
        if(gameWon == 0 && numCleared == ((ROWS * COLS) - MINES))
        {
            playing = false;
            gameWon = 1;
        }
    }
}



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
        if(y == 0)
        {
            char temp[10] = {'\0'};
            snprintf(temp, sizeof(temp), "%d", numFlagged);
            abAppend(ab, "Mines left: ", 12);
            abAppend(ab, temp, sizeof(temp));
        }
        

        if(y >= offsetY && y - offsetY < ROWS)
        {
            for(int x = 0; x < offsetX; x++)
                abAppend(ab, " ", 1);
            for(int x = 0; x < COLS; x++)
            {
                char temp[2] = {'\0'};
                snprintf(temp, sizeof(temp), "%d", board[y-offsetY][x]);
                
                if(state[y - offsetY][x] & FLAG_VISIBLE)
                {
                    if(board[y - offsetY][x] > 8)
                        abAppend(ab, mine, sizeof(mine));
                    else if (board[y - offsetY][x] > 0)
                        abAppend(ab, temp, sizeof(temp));
                    else
                        abAppend(ab, openSquare, sizeof(openSquare));
                } else if (state[y-offsetY][x] & FLAG_FLAGGED)
                {
                    abAppend(ab, flag, sizeof(flag));
                } else 
                {
                    abAppend(ab, square, sizeof(square));
                }
                abAppend(ab, " ", 1);
            }
        }
        if( y == offsetY + ROWS)
        {

            if(!playing && gameWon == 2)
            {
                int temp = (E.screenCols - sizeof(endMessage))/2;

                for(int x = 0; x < temp; x++)
                    abAppend(ab, " ", 1);
                abAppend(ab, endMessage, sizeof(endMessage));
            } else if (!playing && gameWon == 1)
            {
                int temp = (E.screenCols - sizeof(winMessage))/2;
                for(int x = 0; x < temp; x++)
                    abAppend(ab, " ", 1);
                abAppend(ab, winMessage, sizeof(winMessage));
            }
                
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
                    if( !(state[E.celly][E.cellx] & FLAG_VISIBLE) ) 
                        if( state[E.celly][E.cellx] & FLAG_FLAGGED )
                        {
                            state[E.celly][E.cellx] &= ~FLAG_FLAGGED;
                            numFlagged++;
                        } else
                        {
                            state[E.celly][E.cellx] |= FLAG_FLAGGED;
                            numFlagged--;
                        }
                    break;
                case ' ':
                    if( !(state[E.celly][E.cellx] & FLAG_FLAGGED) )
                        recursiveClear(E.celly, E.cellx);
                    
                    break;
            }

    }
    return 0;
}
