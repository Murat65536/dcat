#pragma once
#include <stdbool.h>

typedef struct TerminalSession {
    bool active;
} TerminalSession;

void terminal_session_begin(TerminalSession *session, bool mouse_orbit);
void terminal_session_end(TerminalSession *session);
