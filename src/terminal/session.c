#include "terminal/session.h"
#include "terminal/terminal.h"
#include "core/signals.h"

void terminal_session_begin(TerminalSession *session, const bool mouse_orbit) {
    session->active = true;
    session->mouse_orbit_enabled = mouse_orbit;
    signals_set_terminal_session_active(true);
    terminal_arm_recovery();

    hide_cursor();
    enter_alternate_screen();
    enable_raw_mode();
    terminal_set_mouse_input_enabled(mouse_orbit);
    enable_kitty_keyboard();
    if (mouse_orbit) {
        enable_mouse_orbit_tracking();
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
