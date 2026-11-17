// symlink_test: user-level tests for symlink() and open() link following.
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

static int failures;

static void
check(int ok, char *what)
{
  printf(1, "  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if(!ok)
    failures++;
}

static void
writefile(char *path, char *s)
{
  int fd;

  unlink(path);   // xv6 has no O_TRUNC; start from an empty file
  fd = open(path, O_CREATE | O_RDWR);
  if(fd < 0){
    printf(1, "cannot create %s\n", path);
    exit();
  }
  write(fd, s, strlen(s));
  close(fd);
}

// Read up to n-1 bytes from path into buf (NUL terminated); -1 if open fails.
static int
readfile(char *path, int flags, char *buf, int n)
{
  int fd = open(path, flags);
  int r;

  if(fd < 0)
    return -1;
  r = read(fd, buf, n - 1);
  close(fd);
  if(r < 0)
    return -1;
  buf[r] = 0;
  return r;
}

static void
cleanup(void)
{
  unlink("/sl_file");
  unlink("/sl_link");
  unlink("/sl_a");
  unlink("/sl_b");
  unlink("/sl_c");
  unlink("/sl_dir/inner");
  unlink("/sl_dir");
  unlink("/sl_dl");
  unlink("/sl_n");
}

static void
test_basic(void)
{
  char buf[64];
  int fd;

  printf(1, "test 1: read/write through a link\n");
  writefile("/sl_file", "hello");
  check(symlink("/sl_file", "/sl_link") == 0, "symlink() succeeds");
  check(readfile("/sl_link", O_RDONLY, buf, sizeof(buf)) == 5 &&
        strcmp(buf, "hello") == 0, "read through link gives file content");
  fd = open("/sl_link", O_WRONLY);
  check(fd >= 0, "open link for writing");
  if(fd >= 0){
    write(fd, "HELLO!", 6);
    close(fd);
  }
  check(readfile("/sl_file", O_RDONLY, buf, sizeof(buf)) == 6 &&
        strcmp(buf, "HELLO!") == 0, "write through link visible in file");
  printf(1, "write via link, read via file: %s\n",
         strcmp(buf, "HELLO!") == 0 ? "ok" : "FAILED");
  check(symlink("/sl_file", "/sl_link") == -1,
        "symlink() on existing path fails");
  check(symlink("/sl_file", "/nodir/x") == -1,
        "symlink() in missing directory fails");
  check(symlink("", "/sl_n") == -1, "symlink() with empty target fails");
  unlink("/sl_link");
}

static void
test_chain(void)
{
  char buf[64];

  printf(1, "test 2: chain a -> b -> c\n");
  writefile("/sl_c", "real");
  symlink("/sl_c", "/sl_b");
  symlink("/sl_b", "/sl_a");
  check(readfile("/sl_a", O_RDONLY, buf, sizeof(buf)) == 4 &&
        strcmp(buf, "real") == 0, "chain resolves to the real file");
  printf(1, "chain a -> b -> c: %s\n", strcmp(buf, "real") == 0 ? "ok" : "FAILED");
}

static void
test_dangling(void)
{
  int fd;

  printf(1, "test 3: dangling link\n");
  symlink("/sl_does_not_exist", "/sl_dl");
  fd = open("/sl_dl", O_RDONLY);
  printf(1, "dangling link open: %d (expected -1)\n", fd);
  check(fd == -1, "open of dangling link fails");
  if(fd >= 0)
    close(fd);
}

static void
test_loop(void)
{
  int fd, i;
  char name[16], next[16];

  printf(1, "test 4: loops and long chains\n");
  unlink("/sl_a");
  unlink("/sl_b");
  symlink("/sl_b", "/sl_a");
  symlink("/sl_a", "/sl_b");
  fd = open("/sl_a", O_RDONLY);
  printf(1, "loop a -> b -> a open: %d (expected -1)\n", fd);
  check(fd == -1, "open of a symlink loop fails (no hang)");
  if(fd >= 0)
    close(fd);
  // A link pointing at itself.
  symlink("/sl_self", "/sl_self");
  check(open("/sl_self", O_RDONLY) == -1, "self-referencing link fails");
  unlink("/sl_self");
  unlink("/sl_a");
  unlink("/sl_b");

  // Chain of exactly 10 links works, 11 links does not.
  writefile("/sl_t0", "end");
  for(i = 1; i <= 11; i++){
    strcpy(name, "/sl_t");
    name[5] = 'a' + i;
    name[6] = 0;
    if(i == 1)
      strcpy(next, "/sl_t0");
    symlink(next, name);
    strcpy(next, name);
    if(i == 10){
      fd = open(name, O_RDONLY);
      check(fd >= 0, "chain of 10 links can be opened");
      if(fd >= 0)
        close(fd);
    }
    if(i == 11){
      fd = open(name, O_RDONLY);
      check(fd == -1, "chain of 11 links is rejected");
      if(fd >= 0)
        close(fd);
    }
  }
  for(i = 1; i <= 11; i++){
    strcpy(name, "/sl_t");
    name[5] = 'a' + i;
    name[6] = 0;
    unlink(name);
  }
  unlink("/sl_t0");
}

static void
test_nofollow(void)
{
  char buf[64];
  int fd;
  struct stat st;

  printf(1, "test 5: O_NOFOLLOW\n");
  writefile("/sl_file", "data");
  symlink("/sl_file", "/sl_link");
  if(readfile("/sl_link", O_NOFOLLOW, buf, sizeof(buf)) < 0)
    buf[0] = 0;
  printf(1, "O_NOFOLLOW read target: %s\n", buf);
  check(strcmp(buf, "/sl_file") == 0, "O_NOFOLLOW returns the target path");
  fd = open("/sl_link", O_NOFOLLOW);
  if(fd >= 0){
    check(fstat(fd, &st) == 0 && st.type == T_SYMLINK,
          "fstat on a NOFOLLOW fd reports T_SYMLINK");
    close(fd);
  } else
    check(0, "open(O_NOFOLLOW) on a link succeeds");
  check(open("/sl_link", O_NOFOLLOW | O_WRONLY) == -1,
        "O_NOFOLLOW link cannot be opened for writing");
  fd = open("/sl_file", O_NOFOLLOW);
  check(fd >= 0, "O_NOFOLLOW on a regular file works");
  if(fd >= 0)
    close(fd);
  // O_NOFOLLOW on a dangling link still opens the link itself.
  symlink("/sl_gone", "/sl_dl");
  fd = open("/sl_dl", O_NOFOLLOW);
  check(fd >= 0, "O_NOFOLLOW on a dangling link opens the link");
  if(fd >= 0)
    close(fd);
  // Only the last component is not followed: intermediate directories are.
  mkdir("/sl_dir");
  writefile("/sl_dir/inner", "x");
  fd = open("/sl_dir/inner", O_NOFOLLOW);
  check(fd >= 0, "O_NOFOLLOW with a directory prefix in the path works");
  if(fd >= 0)
    close(fd);
  unlink("/sl_dl");
}

static void
test_unlink(void)
{
  char buf[64];

  printf(1, "test 6: unlink of a link\n");
  writefile("/sl_file", "keep");
  unlink("/sl_link");
  symlink("/sl_file", "/sl_link");
  check(unlink("/sl_link") == 0, "unlink(link) succeeds");
  check(open("/sl_link", O_NOFOLLOW) == -1, "the link is gone");
  check(readfile("/sl_file", O_RDONLY, buf, sizeof(buf)) == 4 &&
        strcmp(buf, "keep") == 0, "target file still exists");
  printf(1, "unlink link: target %s\n",
         strcmp(buf, "keep") == 0 ? "still exists" : "LOST");
}

static void
test_create_flags(void)
{
  int fd;

  printf(1, "test 7: O_NOFOLLOW | O_CREATE\n");
  fd = open("/sl_new", O_NOFOLLOW | O_CREATE | O_RDWR);
  printf(1, "open(O_NOFOLLOW|O_CREATE): %d (expected -1)\n", fd);
  check(fd == -1, "O_NOFOLLOW|O_CREATE is invalid");
  if(fd >= 0){
    close(fd);
    unlink("/sl_new");
  }
  check(open("/sl_new", O_RDONLY) == -1, "no file was created by the bad call");
}

static void
test_create_on_link(void)
{
  int fd;
  struct stat st;

  printf(1, "test 8: O_CREATE on an existing or dangling link\n");
  writefile("/sl_file", "z");
  unlink("/sl_link");
  symlink("/sl_file", "/sl_link");
  fd = open("/sl_link", O_CREATE | O_RDWR);
  printf(1, "O_CREATE on existing link: %d (expected -1)\n", fd);
  check(fd == -1, "O_CREATE on an existing link fails");
  if(fd >= 0)
    close(fd);

  symlink("/sl_not_there", "/sl_dl");
  fd = open("/sl_dl", O_CREATE | O_RDWR);
  printf(1, "O_CREATE on dangling link: %d (expected -1)\n", fd);
  check(fd == -1, "O_CREATE on a dangling link fails");
  if(fd >= 0)
    close(fd);
  check(open("/sl_not_there", O_RDONLY) == -1,
        "dangling link's target was not created");
  fd = open("/sl_dl", O_NOFOLLOW);
  check(fd >= 0 && fstat(fd, &st) == 0 && st.type == T_SYMLINK,
        "the dangling link itself is unchanged");
  if(fd >= 0)
    close(fd);
}

int
main(void)
{
  printf(1, "symlink_test starting\n");
  cleanup();
  test_basic();
  test_chain();
  test_dangling();
  test_loop();
  test_nofollow();
  test_unlink();
  test_create_flags();
  test_create_on_link();
  cleanup();
  if(failures == 0)
    printf(1, "symlink_test: ALL TESTS PASSED\n");
  else
    printf(1, "symlink_test: %d CHECK(S) FAILED\n", failures);
  exit();
}
