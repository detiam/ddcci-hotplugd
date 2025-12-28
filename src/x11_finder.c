#define _GNU_SOURCE

#include "x11_finder.h"
#include <X11/Xauth.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/fanotify.h>

static bool get_display_from_auth(const char *auth_path, char *dest, size_t dest_len) {
    FILE *f = fopen(auth_path, "rb");
    if (!f) return 0;

    Xauth *auth;
    while ((auth = XauReadAuth(f))) {
        if (auth->family == FamilyLocal || auth->family == FamilyWild) {
            snprintf(dest, dest_len, ":%.*s", auth->number_length, auth->number);
            XauDisposeAuth(auth);
            fclose(f);
            return true;
        }
    }

    fclose(f);
    return false;
}

pid_t wait_x11_startup(char exe_path[512]) {
    int fd;
    char buf[4096];
    ssize_t len;
    char path[PATH_MAX];
    char exe_link[64];

    if (exe_path == NULL)
        exe_path = "/usr/lib/Xorg";

    fd = fanotify_init(FAN_CLASS_NOTIF, O_RDONLY);
    if (fd == -1) {
        perror("fanotify_init");
        return 0;
    }
    if (fanotify_mark(fd, FAN_MARK_ADD, FAN_OPEN_EXEC, AT_FDCWD, exe_path) == -1) {
        perror("fanotify_mark");
        return 0;
    }

    while (1) {
        len = read(fd, buf, sizeof(buf));
        if (len <= 0) break;

        struct fanotify_event_metadata *metadata = (struct fanotify_event_metadata *)buf;

        while (FAN_EVENT_OK(metadata, len)) {
            if (metadata->mask & FAN_OPEN_EXEC) {
                //sleep(0.5);
                //snprintf(exe_link, sizeof(exe_link), "/proc/%d/exe", metadata->pid);
                //ssize_t path_len = readlink(exe_link, path, sizeof(path) - 1);

                //if (path_len != -1) {
                //    path[path_len] = '\0';
                //    if (strcmp(path, exe_path) == 0) {
                        if (metadata->fd >= 0) close(metadata->fd);
                        close(fd);
                        return metadata->pid;
                //    }
                //}
            }

            if (metadata->fd >= 0) close(metadata->fd);
            metadata = FAN_EVENT_NEXT(metadata, len);
        }
    }

    close(fd);
    return 0;
}

bool fetch_x11_env_by_pid(X11Env *env, pid_t pid) {
    char exe_link[512], exe_path[512];
    snprintf(exe_link, sizeof(exe_link), "/proc/%d/exe", pid);

    if (readlink(exe_link, exe_path, sizeof(exe_path)-1) <= 0) return false;

    if (strstr(exe_path, "/Xorg")) {
        char cmd_path[512];
        snprintf(cmd_path, sizeof(cmd_path), "/proc/%d/cmdline", pid);
        
        FILE *f = fopen(cmd_path, "rb");
        if (!f) return false;

        char buf[4096];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);

        for (size_t i = 0; i < n; ) {
            if (strcmp(&buf[i], "-auth") == 0) {
                i += strlen(&buf[i]) + 1;
                if (i < n) {
                    const char *auth_file = &buf[i];
                    if (get_display_from_auth(auth_file, env->display, 32)) {
                        strncpy(env->xauthority, auth_file, 255);
                        return true;
                    }
                }
                break;
            }
            i += strlen(&buf[i]) + 1;
        }
    }

    return false;
}

int fetch_x11_env(X11Env *envs, int max_count) {
    DIR *dir = opendir("/proc");
    if (!dir) return 0;

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) && count < max_count) {
        if (!isdigit(entry->d_name[0])) continue;

        pid_t pid = (pid_t)atoi(entry->d_name);
        if (fetch_x11_env_by_pid(&envs[count], pid)) {
            count++;
        }
    }
    closedir(dir);
    return count;
}