#include <stdbool.h>
#include <fcntl.h>

typedef struct {
    char display[32];      // $DISPLAY
    char xauthority[256];  // $XAUTHORITY
} X11Env;

/**
 * Wait for the X11 server process to start
 * @param exe_path path to the X11 server executable (e.g., "/usr/lib/Xorg")
 * @return PID of the started X11 server process, or 0 on failure
 */
pid_t wait_x11_startup(char exe_path[512]);

/**
 * Scan Xorg processes and fetch their X11 environment variables
 * @param env structure to store found environment
 * @param pid process ID of the Xorg process
 * @return true if environment was found and filled, false otherwise
 */
bool fetch_x11_env_by_pid(X11Env *env, pid_t pid);

/**
 * Scan Xorg processes and fetch their X11 environment variables
 * @param envs array to store found environments
 * @param max_count maximum number of environments to fetch
 * @return number of found environments
 */
int fetch_x11_env(X11Env *envs, int max_count);
