# گزارش نفر اول: `waitpid` (بخش ۶.۲) و `symlink` (بخش ۶.۳)

## فهرست فایل‌های تغییر کرده

| بخش | فایل | تغییر |
|-----|------|-------|
| waitpid | `proc.c` | نوشتن `waitpid(int pid)` و تبدیل `wait()` به `waitpid(-1)` |
| waitpid | `sysproc.c` | پر کردن بدنه‌ی `sys_waitpid` |
| waitpid | `waitpid_test.c` | برنامه‌ی آزمون (۵ سناریو) |
| symlink | `sysfile.c` | نوشتن `sys_symlink`، تابع کمکی `followlinks()`، و تغییر `sys_open` |
| symlink | `symlink_test.c` | برنامه‌ی آزمون (۸ سناریو + حالت‌های مرزی) |

مواردی که از قبل آماده بود و دست نزدم: شماره‌ی سیستم‌کال‌ها در `syscall.h`، جدول در `syscall.c`، تعریف در `user.h` و `defs.h`، پوشاننده‌ها در `usys.S`، ثابت‌های `O_NOFOLLOW` در `fcntl.h` و `T_SYMLINK` در `stat.h`، و فایل‌های آزمون در `Makefile`.

---

## ۱. فراخوانی `waitpid(int pid)`

### ۱.۱ هدف
`wait()` اصلی xv6 منتظر هر فرزندی می‌ماند که تمام شود. `waitpid(pid)` فقط منتظر **فرزند مشخص** می‌ماند:

* `pid > 0`: فقط همان فرزند مستقیم.
* `pid == -1`: مانند `wait()` (هر فرزندی).
* `pid == 0` یا `pid < -1`: نامعتبر، `-1`.

مقدار برگشتی: شناسه‌ی فرزندِ جمع‌آوری‌شده، یا `-1` در این حالت‌ها: pid نامعتبر؛ pid وجود ندارد؛ فرزند من نیست (مثلاً نوه یا فرزند پردازه‌ی دیگر)؛ قبلاً جمع‌آوری شده؛ یا پردازه‌ی منتظر وسط انتظار kill شده است. در این حالت‌ها نباید برای همیشه بلاک شود.

### ۱.۲ کد: `proc.c`

```c
// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  return waitpid(-1);
}

// Wait for a specific child to exit and return its pid.
// pid == -1 waits for any child, like wait().
// Returns -1 immediately if pid is invalid (0 or < -1), is not a child
// of the caller (never existed, already reaped, someone else's child),
// or if the caller is killed while waiting.
// A zombie child other than the target is never reaped here.
int
waitpid(int pid)
{
  struct proc *p;
  int havekids, found;
  struct proc *curproc = myproc();

  if(pid == 0 || pid < -1)
    return -1;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for the target (or any, for -1).
    // The check for zombies and the call to sleep() both happen under
    // ptable.lock, so an exit() between them cannot be missed.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      if(pid != -1 && p->pid != pid)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        found = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return found;
      }
    }

    // No point waiting if we don't have a matching child.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}
```

### ۱.۳ کد: `sysproc.c`

```c
int
sys_waitpid(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return waitpid(pid);
}
```

### ۱.۴ توضیح طراحی

1. **تبدیل `wait()` به `waitpid(-1)`.** منطق قبلی `wait()` در `waitpid` نگه داشته شد و فقط یک فیلتر اضافه شد، پس رفتار `wait()` تغییر نمی‌کند.
2. **فیلتر فرزند هدف.** حلقه فقط پردازه‌هایی را در نظر می‌گیرد که `p->parent == curproc` باشند و (اگر `pid != -1`) `p->pid == pid` باشد. بنابراین:
   * فرزند غیرهدف حتی اگر `ZOMBIE` باشد، `kfree/freevm` نمی‌شود و `UNUSED` نمی‌شود. بعداً با `waitpid` یا `wait` همان فرزند می‌شود جمعش کرد.
   * اگر هیچ فرزند منطبقی نباشد (`havekids == 0`) فوراً `-1` برمی‌گردد و بلاک نمی‌شود. این حالت pid ناموجود، نوه، فرزند دیگران، و pid جمع‌آوری‌شده را پوشش می‌دهد.
3. **قفل و جلوگیری از missed wakeup.** پرسش‌های «آیا فرزند من است؟»، «آیا zombie است؟» و `sleep(curproc, &ptable.lock)` همه زیر `ptable.lock` هستند. `sleep` قفل را به‌صورت اتمیک رها می‌کند و بعد از بیدار شدن دوباره می‌گیرد. اگر قفل بین «چک کردن» و «خوابیدن» آزاد می‌شد، فرزند می‌توانست درست همان لحظه `exit()` کند و `wakeup1(parent)` را قبل از خوابیدن والد بزند. آن بیدارباش از دست می‌رفت و والد برای همیشه می‌خوابید.
4. **`exit()` تغییر نکرد.** `exit()` با `wakeup1(curproc->parent)` والد را بیدار می‌کند. والد حلقه را از اول اجرا می‌کند و شرط را دوباره بررسی می‌کند. اگر فرزند در این فاصله به `init` منتقل شده باشد (والد مرده)، دیگر منطبق نیست و `-1` برمی‌گردد.
5. **kill شدن حین انتظار.** `kill()` پردازه‌ی خوابیده را `RUNNABLE` می‌کند، `sleep` برمی‌گردد و `curproc->killed` دیده می‌شود، پس `waitpid` با `-1` تمام می‌شود. در `trap()` بعد از `syscall()` پردازه‌ی `killed` فوراً `exit()` می‌شود. بنابراین `-1` عملاً به برنامه‌ی کاربر نمی‌رسد؛ نکته‌ی مهم این است که انتظار قطع می‌شود و پردازه برای همیشه نمی‌خوابد.

### ۱.۵ آزمون: `waitpid_test.c`
هر بررسی با `[PASS]` یا `[FAIL]` چاپ می‌شود و در پایان `ALL TESTS PASSED`.

| سناریو | شرح | بررسی |
|--------|-----|-------|
| ۱ | سه فرزند با `sleep` برابر ۳۰، ۱۰، ۲۰ تیک. والد به ترتیب فرزند اول (کندترین)، دوم، سوم را `waitpid` می‌کند | هر بار pid درست؛ `waitpid(اول)` واقعاً صبر می‌کند؛ فرزندان دوم و سوم که zombie شده‌اند توسط `waitpid(اول)` جمع نشده‌اند و بعداً هنوز قابل جمع‌آوری‌اند؛ در پایان `wait() == -1` |
| ۲ | فرزند سریع (۵ تیک) و کند (۴۰ تیک). `waitpid(کند)` | pid کند برمی‌گردد (با خروج فرزند سریع بیدار شده ولی اشتباه نمی‌گیرد)؛ زمان انتظار ≥ ۳۰ تیک؛ فرزند سریع بعداً قابل جمع‌آوری است |
| ۳ | `waitpid(-1)` با یک فرزند | pid فرزند؛ بدون فرزند `-1` |
| ۴ | حالت‌های خطا | `waitpid(getpid())`، pid ناموجود (۹۹۹۹۹)، `0`، `-2`، `-100`، pid جمع‌آوری‌شده، pid نوه: همه `-1`. و kill شدن پردازه‌ای که داخل `waitpid` روی نوه‌ای با `sleep(500)` بلاک است: والد با `kill` آن را می‌کشد و `waitpid(پردازه)` در کمتر از ۱۰۰ تیک (نه ۵۰۰) برمی‌گردد |
| ۵ | `wait()` معمولی | بدون فرزند `-1`؛ با دو فرزند دو بار `wait()` هر دو pid را برمی‌گرداند؛ دوباره `-1` |

خروجی اجرا در QEMU:

```
$ waitpid_test
waitpid_test starting
test 1: waitpid on specific children
  [PASS] waitpid(first child) returns its pid
  [PASS] waitpid(first child) really waited for the slow child
  [PASS] zombie non-target child b was not reaped early
  [PASS] zombie non-target child c was not reaped early
  [PASS] no children left after reaping all three
test 2: other child exits first, target is not mistaken
  [PASS] returned the slow target, not the fast child
  [PASS] waited until the target exited
  [PASS] fast child still collectable afterwards
test 3: waitpid(-1) behaves like wait()
  [PASS] waitpid(-1) collects the only child
  [PASS] waitpid(-1) with no children returns -1
test 4: error cases
  [PASS] waitpid(own pid) == -1
  [PASS] waitpid(nonexistent pid) == -1
  [PASS] waitpid(0) == -1
  [PASS] waitpid(-2) == -1
  [PASS] waitpid(-100) == -1
  [PASS] first waitpid(child) succeeds
  [PASS] waitpid on already reaped pid == -1
  [PASS] waitpid(grandchild) == -1
  [PASS] waitpid(child) reaps the middle process
  [PASS] killed waiter is collected
  [PASS] kill interrupted the blocked waitpid()
test 5: plain wait() still works
  [PASS] wait() without children == -1
  [PASS] two wait() calls collect both children
  [PASS] wait() == -1 afterwards
waitpid_test: ALL TESTS PASSED
$ zombie!
```

پیام `zombie!` از پردازه‌ی `init` است: نوه‌ی آزمون kill بعد از مرگ والدش یتیم شده، به `init` منتقل شده و `init` آن را جمع کرده است. این رفتار طبیعی است.

### ۱.۶ پاسخ پرسش ۸

**(الف) اگر بررسی «آیا فرزند من است؟» حذف شود و فقط منتظر پردازه‌ی با آن pid بمانیم، چه مشکلی پیش می‌آید؟**

* والد می‌تواند پردازه‌ای را که فرزندش نیست جمع‌آوری کند (مثلاً zombie دیگری). منابع آن (پشته‌ی کرنل، جدول صفحه) آزاد می‌شود و پدر واقعی‌اش بعداً `wait` می‌زند و آن فرزند را از دست داده است (نتیجه‌ی غلط یا `-1`). همچنین ممکن است pid بعداً برای پردازه‌ی دیگری دوباره استفاده شود و منتظر اشتباه گرفته شود.
* اگر پردازه‌ی هدف فرزند ما نباشد، ما روی `curproc` می‌خوابیم، ولی `exit()` فقط `wakeup1(curproc->parent)` را صدا می‌زند، یعنی پدر واقعی را بیدار می‌کند، نه ما را. پس هرگز بیدار نمی‌شویم و برای همیشه می‌خوابیم. اگر pid اصلاً وجود نداشته باشد هم همین‌طور.

**(ب) فرزندی که قبل از `waitpid` والد تمام شده در چه حالتی می‌ماند و چرا نمی‌تواند همه‌ی منابعش را در `exit()` آزاد کند؟**

در حالت **`ZOMBIE`** می‌ماند: `exit()` فایل‌ها را می‌بندد و `cwd` را رها می‌کند، `state` را `ZOMBIE` می‌گذارد و به `sched()` می‌رود و دیگر برنگشتنی است. نمی‌تواند پشته‌ی کرنل (`p->kstack`) و جدول صفحه (`p->pgdir`) را آزاد کند، چون `exit()` و `sched()` خودشان روی همین **پشته‌ی کرنل پردازه** اجرا می‌شوند و `swtch` نیز از همین پشته به زمان‌بند می‌رود. آزاد کردن پشته‌ای که هنوز در حال استفاده است یعنی از زیر پای خود کد را خالی کردن. همچنین ورودی `struct proc` باید بماند تا والد pid و وضعیت فرزند را بخواند. به همین دلیل آزادسازی نهایی توسط پردازه‌ی دیگری (والد در `wait/waitpid`) و روی پشته‌ی کرنل خودش انجام می‌شود.

**(ج) اگر والد قبل از فرزند بمیرد چه می‌شود و xv6 چگونه آن را مدیریت می‌کند؟**

در `exit()` والد، حلقه‌ای روی `ptable.proc` می‌گردد و برای هر فرزند `p->parent = initproc` می‌گذارد و اگر آن فرزند zombie باشد `init` را بیدار می‌کند (`wakeup1(initproc)`). پردازه‌ی `init` در حلقه‌ی بی‌نهایت `wait()` می‌زند و این فرزندان یتیم را جمع‌آوری و آزاد می‌کند.

---

## ۲. فراخوانی `symlink` و تغییر `open`

### ۲.۱ هدف
`symlink(target, path)` فایل ویژه‌ای از نوع `T_SYMLINK` در `path` می‌سازد که محتوایش فقط **متن مسیر `target`** است. `open` باید لینک را دنبال کند و فایل مقصد را باز کند. ثابت‌های `T_SYMLINK` (`stat.h`) و `O_NOFOLLOW` (`fcntl.h`) از قبل تعریف شده بودند. در `sysfile.c` دو ثابت محلی اضافه کردم:

```c
#define MAXSYMLINK  128   // max length of a symlink target path
#define MAXSYMLINKS 10    // max links followed by one open()
```

### ۲.۲ کد: `sys_symlink` در `sysfile.c`

```c
int
sys_symlink(void)
{
  char *target, *path;
  struct inode *ip;
  int len;

  if(argstr(0, &target) < 0 || argstr(1, &path) < 0)
    return -1;
  len = strlen(target);
  if(len == 0 || len > MAXSYMLINK)
    return -1;

  begin_op();
  // create() fails if path already exists or its parent directory does not.
  if((ip = create(path, T_SYMLINK, 0, 0)) == 0){
    end_op();
    return -1;
  }
  if(writei(ip, target, 0, len) != len){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}
```

* الگو مثل `sys_mkdir`: همه‌ی کار بین `begin_op()` و `end_op()` (تراکنش لاگ).
* `create()` با نوع `T_SYMLINK` یک inode جدید می‌سازد، `nlink = 1` می‌گذارد و نام را در پوشه‌ی والد می‌نویسد. اگر `path` از قبل وجود داشته باشد (برای نوعی غیر از `T_FILE` همیشه) یا پوشه‌ی والد نباشد، `0` برمی‌گرداند و `symlink` مقدار `-1` می‌دهد.
* مسیر مقصد با `writei` (بدون `\0`) در داده‌ی inode نوشته می‌شود، یعنی `ip->size` برابر طول مسیر است. طول مقصد بین ۱ تا ۱۲۸ بایت است. وجود فایل مقصد بررسی نمی‌شود (لینک شکسته مجاز است).

### ۲.۳ کد: `followlinks()` و تغییر `sys_open` در `sysfile.c`

```c
// Resolve symlinks starting at locked inode ip.  Follows at most MAXSYMLINKS
// links; a dangling link, a read error or a longer chain (e.g. a loop)
// fails.  On success returns the locked, non-symlink target inode.  On
// failure returns 0 with every inode released.  Caller holds a transaction.
static struct inode*
followlinks(struct inode *ip)
{
  char target[MAXSYMLINK + 1];
  int depth;

  for(depth = 0; ip->type == T_SYMLINK; depth++){
    if(depth >= MAXSYMLINKS ||
       ip->size == 0 || ip->size > MAXSYMLINK ||
       readi(ip, target, 0, ip->size) != ip->size){
      iunlockput(ip);
      return 0;
    }
    target[ip->size] = 0;
    iunlockput(ip);
    if((ip = namei(target)) == 0)
      return 0;
    ilock(ip);
  }
  return ip;
}
```

تغییرات `sys_open` (فقط بخش‌های جدید):

```c
  // "Create" and "don't follow" contradict each other.
  if((omode & O_NOFOLLOW) && (omode & O_CREATE))
    return -1;

  begin_op();

  if(omode & O_CREATE){
    // create() refuses to touch an existing symlink (even a dangling one),
    // so this fails with -1 instead of following it or replacing it.
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(!(omode & O_NOFOLLOW) && (ip = followlinks(ip)) == 0){
      end_op();
      return -1;
    }
    // Directories and symlinks opened with O_NOFOLLOW are read-only.
    if((ip->type == T_DIR || ip->type == T_SYMLINK) &&
       (omode & ~O_NOFOLLOW) != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }
```

### ۲.۴ توضیح طراحی

1. **دنبال کردن لینک (`followlinks`).** تا وقتی نوع inode برابر `T_SYMLINK` است: مسیر مقصد با `readi` خوانده و با `\0` تمام می‌شود، inode لینک با `iunlockput` آزاد می‌شود (تا هم‌زمان دو قفل نگه داشته نشود)، سپس `namei(target)` و `ilock` روی مقصد. این کار زنجیره‌ی `a → b → c` را هم حل می‌کند.
2. **حد ۱۰ لینک.** شمارنده‌ی `depth` از ۰ شروع می‌شود و با رسیدن به ۱۰ و دیدن باز هم یک لینک، شکست می‌خورد. پس زنجیره‌ی ۱۰تایی باز می‌شود و ۱۱تایی `-1` می‌دهد. حلقه‌ی `a → b → a` یا `a → a` هم به همین ترتیب بعد از ۱۰ مرحله `-1` می‌دهد و `open` گیر نمی‌کند.
3. **لینک شکسته.** اگر `namei(target)` صفر برگرداند، `-1` و هیچ inode قفل‌شده‌ای نمی‌ماند. لینک با اندازه‌ی ۰ یا خیلی بزرگ هم رد می‌شود (محافظت از خرابی).
4. **`O_NOFOLLOW`.** `followlinks` صدا زده نمی‌شود و خود لینک باز می‌شود و برنامه می‌تواند مسیر مقصد را با `read` بخواند. این فقط برای آخرین مؤلفه‌ی مسیر است؛ پوشه‌های میانی مثل همیشه با `namei` پیمایش می‌شوند.
5. **`O_NOFOLLOW | O_CREATE`.** نامعتبر است و پیش از `begin_op` با `-1` رد می‌شود.
6. **`O_CREATE` روی نام لینک.** `create(path, T_FILE, ...)` وقتی `path` از قبل وجود دارد و `T_FILE` نیست (مثلاً `T_SYMLINK`، حتی شکسته) صفر برمی‌گرداند. پس `open` نه لینک را دنبال می‌کند و نه فایل جدید می‌سازد و `-1` می‌دهد.
7. **باز کردن لینک برای نوشتن (تصمیم طراحی ما).** با `O_NOFOLLOW` لینک فقط برای خواندن باز می‌شود (مثل پوشه‌ها که فقط `O_RDONLY` دارند)، تا متن مقصد خراب نشود. ترکیب `O_NOFOLLOW` با `O_WRONLY/O_RDWR` روی لینک `-1` می‌دهد.
8. **`unlink`.** تغییری نکرد. `sys_unlink` با `nameiparent` و `dirlookup` روی خود نام کار می‌کند و لینک را دنبال نمی‌کند، پس فقط لینک حذف می‌شود و فایل مقصد باقی می‌ماند.
9. طبق صورت پروژه `exec`، `chdir` و `link` لینک را دنبال نمی‌کنند (بخش امتیازی است).

### ۲.۵ آزمون: `symlink_test.c`

| سناریو | شرح | بررسی |
|--------|-----|-------|
| ۱ | ساخت فایل، لینک به آن، نوشتن از طریق لینک و خواندن از فایل اصلی | محتوا درست؛ `symlink` روی نام موجود، پوشه‌ی ناموجود و مقصد خالی `-1` |
| ۲ | زنجیره‌ی `a → b → c` | محتوای `c` خوانده می‌شود |
| ۳ | لینک شکسته | `open` برابر `-1` |
| ۴ | حلقه `a → b → a`، لینک خودارجاع، زنجیره‌ی ۱۰ و ۱۱ تایی | `-1` بدون گیر کردن؛ ۱۰ لینک موفق، ۱۱ لینک `-1` |
| ۵ | `O_NOFOLLOW` | مسیر مقصد خوانده می‌شود؛ `fstat` نوع `T_SYMLINK` می‌دهد؛ باز کردن برای نوشتن `-1`؛ روی فایل عادی و لینک شکسته کار می‌کند؛ با پیشوند پوشه کار می‌کند |
| ۶ | `unlink` لینک | لینک حذف می‌شود و فایل مقصد (با محتوایش) باقی می‌ماند |
| ۷ | `O_NOFOLLOW \| O_CREATE` | `-1` و فایلی ساخته نمی‌شود |
| ۸ | `O_CREATE` روی نام لینک موجود و شکسته | `-1`؛ مقصد ساخته نمی‌شود؛ خود لینک دست‌نخورده می‌ماند |

نکته‌ی پیاده‌سازی تست: xv6 پرچم `O_TRUNC` ندارد، پس تابع کمکی `writefile` قبل از ساخت، فایل را `unlink` می‌کند تا محتوای قبلی باقی نماند.

خروجی اجرا در QEMU:

```
$ symlink_test
symlink_test starting
test 1: read/write through a link
  [PASS] symlink() succeeds
  [PASS] read through link gives file content
  [PASS] open link for writing
  [PASS] write through link visible in file
write via link, read via file: ok
  [PASS] symlink() on existing path fails
  [PASS] symlink() in missing directory fails
  [PASS] symlink() with empty target fails
test 2: chain a -> b -> c
  [PASS] chain resolves to the real file
chain a -> b -> c: ok
test 3: dangling link
dangling link open: -1 (expected -1)
  [PASS] open of dangling link fails
test 4: loops and long chains
loop a -> b -> a open: -1 (expected -1)
  [PASS] open of a symlink loop fails (no hang)
  [PASS] self-referencing link fails
  [PASS] chain of 10 links can be opened
  [PASS] chain of 11 links is rejected
test 5: O_NOFOLLOW
O_NOFOLLOW read target: /sl_file
  [PASS] O_NOFOLLOW returns the target path
  [PASS] fstat on a NOFOLLOW fd reports T_SYMLINK
  [PASS] O_NOFOLLOW link cannot be opened for writing
  [PASS] O_NOFOLLOW on a regular file works
  [PASS] O_NOFOLLOW on a dangling link opens the link
  [PASS] O_NOFOLLOW with a directory prefix in the path works
test 6: unlink of a link
  [PASS] unlink(link) succeeds
  [PASS] the link is gone
  [PASS] target file still exists
unlink link: target still exists
test 7: O_NOFOLLOW | O_CREATE
open(O_NOFOLLOW|O_CREATE): -1 (expected -1)
  [PASS] O_NOFOLLOW|O_CREATE is invalid
  [PASS] no file was created by the bad call
test 8: O_CREATE on an existing or dangling link
O_CREATE on existing link: -1 (expected -1)
  [PASS] O_CREATE on an existing link fails
O_CREATE on dangling link: -1 (expected -1)
  [PASS] O_CREATE on a dangling link fails
  [PASS] dangling link's target was not created
  [PASS] the dangling link itself is unchanged
symlink_test: ALL TESTS PASSED
```

### ۲.۶ پاسخ پرسش ۹

**(الف) تفاوت `link` و `symlink` از نظر inode، `nlink` و رفتار بعد از حذف فایل مقصد.**

* `link` یک **نام دوم برای همان inode** می‌سازد (hard link). `nlink` آن inode یکی زیاد می‌شود و دو نام برابرند. با حذف یک نام، داده تا وقتی `nlink > 0` است باقی می‌ماند و از نام دیگر در دسترس است.
* `symlink` یک **inode جدید** از نوع `T_SYMLINK` می‌سازد که فقط **متن مسیر مقصد** را نگه می‌دارد. `nlink` فایل مقصد تغییر نمی‌کند (`nlink` خود لینک ۱ است). اگر مقصد حذف شود، inode آن آزاد می‌شود و لینک باقی می‌ماند ولی **شکسته (dangling)** است؛ `open` روی آن `-1` می‌دهد.

**(ب) چرا برای `symlink` محدودیت عمق لازم است ولی برای `link` نه؟**

در `link` هر نام مستقیماً به یک inode اشاره می‌کند و هیچ مسیری برای resolve شدن وجود ندارد، پس دنبال کردن بی‌پایان ممکن نیست. (xv6 ساخت hard link به پوشه را هم ممنوع کرده است.) در `symlink` مقصد یک **رشته‌ی مسیر** است که هنگام `open` باید دوباره resolve شود و خودش می‌تواند لینک دیگری باشد. اگر لینک‌ها دور بسازند (`a → b → a` یا `a → a`)، `open` بی‌نهایت می‌چرخد، تراکنش لاگ و قفل inode را نگه می‌دارد و کل سیستم فایل را قفل می‌کند. محدودیت ۱۰ مرحله چنین حالتی را به یک خطای قطعی (`-1`) تبدیل می‌کند.

**(ج) چرا `unlink` روی یک لینک نمادین، فایل مقصد را پاک نمی‌کند؟**

`unlink` نام را از پوشه حذف می‌کند و `nlink` همان inodeای را که آن نام به آن اشاره دارد کم می‌کند. نام لینک به **inode خود لینک** اشاره دارد، نه به فایل مقصد. `sys_unlink` فقط با `nameiparent` و `dirlookup` روی آخرین نام کار می‌کند و مسیر را مثل `open` دنبال نمی‌کند، پس فقط inode لینک حذف می‌شود. اگر `unlink` لینک را دنبال می‌کرد، حذف یک «میانبر» فایل اصلی را از بین می‌برد، که با معنای میانبر در تضاد است.

---

## ۳. ساخت و اجرا

```
chmod +x sign.pl vectors.pl     # در مخزن این دو فایل بیت اجرایی ندارند
make qemu-nox
$ waitpid_test
$ symlink_test
```

هر دو برنامه در پایان `ALL TESTS PASSED` چاپ می‌کنند. چون فایل‌های آزمون در `UPROGS` هستند، با `make` در `fs.img` قرار می‌گیرند.

## ۴. نکات و محدودیت‌ها

* هر دو آزمون بر پایه‌ی `uptime()` و `sleep()` زمان‌بندی می‌شوند؛ مقادیر (مثلاً ≥ ۳۰ تیک یا < ۱۰۰ تیک) با فاصله‌ی امن انتخاب شده‌اند تا به سرعت QEMU حساس نباشند.
* `usertests` (مجموعه‌ی آزمون خود xv6، که در صورت پروژه الزامی نیست) با پیام `balloc: out of blocks` در تست `big files` متوقف می‌شود، چون `fs.img` با `FSSIZE = 1000` تقریباً پر است (شاخه‌ی `main`: ۸۱۴ از ۹۴۱ بلوک، شاخه‌ی ما: ۸۵۲). این مربوط به اندازه‌ی `FSSIZE` در `param.h` است و آزمون‌های درخواست‌شده‌ی پروژه را تحت تأثیر قرار نمی‌دهد.
