#ifndef DCAT_SIGNALS_H
#define DCAT_SIGNALS_H

#include <stdbool.h>

void signals_init(void);
bool signals_should_quit(void);
void signals_request_quit(void);
bool signals_is_resize_pending(void);
void signals_clear_resize_pending(void);
void signals_request_resize(void);
void signals_set_terminal_session_active(bool active);
bool signals_is_terminal_session_active(void);

#endif // DCAT_SIGNALS_H
