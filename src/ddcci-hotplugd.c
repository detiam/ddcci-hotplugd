#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <glob.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>

/* udev */
#include <libudev.h>

/* X11 */
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

/* kmod */
#include <libkmod.h>

/* ddcutil */
#include <ddcutil_c_api.h>

/* x11 finder */
#include "x11_finder.h"

static bool log_enabled = true;

/* ------------------------------------------------------------ */
/* util */

static void msg(const char *fmt, ...) {
    if (!log_enabled)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static void write_string(const char *path, const char *s) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    write(fd, s, strlen(s));
    close(fd);
}

static bool is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

/* ------------------------------------------------------------ */
/* libkmod helpers */

static void load_module(const char *name, const char *options) {
    struct kmod_ctx *ctx = kmod_new(NULL, NULL);
    if (!ctx)
        return;

    struct kmod_module *mod = NULL;
    if (kmod_module_new_from_name(ctx, name, &mod) == 0) {
        kmod_module_probe_insert_module(
            mod,
            KMOD_PROBE_APPLY_BLACKLIST,
            options,
            NULL,
            NULL,
            NULL
        );
        kmod_module_unref(mod);
    }
    kmod_unref(ctx);
}

static void unload_module(const char *name) {
    struct kmod_ctx *ctx = kmod_new(NULL, NULL);
    if (!ctx)
        return;

    struct kmod_module *mod = NULL;
    if (kmod_module_new_from_name(ctx, name, &mod) == 0) {
        kmod_module_remove_module(mod, 0);
        kmod_module_unref(mod);
    }
    kmod_unref(ctx);
}

/* ------------------------------------------------------------ */
/* ddcci module sanity */

static void ensure_ddcci_modules(void) {
    if (!is_directory("/sys/module/ddcci_backlight")) {
        msg("Loading ddcci and ddcci_backlight modules\n");
        load_module("ddcci", "delay=0");
        load_module("ddcci_backlight", NULL);
        return;
    }

    DIR *d = opendir("/sys/class/backlight");
    if (!d)
        return;

    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "ddcci", 5) == 0) {
            char p[PATH_MAX];
            snprintf(p, sizeof(p),
                     "/sys/class/backlight/%s", e->d_name);
            if (!is_directory(p)) {
                msg("Warning: invaild ddcci_backlight interface detected, reloading!\n");
                unload_module("ddcci_backlight");
                unload_module("ddcci");
                load_module("ddcci", "delay=0");
                load_module("ddcci_backlight", NULL);
                break;
            }
        }
    }
    closedir(d);
}

/* ------------------------------------------------------------ */
/* cleanup invalid i2c ddcci devices */

static void cleanup_invalid_ddcci_i2c(void) {
    glob_t g;
    if (glob("/sys/bus/i2c/devices/i2c-*/*-0037", 0, NULL, &g) != 0)
        return;

    for (size_t i = 0; i < g.gl_pathc; i++) {
        char name[PATH_MAX];
        char driver[PATH_MAX];

        snprintf(name, sizeof(name), "%s/name", g.gl_pathv[i]);
        snprintf(driver, sizeof(driver), "%s/driver", g.gl_pathv[i]);

        FILE *f = fopen(name, "r");
        if (!f)
            continue;

        char buf[64] = {0};
        fgets(buf, sizeof(buf), f);
        fclose(f);

        if (strncmp(buf, "ddcci", 5) == 0 &&
            !is_directory(driver)) {

            char del[PATH_MAX];
            snprintf(del, sizeof(del),
                     "%s/../delete_device", g.gl_pathv[i]);
            msg("Cleaning up invalid ddcci i2c device: %s\n", g.gl_pathv[i]);
            write_string(del, "0x37");
        }
    }
    globfree(&g);
}

/* ------------------------------------------------------------ */
/* attach ddcci */

static bool is_attached(int bus) {
    char p[PATH_MAX];
    snprintf(p, sizeof(p),
             "/sys/bus/ddcci/devices/ddcci%d", bus);
    return is_directory(p);
}

static void detach(int bus) {
    if (!is_attached(bus)) {
        msg("ddcci not attached on i2c-%d, skipping\n", bus);
        return;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path),
             "/sys/bus/i2c/devices/i2c-%d/delete_device",
             bus);
    msg("Detaching ddcci on i2c-%d\n", bus);
    write_string(path, "0x37");
}

static void attach(int bus) {
    if (is_attached(bus)) {
        msg("ddcci already attached on i2c-%d, skipping\n", bus);
        return;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path),
             "/sys/bus/i2c/devices/i2c-%d/new_device",
             bus);
    msg("Attaching ddcci on i2c-%d\n", bus);
    write_string(path, "ddcci 0x37");
}

static void get_detect_buses(int **buses, int *ct) {
    DDCA_Display_Info_List *list = NULL;
    *buses = NULL;
    *ct = 0;

    ddca_redetect_displays();
    if (ddca_get_display_info_list2(false, &list) != 0)
        goto out;

    *buses = malloc(sizeof(int) * list->ct);
    if (!*buses) {
        goto out;
    }

    for (int i = 0; i < list->ct; i++) {
        DDCA_Display_Info di = list->info[i];
        int bus = di.path.path.i2c_busno;
        if (bus >= 0)
            (*buses)[(*ct)++] = bus;
    }

out:
    if (list)
        ddca_free_display_info_list(list);
}

static void get_attached_buses(int **buses, int *ct) {
    glob_t g;
    *buses = NULL;
    *ct = 0;

    if (glob("/sys/bus/ddcci/devices/ddcci*", 0, NULL, &g) != 0)
        return;

    *buses = malloc(sizeof(int) * g.gl_pathc);
    if (!*buses) {
        globfree(&g);
        return;
    }

    for (size_t i = 0; i < g.gl_pathc; i++) {
        int bus = -1;
        sscanf(g.gl_pathv[i], "/sys/bus/ddcci/devices/ddcci%d", &bus);
        if (bus >= 0)
            (*buses)[(*ct)++] = bus;
    }
    globfree(&g);
}

static void attach_ddcci(void) {
    int *detected_buses = NULL;
    int detected_ct = 0;
    get_detect_buses(&detected_buses, &detected_ct);

    int *attached_buses = NULL;
    int attached_ct = 0;
    get_attached_buses(&attached_buses, &attached_ct);

    for (int i = 0; i < detected_ct; i++) {
        int bus = detected_buses[i];
        if (!attached_buses ||
            !memchr(attached_buses, bus, sizeof(int) * attached_ct)) {
            attach(bus);
        }
    }

    for (int i = 0; i < attached_ct; i++) {
        int bus = attached_buses[i];
        if (!detected_buses ||
            !memchr(detected_buses, bus, sizeof(int) * detected_ct)) {
            detach(bus);
        }
    }

    if (detected_buses)
        free(detected_buses);
    if (attached_buses)
        free(attached_buses);
}

/* ------------------------------------------------------------ */

static void run_ddcci_attach(void) {
    ensure_ddcci_modules(); // sometimes needed?
    cleanup_invalid_ddcci_i2c(); // sometimes needed too
    attach_ddcci();
}

/* ------------------------------------------------------------ */
/* udev */

static int setup_udev(struct udev **udev,
                      struct udev_monitor **mon) {
    *udev = udev_new();
    if (!*udev)
        return -1;

    *mon = udev_monitor_new_from_netlink(*udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(
        *mon, "drm", NULL);
    udev_monitor_enable_receiving(*mon);
    return udev_monitor_get_fd(*mon);
}

static void handle_udev_event(void* arg) {
    struct udev_monitor *mon = (struct udev_monitor *)arg;
    struct udev_device *dev =
        udev_monitor_receive_device(mon);
    if (!dev)
        return;

    const char *action = udev_device_get_action(dev);
    if (action && strcmp(action, "change") == 0)
        run_ddcci_attach();

    udev_device_unref(dev);
}

/* ------------------------------------------------------------ */
/* XRandR */

static int rr_event_base = -1;

static Display *setup_xrandr(int *xfd) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        if (geteuid() == 0) {
            X11Env envs[1];
            int found = fetch_x11_environments(envs, 1);
            if (found > 0) {
                msg("XRandR DISPLAY='%s' XAUTHORITY='%s'\n",
                    envs[0].display, envs[0].xauthority);
                setenv("DISPLAY", envs[0].display, 1);
                setenv("XAUTHORITY", envs[0].xauthority, 1);
                dpy = XOpenDisplay(NULL);
            }
        } else {
            return NULL;
        }
    }

    int err;
    if (!XRRQueryExtension(dpy, &rr_event_base, &err))
        return NULL;

    Window root = DefaultRootWindow(dpy);
    XRRSelectInput(dpy, root, RROutputChangeNotifyMask);

    *xfd = ConnectionNumber(dpy);
    XFlush(dpy);

    return dpy;
}

static void handle_x_events(void* arg) {
    Display *dpy = (Display *)arg;
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);

        if (ev.type == rr_event_base + RRNotify) {
            XRRNotifyEvent *re = (XRRNotifyEvent *)&ev;
            if (re->subtype == RRNotify_OutputChange) {
                XRROutputChangeNotifyEvent *oe =
                    (XRROutputChangeNotifyEvent *)re;

                if (oe->connection == RR_Connected && oe->mode != None) {
                    run_ddcci_attach();
                }
            }
        }
    }
}

/* ------------------------------------------------------------ */
/* main */

typedef void (*UniversalFunc)(void*);

int main(int argc, char *argv[]) {
    const char *env_val = getenv("DDCCI_HOTPLUGD_LOG");
    if (env_val != NULL && strcmp(env_val, "0") == 0) {
        log_enabled = false;
    }

    if (geteuid() != 0) {
        msg("Warning: This program need privileges to load kernel modules and access i2c devices.\n");
    }

    run_ddcci_attach();   /* initial */

    struct pollfd fds[1];
    UniversalFunc events_handle = NULL;
    void* data = NULL;

    int xfd = -1;
    Display *dpy = setup_xrandr(&xfd);
    if (xfd >= 0 && dpy) {
        msg("Using XRandR for hotplug detection\n");
        fds[0].fd = xfd;
        events_handle = handle_x_events;
        data = dpy;
        goto loop;
    }

    struct udev *udev = NULL;
    struct udev_monitor *mon = NULL;
    int udev_fd = setup_udev(&udev, &mon); // untested
    if (udev_fd >= 0 && mon) {
        msg("Using udev for hotplug detection\n");
        fds[0].fd = udev_fd;
        events_handle = handle_udev_event;
        data = mon;
        goto loop;
    }

loop:
    fds[0].events = POLLIN;

    if (argc > 1 && strcmp(argv[1], "--daemon") == 0) {
        daemon(0, 1);
    }

    for (;;) {
        poll(fds, 1, -1);
        if (fds[0].revents & POLLIN)
            events_handle(data);
    }
}
