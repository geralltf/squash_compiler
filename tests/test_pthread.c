#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/pthread.h"

static int pass_count = 0;
static int fail_count = 0;

static void pass(const char *name) {
    printf("  [PASS] %s\n", name);
    pass_count++;
}
static void fail(const char *name, const char *reason) {
    printf("  [FAIL] %s: %s\n", name, reason);
    fail_count++;
}

/* ---- Test 1: basic thread creation and join ---- */
static int t1_ran = 0;
static void *t1_worker(void *arg) {
    t1_ran = 1;
    (void)arg;
    return 0;
}

static void test_basic_create_join(void) {
    pthread_t tid;
    t1_ran = 0;
    int r = pthread_create(&tid, 0, t1_worker, 0);
    if (r != 0) { fail("basic create/join", "pthread_create failed"); return; }
    pthread_join(tid, 0);
    if (t1_ran) pass("basic create/join");
    else fail("basic create/join", "worker did not run");
}

/* ---- Test 2: thread argument and return value ---- */
static void *t2_worker(void *arg) {
    int n = (int)(long)arg;
    return (void *)(long)(n * n);
}

static void test_arg_retval(void) {
    pthread_t tid;
    void *retval;
    pthread_create(&tid, 0, t2_worker, (void *)7L);
    pthread_join(tid, &retval);
    int got = (int)(long)retval;
    if (got == 49) pass("arg + return value");
    else fail("arg + return value", "expected 49");
}

/* ---- Test 3: multiple threads ---- */
#define N_THREADS 8
static int results[N_THREADS];
static void *t3_worker(void *arg) {
    int idx = (int)(long)arg;
    results[idx] = idx * 3;
    return 0;
}

static void test_multiple_threads(void) {
    pthread_t tids[N_THREADS];
    int i;
    for (i = 0; i < N_THREADS; i++)
        pthread_create(&tids[i], 0, t3_worker, (void *)(long)i);
    for (i = 0; i < N_THREADS; i++)
        pthread_join(tids[i], 0);
    int ok = 1;
    for (i = 0; i < N_THREADS; i++)
        if (results[i] != i * 3) { ok = 0; break; }
    if (ok) pass("8 threads with index args");
    else    fail("8 threads with index args", "wrong result");
}

/* ---- Test 4: mutex protects shared counter ---- */
static pthread_mutex_t counter_mu;
static int shared_counter = 0;
static void *t4_worker(void *arg) {
    int n = (int)(long)arg;
    int i;
    for (i = 0; i < n; i++) {
        pthread_mutex_lock(&counter_mu);
        shared_counter++;
        pthread_mutex_unlock(&counter_mu);
    }
    return 0;
}

static void test_mutex(void) {
#define MU_THREADS 4
#define MU_ITERS   1000
    pthread_t tids[MU_THREADS];
    int i;
    pthread_mutex_init(&counter_mu, 0);
    shared_counter = 0;
    for (i = 0; i < MU_THREADS; i++)
        pthread_create(&tids[i], 0, t4_worker, (void *)(long)MU_ITERS);
    for (i = 0; i < MU_THREADS; i++)
        pthread_join(tids[i], 0);
    pthread_mutex_destroy(&counter_mu);
    int expected = MU_THREADS * MU_ITERS;
    if (shared_counter == expected) pass("mutex protects counter");
    else {
        printf("    got=%d expected=%d\n", shared_counter, expected);
        fail("mutex protects counter", "counter mismatch");
    }
}

/* ---- Test 5: pthread_self and pthread_equal ---- */
static pthread_t self_in_thread;
static void *t5_worker(void *arg) {
    self_in_thread = pthread_self();
    (void)arg;
    return 0;
}

static void test_self_equal(void) {
    pthread_t tid;
    pthread_create(&tid, 0, t5_worker, 0);
    pthread_join(tid, 0);
    if (pthread_equal(tid, self_in_thread)) pass("pthread_self + pthread_equal");
    else fail("pthread_self + pthread_equal", "thread IDs don't match");
}

/* ---- Test 6: stack of values accumulated by threads ---- */
static int sum_result = 0;
static pthread_mutex_t sum_mu;
static void *sum_worker(void *arg) {
    int v = (int)(long)arg;
    pthread_mutex_lock(&sum_mu);
    sum_result += v;
    pthread_mutex_unlock(&sum_mu);
    return 0;
}

static void test_sum_threads(void) {
#define SUM_N 10
    pthread_t tids[SUM_N];
    int i;
    pthread_mutex_init(&sum_mu, 0);
    sum_result = 0;
    for (i = 1; i <= SUM_N; i++)
        pthread_create(&tids[i-1], 0, sum_worker, (void *)(long)i);
    for (i = 0; i < SUM_N; i++)
        pthread_join(tids[i], 0);
    pthread_mutex_destroy(&sum_mu);
    if (sum_result == 55) pass("sum 1..10 across 10 threads");
    else {
        printf("    got=%d expected=55\n", sum_result);
        fail("sum 1..10 across 10 threads", "wrong sum");
    }
}

int main(void) {
    printf("=== pthread test suite ===\n");
    test_basic_create_join();
    test_arg_retval();
    test_multiple_threads();
    test_mutex();
    test_self_equal();
    test_sum_threads();
    printf("---\n");
    printf("pass=%d  fail=%d\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
