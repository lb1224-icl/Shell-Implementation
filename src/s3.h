#ifndef _S3_H_
#define _S3_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdbool.h>

#define MAX_LINE 1024
#define MAX_ARGS 128
#define MAX_PROMPT_LEN 256

enum ArgIndex
{
    ARG_PROGNAME,
    ARG_1,
    ARG_2,
    ARG_3,
};

static inline void reap()
{
    wait(NULL);
}

typedef struct Redirections
{
    char *in_path;         // < file
    char *out_path;        // > or >>
    char *err_path;        // 2> or 2>>
    char *both_path;       // &> file

    bool out_append;       // >>
    bool err_append;       // 2>>
    bool merge_err_to_out; // 2>&1
    bool both_to_path;     // &>
} Redirections;

/// Shell core
void read_command_line(char line[]);
void construct_shell_prompt(char shell_prompt[]);
void parse_command(char line[], char *args[], int *argsc);

/// Redirection parsing + helpers
bool command_with_redirection(const char *line);
int parse_command_and_redirs(char line[], char *args[], int *argsc, Redirections *r);
int open_redirection_fds(const Redirections *r, int fds[3]);
int validate_redirs(const Redirections *r);

/// Child execution
void child_exec_no_redirs(char *args[], int argsc);
void child_exec_with_redirs(char *args[], int argsc, const Redirections *r);

/// Launching
void launch_program(char *args[], int argsc);
void launch_program_with_redirection(char *args[], int argsc, const Redirections *r);

#endif
