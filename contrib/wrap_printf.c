#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <execinfo.h>
#define __USE_GNU
#include <dlfcn.h>

static int (* next_vsnprintf) (char *str, size_t size, const char *format, va_list ap) = NULL;
static int (* next_vfprintf) (FILE *stream, const char *format, va_list ap) = NULL;
static int (* next_snprintf) (char *str, size_t size, const char *format, ...) = NULL;

static void show_stackframe() {
  void *trace[16];
  char **messages = (char **)NULL;
  int i, trace_size = 0;

  trace_size = backtrace(trace, 16);
  messages = backtrace_symbols(trace, trace_size);
  printf("[bt] Execution path:\n");
  for (i=0; i < trace_size; ++i)
        printf("[bt] %s\n", messages[i]);
  free(messages);
}

int
vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
	if (next_vsnprintf == NULL) {
		next_vsnprintf = dlsym(RTLD_NEXT, "vsnprintf");
	}
	show_stackframe();
	return next_vsnprintf(str, size, format, ap);
}

int
vfprintf(FILE *stream, const char *fmt, va_list ap)
{
        if (next_vfprintf == NULL) {
                next_vfprintf = dlsym(RTLD_NEXT, "vfprintf");
        }
        show_stackframe();
        return next_vfprintf(stream, fmt, ap);

}

int
snprintf(char *str, size_t size, const char *fmt, ...)
{
	va_list a;
	int i;

	va_start(a, fmt);
	if (next_vsnprintf == NULL) {
		next_vsnprintf = dlsym(RTLD_NEXT, "vsnprintf");
	}
	if (next_snprintf == NULL) {
		next_snprintf = dlsym(RTLD_NEXT, "snprintf");
	}
	i = next_vsnprintf(str, size, fmt, a);	
	va_end(a);
	return i;
}
