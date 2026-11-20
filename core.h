#pragma once
#include <wchar.h>
static int parse_port(const wchar_t *s) {
  unsigned n = 0;
  if (!s || !*s)
    return 0;
  while (*s) {
    if (*s < L'0' || *s > L'9')
      return 0;
    n = n * 10 + (*s++ - L'0');
    if (n > 65535)
      return 0;
  }
  return (int)n;
}
