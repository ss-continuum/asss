
#ifndef __APP_H
#define __APP_H

typedef struct APPContext APPContext;

typedef int (*FileFinderFunc)(char *dest, int destlen, const char *arena, const char *name);
typedef void (*ReportErrFunc)(const char *error);


APPContext *InitContext(FileFinderFunc finder, ReportErrFunc err, const char *arena);
void FreeContext(APPContext *ctx);

void AddDef(APPContext *ctx, const char *key, const char *val);
void RemoveDef(APPContext *ctx, const char *key);

void AddFile(APPContext *ctx, const char *name);

/* returns false on eof */
int GetLine(APPContext *ctx, char *buf, int buflen);

#endif

