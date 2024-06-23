/****************************************************************************
 * Copyright (C) 2024 by Andre Arante                                       
 ****************************************************************************/

/**
 * @file kilo.c
 * @author Andre Arante
 * @date 6 Jun 2024
 * @brief The file responsible for running the text editor
 */

/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3


#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};


enum editorHighlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)


/*** data ***/

/** @struct erow
 *  @brief erow stands for "editor row" and stores a line of text as a pointer to a 
 * dynamically-allocated character data and a size
 * 
 *  @var foreignstruct::size
 *  Member 'size' contains the size of the char array representing raw text data
 * 
 *  @var foreignstruct::rsize
 *  Member 'rsize' contains the rsize (render size) of the char array representing the 
 * text data displayed to the user
 * 
 *  @var foreignstruct::chars
 *  Member 'chars' raw text data as a dynamically-allocated array
 * 
 *  @var foreignstruct::render
 *  Member 'render' text data to be shown to the user as a dynamically-allocated array
 * 
 *  @var foreignstruct::hl
 *  Member 'hl' stores the highlighting of each line in an array
 */
typedef struct erow {
  int idx;
  int size;
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl;
  int hl_open_comment;
} erow;

/** @struct editorConfig
 *  @brief Stores the global state of the editor
 * 
 *  @var foreignstruct::cx
 *  Member 'cx' contains the x coordinate of the cursor within the text buffer
 * 
 *  @var foreignstruct::cy
 *  Member 'cy' contains the y coordinate of the cursor within the text buffer
 * 
 *  @var foreignstruct::rx
 *  Member 'rx' contains the x coordinate of the cursor within the render buffer
 * 
 *  @var foreignstruct::rowoff
 *  Member 'rowoff' contains vertical offset to allow vertical scrolling
 * 
 *  @var foreignstruct::coloff
 *  Member 'coloff' contains the horizontal offset to allow horizontal scrolling
 * 
 *  @var foreignstruct::numrows
 *  Member 'numrows' contains the total number of rows in the text editor
 * 
 *  @var foreignstruct::erow
 *  Member 'erow' contains raw text data and render buffer of the text editor
 * 
 *  @var foreignstruct::dirty
 *  Member 'dirty' contains a measure of how many changes have been made to the doc
 * since last save
 * 
 *  @var foreignstruct::filename
 *  Member 'filename' contains the filename of the current file
 * 
 *  @var foreignstruct::statusmsg
 *  Member 'statusmsg' contains the msg to be displayed to the user
 * 
 *  @var foreignstruct::statusmsg_time
 *  Member 'statusmsg_time' contains the time to be displayed to the user
 * 
 *  @var foreignstruct::orig_termios
 *  Member 'orig_termios' contains the original terminal attributes to be modified
 * 
 */
struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct editorSyntax *syntax;
  struct termios orig_termios;
};

struct editorConfig E;

/** @struct editorSyntax
 *  @brief Stores filetype detection information
 * 
 *  @var foreignstruct::filetype
 *  Member 'filetype' name of the filetype to display to the user
 * 
 *  @var foreignstruct::filematch
 *  Member 'filematch' an array of strings, where each string contains a pattern to match the filename against
 * 
 *  @var foreignstruct::flags
 *  Member 'flags' contains flags for whether to highlight numbers
 * 
 */
struct editorSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** filetypes ***/

// File extensions for C/C++ files
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

// Highlight Database
struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
};

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen(void);
char *editorPrompt(char *prompt, void (*callback)(char*, int));
void editorFindCallback(char *query, int key);

/*** terminal ***/

/**
 * @brief A panic function that prints an error message and exits the program.
 * 
 * Most C functions set the global var errno function to indicate an error. Die uses
 * perror() reads errno and prints a descriptive message. This function mostly
 * checks library calls for failure.
 * 
 * @param *s the error that was caught and to be displayed to user
 */
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

/**
 * @brief Returns the terminal back to default settings.
 * 
 * Called when the terminal is closed.
 */
void disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

/**
 * @brief Switches the terminal to RAW MODE, which allows the programmer to process
 * each keystroke the user inputs individually
 * 
 * RAW MODE is achieved by disabled a bunch of flags in E.orig_termios, the original
 * terminal attributes.
 * 
 */
void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/**
 * @brief Reads in raw input from the user, and appropriately detects special keys
 * 
 * The input from the user is read char by char and translated into bytes.
 */
int editorReadKey(void) {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}


/**
 * @brief Determines the current x and y position of the cursor.
 * 
 * Utilizes the 'n' command, Device Status Report to query the terminal for status 
 * information. We then parse the resulting cursor position report to extract coordinates.
 * 
 * @param rows a dereference of the y coordinate of the cursor to be modified in place
 * @param cols a dereference of the x coordinate of the cursor to be modified in place
 */
int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}


/**
 * @brief Determines the height and width of the screen
 * 
 * To make this function non device specific, we determine the window size by moving
 * the cursor to bottom right corner of the screen and getting the cursor position
 * 
 * @param rows the x coordinate of the curosor
 * @param cols the y coordinate of the cursor
 */
int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** syntax highlighting ***/

/**
 * @brief Identifies seperator charcters
 * 
 * strchr() returns a pointer to the first matching char of a string.
 * 
 * @param c a char to identify
 */
int is_separator(int c) {
  return isspace(c) || c == "\0" || strchr(",.()+-/*=~%<>[];", c) != NULL;
}


/**
 * @brief Goes through the characters of an erow and highlights them all
 * 
 * @param row the erow we want to highlight
 */
void editorUpdateSyntax(erow *row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);
  if (E.syntax == NULL) return;
  char **keywords = E.syntax->keywords;
  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;
  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;
  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);
  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;
    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }
    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }
    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }
    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }
    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2) klen--;
        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }
    prev_sep = is_separator(c);
    i++;
  }
  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    editorUpdateSyntax(&E.row[row->idx + 1]);
}

/**
 * @brief maps values in hl (highlight array) to actual ANSI color codes
 *
 * @param hl the highlight array
 */
int editorSyntaxToColor(int hl) {
  switch(hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36;
    case HL_KEYWORD1: return 33;
    case HL_KEYWORD2: return 32;
    case HL_STRING: return 35;
    case HL_NUMBER: return 31;
    case HL_MATCH: return 34;
    default: return 37;
  }
}

/**
 * @brief Matches the current filename to one of the filematch fields in
 * HLDB
 */
void editorSelectSyntaxHighlight() {
  E.syntax = NULL;
  if (E.filename == NULL) return;

  // Pointer to the extension part of the filename
  char *ext = strrchr(E.filename, '.');

  // For each pattern in filematch compare
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(E.filename, s->filematch[i]))) {
        E.syntax = s;

        int filerow;
        for (filerow = 0; filerow < E.numrows; filerow++) {
          editorUpdateSyntax(&E.row[filerow]);
        }

        return;
      }
      i++;
    }
  }
}




/*** row operations ***/

/**
 * @brief Calculates the correct row length by counting tabs correctly
 * 
 * @param *row a reference to the row that we are counting
 * @param cx the length of the row to fix 
 */
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  }
  return rx;
}


/**
 * @brief Converts a render index into a index in the chars array
 * 
 * @param *row a reference to the row that we are counting
 * @param rx the render index
 */
int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}


/**
 * @brief Updates the render array whenever the text of the row changes.
 * 
 * The main function of this is to render tabs as multiple space characters
 * and make sure the sizes are being tracked properly. Memory 
 * 
 * @param *row a reference to the raw row data
 */
void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;

  // Allocate enough memory to fit the tabs + the newline char at the end
  free(row->render);
  row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

/**
 * @brief Insert a row at a given index
 * 
 * Allocates space for a new erow, and then copies the given string
 * to a new erow at the end of the E.row array
 * 
 * @param at the index to insert the row at
 * @param s the string to be inserted
 * @param len the length of E.row
 */
void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;

  // Allocate space for an extra row and move the old data to the new 
  // memory location
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
  for (int j = at + 1; j < E.numrows; j++) E.row[j].idx++;

  E.row[at].idx = at;

  // Set at to the index of the new row
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  // Replace row[at] with
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

/**
 * @brief Deallocates memory for a row
 * 
 * @param row the row to deallocate
 */
void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

/**
 * @brief Deletes a specific row
 * 
 * First
 * 
 * @param at the index of the row to remove
 */
void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return; 

  // Free the memory owned by the row
  editorFreeRow(&E.row[at]);

  // Overwrite the deleted row struct with the rest of the rows that come after it
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
  E.numrows--;
  E.dirty++;
}

/**
 * @brief Inserts a char at a given index of a given row
 * 
 * Inserts a char at an index of a row, and dynamically allocates
 * memory to make space
 * 
 * @param row the row to add the char to
 * @param at the index of the row to add the char to
 * @param c the char to insert into the row
 */
void editorRowInsertChar(erow *row, int at, int c) {

  // Only allow char to be inserted at the one position past the end of the line
  if (at < 0 || at > row->size) at = row->size;

  // Allocate memory for one new char plus the null char (end of string)
  row->chars = realloc(row->chars, row->size + 2);

  // Make room for the new char by allocating a new block of memory for rows.chars
  // Only need to reallocate the chars that include and come after at
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;

  // Finally insert the char and update the row in the editor
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

/**
 * @brief Appends a whole string to the end of the row
 * 
 * @param row a row to append a string to the end of
 * @param s the string to append to the end of the string
 * @param len the length of of the string to add
 */
void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

/**
 * @brief Dels a char in a row
 * 
 * @param row a row to delete a char in
 * @param at the index of the char to delete
 */
void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/

/**
 * @brief Called by editorProcessKeypress() to map a basic char to
 * an operation in the text editor.
 * 
 * Functions in editor operations don't have to worry about the details of modifying an erow
 * 
 * @param c a char c to insert at the location of the cursor
 */
void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

/**
 * @brief Deletes the cursor at the location of the cursor
 * 
 */
void editorDelChar(void) {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    // If the row is empty, delete the whole row
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/**
 * @brief Supports the inserting of a new line, if we are at the end
 * of the last line.
 */
void editorInsertNewline(void) {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

/*** file i/o ***/

/**
 * @brief Converts a all the text data to a string that will be written to
 * the disk eventually.
 * 
 * @param buflen the length of the textbuffer that needs to be parsed
 */
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;

  // Add up the lengths of each row of text, including the newline char
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  // Allocate the required memory and memcpy the contents of each row to the end of the buffer
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  // Expect the caller to free memory when it is done
  return buf;
}

/**
 * @brief Opens a locally stored file by name and reads it line by line.
 * 
 * Calls the editorInserRow on each line to insert all of the file's text
 * contents into the editor
 * 
 * @param filename the name of the file to open
 */
void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  editorSelectSyntaxHighlight();

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

/**
 * @brief Either creates a new file and opens it, or modifies the existing
 * file stored in E.filename
 * 
 * TODO: More advanced editors will write to a new, temporary file then rename that
 * file to the actual file the user wants to overwrite. 
 */
void editorSave(void) {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
    editorSelectSyntaxHighlight();
  }

  int len;
  char *buf = editorRowsToString(&len);

  // Create a newfile if it doesn't already exist (O_CREAT)
  // Open the file for reading (O_RDWR)
  // 0644 contains the mode (permissions) giving the ownder read and write
  // permissions and everyone else read only
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    // Sets the file's size to the specified length, and cut off any
    // excess data
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/

/**
 * @brief Prompts the user to search for a string in the file.
 * 
 * Linearly scans every line in the text buffer until it finds an 
 * occurance of the user's query, then moves the user cursor to the
 * location of the found query.
 */
void editorFind(void) {

  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;


  char *query = editorPrompt("Search: %s (ESC/Arrows/Enter)", 
                              editorFindCallback);
 
  if (query) {
    free(query);
  } else {
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }

  
}

/**
 * @brief A callback function that is called everytime the user inputs text
 * into the search bar.
 * 
 * Highlights words blue when in search mode, and remembers the previous
 * highlighting data to restore to when we exit search mode.
 * 
 * @param query the word we are searching for 
 * @param key the key that the user last pressed
 */
void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  // Save previous highlights to restore on exiting search mode
  static int saved_hl_line;
  static char *saved_hl = NULL;

  if (saved_hl) {
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }

  if (saved_hl)

  // If the user presses Enter or Escape they are leaving search mode
  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  int i;

  // Loop through each row and check if query is a substring of that row
  if (last_match == -1) direction = 1;
  int current = last_match;
  for (i = 0; i < E.numrows; i++) {
    current += direction;
    if (current == -1) current = E.numrows - 1;
    else if (current == E.numrows) current = 0;
    erow *row = &E.row[current];

    char *match = strstr(row->render, query);
    if (match) {
      last_match = current;
      E.cy = current;

      // Subtract row->render pointer from match to get cx
      E.cx = editorRowRxToCx(row, match - row->render);

      // Scroll down/up to the word
      E.rowoff = E.numrows;

      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);

      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

/*** append buffer ***/


/** @struct abuf
 *  @brief abuf (append buffer) is a dynamically allocated string that will be 
 * appended to the screen in one big write call.
 * 
 *  @var foreignstruct::*b
 *  Member '*b' a pointer to our buffer in memory
 * 
 *  @var foreignstruct::len
 *  Member 'len' a length of the buffer in memory
 * 
 */
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

/**
 * @brief Appends a string to the end of the append buffer
 * 
 * @param ab a reference to the append buffer
 * @param s a string to append
 * @param len the length of the string to append
 */
void abAppend(struct abuf *ab, const char *s, int len) {
  // Reallocates a new slot in memory for the string that is large enough
  // to append the new string
  char *new = realloc(ab->b, ab->len + len);

  // Copy the current string plus the size of the string we are appending
  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

/**
 * @brief a deconstructor that deallocates the dynamic memory used by abuf
 * 
 * @param ab a referenece to the append buffer
 */
void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** output ***/

/**
 * @brief Allows scrolling through the text editor.
 * 
 * Checks if the cursor has moved outside of the visible window, and if so
 * adjust E.rowoff so that the cursor is just inside the visible window
 * 
 */
void editorScroll(void) {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

/**
 * @brief Handles drawing of each row of the buffer of text being edited.
 * 
 * To store multiple lines, E.row is defined as an array of erow structs.
 * Highlights rows by keeping track of the current text color and looping
 * through all the characters, changing text color via escape sequence when 
 * a new word type is detected.
 * 
 * @param ab the append buffer to append to the screen
 */
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) {
        if (iscntrl(c[j])) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);
          if (current_color != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        } else {
          int color = editorSyntaxToColor(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
    }

    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}


/**
 * @brief Draws the status bar at the bottom of the screen.
 * 
 * Appends a string with specical graphic rendition to the bottom of the
 * append buffer
 * 
 * @param ab a reference to the append buffer
 */
void editorDrawStatusBar(struct abuf *ab) {

  // m command (Select Graphic Rendition) causes the text to be printed with
  // option 7(inverted colors)
  abAppend(ab, "\x1b[7m", 4);

  // Append buffer is a char array of length 80 (80 chars long max)
  char status[80], rstatus[80];


  // Uses snprintf to compose a message and store as a C string "status" 

  // Stores the number of lines and the filename
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    E.filename ? E.filename : "[No Name]", E.numrows,
    E.dirty ? "(modified)" : ""
    );

  // Stires the current line number
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);

  
  if (len > E.screencols) len = E.screencols;

  // Append status at the start of the line
  abAppend(ab, status, len);
  while (len < E.screencols) {
    // Append rstatus at the end of the line
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

/**
 * @brief Appends the message stored in E.statusmsg to the append buffer
 * 
 * @param ab a reference to the append buffer
 */
void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}
 
/**
 * @brief Refreshes the screen
 * 
 * Called only after a keypress
 */
void editorRefreshScreen(void) {
  // Fix the view to allow for scrolling
  editorScroll();

  struct abuf ab = ABUF_INIT;

  // Hide the cursor when repainting
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  // Draw all the text rows
  editorDrawRows(&ab);

  // Draw the status bar and message
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  // Move the cursor to the correct position, including scroll offset
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                            (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  // Renable the cursor
  abAppend(&ab, "\x1b[?25h", 6);

  // Draw the buffer to the screen and clear the buffer
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/**
 * @brief Append the status message from E.statusmsg to the append buffer
 * 
 * @param fmt a format string 
 * @param ... a variable number of arguements, similar to printf
 */
void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/

/**
 * @brief Displays a prompt in the status bar.
 * 
 * Also provides a way for the user to input a line of text after the prompt
 * 
 * @param prompt a prompt to display to the user in the status bar
 */
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  // Allocate a buffer for the prompt + response
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';


  while (1) {
    // Set the message to the prompt
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    // Await a response from the user
    int c = editorReadKey();

    // Allow for the user to backspace within the prompt
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } 

    // Cancels an input prompt with escape
    else if (c == '\x1b') {
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    }

    // When the user hits enter they are done typing
    else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
        return buf;
      }
    } 
    
    // If the user types another char append it to the buffer
    else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback) callback(buf, c);
  }
}

/**
 * @brief Moves the cursor based on the arrow keys
 * 
 * @param key the key that user clicked
 */
void editorMoveCursor(int key) {

  // Get the row that the cursor is currently on
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) {
        E.cy++;
      }
      break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

/**
 * @brief Proccess the user keypress to preform the correct option
 */
void editorProcessKeypress(void) {
  static int quit_times = KILO_QUIT_TIMES;
  int c = editorReadKey();

  switch (c) {
    // Enter key
    case '\r':
      editorInsertNewline();
      break;

    // Quit
    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more time to quit.", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;

    // Save
    case CTRL_KEY('s'):
      editorSave();
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;

    case CTRL_KEY('f'):
      editorFind();
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }

        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    // Ctrl-L and Escape keys are disabled
    case CTRL_KEY('l'):
    case '\x1b':
      break;

    default:
      editorInsertChar(c);
      break;
  }

  quit_times = KILO_QUIT_TIMES;
}

/*** init ***/

/**
 * @brief Initializes default values for editor config 
 */
void initEditor(void) {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.syntax = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage(
    "HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
