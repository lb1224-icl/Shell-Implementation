#include "s3.h"

int main(int argc, char *argv[]) {
    char line[MAX_LINE];
    char *args[MAX_ARGS];
    int argsc;

    while (1) {
        read_command_line(line);

        if (strlen(line) == 0) {
            continue;
        }

        char *commands[MAX_CMDS];
        int num_cmds = split_by_semicolon(line, commands);

        for (int i = 0; i < num_cmds; i++) {
            // Optional: trim whitespace around commands[i]
            char *cmd = trim(commands[i]);
            if (strlen(cmd) == 0) continue;

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

    return 0;
}
