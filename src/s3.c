#include "s3.h"

void construct_shell_prompt(char shell_prompt[])
{
    strcpy(shell_prompt, "[s3]$ ");
}

void read_command_line(char line[])
{
    char shell_prompt[MAX_PROMPT_LEN];
    construct_shell_prompt(shell_prompt);
    printf("%s", shell_prompt);
    fflush(stdout);

    if (fgets(line, MAX_LINE, stdin) == NULL)
    {
        perror("fgets failed");
        exit(1);
    }

    // Strip newline
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
        line[len - 1] = '\0';
}

void parse_command(char line[], char *args[], int *argsc)
{
    char *token = strtok(line, " ");
    *argsc = 0;
    while (token != NULL && *argsc < MAX_ARGS - 1)
    {
        args[(*argsc)++] = token;
        token = strtok(NULL, " ");
    }
    args[*argsc] = NULL;
}

bool command_with_redirection(const char *line)
{
    return strstr(line, "<") || strstr(line, ">");
}

int parse_command_and_redirs(char line[], char *args[], int *argsc, Redirections *r)
{
    memset(r, 0, sizeof(*r));
    *argsc = 0;

    for (char *tok = strtok(line, " "); tok && *argsc < MAX_ARGS - 1; tok = strtok(NULL, " "))
    {
        if (strcmp(tok, "<") == 0)
        {
            char *path = strtok(NULL, " ");
            if (!path) { fprintf(stderr, "s3: syntax error near '<'\n"); return -1; }
            r->in_path = path;
            continue;
        }

        if (strcmp(tok, ">") == 0 || strcmp(tok, ">>") == 0)
        {
            bool append = (tok[1] == '>');
            char *path = strtok(NULL, " ");
            if (!path) { fprintf(stderr, "s3: syntax error near '>'\n"); return -1; }
            r->out_path = path;
            r->out_append = append;
            continue;
        }

        if (strcmp(tok, "2>") == 0 || strcmp(tok, "2>>") == 0)
        {
            bool append = (tok[2] == '>');
            char *path = strtok(NULL, " ");
            if (!path) { fprintf(stderr, "s3: syntax error near '2>'\n"); return -1; }
            r->err_path = path;
            r->err_append = append;
            r->merge_err_to_out = false;
            continue;
        }

        if (strcmp(tok, "2>&1") == 0)
        {
            r->merge_err_to_out = true;
            r->err_path = NULL;
            continue;
        }

        if (strcmp(tok, "&>") == 0 || strcmp(tok, ">&") == 0)
        {
            char *path = strtok(NULL, " ");
            if (!path) { fprintf(stderr, "s3: syntax error near '&>'\n"); return -1; }
            r->both_to_path = true;
            r->both_path = path;
            r->out_path = r->err_path = NULL;
            continue;
        }

        // Normal token
        args[(*argsc)++] = tok;
    }

    args[*argsc] = NULL;
    return (*argsc == 0) ? -1 : 0;
}

int validate_redirs(const Redirections *r)
{
    struct stat st;
    const char *paths[] = { r->out_path, r->err_path, r->both_path };
    for (int i = 0; i < 3; i++)
    {
        const char *p = paths[i];
        if (p && stat(p, &st) == 0 && S_ISDIR(st.st_mode))
        {
            fprintf(stderr, "s3: '%s' is a directory\n", p);
            return -1;
        }
    }
    return 0;
}

int open_redirection_fds(const Redirections *r, int fds[3])
{
    fds[0] = fds[1] = fds[2] = -1;

    if (r->in_path)
    {
        int fd = open(r->in_path, O_RDONLY);
        if (fd < 0) { perror(r->in_path); return -1; }
        fds[0] = fd;
    }

    if (r->both_to_path)
    {
        int fd = open(r->both_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror(r->both_path); return -1; }
        fds[1] = fd;
        fds[2] = dup(fd);
        if (fds[2] < 0) { perror("dup"); close(fd); return -1; }
        return 0;
    }

    if (r->out_path)
    {
        int flags = O_WRONLY | O_CREAT | (r->out_append ? O_APPEND : O_TRUNC);
        int fd = open(r->out_path, flags, 0644);
        if (fd < 0) { perror(r->out_path); return -1; }
        fds[1] = fd;
    }

    // r->merge_err_to_out handled in dup2

    if (r->err_path)
    {
        int flags = O_WRONLY | O_CREAT | (r->err_append ? O_APPEND : O_TRUNC);
        int fd = open(r->err_path, flags, 0644);
        if (fd < 0) { perror(r->err_path); return -1; }
        fds[2] = fd;
    }

    return 0;
}

void child_exec_no_redirs(char *args[], int argsc)
{
    execvp(args[ARG_PROGNAME], args);
    perror(args[ARG_PROGNAME]);
    _exit(127);
}

void child_exec_with_redirs(char *args[], int argsc, const Redirections *r)
{
    int fds[3];
    if (open_redirection_fds(r, fds) < 0)
        _exit(1);

    if (fds[0] >= 0 && dup2(fds[0], STDIN_FILENO) < 0)
    {
        perror("dup2 stdin"); _exit(1);
    }

    if (fds[1] >= 0 && dup2(fds[1], STDOUT_FILENO) < 0)
    {
        perror("dup2 stdout"); _exit(1);
    }

    if (r->merge_err_to_out)
    {
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0)
        {
            perror("dup2 2>&1"); _exit(1);
        }
    }
    else if (fds[2] >= 0 && dup2(fds[2], STDERR_FILENO) < 0)
    {
        perror("dup2 stderr"); _exit(1);
    }

    if (fds[0] >= 0) close(fds[0]);
    if (fds[1] >= 0) close(fds[1]);
    if (fds[2] >= 0) close(fds[2]);

    execvp(args[ARG_PROGNAME], args);
    perror(args[ARG_PROGNAME]);
    _exit(127);
}


void launch_program(char *args[], int argsc)
{
    pid_t rc = fork();
    if (rc < 0)
    {
        perror("fork");
        return;
    }
    if (rc == 0)
    {
        child_exec_no_redirs(args, argsc);
    }
    // parent: reap() called by main
}

void launch_program_with_redirection(char *args[], int argsc, const Redirections *r)
{
    pid_t rc = fork();
    if (rc < 0)
    {
        perror("fork");
        return;
    }
    if (rc == 0)
    {
        child_exec_with_redirs(args, argsc, r);
    }
    // parent: reap() called by main
}
