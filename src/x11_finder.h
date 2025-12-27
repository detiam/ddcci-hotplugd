typedef struct {
    char display[32];      // $DISPLAY
    char xauthority[256];  // $XAUTHORITY
} X11Env;

/**
 * Scan Xorg processes and fetch their X11 environment variables
 * @param envs array to store found environments
 * @param max_count maximum number of environments to fetch
 * @return number of found environments
 */
int fetch_x11_environments(X11Env *envs, int max_count);
