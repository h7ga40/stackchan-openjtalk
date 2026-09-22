#ifndef EST_WALLOC_H
#define EST_WALLOC_H

void *safe_wcalloc(size_t size
#ifdef MPL_DEBUG
	, const char *filename, int linenumber
#endif
);

void wfree(void *mem
#ifdef MPL_DEBUG
	, const char *filename, int linenumber
#endif
);

char *wstrdup(const char *string
#ifdef MPL_DEBUG
	, const char *filename, int linenumber
#endif
);

#endif // EST_WALLOC_H
