#include "s3.h"

int main(int argc, char *argv[]) {
    char line[MAX_LINE];
    char line_copy[MAX_LINE];
    char *args[MAX_ARGS];
    int argsc;

    while (1) {
        read_command_line(line);

        if (strlen(line) == 0) {
            continue;
        }

        strcpy(line_copy, line);

        if (command_with_redirection(line_copy)) {
            parse_command(line, args, &argsc);
            launch_program_with_redirection(args, argsc);
            reap();
        } else {
            parse_command(line, args, &argsc);
            launch_program(args, argsc);
            reap();
        }
    }

    return 0;
}
