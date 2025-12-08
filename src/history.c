#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "history.h"
#include <ctype.h>

static char *history[MAX_HISTORY];
static int history_len = 0;

static const char *HIST_FILE = ".history";

/* Load .history file into memory */
void history_init() {
    FILE *fp = fopen(HIST_FILE, "r");
    if (!fp) return;

    char *line = NULL;
    size_t n = 0;

    while (getline(&line, &n, fp) != -1) {
        line[strcspn(line, "\n")] = '\0';

        if (history_len < MAX_HISTORY) {
            history[history_len++] = strdup(line);
        }
    }
    free(line);
    fclose(fp);
}

/* Add a command to memory */
void history_add(const char *cmd) {
    if (history_len < MAX_HISTORY) {
        history[history_len++] = strdup(cmd);
    }
}

/* Append a command to .history file */
void history_save(const char *cmd) {
    FILE *fp = fopen(HIST_FILE, "a");
    if (!fp) return;
    fprintf(fp, "%s\n", cmd);
    fclose(fp);
}

/* Print numbered history */
void history_print() {
    for (int i = 0; i < history_len; i++) {
        printf("%d  %s\n", i + 1, history[i]);
    }
}

int history_expand(char *input, char *expanded) {
    if (input[0] != '!')
        return 0;

    // Single '!' should not trigger expansion
    if (input[1] == '\0') {
        return 0;
    }

    // Case 1: !! (repeat most recent)
    if (input[1] == '!' && input[2] == '\0') {
        if (history_len == 0) {
            printf("No commands in history.\n");
            return -1;
        }
        strcpy(expanded, history[history_len - 1]);
        return 1;
    }

    // Case 2: !number
    if (isdigit(input[1])) {
        int idx = atoi(input + 1) - 1;
        if (idx >= 0 && idx < history_len) {
            strcpy(expanded, history[idx]);
            return 1;
        } else {
            printf("No such history entry.\n");
            return -1;
        }
    }

    // Case 3: !prefix
    char *prefix = input + 1;
    int prefix_len = strlen(prefix);

    for (int i = history_len - 1; i >= 0; i--) {
        if (strncmp(history[i], prefix, prefix_len) == 0) {
            strcpy(expanded, history[i]);
            return 1;
        }
    }

    printf("No command starts with '%s'.\n", prefix);
    return -1;
}
