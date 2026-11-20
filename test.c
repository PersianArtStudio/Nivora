#include "core.h"
#include <assert.h>
int main(void) {
  assert(parse_port(L"10808") == 10808);
  assert(parse_port(L"65535") == 65535);
  const wchar_t *invalid[] = {L"",      L"0",      L"65536",        L"-1",
                              L"80 90", L"80&cmd", L"999999999999", NULL};
  for (int i = 0; invalid[i]; i++)
    assert(parse_port(invalid[i]) == 0);
  return 0;
}
