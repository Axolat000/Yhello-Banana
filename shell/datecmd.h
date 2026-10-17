#ifndef DATECMD_H
#define DATECMD_H

/* `date` (show, `date set`, `date utc on|off`) and `timezone` (shell/datecmd.c):
 * 1 if argv[0] was one of them (and ran it) */
int datecmd_dispatch(int argc, char** argv);

/* sets the clock to date ("2026-10-17" / "17/10/2026") and time ("18:30[:15]"),
 * local time - either may be NULL or "" (kept as it is): 0, or -1 and err */
int date_set_text(const char* date, const char* time, char* err, int cap);

#endif
