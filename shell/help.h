/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HELP_H
#define HELP_H

/*
 * The help system (shell/help.c): every command's page, kept in one table.
 *
 *   help                 the commands by topic, and how to get more
 *   help <command>       its page: synopsis, description, options, examples
 *   help <topic>         a topic's commands, a line each (files, network...)
 *   help -k <word>       commands whose name or text has the word (apropos)
 *   help -a              every command, a line each (a pager on the screen)
 *   help --check         commands the shell knows that have no page
 *   man <command>, <command> --help   the same page
 */

/* the `help` / `man` / `apropos` commands: args after the command name */
void help_command(const char* args);
void help_man(const char* args);
void help_apropos(const char* args);
/* `<cmd> --help`: 1 if cmd has a page (it was shown) */
int  help_show_page(const char* cmd);
/* an unknown command: "did you mean ...?" lines for the closest names */
void help_suggest(const char* word);
/* the shell's own list of command names, for --check and suggestions */
void help_set_known(const char* const* names);

#endif
