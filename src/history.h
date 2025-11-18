#ifndef HISTORY_H
#define HISTORY_H

#define MAX_HISTORY 500

void history_init();
void history_add(const char *cmd);
void history_save(const char *cmd);
void history_print();
int  history_expand(char *input, char *expanded);

#endif
