// waitpid_test: user-level tests for the waitpid() system call.
#include "types.h"
#include "stat.h"
#include "user.h"

static int failures;

static void
check(int ok, char *what)
{
  printf(1, "  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if(!ok)
    failures++;
}

// Fork a child that sleeps for 'ticks' and exits.
static int
spawn(int ticks)
{
  int pid = fork();

  if(pid < 0){
    printf(1, "fork failed\n");
    exit();
  }
  if(pid == 0){
    sleep(ticks);
    exit();
  }
  return pid;
}

// 1 + 2: waiting for specific children, in an order different from the
// order in which they finish; a finished (zombie) child that is not the
// target must stay uncollected until it is waited for explicitly.
static void
test_specific_children(void)
{
  int a, b, c, start, t;

  printf(1, "test 1: waitpid on specific children\n");
  a = spawn(30);   // finishes last
  b = spawn(10);   // finishes first
  c = spawn(20);   // finishes second
  start = uptime();

  check(waitpid(a) == a, "waitpid(first child) returns its pid");
  t = uptime() - start;
  check(t >= 25, "waitpid(first child) really waited for the slow child");
  // b and c are zombies by now.  They must not have been collected by the
  // waitpid(a) above, so waiting for them has to still succeed.
  check(waitpid(b) == b, "zombie non-target child b was not reaped early");
  check(waitpid(c) == c, "zombie non-target child c was not reaped early");
  check(wait() == -1, "no children left after reaping all three");
}

// A waiter blocked on one child must not be woken up "successfully" by
// another child exiting first.
static void
test_ordering(void)
{
  int slow, fast, start, r;

  printf(1, "test 2: other child exits first, target is not mistaken\n");
  slow = spawn(40);
  fast = spawn(5);
  start = uptime();
  r = waitpid(slow);
  check(r == slow, "returned the slow target, not the fast child");
  check(uptime() - start >= 30, "waited until the target exited");
  check(waitpid(fast) == fast, "fast child still collectable afterwards");
}

static void
test_minus_one(void)
{
  int a;

  printf(1, "test 3: waitpid(-1) behaves like wait()\n");
  a = spawn(5);
  check(waitpid(-1) == a, "waitpid(-1) collects the only child");
  check(waitpid(-1) == -1, "waitpid(-1) with no children returns -1");
}

static void
test_errors(void)
{
  int a, gc, p[2], start;

  printf(1, "test 4: error cases\n");
  check(waitpid(getpid()) == -1, "waitpid(own pid) == -1");
  check(waitpid(99999) == -1, "waitpid(nonexistent pid) == -1");
  check(waitpid(0) == -1, "waitpid(0) == -1");
  check(waitpid(-2) == -1, "waitpid(-2) == -1");
  check(waitpid(-100) == -1, "waitpid(-100) == -1");

  a = spawn(2);
  check(waitpid(a) == a, "first waitpid(child) succeeds");
  check(waitpid(a) == -1, "waitpid on already reaped pid == -1");

  // A grandchild is not our direct child.
  if(pipe(p) < 0){
    printf(1, "pipe failed\n");
    exit();
  }
  a = fork();
  if(a == 0){
    gc = spawn(30);
    write(p[1], &gc, sizeof(gc));
    waitpid(gc);
    exit();
  }
  read(p[0], &gc, sizeof(gc));
  check(waitpid(gc) == -1, "waitpid(grandchild) == -1");
  check(waitpid(a) == a, "waitpid(child) reaps the middle process");
  close(p[0]);
  close(p[1]);

  // Killed while waiting: the child blocks in waitpid() on a long-lived
  // grandchild; after being killed it must stop waiting and die.
  a = fork();
  if(a == 0){
    gc = spawn(500);
    waitpid(gc);
    // Only reached if the kill did not interrupt waitpid() (should not happen).
    exit();
  }
  sleep(10);
  start = uptime();
  kill(a);
  check(waitpid(a) == a, "killed waiter is collected");
  check(uptime() - start < 100, "kill interrupted the blocked waitpid()");
}

static void
test_wait_still_works(void)
{
  int a, b, r1, r2;

  printf(1, "test 5: plain wait() still works\n");
  check(wait() == -1, "wait() without children == -1");
  a = spawn(3);
  b = spawn(6);
  r1 = wait();
  r2 = wait();
  check((r1 == a && r2 == b) || (r1 == b && r2 == a),
        "two wait() calls collect both children");
  check(wait() == -1, "wait() == -1 afterwards");
}

int
main(void)
{
  printf(1, "waitpid_test starting\n");
  test_specific_children();
  test_ordering();
  test_minus_one();
  test_errors();
  test_wait_still_works();
  if(failures == 0)
    printf(1, "waitpid_test: ALL TESTS PASSED\n");
  else
    printf(1, "waitpid_test: %d CHECK(S) FAILED\n", failures);
  exit();
}
