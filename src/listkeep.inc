/* Keeps the previous file list when a refresh fails. */
static inline int ls_action(int n, int present)
{
    if (n >= 0) return 1;
    return present ? -1 : 0;
}
