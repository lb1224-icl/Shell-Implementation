# S3 – Software Systems Shell

## Table of Contents
1. [Repository Layout](#repository-layout)
2. [Building & Running](#building--running)
3. [Core Functionality](#core-functionality)
   - [Command Execution](#command-execution)
   - [Redirections](#redirections)
   - [Semicolons (Batched Commands)](#semicolons-batched-commands)
   - [Shell Built-ins & Prompt](#shell-built-ins--prompt)
   - [Pipelines](#pipelines)
4. [Proposed Extensions (PEs)](#proposed-extensions-pes)
5. [Extra Enhancements (EEs)](#extra-enhancements-ees)
6. [Example Sessions](#example-sessions)

---

## Repository Layout

| Path | Description |
|------|-------------|
| `src/s3main.c` | Main control loop (`execute_command_line`), entry point, and integration of every feature. |
| `src/s3.c` | Implementation of parsing helpers, process launches, redirection plumbing, and pipeline logic. |
| `src/history.c` / `.h` | Simple persistent history database plus expansion logic (`!!`, `!n`, `!prefix`). |
| `src/txt/` | Sample data files (e.g., `phrases.txt`) for redirection / pipeline demos. |
| `tmp/` | Working folder used for testing (not part of the build). |

---

## Building & Running

```bash
# from repo root
gcc src/s3main.c src/s3.c src/history.c -o src/s3
./src/s3
```

The shell persists command history in `.history` (created automatically) and uses the current working directory in its prompt: `[s3 /path/to/cwd]$`.

---

## Core Functionality

### Command Execution

- **Fork / exec path** – `launch_program` and `child` are responsible for basic commands. Each call fork/execs and returns control to the parent, while the parent synchronises via `reap()`:

  ```c
  void launch_program(char *args[], int argsc, int input_fd, int output_fd) {
      pid_t rc = fork();
      if (rc == 0) {
          child(args, argsc, input_fd, output_fd); // execvp happens here
      }
  }
  ```

- **`exit` built-in** – handled in the parent so the shell terminates cleanly and can accept optional status codes (e.g., `exit 7`).

- **Whitespace-aware parsing** – `parse_command` tokenises while preserving quoted substrings and removing quote characters, enabling commands like `echo "== subshell test =="`.

### Redirections

- Detects `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`, `&>` using `command_with_redirection` plus `extract_redirections`. All descriptors are validated (`stat` + `access`) before launching.
- Input/output/error/both file descriptors are opened in the parent, then duplicated in the child via `dup2`. Multiple redirections in a single command obey the “last one wins” rule because later operators overwrite earlier ones inside `Redirections`.
- Safety checks prevent writes to directories and surface clear diagnostics (e.g., “`s3: 'tmp/' is a directory`”).

### Semicolons (Batched Commands)

`split_by_semicolon` walks the line once, keeping track of parentheses and quotes via the `QuoteState` helper so constructs such as `echo one ; (cd src; make)` parse correctly. Each command in the batch executes independently, matching Bash’s `cmdA ; cmdB` semantics.

### Shell Built-ins & Prompt

- **`cd`** – Recognised upfront (`execute_command_line` calls `change_directory` in the parent). Supports `cd .`, `cd ..`, `cd -` (jump back to previous directory), `cd -- <path>` (treats paths starting with `-` literally), and tilde expansion (`cd ~`, `cd ~/path`). The prompt reflects the new directory immediately because `construct_shell_prompt` calls `getcwd` on each loop iteration. A missing argument defaults to `$HOME`.
- **Prompt** – `[s3 <cwd>]$` truncated safely if the current path would exceed `MAX_PROMPT_LEN`.
- **History CLI** – Typing `history` prints numbered entries. The subsystem (see `src/history.c`) loads `.history` on startup and appends after every user command.

### Pipelines

- Quote-aware detection (`command_with_pipe`) and tokenisation (`split_by_pipe`) ensure that only real `|` separators (not those inside strings) split the pipeline.
- `run_pipeline` orchestrates the chain: it creates pipes in the parent, invokes `launch_pipeline_stage` for each segment, closes unused FDs to avoid deadlocks, then waits for all children.
- `launch_pipeline_stage` reuses the same `launch_program` / `_with_redirection` functions by passing the appropriate `input_fd` / `output_fd`. Even mid-pipeline commands can redirect to files (`>`), which effectively “breaks” the stream beyond that point, mirroring Bash’s behaviour.
- Pipelines can include subshell segments. When a stage is wrapped in parentheses, we fork another copy of `s3` with `execlp(shell_path, shell_path, "-c", inner, NULL)` so constructs like `(cd src; tr ...) | grep ...` work exactly as in the reference shell.

---

## Proposed Extensions (PEs)

| Extension | Status | Notes |
|-----------|--------|-------|
| **PE1 – Subshells** | ✅ | We allow arbitrary command groups inside parentheses. Each group executes in a forked child running `s3 -c "<inner>"`, so changes (e.g., `cd`) do not leak back to the parent. |
| **PE2 – Nested Subshells** | ✅ | Parentheses depth is tracked when splitting on semicolons and pipes, so nested groups such as `(echo outer ; (cd src; ls))` are parsed correctly. |

Subshells can also appear inside pipelines, with the child inheriting pipe file descriptors before `execvp`.

---

## Extra Enhancements (EEs)

| Enhancement | Status | Notes |
|-------------|--------|-------|
| **EE1 – History Expansion** | ✅ | Interactive shells expand `!!`, `!n`, and `!prefix`, print the expanded line before running it, and reject unmatched requests with friendly messages (“No such history entry.”). A bare `!` is ignored to avoid surprises. |
| **EE2 – Multi-way Redirection** | ✅ | Redirection parser accepts `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`, and `&>`. Operators can appear multiple times; the last occurrence wins per stream, matching Bash semantics. |

---

## Example Sessions

```text
[s3 /home/lucca/Documents/Shell-Implementation]$ history
1  ls
2  cd src
3  history

[s3 /home/lucca/Documents/Shell-Implementation]$ !!   # repeats `history`
history

[s3 /home/lucca/Documents/Shell-Implementation]$ ls txt | head -n 3
calendar.txt
phrases.txt
phrases_sorted.txt

[s3 /home/lucca/Documents/Shell-Implementation]$ (cd src; tr a-z A-Z < txt/phrases.txt) | grep BURN | sort | uniq -c | head -n 5
      1 BURNING CANDLE
      1 BURNING COAL
      1 BURNING DAWN
      1 BURNING EMBER
      1 BURNING FIELD
```

These examples demonstrate batched commands, history, pipelines, redirections, and subshells working together – covering both the assignment’s core specs and the implemented extensions.

---
