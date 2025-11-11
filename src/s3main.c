#include "s3.h"

static void execute_command_line(char *line, char *shell_path) {
    trim_whitespace(line);

    if (strlen(line) == 0)
        return;

    char *commands[MAX_CMDS];
    int num_cmds = split_by_semicolon(line, commands);

    char *args[MAX_ARGS];
    int argsc;

    for (int i = 0; i < num_cmds; i++) {
        char *cmd = commands[i];
        if (strlen(cmd) == 0) continue;

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

    // In subshell
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        execute_command_line(argv[2], shell_path);
        return 0;
    }

    // Not in subshell and needs input reading
    char line[MAX_LINE];

    while (1) {
        read_command_line(line);
        execute_command_line(line, shell_path);
    }

    return 0;
}
