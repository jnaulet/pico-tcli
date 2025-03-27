#include "tcli.h"

#include <limits.h>
#include <string.h>
#include <picoRTOS.h>

/*
 * hotfixes
 */

#ifndef S_SPLINT_S
# define assert(x) picoRTOS_assert_void(x)
#endif

#if CONFIG_CC_SDCC
# define strnlen(x, y) strlen(x)
#endif

#if TCLI_CMDLINE_MAX_LEN == 0
# error "Command line length must be larger than zero."
#endif
#if TCLI_MAX_TOKENS == 0
# error "Maximum number of tokens must be at least one."
#endif
#if TCLI_HISTORY_BUF_LEN < 0
# error "History buffer length must be at least zero."
#endif
#if TCLI_OUTPUT_BUF_LEN < 0
# error "Output buffer length must be at least zero."
#endif

#define TCLI_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define TCLI_ASSERT(tcli)                                                      \
    assert((tcli)->cmdline.len <= (size_t)TCLI_CMDLINE_MAX_LEN);        \
    assert((tcli)->cmdline.cursor <= (tcli)->cmdline.len);                     \
    assert((tcli)->cmdline.buf[(tcli)->cmdline.len] == '\0')

#if TCLI_HISTORY_BUF_LEN > 0
#define TCLI_ASSERT_RB(rb)                                                     \
    assert((rb)->head < TCLI_ARRAY_SIZE((rb)->buf));                           \
    assert((rb)->tail < TCLI_ARRAY_SIZE((rb)->buf));                           \
    assert((rb)->count <= TCLI_ARRAY_SIZE((rb)->buf));                         \
    assert((rb)->pos < TCLI_ARRAY_SIZE((rb)->buf));                            \
    assert((rb)->index <= (rb)->count)
#endif

#if TCLI_OUTPUT_BUF_LEN > 0
#define TCLI_ASSERT_OUT_BUF(out_buf)                                           \
    assert((out_buf)->len <= TCLI_ARRAY_SIZE((out_buf)->buf))
#endif

enum {
    TCLI_NUL    = 0x00,
    TCLI_SOH    = 0x01,
    TCLI_STX    = 0x02,
    TCLI_ETX    = 0x03,
    TCLI_EOT    = 0x04,
    TCLI_ENQ    = 0x05,
    TCLI_ACK    = 0x06,
    TCLI_BEL    = 0x07,
    TCLI_BS     = 0x08,
    TCLI_HT     = 0x09,
    TCLI_LF     = 0x0A,
    TCLI_VT     = 0x0B,
    TCLI_FF     = 0x0C,
    TCLI_CR     = 0x0D,
    TCLI_SO     = 0x0E,
    TCLI_SI     = 0x0F,
    TCLI_DLE    = 0x10,
    TCLI_DC1    = 0x11,
    TCLI_DC2    = 0x12,
    TCLI_DC3    = 0x13,
    TCLI_DC4    = 0x14,
    TCLI_NAK    = 0x15,
    TCLI_SYN    = 0x16,
    TCLI_ETB    = 0x17,
    TCLI_CAN    = 0x18,
    TCLI_EM     = 0x19,
    TCLI_SUB    = 0x1A,
    TCLI_ESC    = 0x1B,
    TCLI_FS     = 0x1C,
    TCLI_GS     = 0x1D,
    TCLI_RS     = 0x1E,
    TCLI_US     = 0x1F,
    TCLI_DEL    = 0x7F
};

enum {
    TCLI_OP_NONE = 0,
    TCLI_OP_BACKSPACE_MAX,
    TCLI_OP_DELETE_MAX,
    TCLI_OP_BACKSPACE_ONE,
    TCLI_OP_DELETE_ONE,
    TCLI_OP_CURSOR_FORWARD_MAX,
    TCLI_OP_CURSOR_BACKWARD_MAX,
    TCLI_OP_CURSOR_FORWARD_ONE,
    TCLI_OP_CURSOR_BACKWARD_ONE,
#if TCLI_HISTORY_BUF_LEN > 0
    TCLI_OP_HIST_SEARCH,
    TCLI_OP_HIST_PREV,
    TCLI_OP_HIST_NEXT,
    TCLI_OP_HIST_RESTORE,
#endif
    TCLI_OP_COMPLETE,
    TCLI_OP_SIGINT,
    TCLI_OP_CURSOR_FORWARD_WORD,
    TCLI_OP_CURSOR_BACKWARD_WORD,
    TCLI_OP_BACKSPACE_WORD,
    TCLI_OP_DELETE_WORD,
    TCLI_OP_CLEAR,
    TCLI_OP_ESCAPE,
};

/* *INDENT-OFF* */
static const struct {
    char match;
    unsigned char op;
} ctrl_char_table[] = {
    { (char)TCLI_ACK, (unsigned char)TCLI_OP_CURSOR_FORWARD_ONE  },
    { (char)TCLI_STX, (unsigned char)TCLI_OP_CURSOR_BACKWARD_ONE },
    { (char)TCLI_DEL, (unsigned char)TCLI_OP_BACKSPACE_ONE       },
    { (char)TCLI_BS,  (unsigned char)TCLI_OP_BACKSPACE_ONE       },
    { (char)TCLI_EOT, (unsigned char)TCLI_OP_DELETE_ONE          },
    { (char)TCLI_NAK, (unsigned char)TCLI_OP_BACKSPACE_MAX       },
    { (char)TCLI_VT,  (unsigned char)TCLI_OP_DELETE_MAX          },
    { (char)TCLI_ENQ, (unsigned char)TCLI_OP_CURSOR_FORWARD_MAX  },
    { (char)TCLI_SOH, (unsigned char)TCLI_OP_CURSOR_BACKWARD_MAX },
    { (char)TCLI_ETB, (unsigned char)TCLI_OP_BACKSPACE_WORD      },
#if TCLI_HISTORY_BUF_LEN > 0
    { (char)TCLI_DLE, (unsigned char)TCLI_OP_HIST_PREV           },
    { (char)TCLI_SO,  (unsigned char)TCLI_OP_HIST_NEXT           },
    { (char)TCLI_DC2, (unsigned char)TCLI_OP_HIST_SEARCH         },
#endif
    { (char)TCLI_HT,  (unsigned char)TCLI_OP_COMPLETE            },
    { (char)TCLI_ETX, (unsigned char)TCLI_OP_SIGINT              },
    { (char)TCLI_FF,  (unsigned char)TCLI_OP_CLEAR               },
    { (char)TCLI_ESC, (unsigned char)TCLI_OP_ESCAPE              },
    { (char)TCLI_BEL, (unsigned char)TCLI_OP_ESCAPE              }
};
/* *INDENT-ON* */

enum {
    TCLI_ESC_NONE = 0,
    TCLI_ESC_BRACKET,
    TCLI_ESC_HOME,
    TCLI_ESC_INSERT,
    TCLI_ESC_DELETE,
    TCLI_ESC_END,
    TCLI_ESC_PGUP,
    TCLI_ESC_PGDN
};

/* *INDENT-OFF* */
static const struct {
    unsigned char old_esc;
    char match;
    unsigned char new_esc;
} esc_code_table[] = {
    { (unsigned char)TCLI_ESC_NONE,    '[',    (unsigned char)TCLI_ESC_BRACKET    },
    { (unsigned char)TCLI_ESC_BRACKET, '1',    (unsigned char)TCLI_ESC_HOME       },
    { (unsigned char)TCLI_ESC_BRACKET, '2',    (unsigned char)TCLI_ESC_INSERT     },
    { (unsigned char)TCLI_ESC_BRACKET, '3',    (unsigned char)TCLI_ESC_DELETE     },
    { (unsigned char)TCLI_ESC_BRACKET, '4',    (unsigned char)TCLI_ESC_END        },
    { (unsigned char)TCLI_ESC_BRACKET, '5',    (unsigned char)TCLI_ESC_PGUP       },
    { (unsigned char)TCLI_ESC_BRACKET, '6',    (unsigned char)TCLI_ESC_PGDN       },
    { (unsigned char)TCLI_ESC_BRACKET, '7',    (unsigned char)TCLI_ESC_HOME       },
    { (unsigned char)TCLI_ESC_BRACKET, '8',    (unsigned char)TCLI_ESC_END        }
};

static const struct {
    unsigned char esc;
    char match;
    unsigned char op;
} esc_table[] = {
#if TCLI_HISTORY_BUF_LEN > 0
    { (unsigned char)TCLI_ESC_BRACKET, 'A',     (unsigned char)TCLI_OP_HIST_PREV                },
    { (unsigned char)TCLI_ESC_BRACKET, 'B',     (unsigned char)TCLI_OP_HIST_NEXT                },
    { (unsigned char)TCLI_ESC_NONE,    'r',     (unsigned char)TCLI_OP_HIST_RESTORE             },
#endif
    { (unsigned char)TCLI_ESC_BRACKET, 'C',     (unsigned char)TCLI_OP_CURSOR_FORWARD_ONE       },
    { (unsigned char)TCLI_ESC_BRACKET, 'D',     (unsigned char)TCLI_OP_CURSOR_BACKWARD_ONE      },
    { (unsigned char)TCLI_ESC_DELETE,  '~',     (unsigned char)TCLI_OP_DELETE_ONE               },
    { (unsigned char)TCLI_ESC_HOME,    '~',     (unsigned char)TCLI_OP_CURSOR_BACKWARD_MAX      },
    { (unsigned char)TCLI_ESC_END,     '~',     (unsigned char)TCLI_OP_CURSOR_FORWARD_MAX       },
    { (unsigned char)TCLI_ESC_NONE,    'b',     (unsigned char)TCLI_OP_CURSOR_BACKWARD_WORD     },
    { (unsigned char)TCLI_ESC_NONE,    'f',     (unsigned char)TCLI_OP_CURSOR_FORWARD_WORD      },
    { (unsigned char)TCLI_ESC_NONE,    'd',     (unsigned char)TCLI_OP_DELETE_WORD              }
};
/* *INDENT-ON* */

#if TCLI_HISTORY_BUF_LEN > 0
typedef enum tcli_hist_mode {
    TCLI_HIST_PREV = 0,
    TCLI_HIST_NEXT,
    TCLI_HIST_SAME
} tcli_hist_it_t;

static inline void tcli_rb_reset_pos(tcli_rb_t *const rb)
{
    TCLI_ASSERT_RB(rb);
    rb->pos = rb->head;
    rb->index = 0;
}

static inline bool tcli_rb_at_head(tcli_rb_t *const rb)
{
    TCLI_ASSERT_RB(rb);
    return rb->index == 0;
}

static inline void tcli_rb_scan_backward(tcli_rb_t *const rb)
{
    TCLI_ASSERT_RB(rb);

    if (rb->pos == 0)
        rb->pos = TCLI_ARRAY_SIZE(rb->buf);

    rb->pos--;
}

static inline void tcli_rb_scan_forward(tcli_rb_t *const rb) /*@modifies rb->pos@*/
{
    TCLI_ASSERT_RB(rb);

    rb->pos++;

    if (rb->pos >= TCLI_ARRAY_SIZE(rb->buf))
        rb->pos = 0;
}

static void tcli_rb_pop_first(tcli_rb_t *const rb) /*@modifies rb->count, rb->head@*/
{
    TCLI_ASSERT_RB(rb);

    bool end_found = false;
    size_t prev_head = rb->head;

    while (rb->count != 0) {

        if (rb->head == 0)
            rb->head = TCLI_ARRAY_SIZE(rb->buf);

        const char c = rb->buf[--rb->head];

        if (c == '\0') {
            if (!end_found)
                end_found = true;
            else {
                rb->head = prev_head;
                break;
            }
        }

        rb->count--;
        prev_head = rb->head;
    }

    tcli_rb_reset_pos(rb);
}

static void tcli_rb_pop(tcli_rb_t *const rb) /*@modifies rb->count, rb->tail@*/
{
    TCLI_ASSERT_RB(rb);

    while (rb->count != 0) {
        const char c = rb->buf[rb->tail++];

        if (rb->tail >= TCLI_ARRAY_SIZE(rb->buf))
            rb->tail = 0;

        rb->count--;

        if (c == '\0')
            break;
    }

    if (rb->index > rb->count)
        tcli_rb_reset_pos(rb);
}

static size_t tcli_rb_move_backward(tcli_rb_t *const rb, const size_t max_len)
{
    TCLI_ASSERT_RB(rb);

    if (rb->count == 0 || rb->pos == rb->tail)
        return 0;

    const size_t pos = rb->pos;

    tcli_rb_scan_backward(rb);
    assert(rb->buf[rb->pos] == '\0');
    tcli_rb_scan_backward(rb);

    size_t len = 0;

    while (rb->buf[rb->pos] != '\0') {
        len++;

        if (max_len != 0 && len > max_len) {
            rb->pos = pos;
            return 0;
        }

        if (rb->pos == rb->tail)
            break;

        tcli_rb_scan_backward(rb);
    }

    assert(len != 0);

    if (rb->pos != rb->tail)
        tcli_rb_scan_forward(rb);

    rb->index++;
    return len;
}

static bool tcli_rb_peekcmp(tcli_rb_t *restrict const rb,
                            const char *restrict str, size_t len)
{
    TCLI_ASSERT_RB(rb);

    const size_t pos = rb->pos;

    rb->pos = rb->head;     // Always peek from head
    const size_t l = tcli_rb_move_backward(rb, len);

    if (l == 0) {
        rb->pos = pos;
        return false;
    }

    bool match = l == len;

    if (match) {
        for (size_t i = 0; i < l; i++) {
            assert(*str != '\0');
            if (*str++ != rb->buf[rb->pos]) {
                match = false;
                break;
            }
            tcli_rb_scan_forward(rb);
        }
    }

    rb->pos = pos;
    return match;
}

static bool tcli_rb_push(tcli_rb_t *restrict const rb,
                         /*@unique@*/ const char *restrict str,
                         size_t len, const bool move_pos)
{
    TCLI_ASSERT_RB(rb);

    while (len > 0 && str[len - 1] == ' ')
        len--;

    if (len == 0 || len + 1 > TCLI_ARRAY_SIZE(rb->buf))
        return false;

    if (tcli_rb_peekcmp(rb, str, len))
        return true;

    while (rb->count + len + 1 > TCLI_ARRAY_SIZE(rb->buf))
        tcli_rb_pop(rb);

    const size_t end_len = TCLI_ARRAY_SIZE(rb->buf) - rb->head;

    if (len + 1 > end_len) {
        memcpy(&rb->buf[rb->head], str, end_len);
        memcpy(rb->buf, str + end_len, len - end_len);
        rb->head = len - end_len;
    } else {
        memcpy(&rb->buf[rb->head], str, len);
        rb->head += len;
    }

    // Reset scan position
    rb->buf[rb->head++] = '\0';
    rb->count += len + 1;

    if (move_pos)
        tcli_rb_reset_pos(rb);

    return true;
}

static bool tcli_rb_previous(tcli_rb_t *restrict const rb, char *str,
                             size_t *restrict const len,
                             /*@null@*/ const char *const match_str,
                             const size_t match_len)
{
    TCLI_ASSERT_RB(rb);
    assert(len != 0);

SCAN:;

    const size_t l = tcli_rb_move_backward(rb, 0);

    if (l == 0)
        return false;

    const size_t pos = rb->pos;

    if (match_str != NULL) {
        if (match_len == 0 || *match_str == '\0')
            return false;

        if (l < match_len)
            goto SCAN;

        // Check if we match
        size_t matched = 0;
        const char *c = match_str;
        while (*c != '\0') {
            if (rb->buf[rb->pos] == '\0' || *c++ != rb->buf[rb->pos]) {
                rb->pos = pos;
                goto SCAN;
            }
            if (++matched == match_len)
                break;
            tcli_rb_scan_forward(rb);
        }
        rb->pos = pos;
    }

    for (size_t i = 0; i < l; i++) {
        *str++ = rb->buf[rb->pos];
        tcli_rb_scan_forward(rb);
    }
    rb->pos = pos;

    *str++ = '\0';
    *len = l;
    return true;
}

static bool tcli_rb_current(tcli_rb_t *restrict const rb, char *restrict str,
                            size_t *restrict const len)
{
    TCLI_ASSERT_RB(rb);
    assert(len != 0);

    if (rb->count == 0 || rb->pos == rb->head)
        return false;

    const size_t pos = rb->pos;
    size_t l = 0;

    while (rb->buf[rb->pos] != '\0') {
        *str++ = rb->buf[rb->pos];
        l++;
        tcli_rb_scan_forward(rb);
    }
    rb->pos = pos;

    assert(l != 0);

    *str++ = '\0';
    *len = l;
    return true;
}

static bool tcli_rb_next(tcli_rb_t *restrict const rb, char *restrict str,
                         size_t *restrict const len)
{
    TCLI_ASSERT_RB(rb);
    assert(len != 0);

    if (rb->count == 0 || rb->pos == rb->head)
        return false;

    while (rb->buf[rb->pos] != '\0')
        tcli_rb_scan_forward(rb);

    assert(rb->buf[rb->pos] == '\0');
    tcli_rb_scan_forward(rb);

    assert(rb->index != 0);
    rb->index--;

    return tcli_rb_current(rb, str, len);
}
#endif

static int tcli_itoa(int n, char *const str)
{
    n = n < -999 ? -999 : (n > 999 ? 999 : n);
    char tmp[4] = { '\0', '\0', '\0', '\0' };
    int i = 0;

    if (n < 0) {
        assert(i >= 0 && i < (int)sizeof(tmp));
        tmp[i++] = '-';
    }

    while (n != 0) {
        assert(i >= 0 && i < (int)sizeof(tmp));
        tmp[i++] = (char)('0' + (char)(n % 10));
        n /= 10;
    }

    int j = 0;

    while (j < i) {
        assert(i - j - 1 >= 0 && i - j - 1 < (int)sizeof(tmp));
        str[j] = tmp[i - j - 1];
        j++;
    }

    str[j] = '\0';
    return j;
}

#if TCLI_COMPLETE
static size_t tcli_str_match(const char *const a, const char *const b,
			     const size_t max_len)
{
    if (a == b)
        return strnlen(a, max_len);

    size_t len = 0;

    while (len < max_len && a[len] != '\0' && b[len] != '\0' &&
           a[len] == b[len])
        len++;

    return len;
}
#endif

static size_t tcli_tokenize(char *str, /*@partial@*/ const char **const tokens,
                            const size_t max_tokens)
{
    size_t found_tokens = 0;

    while (found_tokens < max_tokens) {
        while (*str == ' ')
            str++;

        if (*str == '\0')
            break;

        const char *start = str;
        bool esc = false;
        char delim = '\0';

        while (*str != '\0') {
            if (esc) {
                esc = false;
                str++;
                continue;
            }

            if ((esc = *str == '\\')) {
                str++;
                continue;
            }

            if (delim == '\0' && (*str == '"' || *str == '\''))
                delim = *str;
            else if (delim != '\0') {
                if (*str == delim)
                    delim = '\0';
            } else if (*str == ' ')
                /*@innerbreak@*/ break;

            str++;
        }

        char *stop = str;

        if (stop > start) {
            const size_t str_len = (size_t)((uintptr_t)stop - (uintptr_t)start);

            if (str_len >= (size_t)2 && (*start == '"' || *start == '\'') &&
                *start == start[str_len - 1]) {
                start++;
                stop--;
            }

            tokens[found_tokens++] = start;

            if (*stop == '\0')
                break;

            *stop = '\0';
        }

        if (str != stop && *str == '\0')
            break;

        str++;
    }

    return found_tokens;
}

static inline bool tcli_is_input_char(const char c)
{
    return c > (char)31 && c < (char)127;
}

static bool tcli_is_word_char(const char c)
{
    if (c >= '0' && c <= '9')
        return true;

    if (c >= 'a' && c <= 'z')
        return true;

    if (c >= 'A' && c <= 'Z')
        return true;

    return false;
}

void tcli_flush(tcli_t *const tcli)
{
#if TCLI_OUTPUT_BUF_LEN > 0
    TCLI_ASSERT(tcli);
    TCLI_ASSERT_OUT_BUF(&tcli->out_buf);

    if (tcli->out_buf.len == 0)
        return;

    assert(tcli->out_buf.len < TCLI_ARRAY_SIZE(tcli->out_buf.buf));
    tcli->out_buf.buf[tcli->out_buf.len] = '\0';
    tcli_out_cb(tcli->arg, tcli->out_buf.buf);
    tcli->out_buf.len = 0;
#endif
}

void tcli_out(tcli_t *const tcli, const char *str)
{
    TCLI_ASSERT(tcli);

#if TCLI_OUTPUT_BUF_LEN == 0
    tcli_out_cb(tcli->arg, str);
#else
    TCLI_ASSERT_OUT_BUF(&tcli->out_buf);

    while (true) {
        while (tcli->out_buf.len + 1 < TCLI_ARRAY_SIZE(tcli->out_buf.buf)) {
            const char c = *str++;
            if (c == '\0')
                return;
            tcli->out_buf.buf[tcli->out_buf.len++] = c;
        }
        tcli_flush(tcli);
    }
#endif
}

static void tcli_echo_out(tcli_t *const tcli, size_t cursor)
{
    TCLI_ASSERT(tcli);
    assert(cursor <= tcli->cmdline.len);

    if (tcli->echo.mode == TCLI_ECHO_ON) {
        tcli_out(tcli, tcli->cmdline.buf + cursor);
        return;
    }

    while (cursor++ < tcli->cmdline.len)
        tcli_out(tcli, "*");
}

static void tcli_term_move(tcli_t *const tcli, const char code, size_t offset)
{
    TCLI_ASSERT(tcli);
    assert(code == 'C' || code == 'D');

    while (offset != 0) {
        const int this_offset = (int)offset > 999 ? 999 : (int)offset;
        offset -= this_offset;

        char str[7] = { '\0', '\0', '\0', '\0', '\0', '\0', '\0' };
        char *c = str;
        *c++ = '\033';
        *c++ = '[';
        if (this_offset > 1) {
            const int len = tcli_itoa(this_offset, c);
            assert(len <= 3);
            c += len;
        }
        *c++ = code;
        *c = '\0';

        assert(c - str <= (int)sizeof(str));
        tcli_out(tcli, str);
    }
}

/*@unused@*/
static inline void tcli_term_move_cursor_forward(tcli_t *const tcli,
                                                 const size_t offset)
{
    TCLI_ASSERT(tcli);
    tcli_term_move(tcli, 'C', offset);
}

/*@unused@*/
static inline void tcli_term_move_cursor_backward(tcli_t *const tcli,
                                                  const size_t offset)
{
    TCLI_ASSERT(tcli);
    tcli_term_move(tcli, 'D', offset);
}

/*@unused@*/
static inline void tcli_term_clear(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_out(tcli, "\033[2J");
}

/*@unused@*/
static inline void tcli_term_cursor_home(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_out(tcli, "\033[f");
}

/*@unused@*/
static inline void tcli_term_return(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_out(tcli, "\r");
}

/*@unused@*/
static inline void tcli_term_newline(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_out(tcli, "\r\n");
}

/*@unused@*/
static inline void tcli_term_cut(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_out(tcli, "\033[K");
}

/*@unused@*/
static inline void tcli_term_return_cut(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_term_return(tcli);
    tcli_term_cut(tcli);
}

/*@unused@*/
static inline void tcli_term_erase_all(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_out(tcli, "\033[2K");
}

/*@unused@*/
static inline void tcli_term_save_cursor(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_out(tcli, "\033[s");
}

/*@unused@*/
static inline void tcli_term_restore_cursor(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_out(tcli, "\033[u");
}

/*@unused@*/
static inline void tcli_term_cursor_up(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_out(tcli, "\033[A");
}

/*@unused@*/
static inline void tcli_term_cursor_down(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_out(tcli, "\033[B");
}

/*@unused@*/
static void tcli_term_print_from_cursor(tcli_t *const tcli,
                                        const bool save_cursor,
                                        /*@null@*/ const char *color)
{
    TCLI_ASSERT(tcli);

    if (save_cursor)
        tcli_term_save_cursor(tcli);
    if (color != NULL)
        tcli_out(tcli, color);
    tcli_echo_out(tcli, tcli->cmdline.cursor);
    if (color != NULL)
        tcli_out(tcli, TCLI_FORMAT_RESET);
    if (save_cursor)
        tcli_term_restore_cursor(tcli);
}

/*@unused@*/
static inline void tcli_term_reprint_from_cursor(tcli_t *const tcli,
                                                 const bool save_cursor,
                                                 /*@null@*/ const char *const color)
{
    TCLI_ASSERT(tcli);

    tcli_term_cut(tcli);
    tcli_term_print_from_cursor(tcli, save_cursor, color);
}

static void tcli_term_reprint_all(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);
    tcli_term_return_cut(tcli);

#if TCLI_HISTORY_BUF_LEN > 0
    if (tcli->hist.search)
        tcli_out(tcli, tcli->hist.search_prompt);
    else
#endif
    if (tcli->res != 0)
        tcli_out(tcli, tcli->error_prompt);
    else
        tcli_out(tcli, tcli->prompt);

    tcli_echo_out(tcli, 0);

    tcli_term_move_cursor_backward(tcli,
                                   tcli->cmdline.len - tcli->cmdline.cursor);
}

static size_t tcli_max_forward_len(const tcli_t *const tcli, const size_t len)
{
    TCLI_ASSERT(tcli);

    if (len == 0 || tcli->cmdline.cursor == tcli->cmdline.len)
        return 0;

    if (len > tcli->cmdline.len - tcli->cmdline.cursor)
        return tcli->cmdline.len - tcli->cmdline.cursor;

    return len;
}

static size_t tcli_max_backward_len(const tcli_t *const tcli, const size_t len)
{
    TCLI_ASSERT(tcli);

    if (len == 0 || tcli->cmdline.cursor == 0)
        return 0;

    if (len > tcli->cmdline.cursor)
        return tcli->cmdline.cursor;

    return len;
}

static void tcli_cursor_forward(tcli_t *const tcli, size_t len)
{
    TCLI_ASSERT(tcli);

    if ((len = tcli_max_forward_len(tcli, len)) == 0)
        return;

    tcli->cmdline.cursor += len;
    tcli_term_move_cursor_forward(tcli, len);
}

static void tcli_cursor_backward(tcli_t *const tcli, size_t len)
{
    TCLI_ASSERT(tcli);

    if ((len = tcli_max_backward_len(tcli, len)) == 0)
        return;

    tcli->cmdline.cursor -= len;
    tcli_term_move_cursor_backward(tcli, len);
}

static size_t tcli_offset_next_word(const tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    const tcli_cmdline_t *const cmdline = &tcli->cmdline;
    size_t pos = cmdline->cursor;

    if (pos == cmdline->len)
        return 0;

    if (!tcli_is_word_char(cmdline->buf[pos]))
        pos++;

    while (pos != cmdline->len && cmdline->buf[pos] == ' ')
        pos++;

    while (pos != cmdline->len && tcli_is_word_char(cmdline->buf[pos]))
        pos++;

    assert(pos >= cmdline->cursor);
    return pos - cmdline->cursor;
}

static size_t tcli_offset_prev_word(const tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    const tcli_cmdline_t *const cmdline = &tcli->cmdline;
    size_t pos = cmdline->cursor;

    if (pos == 0)
        return 0;

    pos--;

    if (pos != 0 && !tcli_is_word_char(cmdline->buf[pos]))
        pos--;

    while (pos != 0 && cmdline->buf[pos] == ' ')
        pos--;

    while (pos != 0 && tcli_is_word_char(cmdline->buf[pos]))
        pos--;

    assert(cmdline->cursor >= pos);
    size_t offset = cmdline->cursor - pos;

    if (pos != 0 && offset != 0)
        offset--;

    return offset;
}

static void tcli_reprint_line(tcli_t *const tcli, size_t new_len)
{
    TCLI_ASSERT(tcli);

    if (new_len > (size_t)TCLI_CMDLINE_MAX_LEN)
        new_len = (size_t)TCLI_CMDLINE_MAX_LEN;

    tcli_term_move_cursor_backward(tcli, tcli->cmdline.cursor);
    tcli_term_cut(tcli);
    tcli->cmdline.len = tcli->cmdline.cursor = new_len;
    assert(tcli->cmdline.buf[tcli->cmdline.len] == '\0');
}

static inline void tcli_erase_line(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    tcli->cmdline.buf[0] = '\0';
    tcli_reprint_line(tcli, 0);
}

static void tcli_reprint_all(tcli_t *const tcli, size_t new_len)
{
    TCLI_ASSERT(tcli);

    if (new_len > (size_t)TCLI_CMDLINE_MAX_LEN)
        new_len = (size_t)TCLI_CMDLINE_MAX_LEN;

    tcli_term_return_cut(tcli);
    tcli->cmdline.len = tcli->cmdline.cursor = new_len;
    assert(tcli->cmdline.buf[tcli->cmdline.len] == '\0');
    tcli_term_reprint_all(tcli);
}

static inline void tcli_erase_all(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    tcli->cmdline.buf[0] = '\0';
    tcli_reprint_all(tcli, 0);
}

void tcli_clear_screen(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    tcli_term_clear(tcli);
    tcli_term_cursor_home(tcli);
    tcli_term_reprint_all(tcli);
}

static void tcli_insert(tcli_t *const tcli,
                        /*@unique@*/ const char *const str,
                        size_t len,
                        const bool output)
{
    TCLI_ASSERT(tcli);

    if (len == 0 || tcli->cmdline.len == (size_t)TCLI_CMDLINE_MAX_LEN)
        return;

    if (len > (size_t)TCLI_CMDLINE_MAX_LEN - tcli->cmdline.len)
        len = (size_t)TCLI_CMDLINE_MAX_LEN - tcli->cmdline.len;

    memmove(tcli->cmdline.buf + tcli->cmdline.cursor + len,
            tcli->cmdline.buf + tcli->cmdline.cursor,
            tcli->cmdline.len - tcli->cmdline.cursor + 1);
    memcpy(tcli->cmdline.buf + tcli->cmdline.cursor, str, len);

    tcli->cmdline.len += len;
    if (output)
        tcli_term_print_from_cursor(tcli, false, NULL);
    tcli->cmdline.cursor += len;
    if (output)
        tcli_term_move_cursor_backward(tcli, tcli->cmdline.len -
                                       tcli->cmdline.cursor);
}

static void tcli_backspace(tcli_t *const tcli, size_t len, const bool output)
{
    TCLI_ASSERT(tcli);

    if ((len = tcli_max_backward_len(tcli, len)) == 0)
        return;

    memmove(tcli->cmdline.buf + tcli->cmdline.cursor - len,
            tcli->cmdline.buf + tcli->cmdline.cursor,
            tcli->cmdline.len - tcli->cmdline.cursor + 1);

    tcli->cmdline.len -= len;
    tcli->cmdline.cursor -= len;
    if (output) {
        tcli_term_move_cursor_backward(tcli, len);
        tcli_term_reprint_from_cursor(tcli, true, NULL);
    }
}

static void tcli_delete(tcli_t *const tcli, size_t len, const bool output)
{
    TCLI_ASSERT(tcli);

    if ((len = tcli_max_forward_len(tcli, len)) == 0)
        return;

    memmove(tcli->cmdline.buf + tcli->cmdline.cursor,
            tcli->cmdline.buf + tcli->cmdline.cursor + len,
            tcli->cmdline.len - tcli->cmdline.cursor + 1);

    tcli->cmdline.len -= len;
    if (output)
        tcli_term_reprint_from_cursor(tcli, true, NULL);
}

/*@unused@*/
static inline void tcli_replace(tcli_t *const tcli, size_t len,
                                const char *const str, const size_t str_len,
                                const bool output)
{
    TCLI_ASSERT(tcli);

    tcli_delete(tcli, len, str_len == 0);
    tcli_insert(tcli, str, str_len, output);
}

#if TCLI_HISTORY_BUF_LEN > 0
static void tcli_hist_reset(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    if (tcli->hist.has_line)
        tcli_rb_pop_first(&tcli->hist.rb);

    tcli_rb_reset_pos(&tcli->hist.rb);
    tcli->hist.has_line = false;
    tcli->hist.search = false;
    tcli->hist.next = false;
}

static inline void tcli_hist_reset_mode(tcli_t *const tcli)
{
    if (tcli->hist.mode == TCLI_HIST_OFF_ONCE)
        tcli->hist.mode = TCLI_HIST_ON;
}
#endif

static inline void tcli_echo_reset_mode(tcli_t *const tcli)
{
    if (tcli->echo.mode == TCLI_ECHO_OFF_ONCE)
        tcli->echo.mode = TCLI_ECHO_ON;
}

#if TCLI_COMPLETE
static size_t tcli_complete_match_overlap_len(const char **const matches,
                                              const size_t count)
{
    if (count == 0)
        return 0;

    const char *const match = matches[0];

    assert(match != NULL);

    if (count == (size_t)1)
        return strlen(match);

    size_t match_len = SIZE_MAX;

    for (size_t i = (size_t)1; i < count; i++) {
        assert(matches[i] != NULL);
        const size_t m = tcli_str_match(match, matches[i], match_len);
        if (m >= match_len)
            continue;
        match_len = m;
        if (m == 0)
            break;
    }

    return match_len;
}

static size_t
tcli_complete_match_tokenize(tcli_t *const tcli, const size_t cursor,
                             const char **const token, size_t *const token_len,
                             /*@partial@*/ const char **const tokens,
			     const size_t max_tokens)
{
    TCLI_ASSERT(tcli);
    assert(token_len != 0);

    if (max_tokens == 0)
        return 0;

    assert(cursor <= tcli->cmdline.len && cursor <= tcli->cmdline.cursor);
    size_t token_count = tcli_tokenize(tcli->cmdline.buf, tokens, max_tokens);

    assert(token_count <= max_tokens);

    *token = NULL;

    size_t match_index = token_count;

    while (match_index != 0) {
        const char *const t = tokens[--match_index];
        assert(t != NULL);
        if (tcli->cmdline.buf + cursor >= t) {
            *token = t;
            break;
        }
    }

    if (*token != NULL) {
        const char *end_token = NULL;
        size_t end_index = token_count;
        while (end_index > match_index + 1) {
            const char *const t = tokens[--end_index];
            assert(t != NULL);
            if (tcli->cmdline.buf + cursor >= t) {
                end_token = t;
                break;
            }
        }

        if (end_token != NULL) {
            assert(end_token > *token);
            *token_len = end_token - *token;
        } else if (tcli->cmdline.buf + cursor <= *token + strlen(*token)) {
            assert(*token >= tcli->cmdline.buf);
            assert(tcli->cmdline.cursor >=
                   (size_t)(*token - tcli->cmdline.buf));
            *token_len = tcli->cmdline.cursor - (*token - tcli->cmdline.buf);
        } else
            *token = NULL;
    }

    if (!(*token != NULL) && token_count < max_tokens) {
        *token = tokens[token_count++] = &tcli->cmdline.buf[cursor];
        *token_len = 0;
    }

    return token_count;
}

static size_t tcli_complete_match_complete(tcli_t *const tcli,
                                           const char *const token,
                                           const char **const tokens,
                                           const size_t token_count,
                                           const char **const completions,
                                           const size_t max_completions)
{
    TCLI_ASSERT(tcli);

    if (token_count == 0 || max_completions == 0)
        return 0;

    assert(token_count <= (size_t)INT_MAX);
    size_t completion_count =
        tcli_complete_cb(tcli->arg, (int)token_count, tokens, token,
                         completions, max_completions);

    assert(completion_count <= max_completions);

    size_t valid_completion_count = 0;

    for (size_t i = 0; i < completion_count; i++) {
        if (completions[i] == NULL)
            continue;

        const char *c = completions[i];
        bool valid = *c != '\0';
        while (valid && *c != '\0')
            valid = tcli_is_input_char(*c++);
        if (valid)
            completions[valid_completion_count++] = completions[i];
    }

    return valid_completion_count;
}

static size_t tcli_complete_match(tcli_t *const tcli, const char **const token,
                                  size_t *const token_len,
                                  /*@partial@*/ const char **const matches,
                                  const size_t max_matches,
                                  size_t *const match_len)
{
    TCLI_ASSERT(tcli);
    assert(token_len != 0);
    assert(match_len != 0);

    if (max_matches == 0)
        return 0;

    assert(!(tcli->complete.selected && !tcli->complete.active));

    // Select cursor to match to
    const size_t cursor =
        tcli->complete.active ? tcli->complete.cursor : tcli->cmdline.cursor;

    if (!tcli->complete.active)
        tcli->complete.cursor = cursor;

    assert(cursor <= tcli->cmdline.len && cursor <= tcli->cmdline.cursor);

    // Split tokens
    const char *tokens[TCLI_MAX_TOKENS];
    const size_t token_count = tcli_complete_match_tokenize(
        tcli, cursor, token, token_len, tokens, TCLI_ARRAY_SIZE(tokens));

    assert(token_count <= TCLI_ARRAY_SIZE(tokens));

    size_t match_count = 0;

    if (token_count != 0) {
        assert(*token != NULL);
        assert(tcli->cmdline.buf + cursor >= *token &&
               tcli->cmdline.buf + cursor <= *token + *token_len);

        // Limit token to cursor
        const char old_cursor_char = tcli->cmdline.buf[cursor];
        tcli->cmdline.buf[cursor] = '\0';

        match_count = tcli_complete_match_complete(
            tcli, *token, tokens, token_count, matches, max_matches);
        assert(match_count <= max_matches);

        if (match_count != 0)
            *match_len = tcli_complete_match_overlap_len(matches, match_count);

        // Restore cursor
        tcli->cmdline.buf[cursor] = old_cursor_char;
    }

    // Restore splits
    for (size_t i = 0; i < tcli->cmdline.len; i++)
        if (tcli->cmdline.buf[i] == '\0')
            tcli->cmdline.buf[i] = ' ';

    return match_count;
}

static void tcli_complete_apply(tcli_t *const tcli, const char *const token,
                                const size_t token_len, const char *const match,
                                const size_t match_len, const bool append_space)
{
    TCLI_ASSERT(tcli);

    if (match_len == 0)
        return;

    assert(token >= tcli->cmdline.buf &&
           token <= tcli->cmdline.buf + tcli->cmdline.len);

    const size_t cursor = token - tcli->cmdline.buf;

    assert(cursor <= tcli->cmdline.cursor);
    const size_t pre_move = tcli->cmdline.cursor - cursor;
    size_t post_move = match_len;

    tcli->cmdline.cursor = cursor;
    tcli_replace(tcli, token_len, match, match_len, false);

    if (append_space) {
        post_move++;
        tcli_insert(tcli, " ", (size_t)1, false);
    }

    tcli_term_move_cursor_backward(tcli, pre_move);
    const size_t new_cursor = tcli->cmdline.cursor;

    tcli->cmdline.cursor = cursor;
    tcli_term_reprint_from_cursor(tcli, true, NULL);
    tcli_term_move_cursor_forward(tcli, post_move);
    tcli->cmdline.cursor = new_cursor;
}

static void tcli_complete_select(tcli_t *const tcli, const char *const token,
                                 const size_t token_len,
                                 const char *const *const matches,
                                 const size_t match_count)
{
    TCLI_ASSERT(tcli);

    if (!tcli->complete.active)
        return;

    if (tcli->complete.selected) {
        if (++tcli->complete.index >= match_count)
            tcli->complete.index = 0;
    } else {
        tcli->complete.selected = true;
        tcli->complete.index = 0;
    }

    assert(tcli->complete.index < match_count);
    const char *const match = matches[tcli->complete.index];

    tcli_complete_apply(tcli, token, token_len, match, strlen(match),
                        tcli->complete.space);
}

static void tcli_complete_print(tcli_t *const tcli,
                                const char *const *const matches,
                                const size_t match_count,
                                const size_t match_len)
{
    TCLI_ASSERT(tcli);

    if (match_count == 0)
        return;

    tcli->complete.active = true;
    tcli_term_save_cursor(tcli);
    tcli_term_newline(tcli);

    for (size_t i = 0; i < match_count; i++) {
        const char *match = matches[i];
        assert(match != NULL);

        if (*match == '\0')
            continue;

        const bool selected =
            i == tcli->complete.index && tcli->complete.selected;

        if (selected)
            tcli_out(tcli, TCLI_SELECTION_FORMAT);

        char buf[2];
        buf[1] = '\0';

        while (*match != '\0') {
            if (match == matches[i] + match_len)
                tcli_out(tcli, TCLI_MATCH_FORMAT);
            buf[0] = *match++;
            tcli_out(tcli, buf);
        }

        tcli_out(tcli, TCLI_FORMAT_RESET);
        if (i + 1 < match_count)
            tcli_out(tcli, " ");
    }

    tcli_term_cursor_up(tcli);
    tcli_term_restore_cursor(tcli);
    tcli_term_cursor_down(tcli);
    tcli_term_cursor_up(tcli);
}

static void tcli_complete(tcli_t *const tcli, const bool select)
{
    TCLI_ASSERT(tcli);

    if (tcli->cmdline.len == (size_t)TCLI_CMDLINE_MAX_LEN)
        return;

    if (tcli->echo.mode != TCLI_ECHO_ON)
        return;

    const char *token = NULL;
    size_t token_len = 0;
    const char *matches[TCLI_MAX_TOKENS];
    size_t match_len = 0;
    const size_t match_count =
        tcli_complete_match(tcli, &token, &token_len, matches,
                            TCLI_ARRAY_SIZE(matches), &match_len);

    if (match_count == 0)
        return;

    assert(matches[0] != NULL);
    assert(token != NULL);
    const bool single_match = (match_count == (size_t)1);

    if (!tcli->complete.active) {
        const bool space_at_cursor =
            (tcli->cmdline.cursor < tcli->cmdline.len &&
             tcli->cmdline.buf[tcli->cmdline.cursor] == ' ') ||
            (tcli->cmdline.cursor + 1 < tcli->cmdline.len &&
             tcli->cmdline.buf[tcli->cmdline.cursor + 1] == ' ');
        tcli->complete.space =
            tcli->cmdline.cursor == tcli->cmdline.len || !space_at_cursor;
        tcli_complete_apply(tcli, token, token_len, matches[0], match_len,
                            single_match ||
                            (!space_at_cursor &&
                             tcli->cmdline.cursor != tcli->cmdline.len));
    }

    if (!single_match) {
        if (select)
            tcli_complete_select(tcli, token, token_len, matches, match_count);

        tcli_complete_print(tcli, matches, match_count, match_len);
    }
}

static void tcli_complete_clear(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    if (!tcli->complete.active)
        return;

    tcli_term_cursor_down(tcli);
    tcli_term_erase_all(tcli);
    tcli_term_cursor_up(tcli);
}

static void tcli_complete_reset(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    if (!tcli->complete.active)
        return;

    tcli->complete.active = false;
    tcli->complete.selected = false;
    tcli->complete.space = false;
}

static void tcli_complete_exit(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    if (tcli->complete.selected) {
        assert(tcli->complete.active);
        assert(tcli->complete.cursor <= tcli->cmdline.cursor);
        const size_t len = tcli->cmdline.cursor - tcli->complete.cursor;
        tcli_cursor_backward(tcli, len);
        tcli_delete(tcli, len, true);
    }

    tcli_complete_reset(tcli);
}
#endif

static void tcli_reset_all(tcli_t *const tcli, const bool reset_modes,
                           const bool erase)
{
    TCLI_ASSERT(tcli);

#if TCLI_COMPLETE
    tcli_complete_reset(tcli);
#endif

#if TCLI_HISTORY_BUF_LEN > 0
    tcli_hist_reset(tcli);
#endif

    if (reset_modes) {
#if TCLI_HISTORY_BUF_LEN > 0
        tcli_hist_reset_mode(tcli);
#endif
        tcli_echo_reset_mode(tcli);
    }

    if (erase)
        tcli_erase_all(tcli);
}

static bool tcli_unescape(tcli_t *const tcli, const char c,
                          unsigned char *const op)
{
    TCLI_ASSERT(tcli);

    if (c == (char)TCLI_ESC)
        return tcli->esc.esc = !tcli->esc.esc;
    else if (!tcli->esc.esc)
        return false;

    for (size_t i = 0; i < TCLI_ARRAY_SIZE(esc_code_table); i++) {
        if (c != esc_code_table[i].match ||
            tcli->esc.code != esc_code_table[i].old_esc)
            continue;

        tcli->esc.code = esc_code_table[i].new_esc;
        return true;
    }

    for (size_t i = 0; i < TCLI_ARRAY_SIZE(esc_table); i++) {
        if (c != esc_table[i].match || tcli->esc.code != esc_table[i].esc)
            continue;

        *op = esc_table[i].op;
        break;
    }

    tcli->esc.code = (unsigned char)TCLI_ESC_NONE;
    return tcli->esc.esc = false;
}

static void tcli_exec(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    const char *tokens[TCLI_MAX_TOKENS];
    size_t count = tcli_tokenize(tcli->cmdline.buf, tokens, TCLI_ARRAY_SIZE(tokens));

    assert(count <= (size_t)TCLI_MAX_TOKENS);

    if (count == 0)
        return;

    assert(count <= (size_t)INT_MAX);
    tcli->executing = true;
    tcli->res = tcli_exec_cb(tcli->arg, (int)count, tokens);
    tcli->executing = false;
}

static bool tcli_newline(tcli_t *const tcli, const char c)
{
    TCLI_ASSERT(tcli);

    if (c != (char)TCLI_CR && c != (char)TCLI_LF && c != (char)TCLI_NUL) {
        tcli->last_endl = '\0';
        return false;
    }

    if ((tcli->last_endl == (char)TCLI_CR && (c == (char)TCLI_LF || c == (char)TCLI_NUL)) ||
        (tcli->last_endl == (char)TCLI_LF && c == (char)TCLI_CR)) {
        tcli->last_endl = '\0';
        return true;
    }

    tcli->last_endl = c;

#if TCLI_HISTORY_BUF_LEN > 0
    tcli_hist_reset(tcli);

    if (tcli->echo.mode == TCLI_ECHO_ON && tcli->hist.mode == TCLI_HIST_ON)
        (void)tcli_rb_push(&tcli->hist.rb, tcli->cmdline.buf, tcli->cmdline.len, true);
#endif

    tcli_term_newline(tcli);
    tcli_flush(tcli);
    tcli_reset_all(tcli, true, false);
    tcli_exec(tcli);
    tcli_reset_all(tcli, false, true);
    return true;
}

static void tcli_insert_char(tcli_t *const tcli, const char c)
{
    TCLI_ASSERT(tcli);

    if (!tcli_is_input_char(c))
        return;

    tcli_insert(tcli, &c, (size_t)1, true);
}

static void tcli_sigint(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    tcli_sigint_cb(tcli->arg);

    tcli_reset_all(tcli, true, true);
}

#if TCLI_HISTORY_BUF_LEN > 0
static void tcli_hist_remove_line(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    if (tcli->hist.has_line && tcli_rb_at_head(&tcli->hist.rb)) {
        tcli_rb_pop_first(&tcli->hist.rb);
        tcli->hist.has_line = false;
    }
}

static void tcli_hist_set_search_mode(tcli_t *const tcli, const bool search)
{
    TCLI_ASSERT(tcli);

    /* safe(r) boolean comparison */
    if ((tcli->hist.search ? search : !search)) {
        tcli->hist.next = search;
        return;
    }

    if (tcli->echo.mode != TCLI_ECHO_ON && search)
        return;

    if (!search)
        tcli_hist_reset(tcli);

    tcli->hist.search = search;
    tcli_term_reprint_all(tcli);
}

static void tcli_hist_navigate(tcli_t *const tcli, const tcli_hist_it_t mode)
{
    TCLI_ASSERT(tcli);
    assert(mode == TCLI_HIST_PREV || mode == TCLI_HIST_NEXT ||
           mode == TCLI_HIST_SAME);

    if (tcli->echo.mode != TCLI_ECHO_ON)
        return;

    size_t len = 0;

    if (mode == TCLI_HIST_PREV) {
        if (tcli_rb_at_head(&tcli->hist.rb))
            tcli->hist.has_line = tcli_rb_push(&tcli->hist.rb, tcli->cmdline.buf,
                                               tcli->cmdline.len, false);
        if (!tcli_rb_previous(&tcli->hist.rb, tcli->cmdline.buf, &len, NULL, 0)) {
            tcli_hist_remove_line(tcli);
            return;
        }
    } else if (mode == TCLI_HIST_NEXT) {
        if (!tcli_rb_next(&tcli->hist.rb, tcli->cmdline.buf, &len)) {
            tcli_erase_line(tcli);
            tcli_hist_reset(tcli);
            return;
        } else
            tcli->cmdline.cursor = tcli->cmdline.len = len;
        tcli_hist_remove_line(tcli);
    } else {
        if (tcli_rb_at_head(&tcli->hist.rb))
            return;
        if (!tcli_rb_current(&tcli->hist.rb, tcli->cmdline.buf, &len)) {
            tcli_erase_line(tcli);
            tcli_hist_reset(tcli);
            return;
        }
    }

    tcli->cmdline.cursor = tcli->cmdline.len = len;
    tcli_reprint_all(tcli, len);
}

static void tcli_hist_search(tcli_t *const tcli)
{
    TCLI_ASSERT(tcli);

    if (!tcli->hist.search)
        return;

    if (tcli->echo.mode != TCLI_ECHO_ON) {
        tcli_hist_set_search_mode(tcli, false);
        return;
    }

    if (tcli->cmdline.len == 0)
        return;

    if (!tcli->hist.next) {
        tcli_hist_reset(tcli);
        tcli->hist.search = true;
    }

    tcli->hist.next = false;

    if (tcli_rb_previous(&tcli->hist.rb, tcli->cmdline.buf, &tcli->cmdline.len,
                         tcli->cmdline.buf, tcli->cmdline.cursor))
        tcli_term_reprint_from_cursor(tcli, true, TCLI_MATCH_FORMAT);
}
#endif

static void tcli_convert_op(const char c, unsigned char *const op)
{
    for (size_t i = 0; i < TCLI_ARRAY_SIZE(ctrl_char_table); i++) {
        if (c == ctrl_char_table[i].match) {
            *op = ctrl_char_table[i].op;
            return;
        }
    }
}

void tcli_input_char(tcli_t *const tcli, char c)
{
    TCLI_ASSERT(tcli);

    if (c == '\0')
        return;

    unsigned char op = (unsigned char)TCLI_OP_NONE;

    if (tcli_unescape(tcli, c, &op))
        return;

#if TCLI_COMPLETE
    tcli_complete_clear(tcli);
#endif

    if (tcli_newline(tcli, c)) {
        tcli_flush(tcli);
        return;
    }

    if (op == (unsigned char)TCLI_OP_NONE)
        tcli_convert_op(c, &op);

#if TCLI_COMPLETE
    if (op != (unsigned char)TCLI_OP_COMPLETE) {
        if (op == (unsigned char)TCLI_OP_ESCAPE)
            tcli_complete_exit(tcli);
        else
            tcli_complete_reset(tcli);
    }
#endif

    switch (op) {
    case TCLI_OP_NONE:
        tcli_insert_char(tcli, c);
        break;
    case TCLI_OP_BACKSPACE_ONE:
        tcli_backspace(tcli, (size_t)1, true);
        break;
    case TCLI_OP_DELETE_ONE:
        tcli_delete(tcli, (size_t)1, true);
        break;
    case TCLI_OP_BACKSPACE_MAX:
        tcli_backspace(tcli, (size_t)TCLI_CMDLINE_MAX_LEN, true);
        break;
    case TCLI_OP_DELETE_MAX:
        tcli_delete(tcli, (size_t)TCLI_CMDLINE_MAX_LEN, true);
        break;
    case TCLI_OP_BACKSPACE_WORD:
        tcli_backspace(tcli, tcli_offset_prev_word(tcli), true);
        break;
    case TCLI_OP_DELETE_WORD:
        tcli_delete(tcli, tcli_offset_next_word(tcli), true);
        break;
    case TCLI_OP_CURSOR_FORWARD_MAX:
        tcli_cursor_forward(tcli, (size_t)TCLI_CMDLINE_MAX_LEN);
        break;
    case TCLI_OP_CURSOR_BACKWARD_MAX:
        tcli_cursor_backward(tcli, (size_t)TCLI_CMDLINE_MAX_LEN);
        break;
    case TCLI_OP_CURSOR_FORWARD_ONE:
        tcli_cursor_forward(tcli, (size_t)1);
        break;
    case TCLI_OP_CURSOR_BACKWARD_ONE:
        tcli_cursor_backward(tcli, (size_t)1);
        break;
    case TCLI_OP_CURSOR_FORWARD_WORD:
        tcli_cursor_forward(tcli, tcli_offset_next_word(tcli));
        break;
    case TCLI_OP_CURSOR_BACKWARD_WORD:
        tcli_cursor_backward(tcli, tcli_offset_prev_word(tcli));
        break;
#if TCLI_HISTORY_BUF_LEN > 0
    case TCLI_OP_HIST_PREV:
        tcli_hist_navigate(tcli, TCLI_HIST_PREV);
        break;
    case TCLI_OP_HIST_NEXT:
        tcli_hist_navigate(tcli, TCLI_HIST_NEXT);
        break;
    case TCLI_OP_HIST_RESTORE:
        tcli_hist_navigate(tcli, TCLI_HIST_SAME);
        break;
    case TCLI_OP_HIST_SEARCH:
        tcli_hist_set_search_mode(tcli, true);
        break;
#endif
#if TCLI_COMPLETE
    case TCLI_OP_COMPLETE:
        tcli_complete(tcli, true);
        break;
#endif
    case TCLI_OP_CLEAR:
        tcli_clear_screen(tcli);
        break;
    case TCLI_OP_SIGINT:
        tcli_sigint(tcli);
        break;
    case TCLI_OP_ESCAPE:
#if TCLI_HISTORY_BUF_LEN > 0
        tcli_hist_set_search_mode(tcli, false);
#endif
        break;
    default:
        break;
    }

#if TCLI_HISTORY_BUF_LEN > 0
    tcli_hist_search(tcli);
#endif

    tcli_flush(tcli);
}

void tcli_input_str(tcli_t *const tcli, const char *str)
{
    while (*str != '\0')
        tcli_input_char(tcli, *str++);
}

void tcli_input(tcli_t *const tcli, const void *const buf, size_t len)
{
    const char *str = buf;

    while (len != 0) {
        tcli_input_char(tcli, *str++);
        len--;
    }
}

void tcli_set_echo(tcli_t *const tcli, const tcli_echo_mode_t mode)
{
    if (mode > TCLI_ECHO_OFF_ONCE)
        return;

    tcli->echo.mode = mode;
}

#if TCLI_HISTORY_BUF_LEN > 0
void tcli_set_hist(tcli_t *const tcli, const tcli_history_mode_t mode)
{
    if (mode > TCLI_HIST_OFF_ONCE)
        return;

    tcli->hist.mode = mode;
}
#endif

void tcli_set_arg(tcli_t *const tcli, void *const arg)
{
    tcli->arg = arg;
}

void tcli_set_prompt(tcli_t *const tcli, const char *const prompt)
{
    tcli->prompt = prompt;
    tcli_term_reprint_all(tcli);
    tcli_flush(tcli);
}

void tcli_set_error_prompt(tcli_t *const tcli, const char *const error_prompt)
{
    tcli->error_prompt = error_prompt;
    tcli_term_reprint_all(tcli);
    tcli_flush(tcli);
}

#if TCLI_HISTORY_BUF_LEN > 0
void tcli_set_search_prompt(tcli_t *const tcli, const char *const search_prompt)
{
    tcli->hist.search_prompt = search_prompt;
    tcli_term_reprint_all(tcli);
    tcli_flush(tcli);
}
#endif

void tcli_init(tcli_t *const tcli, void *const arg)
{
    memset(tcli, 0, sizeof(*tcli));

    tcli->arg = arg;
    tcli->prompt = TCLI_DEFAULT_PROMPT;
    tcli->error_prompt = TCLI_DEFAULT_ERROR_PROMPT;
#if TCLI_HISTORY_BUF_LEN > 0
    tcli->hist.search_prompt = TCLI_DEFAULT_SEARCH_PROMPT;
#endif
    tcli_term_reprint_all(tcli);
    tcli_flush(tcli);
}

void tcli_log(tcli_t *const tcli, const char *const str)
{
    if (tcli->executing) {
        tcli_out(tcli, str);
        tcli_flush(tcli);
        return;
    }

    tcli_term_return_cut(tcli);
    tcli_out(tcli, str);
    tcli_term_reprint_all(tcli);

#if TCLI_COMPLETE
    if (tcli->complete.active)
        tcli_complete(tcli, false);
#endif

    tcli_flush(tcli);
}
