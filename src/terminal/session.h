#ifndef DCAT_SESSION_H
#define DCAT_SESSION_H

#include <stdbool.h>

typedef struct TerminalSession {
    bool active;
    bool mouse_orbit_enabled;
} TerminalSession;

void terminal_session_begin(TerminalSession *session, bool mouse_orbit);
void terminal_session_end(TerminalSession *session);

#endif // DCAT_SESSION_H
