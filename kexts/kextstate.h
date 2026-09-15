static inline const char *kx_state(int status)
{
    switch(status){
    case 0:return "Loaded";
    case 40:return "Incomplete file";
    case 41:return "Missing symbol";
    case 42:return "Memory full";
    case 43:return "Newer API needed";
    case 44:return "Startup failed";
    case 45:return "Fault disabled";
    case 46:return "Inactive";
    case 47:return "On demand";
    default:return "Load failed";
    }
}
static inline int kx_failed(int status){return status&&status!=46&&status!=47;}
