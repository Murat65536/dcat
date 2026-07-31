#include "terminal/session.h"
#include "core/signals.h"
#include "terminal/chafa_driver.h"
#include "terminal/terminal.h"

void terminal_session_begin(TerminalSession *session, const bool mouse_orbit) {
    ChafaTermInfo *term_info = chafa_driver_get_term_info();
    session->active = true;
    terminal_arm_recovery(term_info);
    signals_set_terminal_session_active(true);

    char sequence[CHAFA_TERM_SEQ_LENGTH_MAX * 2U];
    char *end = chafa_term_info_emit_disable_cursor(term_info, sequence);
    end = chafa_term_info_emit_enable_alt_screen(term_info, end);
    safe_write(sequence, (size_t)(end - sequence));
    enable_raw_mode();
    terminal_set_mouse_input_enabled(mouse_orbit);
#ifdef _WIN32
    safe_write("\x1b[?1004h", 8);
#else
    // Windows Terminal <1.25 uses Win32-input mode; newer terminals prefer Kitty.
    safe_write("\x1b[?9001h\x1b[>31u", 14);
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
}
