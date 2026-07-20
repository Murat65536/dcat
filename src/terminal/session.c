#include "terminal/session.h"
#include "core/signals.h"
#include "terminal/terminal.h"

void terminal_session_begin(TerminalSession *session, const bool mouse_orbit) {
    session->active = true;
    session->mouse_orbit_enabled = mouse_orbit;
    signals_set_terminal_session_active(true);
    terminal_arm_recovery();

    safe_write("\x1b[?25l\x1b[?1049h", 14);
    enable_raw_mode();
    terminal_set_mouse_input_enabled(mouse_orbit);
#ifdef _WIN32
    safe_write("\x1b[?1004h", 8);
#else
    safe_write("\x1b[>31u", 6);
#endif
    if (mouse_orbit) {
        safe_write("\x1b[?1002h\x1b[?1006h\x1b[?1016h", 24);
    }
}

void terminal_session_end(TerminalSession *session) {
    if (!session->active) {
        return;
    }

    terminal_restore_default_state();
    signals_set_terminal_session_active(false);
    session->active = false;
    session->mouse_orbit_enabled = false;
}
