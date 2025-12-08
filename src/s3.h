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
#define MAX_CMDS 128
#define PATH_MAX 600

enum ArgIndex {
    ARG_PROGNAME,
    ARG_1,
    ARG_2,
    ARG_3,
};

typedef struct Redirections {
    char *in_path;
    char *out_path;
    char *err_path;
    char *both_path;
    bool out_append;
    bool err_append;
    bool merge_err_to_out;
    bool both_to_path;
} Redirections;

typedef struct QuoteState {
    int in_quote;
    char quote_char;
} QuoteState;

static inline void reap(void) {
    wait(NULL);
}

void construct_shell_prompt(char shell_prompt[]);
int change_directory(const char *path);
void quote_state_init(QuoteState *qs);
void quote_state_consume(QuoteState *qs, char c);
void read_command_line(char line[]);
void trim_whitespace(char *str);

int split_by_semicolon(char line[], char *commands[]);
int split_by_pipe(char line[], char *commands[]);
int is_subshell(char *cmd);

void run_subshell(char *cmd, char *shell_path);

void parse_command(char line[], char *args[], int *argsc);
bool command_with_redirection(const char *line);
bool command_with_pipe(const char *line);
void run_pipeline(char *cmd, const char *shell_path);

int validate_redirs(const Redirections *r);
int open_redirection_fds(const Redirections *r, int fds[3]);
void apply_redirections(const Redirections *r, int fds[3]);

void child_exec_with_redirs(char *args[], int argsc, const Redirections *r, int input_fd, int output_fd);
void child(char *args[], int argsc, int input_fd, int output_fd);

void launch_program(char *args[], int argsc, int input_fd, int output_fd);
void launch_program_with_redirection(char *args[], int argsc, int input_fd, int output_fd);

#endif
