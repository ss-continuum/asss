
/* dist: public */

#ifdef WIN32

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
#define NAME_MAX 256
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
#define chdir(a) _chdir(a)
#define random rand

typedef int socklen_t;

#define BROKEN_VSNPRINTF

/* a few things that windows is missing */
const char * strcasestr(const char* haystack, const char* needle);
int mkstemp(char *template);


/* directory listing */
typedef struct DIR DIR;

struct dirent
{
	char d_name[NAME_MAX];
};

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
void closedir(DIR *dir);

int inet_aton(char *cp, struct in_addr *pin);

#endif
