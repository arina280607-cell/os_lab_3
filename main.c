#define _GNU_SOURCE

#include "filter.h"

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    const double *erosion_src;
    const double *dilation_src;
    double *erosion_dst;
    double *dilation_dst;
    size_t rows;
    size_t cols;
    size_t begin_row;
    size_t end_row;
    size_t iteration;
    int verbose;
} Task;

static void usage(const char *program)
{
    fprintf(stderr,
        "Usage: %s [-r rows] [-c cols] [-k passes] "
        "[-t threads] [-p] [-v]\n"
        "  -r  Number of rows (default: 3)\n"
        "  -c  Number of columns (default: 3)\n"
        "  -k  Number of filter passes (default: 1)\n"
        "  -t  Total thread limit, including main (default: 1)\n"
        "  -p  Print matrices\n"
        "  -v  Print PID/TID and assigned rows\n",
        program);
}

static int parse_positive(const char *text, size_t *result)
{
    if (*text == '\0') {
        return -1;
    }

    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
    }

    errno = 0;
    char *end = NULL;
    uintmax_t value = strtoumax(text, &end, 10);

    if (errno != 0 || *end != '\0' ||
        value == 0 || value > SIZE_MAX) {
        return -1;
    }

    *result = (size_t)value;
    return 0;
}

static void print_matrix(
    const char *name,
    const double *matrix,
    size_t rows,
    size_t cols)
{
    printf("%s:\n", name);

    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < cols; ++col) {
            printf("%7.1f", matrix[row * cols + col]);
        }
        putchar('\n');
    }
}

static void *worker(void *argument)
{
    const Task *task = argument;

    if (task->verbose) {
        printf(
            "PID=%ld TID=%ld pass=%zu rows=[%zu,%zu)\n",
            (long)getpid(),
            (long)syscall(SYS_gettid),
            task->iteration,
            task->begin_row,
            task->end_row);
    }

    filter_rows(
        task->erosion_src,
        task->dilation_src,
        task->erosion_dst,
        task->dilation_dst,
        task->rows,
        task->cols,
        task->begin_row,
        task->end_row);

    return NULL;
}

static void join_workers(pthread_t *threads, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        int error = pthread_join(threads[i], NULL);

        if (error != 0) {
            fprintf(stderr, "pthread_join: %s\n",
                    strerror(error));

            /*
             * We cannot safely reuse or free buffers if a worker
             * might still be using them. Terminate the process.
             */
            exit(EXIT_FAILURE);
        }
    }
}

static int run_pass(
    Task *tasks,
    pthread_t *threads,
    size_t count,
    const double *erosion_src,
    const double *dilation_src,
    double *erosion_dst,
    double *dilation_dst,
    size_t rows,
    size_t cols,
    size_t iteration,
    int verbose)
{
    size_t start = 0;
    size_t base = rows / count;
    size_t remainder = rows % count;

    for (size_t i = 0; i < count; ++i) {
        size_t length = base + (i < remainder ? 1 : 0);

        tasks[i] = (Task){
            .erosion_src = erosion_src,
            .dilation_src = dilation_src,
            .erosion_dst = erosion_dst,
            .dilation_dst = dilation_dst,
            .rows = rows,
            .cols = cols,
            .begin_row = start,
            .end_row = start + length,
            .iteration = iteration,
            .verbose = verbose
        };

        start += length;
    }

    size_t created = 0;

    for (size_t i = 1; i < count; ++i) {
        int error = pthread_create(
            &threads[created], NULL, worker, &tasks[i]);

        if (error != 0) {
            fprintf(stderr, "pthread_create: %s\n",
                    strerror(error));
            join_workers(threads, created);
            return -1;
        }

        ++created;
    }

    /* The main thread computes its own range of rows. */
    worker(&tasks[0]);

    join_workers(threads, created);
    return 0;
}

static double elapsed_seconds(
    struct timespec begin,
    struct timespec end)
{
    return (double)(end.tv_sec - begin.tv_sec)
         + (double)(end.tv_nsec - begin.tv_nsec) / 1e9;
}

int main(int argc, char **argv)
{
    size_t rows = 3;
    size_t cols = 3;
    size_t passes = 1;
    size_t requested_threads = 1;
    int print = 0;
    int verbose = 0;
    int option;

    while ((option = getopt(argc, argv, "r:c:k:t:pvh")) != -1) {
        size_t *destination = NULL;

        switch (option) {
        case 'r':
            destination = &rows;
            break;
        case 'c':
            destination = &cols;
            break;
        case 'k':
            destination = &passes;
            break;
        case 't':
            destination = &requested_threads;
            break;
        case 'p':
            print = 1;
            continue;
        case 'v':
            verbose = 1;
            continue;
        case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            usage(argv[0]);
            return EXIT_FAILURE;
        }

        if (parse_positive(optarg, destination) != 0) {
            fprintf(stderr,
                    "Invalid positive integer for -%c: %s\n",
                    option, optarg);
            return EXIT_FAILURE;
        }
    }

    if (optind != argc) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Check multiplication before calculating allocation sizes. */
    if (rows > SIZE_MAX / cols) {
        fprintf(stderr, "Matrix dimensions are too large\n");
        return EXIT_FAILURE;
    }

    size_t cells = rows * cols;

    if (cells > SIZE_MAX / sizeof(double)) {
        fprintf(stderr, "Matrix allocation size is too large\n");
        return EXIT_FAILURE;
    }

    size_t bytes = cells * sizeof(double);
    size_t count = requested_threads < rows
                 ? requested_threads : rows;

    double *source = malloc(bytes);
    double *erosion_current = malloc(bytes);
    double *dilation_current = malloc(bytes);
    double *erosion_next = malloc(bytes);
    double *dilation_next = malloc(bytes);

    Task *tasks = calloc(count, sizeof(*tasks));
    pthread_t *threads = NULL;

    if (count > 1) {
        threads = calloc(count - 1, sizeof(*threads));
    }

    int status = EXIT_FAILURE;

    if (source == NULL ||
        erosion_current == NULL ||
        dilation_current == NULL ||
        erosion_next == NULL ||
        dilation_next == NULL ||
        tasks == NULL ||
        (count > 1 && threads == NULL)) {
        fprintf(stderr, "Unable to allocate memory\n");
        goto cleanup;
    }

    /*
     * A fixed seed produces the same input on repeated runs.
     * All generated values are finite.
     */
    uint32_t state = 12;

    for (size_t i = 0; i < cells; ++i) {
        state = state * UINT32_C(1664525)
              + UINT32_C(1013904223);
        source[i] = (double)((state >> 16) % 1000) / 10.0;
    }

    memcpy(erosion_current, source, bytes);
    memcpy(dilation_current, source, bytes);

    printf(
        "PID=%ld rows=%zu cols=%zu K=%zu "
        "thread_limit=%zu threads_used=%zu\n",
        (long)getpid(), rows, cols, passes,
        requested_threads, count);

    struct timespec begin;
    struct timespec end;

    if (clock_gettime(CLOCK_MONOTONIC, &begin) == -1) {
        perror("clock_gettime");
        goto cleanup;
    }

    for (size_t pass = 0; pass < passes; ++pass) {
        if (run_pass(
                tasks, threads, count,
                erosion_current, dilation_current,
                erosion_next, dilation_next,
                rows, cols, pass + 1, verbose) != 0) {
            goto cleanup;
        }

        double *temporary = erosion_current;
        erosion_current = erosion_next;
        erosion_next = temporary;

        temporary = dilation_current;
        dilation_current = dilation_next;
        dilation_next = temporary;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
        perror("clock_gettime");
        goto cleanup;
    }

    printf("Elapsed: %.6f seconds\n",
           elapsed_seconds(begin, end));

    if (print) {
        print_matrix("Source", source, rows, cols);
        print_matrix("Erosion", erosion_current, rows, cols);
        print_matrix("Dilation", dilation_current, rows, cols);
    }

    status = EXIT_SUCCESS;

cleanup:
    free(threads);
    free(tasks);
    free(dilation_next);
    free(erosion_next);
    free(dilation_current);
    free(erosion_current);
    free(source);

    return status;
}