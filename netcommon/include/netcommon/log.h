#include <stdarg.h>

#define LOG_TRANSPORT_DBG(...) write_log(__FILE__, __LINE__, 0, __VA_ARGS__)
#define LOG_TRANSPORT_INF(...) write_log(__FILE__, __LINE__, 1, __VA_ARGS__)
#define LOG_TRANSPORT_WAR(...) write_log(__FILE__, __LINE__, 2, __VA_ARGS__)
#define LOG_TRANSPORT_ERR(...) write_log(__FILE__, __LINE__, 3, __VA_ARGS__)

void write_log(const char *filename, int line, int level, const char *format,
               ...);

void write_log(const char *filename, int line, int level, const char *format,
               va_list arg_list);
