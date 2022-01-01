#include <sys/types.h>
#include <stdio.h>

int chown(const char *pathname,
          uid_t owner,
          gid_t group)
{
  fprintf(stderr, "stub-chown: Stubbed out attempt to chown '%s'\n", pathname);
  return 0;
}
