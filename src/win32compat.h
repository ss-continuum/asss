
/* dist: public */

#define EXPORT __declspec(dllexport)

#ifndef NDEBUG
#define inline
#else
#define inline __inline
#endif

#define strcasecmp(a,b) stricmp((a),(b))
#define strncasecmp(a,b,c) strnicmp((a),(b),(c))
#define M_PI 3.14159265358979323846
#define PATH_MAX 256
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define usleep(x) Sleep((x)/1000)
#define sleep(x) Sleep((x)*1000)
#define alloca _alloca
#define access _access
#define R_OK 4
#define S_ISDIR(a) ((a) & _S_IFDIR)
#define mkdir(a,b) _mkdir(a)
#define mktemp(a) _mktemp(a)
#define open(a) _open(a)

typedef int socklen_t;

#define BROKEN_VSNPRINTF

/* a few things that windows is missing */
extern const char * strcasestr(const char* haystack, const char* needle);
extern int mkstemp(char *template);

