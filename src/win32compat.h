
#define EXPORT __declspec(dllexport)
#define inline
#define strcasecmp(a,b) stricmp((a),(b))
#define strncasecmp(a,b,c) strnicmp((a),(b),(c))
#define M_PI 3.14159265358979323846
#define PATH_MAX 256
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define usleep(x) Sleep((x)/1000)
#define alloca _alloca
#define access _access
#define R_OK 4
#define S_ISDIR(a) ((a) & _S_IFDIR)

