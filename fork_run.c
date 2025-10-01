#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void writeoutput(const char *command, const char *out_path, const char *err_path) {
    .
    int out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    int err_fd = open(err_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    pid_t pid = fork();
    if (pid == 0) {
        
        if (out_fd >= 0) {
            dup2(out_fd, STDOUT_FILENO);
        }
        if (err_fd >= 0) {
            dup2(err_fd, STDERR_FILENO);
        }
        
        if (out_fd >= 0) close(out_fd);
        if (err_fd >= 0) close(err_fd);

        
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);

        
        _exit(127);
    } else {
        
        if (out_fd >= 0) close(out_fd);
        if (err_fd >= 0) close(err_fd);
        if (pid > 0) {
            (void)waitpid(pid, NULL, 0);
        }
    }
}

void parallelwriteoutput(int count, const char **argv_base, const char *out_path) {
   
    int out_fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    int base_len = 0;
    while (argv_base[base_len] != NULL) base_len++;

    pid_t *pids = NULL;
    if (count > 0) {
        pids = (pid_t *)malloc(sizeof(pid_t) * (size_t)count);
    }

    for (int i = 0; i < count; i++) {
        pid_t pid = fork();
        if (pid == 0) {

            if (out_fd >= 0) {
                dup2(out_fd, STDOUT_FILENO);
            }
            if (out_fd >= 0) close(out_fd);

            char **argv = (char **)malloc(sizeof(char *) * (size_t)(base_len + 2));

            for (int k = 0; k < base_len; k++) {
                argv[k] = (char *)argv_base[k];
            }

            char idxbuf[32];
            snprintf(idxbuf, sizeof(idxbuf), "%d", i);
            argv[base_len] = idxbuf;
            argv[base_len + 1] = NULL;

            execv(argv_base[0], argv);

            _exit(127);
        } else {

            if (pids) pids[i] = pid;
        }
    }

    if (out_fd >= 0) close(out_fd);

    if (pids) {
        for (int i = 0; i < count; i++) {
            if (pids[i] > 0) {
                (void)waitpid(pids[i], NULL, 0);
            }
        }
        free(pids);
    }
}
