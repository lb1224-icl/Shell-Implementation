#include "s3.h"

void construct_shell_prompt(char shell_prompt[]) {
    strcpy(shell_prompt, "[s3]$ ");
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

void parse_command(char line[], char *args[], int *argsc) {
    char *tok = strtok(line, " ");
    *argsc = 0;

    while (tok && *argsc < MAX_ARGS - 1) {
        args[(*argsc)++] = tok;
        tok = strtok(NULL, " ");
    }

    args[*argsc] = NULL;
}

bool command_with_redirection(const char *line) {
    return strstr(line, "<") || strstr(line, ">");
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

void child(char *args[], int argsc) {
    execvp(args[ARG_PROGNAME], args); // change child with requested program
    
    //following only ran if execvp goes wrong
    perror(args[ARG_PROGNAME]);
    _exit(127);
}

void child_exec_with_redirs(char *args[], int argsc, const Redirections *r) {
    int fds[3];

    if (open_redirection_fds(r, fds) < 0) {
        _exit(1);
    }

    apply_redirections(r, fds);
    execvp(args[ARG_PROGNAME], args);

    //following only ran if execvp goes wrong
    perror(args[ARG_PROGNAME]);
    _exit(127);
}

void launch_program(char *args[], int argsc) {
    if (argsc > 0 && strcmp(args[ARG_PROGNAME], "exit") == 0) {
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
        child(args, argsc);
    }
}

void launch_program_with_redirection(char *args[], int argsc) {
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
        child_exec_with_redirs(exec_args, exec_argc, &r); // change exec of child
    }
}
