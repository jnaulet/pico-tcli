#ifndef TCLI_H
#define TCLI_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TCLI_COLOR_BLACK "\033[30m"
#define TCLI_COLOR_RED "\033[31m"
#define TCLI_COLOR_GREEN "\033[32m"
#define TCLI_COLOR_YELLOW "\033[33m"
#define TCLI_COLOR_BLUE "\033[34m"
#define TCLI_COLOR_MAGENTA "\033[35m"
#define TCLI_COLOR_CYAN "\033[36m"
#define TCLI_COLOR_WHITE "\033[37m"

#define TCLI_COLOR_BRIGHT_BLACK "\033[90m"
#define TCLI_COLOR_BRIGHT_RED "\033[91m"
#define TCLI_COLOR_BRIGHT_GREEN "\033[92m"
#define TCLI_COLOR_BRIGHT_YELLOW "\033[93m"
#define TCLI_COLOR_BRIGHT_BLUE "\033[94m"
#define TCLI_COLOR_BRIGHT_MAGENTA "\033[95m"
#define TCLI_COLOR_BRIGHT_CYAN "\033[96m"
#define TCLI_COLOR_BRIGHT_WHITE "\033[97m"

#define TCLI_COLOR_DEFAULT "\033[39m"

#define TCLI_BG_COLOR_RED "\033[40m"
#define TCLI_BG_COLOR_GREEN "\033[41m"
#define TCLI_BG_COLOR_YELLOW "\033[42m"
#define TCLI_BG_COLOR_BLUE "\033[43m"
#define TCLI_BG_COLOR_PURPLE "\033[44m"
#define TCLI_BG_COLOR_MAGENTA "\033[45m"
#define TCLI_BG_COLOR_CYAN "\033[46m"
#define TCLI_BG_COLOR_WHITE "\033[47m"

#define TCLI_BG_COLOR_DEFAULT "\033[49m"

#define TCLI_FORMAT_BOLD "\033[1m"
#define TCLI_FORMAT_DIM "\033[2m"
#define TCLI_FORMAT_ITALIC "\033[3m"
#define TCLI_FORMAT_UNDERLINE "\033[4m"

#define TCLI_FORMAT_RESET "\033[0m"

#ifndef TCLI_CMDLINE_MAX_LEN
/**
 * The maximum number of characters that the command line can hold (not
 * including null-terminator).
 */
#define TCLI_CMDLINE_MAX_LEN 64
#endif

#ifndef TCLI_MAX_TOKENS
/**
 * The maximum number of tokens that can be processed when tokenizing.
 */
#define TCLI_MAX_TOKENS 12
#endif

#ifndef TCLI_HISTORY_BUF_LEN
/**
 * The size of the ring buffer used to save command history.
 * Set to zero to disable history.
 */
#define TCLI_HISTORY_BUF_LEN 512
#endif

#ifndef TCLI_OUTPUT_BUF_LEN
/**
 * The size of the output buffer.
 * Set to zero to disable output buffering.
 */
#define TCLI_OUTPUT_BUF_LEN 256
#endif

#ifndef TCLI_COMPLETE
/**
 * Enable or disable tab-completion.
 */
#define TCLI_COMPLETE 1
#endif

#ifndef TCLI_DEFAULT_PROMPT
/**
 * The default prompt.
 */
#define TCLI_DEFAULT_PROMPT (TCLI_COLOR_GREEN "> " TCLI_COLOR_DEFAULT)
#endif

#ifndef TCLI_DEFAULT_SEARCH_PROMPT
/**
 * The default search prompt used for backwards search.
 */
#define TCLI_DEFAULT_SEARCH_PROMPT (TCLI_COLOR_GREEN "? " TCLI_COLOR_DEFAULT)
#endif

#ifndef TCLI_DEFAULT_ERROR_PROMPT
/**
 * The default error prompt (displayed when the previous command failed).
 */
#define TCLI_DEFAULT_ERROR_PROMPT (TCLI_COLOR_RED "> " TCLI_COLOR_DEFAULT)
#endif

#ifndef TCLI_MATCH_FORMAT
/**
 * Format used for match during search and tab-completion.
 */
#define TCLI_MATCH_FORMAT TCLI_COLOR_BRIGHT_BLACK
#endif

#ifndef TCLI_SELECTION_FORMAT
/**
 * Format used for selectable matches during tab-completion.
 */
#define TCLI_SELECTION_FORMAT (TCLI_BG_COLOR_WHITE TCLI_COLOR_BLACK)
#endif

typedef struct tcli_cmdline {
    size_t len;
    size_t cursor;
    char buf[TCLI_CMDLINE_MAX_LEN + 1];
} tcli_cmdline_t;

typedef struct tcli_esc {
    unsigned char code;
    bool esc : 1;
} tcli_esc_t;

typedef enum tcli_echo_mode {
    TCLI_ECHO_ON = 0,
    TCLI_ECHO_OFF,
    TCLI_ECHO_OFF_ONCE
} tcli_echo_mode_t;

typedef struct tcli_echo {
    tcli_echo_mode_t mode : 2;
} tcli_echo_t;

#if TCLI_HISTORY_BUF_LEN > 0
typedef enum tcli_history_mode {
    TCLI_HIST_ON = 0,
    TCLI_HIST_OFF,
    TCLI_HIST_OFF_ONCE
} tcli_history_mode_t;

typedef struct tcli_rb {
    size_t head;
    size_t tail;
    size_t count;
    size_t pos;
    size_t index;
    char buf[TCLI_HISTORY_BUF_LEN];
} tcli_rb_t;

typedef struct tcli_hist {
    tcli_rb_t rb;
    tcli_history_mode_t mode : 2;
    bool has_line : 1;
    bool search : 1;
    bool next : 1;
    /*@observer@*/ const char *search_prompt;
} tcli_hist_t;
#endif

#if TCLI_OUTPUT_BUF_LEN > 0
typedef struct tcli_out_buf {
    size_t len;
    char buf[TCLI_OUTPUT_BUF_LEN];
} tcli_out_buf_t;
#endif

#if TCLI_COMPLETE
typedef struct tcli_complete {
    bool active : 1;
    bool selected : 1;
    bool space : 1;
    size_t cursor;
    size_t index;
} tcli_complete_t;
#endif

typedef struct tcli {
    tcli_cmdline_t cmdline;
    tcli_esc_t esc;
    tcli_echo_t echo;
#if TCLI_HISTORY_BUF_LEN > 0
    tcli_hist_t hist;
#endif
#if TCLI_OUTPUT_BUF_LEN > 0
    tcli_out_buf_t out_buf;
#endif
#if TCLI_COMPLETE
    tcli_complete_t complete;
#endif
    /*@temp@*/ void *arg;
    int res;
    /*@observer@*/ const char *prompt;
    /*@observer@*/ const char *error_prompt;
    char last_endl;
    volatile bool executing : 1;
} tcli_t;

/**
 * Initializes the instance.
 * @param tcli Pointer to instance.
 * @param arg User data to be passed to callback functions.
 */
void tcli_init(tcli_t *tcli, void *arg);

/**
 * Pass input to be processed.
 * @param tcli Instance pointer.
 * @param c Character to process.
 */
void tcli_input_char(tcli_t *tcli, char c);

/**
 * Pass string input to be processed.
 * @param tcli Instance pointer.
 * @param str Null-terminated string to process.
 */
void tcli_input_str(tcli_t *tcli, const char *str);

/**
 * Pass data input to be processed.
 * @param tcli Instance pointer.
 * @param buf Pointer to data buffer.
 * @param len Data length.
 */
void tcli_input(tcli_t *tcli, const void *buf, size_t len);

/**
 * Callback function for output.
 * @param arg Pointer to user data.
 * @param str The string to display
 */
extern void tcli_out_cb(void *arg, const char *str);

/**
 * Callback function for command execution.
 * @param arg Pointer to user data.
 * @param argc Number of command line tokens.
 * @param argv Command line tokens.
 */
extern int tcli_exec_cb(void *arg, int argc, const char **argv);

/**
 * Set user data for callback functions.
 * @param tcli Instance pointer.
 * @param arg Pointer to user data.
 */
/*@unused@*/ void tcli_set_arg(tcli_t *tcli, void *arg);

/**
 * Set echo mode for output.
 * @param tcli Instance pointer.
 * @param mode Echo mode.
 */
void tcli_set_echo(tcli_t *tcli, tcli_echo_mode_t mode);

#if TCLI_COMPLETE

/**
 * Callback function for tab-completion.
 * @param arg Pointer to user data.
 * @param argc Number of command line tokens.
 * @param argv Command line tokens.
 * @param
 * @param
 * @param
 */
extern size_t tcli_complete_cb(void *arg, int argc, const char **argv,
                               const char *match, const char **completions,
                               size_t max_count);
#endif

/**
 * Callback function for SIGINT (ctrl+c).
 * @param arg Pointer to user data.
 */
void tcli_sigint_cb(void *arg);

/**
 * Set default prompt string.
 * @param tcli Instance pointer.
 * @param prompt Prompt string.
 */
/*@unused@*/ void tcli_set_prompt(tcli_t *tcli, /*@observer@*/ const char *prompt);

/**
 * Set error prompt string (used when previous command failed).
 * @param tcli Instance pointer.
 * @param error_prompt Prompt string.
 */
/*@unused@*/ void tcli_set_error_prompt(tcli_t *tcli, /*@observer@*/ const char *error_prompt);

#if TCLI_HISTORY_BUF_LEN > 0

/**
 * Set history mode for recording entered lines.
 * @param tcli Instance pointer.
 * @param mode History mode.
 */
/*@unused@*/ void tcli_set_hist(tcli_t *tcli, tcli_history_mode_t mode);

/**
 * Set search prompt (used when backwards-searching).
 * @param tcli Instance pointer.
 * @param search_prompt Prompt string.
 */
/*@unused@*/ void tcli_set_search_prompt(tcli_t *tcli, /*@observer@*/ const char *search_prompt);

#endif

/**
 * Logs a string without disturbing the current prompt.
 * @param tcli Instance pointer.
 * @param str String.
 */
void tcli_log(tcli_t *tcli, const char *str);

/**
 * Flushes output buffer (if used).
 * @param tcli Instance pointer.
 */
/*@unused@*/ void tcli_flush(tcli_t *tcli);

/**
 * Outputs a string through the instance output callback function. May be
 * buffered.
 * @param tcli Instance pointer.
 * @param str String to output.
 */
/*@unused@*/ void tcli_out(tcli_t *tcli, const char *str);

/**
 * Clears the current screen output.
 * @param tcli Instance pointer.
 */
/*@unused@*/ void tcli_clear_screen(tcli_t *tcli);

#endif
