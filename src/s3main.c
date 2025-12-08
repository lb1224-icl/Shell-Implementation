#include "s3.h"
#include "history.h"

int interactive_shell = 1;

static void execute_command_line(char *line, char *shell_path) {
    trim_whitespace(line);
    if (strlen(line) == 0)
        return;

    char expanded[MAX_LINE];
    if (interactive_shell) {
        int exp = history_expand(line, expanded);
        if (exp == -1) return;
        if (exp == 1) {
            printf("%s\n", expanded);
            strncpy(line, expanded, MAX_LINE);
        }
    }

    if (interactive_shell) {
        history_add(line);
        history_save(line);
    }

    char *commands[MAX_CMDS];
    int num_cmds = split_by_semicolon(line, commands);

    char *args[MAX_ARGS];
    int argsc;

    for (int i = 0; i < num_cmds; i++) {
        char *cmd = commands[i];
        trim_whitespace(cmd);

        if (strlen(cmd) == 0) continue;

        // Built-in history (only in interactive shell)
        if (interactive_shell && strcmp(cmd, "history") == 0) {
            history_print();
            continue;
        }

        if (is_subshell(cmd)) {
            run_subshell(cmd, shell_path);
            continue;
        }

        if (command_with_redirection(cmd)) {
            parse_command(cmd, args, &argsc);
            launch_program_with_redirection(args, argsc);
            reap();
        } else {
            parse_command(cmd, args, &argsc);
            launch_program(args, argsc);
            reap();
        }
    }
}

int main(int argc, char *argv[]) {
    char shell_path[256];
    strcpy(shell_path, argv[0]);

    // If this is a subshell: do NOT load or save history
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        interactive_shell = 0;
        execute_command_line(argv[2], shell_path);
        return 0;
    }

    // Interactive mode
    interactive_shell = 1;
    history_init();

    char line[MAX_LINE];

    while (1) {
        read_command_line(line);
        execute_command_line(line, shell_path);
    }

    return 0;
}