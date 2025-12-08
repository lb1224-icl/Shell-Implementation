#include "s3.h"
#include "ctype.h"
#include <limits.h>

static void apply_pipe_fds(int input_fd, int output_fd);

void quote_state_init(QuoteState *qs) {
    qs->in_quote = 0;
    qs->quote_char = '\0';
}

void quote_state_consume(QuoteState *qs, char c) {
    if (c == '"' || c == '\'') {
        if (qs->in_quote && c == qs->quote_char) {
            qs->in_quote = 0;
            qs->quote_char = '\0';
        } else if (!qs->in_quote) {
            qs->in_quote = 1;
            qs->quote_char = c;
        }
    }
}

void construct_shell_prompt(char shell_prompt[]) {
    char cwd[PATH_MAX];
    static const char *prefix = "[s3 ";
    static const char *suffix = "]$ ";

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        strcpy(cwd, "?");
    }

    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t available = 0;
    if (MAX_PROMPT_LEN > prefix_len + suffix_len + 1) {
        available = MAX_PROMPT_LEN - prefix_len - suffix_len - 1; // cwd space + null
    }

    char cwd_display[MAX_PROMPT_LEN];
    if (available > 0) {
        strncpy(cwd_display, cwd, available);
        cwd_display[available] = '\0';
    } else {
        cwd_display[0] = '\0';
    }

    snprintf(shell_prompt, MAX_PROMPT_LEN, "%s%s%s", prefix, cwd_display, suffix);
}

static char prev_directory[PATH_MAX] = "";

int change_directory(const char *path, bool allow_oldpwd) {
    const char *target = path;
    char current_directory[PATH_MAX];

    if (getcwd(current_directory, sizeof(current_directory)) == NULL) {
        current_directory[0] = '\0';
    }

    if (target == NULL || strlen(target) == 0) {
        target = getenv("HOME");
        if (target == NULL)
            target = "/";
    }

    char expanded_target[PATH_MAX];
    if (target[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = "/";
        if (target[1] == '\0') {
            strncpy(expanded_target, home, sizeof(expanded_target));
            expanded_target[sizeof(expanded_target) - 1] = '\0';
        } else if (target[1] == '/' ) {
            snprintf(expanded_target, sizeof(expanded_target), "%s%s", home, target + 1); //target + 1 just means skip first element (linked list)
        } else {
            strncpy(expanded_target, target, sizeof(expanded_target));
            expanded_target[sizeof(expanded_target) - 1] = '\0';
        }
        target = expanded_target;
    }

    if (allow_oldpwd && strcmp(target, "-") == 0) {
        if (prev_directory[0] == '\0') {
            fprintf(stderr, "cd: OLDPWD not set\n");
            return -1;
        }
        target = prev_directory;
        printf("%s\n", target);
    }

    if (chdir(target) != 0) {
        perror("cd");
        return -1;
    }

    if (current_directory[0] != '\0') {
        strncpy(prev_directory, current_directory, sizeof(prev_directory));
        prev_directory[sizeof(prev_directory) - 1] = '\0';
    }

    return 0;
}

void read_command_line(char line[]) {
    char shell_prompt[MAX_PROMPT_LEN];

    construct_shell_prompt(shell_prompt);
    printf("%s", shell_prompt);

    if (fgets(line, MAX_LINE, stdin) == NULL) {
        perror("fgets failed");
        exit(1);
    }

    size_t len = strlen(line);

    line[len - 1] = '\0';
}

void trim_whitespace(char *str) {
    if (str == NULL) return;

    char *src = str;
    char *dst = str;
    QuoteState qs;
    quote_state_init(&qs);

    // Skip initial spaces
    while (isspace((unsigned char)*src)) src++;

    while (*src) {
        if (*src == '"' || *src == '\'') {
            quote_state_consume(&qs, *src);
            *dst++ = *src++;
        } else if (!qs.in_quote && isspace((unsigned char)*src)) {
            // Collapse multiple spaces to one
            *dst++ = ' ';
            while (isspace((unsigned char)*(++src)));
        } else {
            *dst++ = *src++;
        }
    }

    // Remove trailing space
    if (dst > str && isspace((unsigned char)*(dst - 1))) dst--;
    *dst = '\0';
}

int split_by_semicolon(char line[], char *commands[]) {
    int count = 0;
    int depth = 0;
    QuoteState qs;
    quote_state_init(&qs);
    char *start = line;

    for (char *p = line; ; p++) {
        char c = *p;

        if (c == '"' || c == '\'') {
            quote_state_consume(&qs, c);
        } else if (!qs.in_quote) {
            if (c == '(') depth++;
            else if (c == ')') {
                if (depth > 0) depth--;
                else {
                    fprintf(stderr, "Syntax error: unmatched ')'\n");
                    return 0;
                }
            } else if ((c == ';' && depth == 0) || c == '\0') {
                if (p > start) {
                    // Trim trailing spaces
                    char *end = p - 1;
                    while (end > start && isspace((unsigned char)*end)) end--;
                    *(end + 1) = '\0';

                    // Trim leading spaces
                    while (start < end && isspace((unsigned char)*start)) start++;

                    // Store all the start characters, then can iterate over each as its own line (as null terminated)
                    if (*start != '\0')
                        commands[count++] = start;
                }

                if (c == '\0' || count >= MAX_CMDS)
                    break;

                start = p + 1;
            }
        }

        if (c == '\0')
            break;
    }

    if (depth != 0) {
        fprintf(stderr, "Syntax error: unmatched '('\n");
        return 0;
    }

    return count;
}

int is_subshell(char *cmd){
    if (cmd == NULL) return 0;

    while (isspace((unsigned char)*cmd)) cmd++;
    if (*cmd != '(') return 0;

    size_t len = strlen(cmd);
    if (len == 0) return 0;

    while (len > 0 && isspace((unsigned char)cmd[len - 1])) {
        cmd[--len] = '\0';
    }

    if (len == 0) return 0;

    if (cmd[len - 1] != ')')
        return 0;

    return 1;
}

void run_subshell(char *cmd, char *shell_path){
    char *inner = cmd;
    if (*inner == '(') inner++;
    size_t len = strlen(inner);
    if (len > 0 && inner[len - 1] == ')') inner[len - 1] = '\0';

    pid_t pid = fork();
    
    if (pid == 0) {
        execlp(shell_path, shell_path, "-c", inner, NULL);
        perror("exec failed");
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    } else {
        perror("fork failed");
    }
}

void parse_command(char line[], char *args[], int *argsc) {
    char *tok = strtok(line, " ");
    *argsc = 0;

    while (tok && *argsc < MAX_ARGS - 1) {
        char *arg = tok;   // final argument lives in-place inside the original buffer
        char *write = tok; // where we copy characters after stripping quotes
        char *read = tok;  // cursor walking over the raw token
        QuoteState qs;
        quote_state_init(&qs);

        while (1) {
            while (*read) {
                if (*read == '"' || *read == '\'') {
                    if (qs.in_quote && *read == qs.quote_char) {
                        quote_state_consume(&qs, *read); // closing quote, omit from output
                        read++; //increment read so when we copy to write we skip the '"'
                        continue;
                    } else if (!qs.in_quote) {
                        quote_state_consume(&qs, *read); // opening quote, omit from output
                        read++;
                        continue;
                    }
                }

                *write++ = *read++; // regular character survives untouched
            }

            if (!qs.in_quote)
                break;

            *write++ = ' '; // replace the delimiter eaten by strtok when quotes span tokens
            char *next = strtok(NULL, " ");
            if (!next) {
                fprintf(stderr, "s3: unmatched quote\n");
                *write = '\0';
                *argsc = 0;
                return;
            }
            read = next;
        }

        *write = '\0';           // terminate the cleaned argument
        args[(*argsc)++] = arg;   // store pointer to it (arg points to beginning, write points to end)
        tok = strtok(NULL, " ");
    }

    args[*argsc] = NULL;
}

bool command_with_redirection(const char *line) {
    return strstr(line, "<") || strstr(line, ">");
}

bool command_with_pipe(const char *line) {
    QuoteState qs;
    quote_state_init(&qs);

    for (const char *p = line; *p; p++) {
        if (*p == '"' || *p == '\'') {
            quote_state_consume(&qs, *p);
        } else if (!qs.in_quote && *p == '|') {
            return true;
        }
    }

    return false;
}

int split_by_pipe(char line[], char *commands[]) {
    int count = 0;
    QuoteState qs;
    quote_state_init(&qs);
    char *start = line;

    for (char *p = line; ; p++) {
        char c = *p;

        if (c == '"' || c == '\'') {
            quote_state_consume(&qs, c);
        }

        if ((!qs.in_quote && c == '|') || c == '\0') {
            if (p > start) {
                char *end = p - 1;
                while (end > start && isspace((unsigned char)*end)) end--;
                *(end + 1) = '\0';
                while (start < end && isspace((unsigned char)*start)) start++;
                if (*start != '\0')
                    commands[count++] = start;
            }

            if (c == '\0' || count >= MAX_CMDS)
                break;

            start = p + 1;
        }

        if (c == '\0')
            break;
    }

    return count;
}

static void pipeline_subshell_child(const char *inner, const char *shell_path, int input_fd, int output_fd) {
    apply_pipe_fds(input_fd, output_fd);
    execlp(shell_path, shell_path, "-c", inner, NULL);
    perror("subshell exec");
    _exit(127);
}

static int launch_pipeline_stage(char *segment, int input_fd, int output_fd, const char *shell_path) {
    char segment_copy[MAX_LINE]; 
    strncpy(segment_copy, segment, MAX_LINE);
    segment_copy[MAX_LINE - 1] = '\0';
    trim_whitespace(segment_copy);

    if (strlen(segment_copy) == 0) {
        fprintf(stderr, "s3: invalid null command\n");
        return -1;
    }

    if (is_subshell(segment_copy)) {
        char *inner = segment_copy;
        while (isspace((unsigned char)*inner)) inner++;
        inner++;

        size_t len = strlen(inner);
        while (len > 0 && isspace((unsigned char)inner[len - 1])) {
            inner[--len] = '\0';
        }
        if (len > 0 && inner[len - 1] == ')') {
            inner[len - 1] = '\0';
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            pipeline_subshell_child(inner, shell_path, input_fd, output_fd);
        }
        return 0;
    }

    char *args[MAX_ARGS];
    int argsc;
    bool has_redir = command_with_redirection(segment_copy);
    parse_command(segment_copy, args, &argsc);

    if (argsc == 0) {
        fprintf(stderr, "s3: invalid null command\n");
        return -1;
    }

    if (strcmp(args[ARG_PROGNAME], "cd") == 0) {
        fprintf(stderr, "s3: cd cannot be used inside a pipeline\n");
        return -1;
    }

    if (strcmp(args[ARG_PROGNAME], "history") == 0) {
        fprintf(stderr, "s3: history cannot be used inside a pipeline\n");
        return -1;
    }

    if (has_redir) {
        launch_program_with_redirection(args, argsc, input_fd, output_fd);
    } else {
        launch_program(args, argsc, input_fd, output_fd);
    }

    return 0;
}

void run_pipeline(char *cmd, const char *shell_path) {
    char *segments[MAX_CMDS];
    int num = split_by_pipe(cmd, segments);
    if (num <= 0)
        return;

    int prev_read = -1; // shell stdin
    int launched = 0;

    for (int i = 0; i < num; i++) {
        int pipefd[2] = { -1, -1 }; // placeholder for new pipe
        if (i < num - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                if (prev_read >= 0) close(prev_read);
                break;
            }
        }

        // pipefd[1] is write end of pipe, pipefd[0] is read end of pipe

        int input_fd = prev_read;
        int output_fd = (pipefd[1] >= 0) ? pipefd[1] : -1; // pipe changes pipefd values so if pipefd is created then set output to pipefd[1]

        if (launch_pipeline_stage(segments[i], input_fd, output_fd, shell_path) != 0) {
            if (pipefd[0] >= 0) close(pipefd[0]);
            if (pipefd[1] >= 0) close(pipefd[1]);
            if (prev_read >= 0) close(prev_read);
            break;
        }

        launched++;

        if (pipefd[1] >= 0) close(pipefd[1]);
        if (prev_read >= 0) close(prev_read);
        prev_read = (pipefd[0] >= 0) ? pipefd[0] : -1;
    }

    if (prev_read >= 0) close(prev_read);

    for (int i = 0; i < launched; i++) {
        reap();
    }
}

static int extract_redirections(char *args[], int argsc,
                                char *exec_args[], int *exec_argc,
                                Redirections *r) {
    memset(r, 0, sizeof(*r));
    *exec_argc = 0; //new args without ">" and so on

    for (int i = 0; i < argsc; i++) {
        if (strcmp(args[i], "<") == 0) {
            if (i + 1 >= argsc) {
                fprintf(stderr, "s3: syntax error near '<'\n");
                return -1;
            }
            r->in_path = args[++i]; // next argument is in path
        } else if (strcmp(args[i], ">") == 0 || strcmp(args[i], ">>") == 0) {
            if (i + 1 >= argsc) {
                fprintf(stderr, "s3: syntax error near '>'\n");
                return -1;
            }
            r->out_append = (args[i][1] == '>'); // check if ">>"
            r->out_path = args[++i]; // next argument is out path
        } else if (strcmp(args[i], "2>") == 0 || strcmp(args[i], "2>>") == 0) {
            if (i + 1 >= argsc) {
                fprintf(stderr, "s3: syntax error near '2>'\n");
                return -1;
            }
            r->err_append = (args[i][2] == '>'); // check if "2>>"
            r->err_path = args[++i]; // next argument is out path
            r->merge_err_to_out = false;
        } else if (strcmp(args[i], "2>&1") == 0) {
            r->merge_err_to_out = true;
            r->err_path = NULL;
        } else if (strcmp(args[i], "&>") == 0 || strcmp(args[i], ">&") == 0) {
            if (i + 1 >= argsc) {
                fprintf(stderr, "s3: syntax error near '&>'\n");
                return -1;
            }
            r->both_to_path = true;
            r->both_path = args[++i];
            r->out_path = NULL;
            r->err_path = NULL;
        } else {
            exec_args[(*exec_argc)++] = args[i]; // if not a redirect, add to exec_args
        }
    }

    exec_args[*exec_argc] = NULL;

    if (*exec_argc == 0) {
        fprintf(stderr, "s3: empty command\n");
        return -1;
    }

    return 0;
}

int validate_redirs(const Redirections *r) {
    struct stat st;

    const char *outs[] = { r->out_path, r->err_path, r->both_path };

    for (int i = 0; i < 3; i++) {
        const char *p = outs[i];

        if (p && stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
            fprintf(stderr, "s3: '%s' is a directory\n", p);
            return -1;
        }
    }

    if (r->in_path) {
        if (access(r->in_path, F_OK) != 0) { // check if in path exists
            perror(r->in_path);
            return -1;
        }
        if (stat(r->in_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            fprintf(stderr, "s3: '%s' is a directory\n", r->in_path);
            return -1;
        }
    }

    return 0;
}

int open_redirection_fds(const Redirections *r, int fds[3]) {
    fds[0] = fds[1] = fds[2] = -1;

    if (r->in_path) {
        int fd = open(r->in_path, O_RDONLY);
        if (fd < 0) {
            perror(r->in_path);
            return -1;
        }
        fds[0] = fd;
    }

    if (r->both_to_path) {
        int fd = open(r->both_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror(r->both_path);
            return -1;
        }
        fds[1] = fd;
        fds[2] = dup(fd); // differen fd for same file (so not affected by eachother)
        if (fds[2] < 0) {
            perror("dup");
            close(fd);
            return -1;
        }
        return 0;
    }

    if (r->out_path) {
        int flags = O_WRONLY | O_CREAT | (r->out_append ? O_APPEND : O_TRUNC);
        int fd = open(r->out_path, flags, 0644);
        if (fd < 0) {
            perror(r->out_path);
            return -1;
        }
        fds[1] = fd;
    }

    if (r->err_path) {
        int flags = O_WRONLY | O_CREAT | (r->err_append ? O_APPEND : O_TRUNC);
        int fd = open(r->err_path, flags, 0644);
        if (fd < 0) {
            perror(r->err_path);
            return -1;
        }
        fds[2] = fd;
    }

    return 0;
}

void apply_redirections(const Redirections *r, int fds[3]) {
    if (fds[0] >= 0 && dup2(fds[0], STDIN_FILENO) < 0) { // dup2 returns -1 if it fails
        perror("dup2 stdin");
        _exit(1); // _exit is the exit for children, doesnt waste time cleaning up buffers, wait() in parent will deal with it
    }

    if (fds[1] >= 0 && dup2(fds[1], STDOUT_FILENO) < 0) {
        perror("dup2 stdout");
        _exit(1);
    }

    if (r->merge_err_to_out) {
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) { // same fd if we use 2>&1
            perror("dup2 2>&1");
            _exit(1);
        }
    } else if (fds[2] >= 0 && dup2(fds[2], STDERR_FILENO) < 0) {
        perror("dup2 stderr");
        _exit(1);
    }

    if (fds[0] >= 0) close(fds[0]); // close all fds when we no longer need them
    if (fds[1] >= 0) close(fds[1]); // saves space in file descriptor table
    if (fds[2] >= 0) close(fds[2]);
}

static void apply_pipe_fds(int input_fd, int output_fd) {
    if (input_fd >= 0) {
        if (dup2(input_fd, STDIN_FILENO) < 0) {
            perror("dup2 pipe stdin");
            _exit(1);
        }
        close(input_fd);
    }

    if (output_fd >= 0) {
        if (dup2(output_fd, STDOUT_FILENO) < 0) {
            perror("dup2 pipe stdout");
            _exit(1);
        }
        close(output_fd);
    }
}

void child(char *args[], int argsc, int input_fd, int output_fd) {
    apply_pipe_fds(input_fd, output_fd);
    execvp(args[ARG_PROGNAME], args); // change child with requested program
    
    //following only ran if execvp goes wrong
    perror(args[ARG_PROGNAME]);
    _exit(127);
}

void child_exec_with_redirs(char *args[], int argsc, const Redirections *r, int input_fd, int output_fd) {
    int fds[3];

    if (open_redirection_fds(r, fds) < 0) {
        _exit(1);
    }

    apply_pipe_fds(input_fd, output_fd);
    apply_redirections(r, fds);
    execvp(args[ARG_PROGNAME], args);

    //following only ran if execvp goes wrong
    perror(args[ARG_PROGNAME]);
    _exit(127);
}

static int exit_allowed(int input_fd, int output_fd) {
    return (input_fd < 0 && output_fd < 0);
}

void launch_program(char *args[], int argsc, int input_fd, int output_fd) {
    if (argsc > 0 && strcmp(args[ARG_PROGNAME], "exit") == 0) {
        if (!exit_allowed(input_fd, output_fd)) {
            fprintf(stderr, "s3: exit cannot be used in a pipeline\n");
            return;
        }
        int status = 0;

        if (argsc > 1) {
            status = atoi(args[1]);
        }

        printf("Exiting shell with status %d\n", status);
        exit(status);
    }

    pid_t rc = fork();

    if (rc < 0) {
        perror("fork");
        return;
    }

    if (rc == 0) {
        child(args, argsc, input_fd, output_fd);
    }
}

void launch_program_with_redirection(char *args[], int argsc, int input_fd, int output_fd) {
    Redirections r;
    char *exec_args[MAX_ARGS];
    int exec_argc;

    if (extract_redirections(args, argsc, exec_args, &exec_argc, &r) < 0) {
        return;
    }

    if (validate_redirs(&r) != 0) {
        return;
    }

    if (exec_argc > 0 && strcmp(exec_args[ARG_PROGNAME], "exit") == 0) {
        if (!exit_allowed(input_fd, output_fd)) {
            fprintf(stderr, "s3: exit cannot be used in a pipeline\n");
            return;
        }
        int status = 0;

        if (exec_argc > 1) {
            status = atoi(exec_args[1]);
        }

        printf("Exiting shell with status %d\n", status);
        exit(status);
    }

    pid_t rc = fork();

    if (rc < 0) {
        perror("fork");
        return;
    }

    if (rc == 0) {
        child_exec_with_redirs(exec_args, exec_argc, &r, input_fd, output_fd); // change exec of child
    }
}
