// gettimings.c
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>

// ---------- compiler barrier ----------
#if defined(_MSC_VER)
  #include <intrin.h>
  #define COMPILER_BARRIER() _ReadWriteBarrier()
#else
  #define COMPILER_BARRIER() __asm__ __volatile__ ("" ::: "memory")
#endif

// ---------- high-resolution clock ----------
static inline uint64_t nsecs_now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

// ---------- prevent optimization ----------
__attribute__((noinline))
static void empty_function(void) {
    COMPILER_BARRIER();
}

static volatile uint64_t sink_u64;
static volatile double   sink_double;

// ========== measurement harness ==========
typedef void (*action_fn)(void);

static void measure(const char* label,
                    action_fn setup_each, action_fn action, action_fn teardown_each,
                    uint64_t iters, bool subtract_overhead)
{
    if (iters == 0) { fprintf(stderr, "iters must be > 0\n"); exit(2); }

    // warm-up
    uint64_t warm = iters/10 + 1;
    for (uint64_t i = 0; i < warm; ++i) {
        if (setup_each) setup_each();
        COMPILER_BARRIER();
        action();
        COMPILER_BARRIER();
        if (teardown_each) teardown_each();
    }

    // timed
    uint64_t total_ns = 0;
    for (uint64_t i = 0; i < iters; ++i) {
        if (setup_each) setup_each();
        COMPILER_BARRIER();
        uint64_t t0 = nsecs_now();
        action();
        uint64_t t1 = nsecs_now();
        COMPILER_BARRIER();
        if (teardown_each) teardown_each();
        total_ns += (t1 - t0);
    }
    double mean_ns = (double)total_ns / (double)iters;

    double overhead_ns = 0.0;
    if (subtract_overhead) {
        uint64_t total_o = 0;
        for (uint64_t i = 0; i < iters; ++i) {
            if (setup_each) setup_each();
            COMPILER_BARRIER();
            uint64_t t0 = nsecs_now();
            // empty critical section
            COMPILER_BARRIER();
            uint64_t t1 = nsecs_now();
            if (teardown_each) teardown_each();
            total_o += (t1 - t0);
        }
        overhead_ns = (double)total_o / (double)iters;
    }

    printf("%s\n", label);
    printf("iters,%" PRIu64 "\n", iters);
    printf("mean_ns_total,%.3f\n", mean_ns);
    if (subtract_overhead) {
        printf("mean_ns_overhead,%.3f\n", overhead_ns);
        printf("mean_ns_subtracted,%.3f\n", mean_ns - overhead_ns);
    }
    printf("\n");
}

// ========== scenarios ==========

// 1) empty function
static void act_call_empty(void) { empty_function(); }

// 2) drand48
static void act_drand48(void) { sink_double = drand48(); }

// 3) getppid
static void act_getppid(void) { sink_u64 = (uint64_t)getppid(); }

// 4) fork() return in parent
static pid_t last_child = -1;
static void act_fork_parent_return(void) {
    pid_t p = fork();
    if (p < 0) { perror("fork"); exit(1); }
    if (p == 0) { _exit(0); }
    last_child = p;
    sink_u64 ^= (uint64_t)p;
}
static void teardown_wait_for_last_child(void) {
    if (last_child > 0) {
        int st;
        if (waitpid(last_child, &st, 0) < 0) { perror("waitpid"); exit(1); }
        last_child = -1;
    }
}

// 5) waitpid already-terminated
static pid_t ready_zombie = -1;
static void setup_waitpid_ready(void) {
    pid_t p = fork();
    if (p < 0) { perror("fork"); exit(1); }
    if (p == 0) { _exit(0); }
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 2*1000*1000 }; // 2 ms
    nanosleep(&ts, NULL);
    ready_zombie = p;
}
static void act_waitpid_ready(void) {
    int st;
    pid_t r = waitpid(ready_zombie, &st, 0);
    if (r != ready_zombie) { perror("waitpid"); exit(1); }
    sink_u64 ^= (uint64_t)r;
}
// NEW: teardown that reaps child if needed
static void teardown_wait_ready(void) {
    if (ready_zombie > 0) {
        int st;
        pid_t r = waitpid(ready_zombie, &st, 0);
        if (r < 0 && errno != ECHILD) { perror("waitpid"); exit(1); }
        ready_zombie = -1;
    }
}

// 6) child exits + waitpid
static void act_fork_child_exit_wait(void) {
    pid_t p = fork();
    if (p < 0) { perror("fork"); exit(1); }
    if (p == 0) { _exit(0); }
    int st;
    if (waitpid(p, &st, 0) < 0) { perror("waitpid"); exit(1); }
    sink_u64 ^= (uint64_t)st;
}

// 7) system("/bin/true")
static const char* TRUE_PATH = "/bin/true";
static void act_system_true(void) {
    int rc = system(TRUE_PATH);
    if (rc == -1) { perror("system"); exit(1); }
}

// 8) mkdir + rmdir
static char dir_template[] = "/tmp/gtXXXXXX";
static char workdir[PATH_MAX];
static void setup_mkdir_rmdir(void) {
    strcpy(workdir, dir_template);
}
static void act_mkdir_rmdir(void) {
    char buf[PATH_MAX];
    strcpy(buf, workdir);
    if (!mkdtemp(buf)) { perror("mkdtemp"); exit(1); }
    if (rmdir(buf) != 0) { perror("rmdir"); exit(1); }
}

// ========== driver ==========
static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s <scenario 1..8>\n", prog);
}

int main(int argc, char** argv) {
    if (argc != 2) { usage(argv[0]); return 2; }
    int which = atoi(argv[1]);

    uint64_t iters;
    bool subtract_overhead;

    if (access(TRUE_PATH, X_OK) != 0 && access("/usr/bin/true", X_OK) == 0) {
        TRUE_PATH = "/usr/bin/true";
    }

    srand48(0xC0FFEE);

    switch (which) {
        case 1:
            iters = 200000;
            subtract_overhead = true;
            measure("scenario_1_empty_function_call",
                    NULL, act_call_empty, NULL,
                    iters, subtract_overhead);
            break;
        case 2:
            iters = 200000;
            subtract_overhead = true;
            measure("scenario_2_drand48",
                    NULL, act_drand48, NULL,
                    iters, subtract_overhead);
            break;
        case 3:
            iters = 200000;
            subtract_overhead = true;
            measure("scenario_3_getppid",
                    NULL, act_getppid, NULL,
                    iters, subtract_overhead);
            break;
        case 4:
            iters = 8000;
            subtract_overhead = true;
            measure("scenario_4_fork_parent_return",
                    NULL, act_fork_parent_return, teardown_wait_for_last_child,
                    iters, subtract_overhead);
            break;
        case 5:
            iters = 2000; // smaller default to avoid resource limits
            subtract_overhead = true;
            measure("scenario_5_waitpid_already_terminated",
                    setup_waitpid_ready, act_waitpid_ready, teardown_wait_ready,
                    iters, subtract_overhead);
            break;
        case 6:
            iters = 4000;
            subtract_overhead = false;
            measure("scenario_6_fork_child_exit_waitpid",
                    NULL, act_fork_child_exit_wait, NULL,
                    iters, subtract_overhead);
            break;
        case 7:
            iters = 2500;
            subtract_overhead = false;
            measure("scenario_7_system_true",
                    NULL, act_system_true, NULL,
                    iters, subtract_overhead);
            break;
        case 8:
            iters = 20000;
            subtract_overhead = true;
            measure("scenario_8_mkdir_rmdir",
                    setup_mkdir_rmdir, act_mkdir_rmdir, NULL,
                    iters, subtract_overhead);
            break;
        default:
            usage(argv[0]); return 2;
    }

    return 0;
}
