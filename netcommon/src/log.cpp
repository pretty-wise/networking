#include "netcommon/log.h"
#include "netcommon/time.h"
#include <stdio.h>
#include <string.h>

static const int g_log_level = 1;

static const char *get_level_string(int level) {
  switch(level) {
  case 0:
    return "DBG";
  case 1:
    return "INF";
  case 2:
    return "WAR";
  case 3:
    return "ERR";
  case 4:
    return "CRI";
  default:
    break;
  }
  return "???";
}

static const char *get_filename(const char *path) {
  const char *filename = strrchr(path, '/');
  return filename ? filename + 1 : path;
}

void write_log(const char *file, int line, int level, const char *format, ...) {
  va_list args;
  va_start(args, format);
  write_log(file, line, level, format, args);
  va_end(args);
}
void write_log(const char *file, int line, int level, const char *format,
               va_list arg_list) {

  if(level < g_log_level)
    return;

  const int max_line_length = 128;
  char logline[max_line_length];
  // remove three bytes to ensure that there is space for the CRLF and the null
  // terminator.
  int avail = max_line_length - 3;
  int offset = 0;
  int n = 0;

  const char *level_string = get_level_string(level);
  n = snprintf(&logline[offset], avail, "%d|%s|", get_time_ms(), level_string);
  offset += n;
  avail -= n;

  // write the log message
  n = vsnprintf(&logline[offset], avail, format, arg_list);
  if(n >= 0 && n < avail) {
    offset += n;
    avail -= n;
  } else {
    offset = max_line_length - 3;
    avail = 0;
  }

  // early out if there is no message or buffer full.
  if(0 >= n || avail < 0) {
    return;
  }

  // write the log suffix
  file = get_filename(file);
  n = snprintf(&logline[offset], avail, "|%s:%d", file, line);

  if(n >= 0 && n < avail) {
    offset += n;
    avail -= n;
  } else {
    offset = max_line_length - 3;
    avail = 0;
  }

  logline[offset++] = '\r';
  logline[offset++] = '\n';
  logline[offset] = 0;

  printf("%s", logline);
}