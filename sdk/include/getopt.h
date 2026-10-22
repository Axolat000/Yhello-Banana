/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _GETOPT_H
#define _GETOPT_H
#ifdef __cplusplus
extern "C" {
#endif
/* command line options, as GNU's getopt: the arguments are permuted so
 * options may come after operands (unless POSIXLY_CORRECT is set or the
 * option string starts with +); -- ends the options */
extern char* optarg;
extern int optind, opterr, optopt;
struct option {
    const char* name;
    int has_arg;              /* no_argument, required_argument, optional_argument */
    int* flag;
    int val;
};
#define no_argument       0
#define required_argument 1
#define optional_argument 2
int getopt(int argc, char* const argv[], const char* opts);
int getopt_long(int argc, char* const argv[], const char* opts, const struct option* longopts, int* index);
int getopt_long_only(int argc, char* const argv[], const char* opts, const struct option* longopts, int* index);
#ifdef __cplusplus
}
#endif
#endif
