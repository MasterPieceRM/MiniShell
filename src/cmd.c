// SPDX-License-Identifier: BSD-3-Clause

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd.h"
#include "utils.h"

#define READ 0
#define WRITE 1

/**
 * Internal change-directory command.
 */
static char *get_value_env(word_t *aux) {
    char *value = NULL;
    if (aux->expand == true) {
        if (getenv(aux->string) == NULL)
            setenv(aux->string, "", 1);
        value = malloc(sizeof(char) * (strlen(getenv(aux->string)) + 1));
        strcpy(value, getenv(aux->string));
    } else {
        value = malloc(sizeof(char) * (strlen(aux->string) + 1));
        strcpy(value, aux->string);
    }
    aux = aux->next_part;
    while (aux) {
        if (aux->expand == true) {
            if (getenv(aux->string) == NULL)
                setenv(aux->string, "", 1);
            value = realloc(
                value, sizeof(char) *
                           (strlen(value) + strlen(getenv(aux->string)) + 1));
            if (!value) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            strcat(value, getenv(aux->string));
        } else {
            value = realloc(value, sizeof(char) * (strlen(value) +
                                                   strlen(aux->string) + 1));
            if (!value) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            strcat(value, aux->string);
        }
        aux = aux->next_part;
    }
    return value;
}

static bool shell_cd(word_t *dir, simple_command_t *s) {
    /* TODO: Execute cd. */
    int fd;
    int saved_stdout = dup(STDOUT_FILENO);

    if (dir == NULL || dir->next_part != NULL || dir->next_word != NULL)
        return -1;
    if (s->out && s->err) {
        fd = open(s->err->string, O_CREAT | O_WRONLY | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        dup2(fd, STDERR_FILENO);
        close(fd);
        fd = open(s->out->string, O_CREAT | O_WRONLY | O_APPEND,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    if (s->out && !s->err) {
        if (s->io_flags == IO_OUT_APPEND) {
            fd = open(s->out->string, O_CREAT | O_WRONLY | O_APPEND,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        } else {
            fd = open(s->out->string, O_CREAT | O_WRONLY | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
    }
    if (s->err && !s->out) {
        if (s->io_flags == IO_ERR_APPEND) {
            fd = open(s->err->string, O_CREAT | O_WRONLY | O_APPEND,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            dup2(fd, STDERR_FILENO);
            close(fd);
        } else {
            fd = open(s->err->string, O_CREAT | O_WRONLY | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
    }
    if (dir->expand == true) {
        if (chdir(getenv(dir->string)) == -1) {
            fprintf(stderr, "no such file or directory\n");
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
            return -1;
        }
        return 0;
    }
    if (chdir(dir->string) == -1) {
        fprintf(stderr, "no such file or directory\n");
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
        return -1;
    }
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    return 0;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void) {
    /* TODO: Execute exit/quit. */
    return SHELL_EXIT;
}

static int shell_pwd(simple_command_t *s) {
    int saved_stdout = dup(STDOUT_FILENO);
    if (s->out) {
        int fd;
        word_t *aux = s->out;
        char *arg = get_value_env(aux);
        if (s->io_flags == IO_OUT_APPEND) {
            fd = open(arg, O_CREAT | O_WRONLY | O_APPEND,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        } else {
            fd = open(arg, O_CREAT | O_WRONLY | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        }
        if (fd == -1) {
            perror("open() error");
            return 1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
        fflush(STDIN_FILENO);
    } else
        perror("getcwd() error");

    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    return 0;
}
/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father) {
    /* TODO: Sanity checks. */
    if (!s || !s->verb)
        return 0;
    int fd;
    if (strcmp(s->verb->string, "false") == 0)
        return 1;
    if (strcmp(s->verb->string, "true") == 0)
        return 0;
    if (strcmp(s->verb->string, "cd") == 0)
        return shell_cd(s->params, s);
    if (strcmp(s->verb->string, "exit") == 0 ||
        strcmp(s->verb->string, "quit") == 0)
        return shell_exit();
    if (strcmp(s->verb->string, "pwd") == 0) {
        return shell_pwd(s);
    }

    if (s->verb->expand == true && s->verb->next_part == NULL) {
        if (getenv(s->verb->string) == NULL)
            setenv(s->verb->string, "", 1);
        printf("%s\n", getenv(s->verb->string));
        return 0;
    }
    if (s->verb->next_part != NULL && s->verb->next_part->string[0] == '=') {
        char *value = NULL;
        char *var = malloc(sizeof(char) * (strlen(s->verb->string) + 1));
        strcpy(var, s->verb->string);
        word_t *aux = s->verb->next_part->next_part;
        value = get_value_env(aux);
        setenv(var, value, 1);
        return 0;
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        char **args = NULL;
        int index = 0;
        args = malloc(sizeof(char *));
        args[index] = malloc(sizeof(char) * (strlen(s->verb->string) + 1));
        strcpy(args[index], s->verb->string);
        index++;
        simple_command_t *aux = s;
        while (aux->params) {
            args = realloc(args, sizeof(char *) * (index + 1));
            if (!args) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            args[index] =
                malloc(sizeof(char) * (strlen(aux->params->string) + 1));
            if (aux->params->expand == true) {
                if (getenv(aux->params->string) == NULL)
                    setenv(aux->params->string, "", 1);
                strcpy(args[index], getenv(aux->params->string));
            } else {
                strcpy(args[index], aux->params->string);
            }
            index++;
            aux->params = aux->params->next_word;
        }
        args = realloc(args, sizeof(char *) * (index + 1));
        if (!args) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        args[index] = NULL;
        if (s->out && s->err) {
            fd = open(s->err->string, O_CREAT | O_WRONLY | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            dup2(fd, STDERR_FILENO);
            close(fd);
            fd = open(s->out->string, O_CREAT | O_WRONLY | O_APPEND,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        if (s->out && !s->err) {
            if (s->io_flags == IO_OUT_APPEND) {
                fd = open(s->out->string, O_CREAT | O_WRONLY | O_APPEND,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                dup2(fd, STDOUT_FILENO);
                close(fd);
            } else {
                fd = open(s->out->string, O_CREAT | O_WRONLY | O_TRUNC,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
        }
        if (s->err && !s->out) {
            if (s->io_flags == IO_ERR_APPEND) {
                fd = open(s->err->string, O_CREAT | O_WRONLY | O_APPEND,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                dup2(fd, STDERR_FILENO);
                close(fd);
            } else {
                fd = open(s->err->string, O_CREAT | O_WRONLY | O_TRUNC,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
        }
        if (s->in) {
            fd = open(s->in->string, O_RDONLY);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (execvp(s->verb->string, args) == -1) {
            fprintf(stderr, "Execution failed for '%s'\n", s->verb->string);
            exit(1);
        }
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
    }
    return 0;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
                            command_t *father) {
    /* TODO: Execute cmd1 and cmd2 simultaneously. */
    pid_t pid1, pid2;
    int status1, status2;
    pid1 = fork();
    if (pid1 == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid1 == 0) {
        status1 = parse_command(cmd1, level + 1, father);
        exit(status1);
    } else {
        pid2 = fork();
        if (pid2 == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid2 == 0) {
            status2 = parse_command(cmd2, level + 1, father);
            exit(status2);
        } else {
            waitpid(pid1, &status1, 0);
            waitpid(pid2, &status2, 0);
            if (WIFEXITED(status1) && WIFEXITED(status2))
                return 0;
        }
    }
    return true;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
                        command_t *father) {
    /* TODO: Redirect the output of cmd1 to the input of cmd2. */
    int fd[2];
    pid_t pid1, pid2;
    int status1, status2;
    if (pipe(fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
    pid1 = fork();
    if (pid1 == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid1 == 0) {
        close(fd[READ]);
        dup2(fd[WRITE], STDOUT_FILENO);
        close(fd[WRITE]);
        status1 = parse_command(cmd1, level + 1, father);
        exit(status1);
    } else {
        pid2 = fork();
        if (pid2 == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid2 == 0) {
            close(fd[WRITE]);
            dup2(fd[READ], STDIN_FILENO);
            close(fd[READ]);
            status2 = parse_command(cmd2, level + 1, father);
            exit(status2);
        } else {
            close(fd[READ]);
            close(fd[WRITE]);
            waitpid(pid1, &status1, 0);
            waitpid(pid2, &status2, 0);
            if (WIFEXITED(status1) && WIFEXITED(status2) && status2 == 0)
                return 0;
        }
    }
    return true;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father) {
    /* TODO: sanity checks */
    int status;
    if (!c)
        return 1;
    if (c->op == OP_NONE) {
        /* TODO: Execute a simple command. */
        status = parse_simple(c->scmd, level, father);
        return status;
    }

    switch (c->op) {
    case OP_SEQUENTIAL:
        /* TODO: Execute the commands one after the other. */
        if (c->cmd1)
            status = parse_command(c->cmd1, level + 1, c);
        if (c->cmd2)
            status = parse_command(c->cmd2, level + 1, c);
        break;

    case OP_PARALLEL:
        /* TODO: Execute the commands simultaneously. */
        if (c->cmd1 && c->cmd2)
            status = run_in_parallel(c->cmd1, c->cmd2, level + 1, c);
        break;

    case OP_CONDITIONAL_NZERO:
        /* TODO: Execute the second command only if the first one
         * returns non zero.
         */
        if (c->cmd1) {
            status = parse_command(c->cmd1, level + 1, c);
            if (status != 0) {
                if (c->cmd2)
                    status = parse_command(c->cmd2, level + 1, c);
            } else
                return status;
        }
        break;

    case OP_CONDITIONAL_ZERO:
        /* TODO: Execute the second command only if the first one
         * returns zero.
         */
        if (c->cmd1) {
            status = parse_command(c->cmd1, level + 1, c);
            if (status == 0) {
                if (c->cmd2)
                    status = parse_command(c->cmd2, level + 1, c);
            } else
                return status;
        }
        break;

    case OP_PIPE:
        /* TODO: Redirect the output of the first command to the
         * input of the second.
         */
        if (c->cmd1 && c->cmd2)
            status = run_on_pipe(c->cmd1, c->cmd2, level + 1, c);
        break;

    default:
        return SHELL_EXIT;
    }

    return status; /* TODO: Replace with actual exit code of command. */
}
