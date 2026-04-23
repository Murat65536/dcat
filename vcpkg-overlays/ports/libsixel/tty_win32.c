#include "src/tty.h"

SIXELSTATUS
sixel_tty_wait_stdin(int usec)
{
    (void)usec;
    return SIXEL_OK;
}

SIXELSTATUS
sixel_tty_scroll(
    sixel_write_function f_write,
    int outfd,
    int height,
    int is_animation)
{
    (void)f_write;
    (void)outfd;
    (void)height;
    (void)is_animation;
    return SIXEL_OK;
}
