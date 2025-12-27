#define _GNU_SOURCE

#include "x11_finder.h"
#include <X11/Xauth.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>

static int get_display_from_auth(const char *auth_path, char *dest, size_t dest_len) {
    FILE *f = fopen(auth_path, "rb");
    if (!f) return 0;

    Xauth *auth;
    while ((auth = XauReadAuth(f))) {
        if (auth->family == FamilyLocal || auth->family == FamilyWild) {
            snprintf(dest, dest_len, ":%.*s", auth->number_length, auth->number);
            XauDisposeAuth(auth);
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}

int fetch_x11_environments(X11Env *envs, int max_count) {
    DIR *dir = opendir("/proc");
    if (!dir) return 0;

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) && count < max_count) {
        if (!isdigit(entry->d_name[0])) continue;

        char exe_link[512], exe_path[512];
        snprintf(exe_link, sizeof(exe_link), "/proc/%s/exe", entry->d_name);
        
        if (readlink(exe_link, exe_path, sizeof(exe_path)-1) <= 0) continue;

        if (strstr(exe_path, "/Xorg") || strstr(exe_path, "/Xwayland")) {
            char cmd_path[512];
            snprintf(cmd_path, sizeof(cmd_path), "/proc/%s/cmdline", entry->d_name);
            
            FILE *f = fopen(cmd_path, "rb");
            if (!f) continue;

            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf), f);
            fclose(f);

            for (size_t i = 0; i < n; ) {
                if (strcmp(&buf[i], "-auth") == 0) {
                    i += strlen(&buf[i]) + 1;
                    if (i < n) {
                        const char *auth_file = &buf[i];
                        if (get_display_from_auth(auth_file, envs[count].display, 32)) {
                            strncpy(envs[count].xauthority, auth_file, 255);
                            count++;
                        }
                    }
                    break;
                }
                i += strlen(&buf[i]) + 1;
            }
        }
    }
    closedir(dir);
    return count;
}