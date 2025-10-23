#include "s3.h"

int main(int argc, char *argv[])
{
    char line[MAX_LINE];
    char *args[MAX_ARGS];
    int argsc;

    while (1)
    {
        read_command_line(line);

        // empty input check
        if (strlen(line) == 0)
            continue;

        Redirections r;
        int parse_result = parse_command_and_redirs(line, args, &argsc, &r);
        if (parse_result < 0)
            continue; // syntax error already printed in function so just 

        // built-in exit
        if (strcmp(args[ARG_PROGNAME], "exit") == 0)
        {
            int status = (argsc > 1) ? atoi(args[1]) : 0;
            printf("Exiting shell with status %d\n", status);
            exit(status);
        }

        bool has_redirs = r.in_path || r.out_path || r.err_path || r.both_to_path || r.merge_err_to_out;

        if (has_redirs)
        {
            // check if all redirects are valid (print to terminal if not)
            if (validate_redirs(&r) == 0)
            {
                launch_program_with_redirection(args, argsc, &r);
            }
        }
        else
        {
            launch_program(args, argsc);
        }

        reap();
    }

    return 0;
}
