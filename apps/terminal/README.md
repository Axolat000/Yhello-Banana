# Terminal preferences

The integrated Terminal remains the desktop's single terminal launcher. Open
preferences with the sliders button beside **+**, or **Ctrl+Shift+,**.

- Four starting palettes: midnight, light, solarized and mono.
- Background, text and cursor colors, with a live preview. Click a hex value
  to type six RGB digits; Enter applies, Esc cancels. Arrows cycle color swatches.
- Monospace text size from 8 to 40 px; Ctrl+= / Ctrl+- also saves the new size.
- Block, underline or bar cursor.
- System shell, Bash or Zsh for new tabs/windows. System follows `chsh`;
  a terminal-specific choice leaves existing sessions and the console unchanged.

Changes apply to every window in that Terminal process and are saved automatically
in `~/.config/banana-terminal.conf`. A save error is shown in preferences.
Closing/reopening Terminal reloads the saved settings. An installed system keeps
them across reboot. A live CD needs installation or persistence to keep files.

Advanced usage (the internal executable, without a separate application entry):

    /apps/terminal/app-x86_64 --preferences
    /apps/terminal/app-x86_64 --show-config
    /apps/terminal/app-x86_64 --set theme solarized
    /apps/terminal/app-x86_64 --set foreground '#D6E3EE'
    /apps/terminal/app-x86_64 --set cursor bar
    /apps/terminal/app-x86_64 --set shell zsh

Other keys are `background`, `cursor_color` and `font`. RGB values must contain
exactly six hexadecimal digits (an optional `#` is accepted). Invalid values
leave the saved configuration unchanged.

**F11** toggles fullscreen for the focused desktop application. It hides the
window frame/taskbar and restores the previous geometry, including maximized or
snapped state. Resizable SDK apps receive their new client dimensions; fixed-size
apps are centered without changing their buffers. Alt+Tab and Alt+F4 remain available.
