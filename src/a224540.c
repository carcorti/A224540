#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef A224540_BUILD_CFLAGS
#define A224540_BUILD_CFLAGS "unknown"
#endif

#define A224540_MAX_KNOWN_N 21u

typedef struct {
    uint64_t count;
    uint64_t double_pushes;
    uint64_t odd_pushes;
    uint64_t max_stack_len;
    uint64_t max_value;
    double elapsed_sec;
} CountResult;

typedef struct {
    uint64_t *data;
    size_t len;
    size_t cap;
} Stack;

typedef struct {
    const char *mode;
    unsigned n;
    uint64_t m;
    uint64_t count;
    bool has_expected;
    uint64_t expected;
    bool ok;
    CountResult telemetry;
} Row;

typedef struct {
    bool exists;
    bool has_normalized;
    dev_t dev;
    ino_t ino;
    char *normalized;
} PathIdentity;

typedef struct {
    uint64_t x;
    unsigned depth;
} DepthNode;

typedef struct {
    DepthNode *data;
    size_t len;
    size_t cap;
} DepthStack;

typedef struct {
    uint64_t n;
    uint64_t m;
    unsigned depth;
    uint64_t first_index;
    uint64_t last_index;
    uint64_t prefix_count;
    uint64_t frontier_count;
    uint64_t roots_processed;
    uint64_t count;
    CountResult telemetry;
} SegmentRun;

typedef struct {
    unsigned n;
    uint64_t m;
    unsigned depth;
    uint64_t first_index;
    uint64_t last_index;
    uint64_t prefix_count;
    uint64_t frontier_count;
    uint64_t roots_processed;
    uint64_t count;
    uint64_t double_pushes;
    uint64_t odd_pushes;
    uint64_t max_stack_len;
    uint64_t max_value;
} SegmentRecord;

typedef struct {
    SegmentRecord *data;
    size_t len;
    size_t cap;
} SegmentRecordVec;

static const uint64_t known_terms[] = {
    1ULL, 2ULL, 4ULL, 12ULL, 36ULL, 106ULL, 249ULL, 613ULL,
    1732ULL, 8028ULL, 23348ULL, 69370ULL, 210807ULL, 634839ULL,
    1893582ULL, 5686389ULL, 17031777ULL, 51073675ULL,
    153185957ULL, 459516225ULL, 1378707224ULL, 4135278456ULL
};

static double now_monotonic(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(2);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static _Noreturn void die(const char *msg) {
    fprintf(stderr, "error: %s\n", msg);
    exit(2);
}

static _Noreturn void die_errno(const char *msg) {
    fprintf(stderr, "error: %s: %s\n", msg, strerror(errno));
    exit(2);
}

static void stack_init(Stack *s) {
    s->cap = 1024;
    s->len = 0;
    s->data = (uint64_t *)malloc(s->cap * sizeof(*s->data));
    if (s->data == NULL) {
        die_errno("malloc stack");
    }
}

static void stack_free(Stack *s) {
    free(s->data);
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

static void stack_push(Stack *s, uint64_t x, CountResult *r) {
    if (s->len == s->cap) {
        if (s->cap > SIZE_MAX / (2u * sizeof(*s->data))) {
            die("stack capacity overflow");
        }
        size_t new_cap = s->cap * 2u;
        uint64_t *new_data = (uint64_t *)realloc(s->data, new_cap * sizeof(*s->data));
        if (new_data == NULL) {
            die_errno("realloc stack");
        }
        s->data = new_data;
        s->cap = new_cap;
    }
    s->data[s->len++] = x;
    if ((uint64_t)s->len > r->max_stack_len) {
        r->max_stack_len = (uint64_t)s->len;
    }
}

static uint64_t stack_pop(Stack *s) {
    return s->data[--s->len];
}

static void depth_stack_init(DepthStack *s) {
    s->cap = 1024;
    s->len = 0;
    s->data = (DepthNode *)malloc(s->cap * sizeof(*s->data));
    if (s->data == NULL) {
        die_errno("malloc depth stack");
    }
}

static void depth_stack_free(DepthStack *s) {
    free(s->data);
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

static void depth_stack_push(DepthStack *s, uint64_t x, unsigned depth, CountResult *r) {
    if (s->len == s->cap) {
        if (s->cap > SIZE_MAX / (2u * sizeof(*s->data))) {
            die("depth stack capacity overflow");
        }
        size_t new_cap = s->cap * 2u;
        DepthNode *new_data = (DepthNode *)realloc(s->data, new_cap * sizeof(*s->data));
        if (new_data == NULL) {
            die_errno("realloc depth stack");
        }
        s->data = new_data;
        s->cap = new_cap;
    }
    s->data[s->len].x = x;
    s->data[s->len].depth = depth;
    s->len++;
    if ((uint64_t)s->len > r->max_stack_len) {
        r->max_stack_len = (uint64_t)s->len;
    }
}

static DepthNode depth_stack_pop(DepthStack *s) {
    return s->data[--s->len];
}

static void checked_inc_u64(uint64_t *value, const char *label) {
    if (*value == UINT64_MAX) {
        die(label);
    }
    (*value)++;
}

static void checked_add_u64(uint64_t *dst, uint64_t add, const char *label) {
    if (*dst > UINT64_MAX - add) {
        die(label);
    }
    *dst += add;
}

static uint64_t parse_u64(const char *text, const char *label) {
    if (text[0] == '\0') {
        fprintf(stderr, "error: invalid %s: %s\n", label, text);
        exit(2);
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            fprintf(stderr, "error: invalid %s: %s\n", label, text);
            exit(2);
        }
    }
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        fprintf(stderr, "error: invalid %s: %s\n", label, text);
        exit(2);
    }
    return (uint64_t)value;
}

static unsigned parse_unsigned(const char *text, const char *label) {
    uint64_t value = parse_u64(text, label);
    if (value > UINT_MAX) {
        fprintf(stderr, "error: %s too large: %s\n", label, text);
        exit(2);
    }
    return (unsigned)value;
}

static bool parse_decimal_token_u64(const char *begin, size_t len, uint64_t *out) {
    if (len == 0u) {
        return false;
    }
    if (len > 1u && begin[0] == '0') {
        return false;
    }

    __uint128_t value = 0;
    for (size_t i = 0; i < len; i++) {
        if (begin[i] < '0' || begin[i] > '9') {
            return false;
        }
        value = value * 10u + (unsigned)(begin[i] - '0');
        if (value > UINT64_MAX) {
            return false;
        }
    }

    *out = (uint64_t)value;
    return true;
}

static bool pow3_u64(unsigned n, uint64_t *out) {
    __uint128_t value = 1;
    for (unsigned i = 0; i < n; i++) {
        value *= 3u;
        if (value > UINT64_MAX) {
            return false;
        }
    }
    *out = (uint64_t)value;
    return true;
}

static uint64_t pow3_checked(unsigned n) {
    uint64_t value = 0;
    if (!pow3_u64(n, &value)) {
        die("3^n exceeds uint64_t");
    }
    return (uint64_t)value;
}

static void valid_children(uint64_t x, uint64_t m, uint64_t child[2], size_t *num) {
    *num = 0;
    if (x <= m / 2u) {
        child[(*num)++] = x * 2u;
    }
    if (x % 3u == 1u) {
        uint64_t y = (x - 1u) / 3u;
        if (y > 1u && (y & 1u) == 1u && y <= m) {
            child[(*num)++] = y;
        }
    }
}

static CountResult count_subtree_for_m(uint64_t root, uint64_t m, bool reverse_child_order) {
    CountResult r;
    memset(&r, 0, sizeof(r));
    r.max_value = root;

    Stack stack;
    stack_init(&stack);

    double t0 = now_monotonic();
    stack_push(&stack, root, &r);

    while (stack.len != 0) {
        uint64_t x = stack_pop(&stack);
        checked_inc_u64(&r.count, "count overflow");
        if (x > r.max_value) {
            r.max_value = x;
        }

        uint64_t child[2];
        size_t num = 0;
        valid_children(x, m, child, &num);

        if (!reverse_child_order) {
            for (size_t i = 0; i < num; i++) {
                if (i == 0u && x <= m / 2u) {
                    checked_inc_u64(&r.double_pushes, "double_pushes overflow");
                } else {
                    checked_inc_u64(&r.odd_pushes, "odd_pushes overflow");
                }
                stack_push(&stack, child[i], &r);
            }
        } else {
            for (size_t i = num; i > 0; i--) {
                uint64_t y = child[i - 1u];
                if (i - 1u == 0u && x <= m / 2u) {
                    checked_inc_u64(&r.double_pushes, "double_pushes overflow");
                } else {
                    checked_inc_u64(&r.odd_pushes, "odd_pushes overflow");
                }
                stack_push(&stack, y, &r);
            }
        }
    }

    r.elapsed_sec = now_monotonic() - t0;
    stack_free(&stack);
    return r;
}

static CountResult count_for_m(uint64_t m, bool reverse_child_order) {
    return count_subtree_for_m(1u, m, reverse_child_order);
}

static CountResult count_for_n(unsigned n, bool reverse_child_order) {
    return count_for_m(pow3_checked(n), reverse_child_order);
}

static double nodes_per_sec(const CountResult *r) {
    if (r->elapsed_sec <= 0.0) {
        return 0.0;
    }
    return (double)r->count / r->elapsed_sec;
}

static char *dup_range(const char *begin, size_t len) {
    char *s = (char *)malloc(len + 1u);
    if (s == NULL) {
        die_errno("malloc path");
    }
    memcpy(s, begin, len);
    s[len] = '\0';
    return s;
}

static char *join_parent_base(const char *parent, const char *base) {
    size_t parent_len = strlen(parent);
    size_t base_len = strlen(base);
    bool need_slash = parent_len != 0u && parent[parent_len - 1u] != '/';
    size_t total = parent_len + (need_slash ? 1u : 0u) + base_len;
    char *joined = (char *)malloc(total + 1u);
    if (joined == NULL) {
        die_errno("malloc normalized path");
    }
    memcpy(joined, parent, parent_len);
    size_t pos = parent_len;
    if (need_slash) {
        joined[pos++] = '/';
    }
    memcpy(joined + pos, base, base_len);
    joined[total] = '\0';
    return joined;
}

static char *parent_for_path(const char *path, const char **base_out) {
    if (path[0] == '\0') {
        die("empty path");
    }

    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        *base_out = path;
        return dup_range(".", 1u);
    }
    *base_out = slash + 1;
    if (slash == path) {
        return dup_range("/", 1u);
    }
    return dup_range(path, (size_t)(slash - path));
}

static PathIdentity path_identity(const char *path) {
    PathIdentity id;
    memset(&id, 0, sizeof(id));

    struct stat st;
    if (stat(path, &st) == 0) {
        id.exists = true;
        id.dev = st.st_dev;
        id.ino = st.st_ino;
        char *resolved = realpath(path, NULL);
        if (resolved != NULL) {
            id.normalized = resolved;
            id.has_normalized = true;
        }
        return id;
    }
    if (errno != ENOENT && errno != ENOTDIR) {
        die_errno("stat path");
    }

    const char *base = NULL;
    char *parent = parent_for_path(path, &base);
    char *resolved_parent = realpath(parent, NULL);
    if (resolved_parent == NULL) {
        resolved_parent = parent;
        parent = NULL;
    }
    id.normalized = join_parent_base(resolved_parent, base);
    id.has_normalized = true;
    free(resolved_parent);
    free(parent);
    return id;
}

static void path_identity_free(PathIdentity *id) {
    free(id->normalized);
    id->normalized = NULL;
}

static bool paths_may_alias(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    if (strcmp(a, b) == 0) {
        return true;
    }

    PathIdentity ia = path_identity(a);
    PathIdentity ib = path_identity(b);
    bool alias = false;
    if (ia.exists && ib.exists) {
        alias = ia.dev == ib.dev && ia.ino == ib.ino;
    } else if (ia.has_normalized && ib.has_normalized) {
        alias = strcmp(ia.normalized, ib.normalized) == 0;
    }

    path_identity_free(&ia);
    path_identity_free(&ib);
    return alias;
}

static void reject_path_alias(const char *a, const char *b, const char *msg) {
    if (paths_may_alias(a, b)) {
        die(msg);
    }
}

static FILE *open_output_or_stdout(const char *path) {
    if (path == NULL) {
        return stdout;
    }
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(path, flags, 0666);
    if (fd < 0) {
        die_errno(path);
    }
    FILE *f = fdopen(fd, "w");
    if (f == NULL) {
        int saved = errno;
        close(fd);
        errno = saved;
        die_errno(path);
    }
    return f;
}

static bool path_is_blank_or_comment(const char *line) {
    const char *p = line;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return *p == '\0' || *p == '#';
}

static bool parse_bfile_line(const char *line, unsigned *n, uint64_t *expected) {
    const char *p = line;
    if (*p < '0' || *p > '9') {
        return false;
    }

    const char *n_begin = p;
    while (*p >= '0' && *p <= '9') {
        p++;
    }
    size_t n_len = (size_t)(p - n_begin);
    if (*p != ' ' && *p != '\t') {
        return false;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    const char *value_begin = p;
    while (*p >= '0' && *p <= '9') {
        p++;
    }
    size_t value_len = (size_t)(p - value_begin);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p == '\r') {
        p++;
    }
    if (*p == '\n') {
        p++;
    }
    if (*p != '\0') {
        return false;
    }

    uint64_t n64 = 0;
    if (!parse_decimal_token_u64(n_begin, n_len, &n64) || n64 > UINT_MAX) {
        return false;
    }
    if (!parse_decimal_token_u64(value_begin, value_len, expected)) {
        return false;
    }
    *n = (unsigned)n64;
    return true;
}

static bool split_tabs_exact(char *line, char **fields, size_t expected_fields) {
    /* Destructive split: the caller must not need the original line afterward. */
    char *p = line;
    for (size_t i = 0; i < expected_fields; i++) {
        fields[i] = p;
        char *tab = strchr(p, '\t');
        if (i + 1u == expected_fields) {
            return tab == NULL;
        }
        if (tab == NULL) {
            return false;
        }
        *tab = '\0';
        p = tab + 1;
    }
    return false;
}

static void trim_line_end(char *line) {
    size_t len = strlen(line);
    while (len != 0u && (line[len - 1u] == '\n' || line[len - 1u] == '\r')) {
        line[--len] = '\0';
    }
}

static bool parse_field_u64(const char *text, uint64_t *out) {
    return parse_decimal_token_u64(text, strlen(text), out);
}

static bool parse_field_unsigned(const char *text, unsigned *out) {
    uint64_t value = 0;
    if (!parse_field_u64(text, &value) || value > UINT_MAX) {
        return false;
    }
    *out = (unsigned)value;
    return true;
}

static bool read_segment_record(const char *path, SegmentRecord *rec) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        die_errno(path);
    }

    char header[512];
    char row[512];
    if (fgets(header, sizeof(header), f) == NULL || fgets(row, sizeof(row), f) == NULL) {
        fclose(f);
        return false;
    }
    trim_line_end(header);
    trim_line_end(row);
    if (strcmp(header, "mode\tn\tM\tdepth\tfirst_index\tlast_index\tprefix_count\tfrontier_count\troots_processed\tcount\tstatus\telapsed_sec\tnodes_per_sec\tmax_stack_len\tmax_value\tdouble_pushes\todd_pushes") != 0) {
        fclose(f);
        return false;
    }

    char extra[8];
    if (fgets(extra, sizeof(extra), f) != NULL) {
        fclose(f);
        return false;
    }
    if (ferror(f)) {
        die_errno("read segment TSV");
    }
    if (fclose(f) != 0) {
        die_errno("fclose segment TSV");
    }

    char *fields[17];
    if (!split_tabs_exact(row, fields, 17u)) {
        return false;
    }
    if (strcmp(fields[0], "segment") != 0 || strcmp(fields[10], "SEGMENT") != 0) {
        return false;
    }

    memset(rec, 0, sizeof(*rec));
    bool parsed = parse_field_unsigned(fields[1], &rec->n) &&
           parse_field_u64(fields[2], &rec->m) &&
           parse_field_unsigned(fields[3], &rec->depth) &&
           parse_field_u64(fields[4], &rec->first_index) &&
           parse_field_u64(fields[5], &rec->last_index) &&
           parse_field_u64(fields[6], &rec->prefix_count) &&
           parse_field_u64(fields[7], &rec->frontier_count) &&
           parse_field_u64(fields[8], &rec->roots_processed) &&
           parse_field_u64(fields[9], &rec->count) &&
           parse_field_u64(fields[13], &rec->max_stack_len) &&
           parse_field_u64(fields[14], &rec->max_value) &&
           parse_field_u64(fields[15], &rec->double_pushes) &&
           parse_field_u64(fields[16], &rec->odd_pushes);
    if (!parsed) {
        return false;
    }

    uint64_t canonical_m = 0;
    if (!pow3_u64(rec->n, &canonical_m) || rec->m != canonical_m) {
        return false;
    }
    return true;
}

static void fsync_parent_dir(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *dir_path = ".";
    char *owned = NULL;

    if (slash != NULL) {
        size_t len = (size_t)(slash - path);
        if (len == 0u) {
            dir_path = "/";
        } else {
            owned = (char *)malloc(len + 1u);
            if (owned == NULL) {
                die_errno("malloc parent directory path");
            }
            memcpy(owned, path, len);
            owned[len] = '\0';
            dir_path = owned;
        }
    }

    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int fd = open(dir_path, flags);
    if (fd < 0) {
        die_errno("open parent directory");
    }
    if (fsync(fd) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        die_errno("fsync parent directory");
    }
    if (close(fd) != 0) {
        die_errno("close parent directory");
    }
    free(owned);
}

static void close_output(FILE *f, const char *path) {
    if (f == stdout) {
        if (fflush(f) != 0) {
            die_errno("fflush stdout");
        }
        return;
    }

    if (fflush(f) != 0) {
        die_errno("fflush output");
    }
    int fd = fileno(f);
    if (fd < 0) {
        die_errno("fileno output");
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        die_errno("fstat output");
    }
    bool regular = S_ISREG(st.st_mode);
    if (regular && fsync(fd) != 0) {
        die_errno("fsync output");
    }
    if (fclose(f) != 0) {
        die_errno("fclose output");
    }
    if (regular && path != NULL) {
        fsync_parent_dir(path);
    }
}

static void print_tsv_header(FILE *f) {
    fprintf(f, "mode\tn\tM\tcount\texpected\tstatus\telapsed_sec\tnodes_per_sec\tmax_stack_len\tmax_value\tdouble_pushes\todd_pushes\n");
}

static const char *row_status(const Row *row) {
    if (!row->ok) {
        return "FAIL";
    }
    return row->has_expected ? "PASS" : "COMPUTED";
}

static void print_row(FILE *f, const Row *row) {
    fprintf(f, "%s\t%u\t%" PRIu64 "\t%" PRIu64 "\t", row->mode, row->n, row->m, row->count);
    if (row->has_expected) {
        fprintf(f, "%" PRIu64, row->expected);
    }
    fprintf(f, "\t%s\t%.9f\t%.3f\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n",
            row_status(row),
            row->telemetry.elapsed_sec,
            nodes_per_sec(&row->telemetry),
            row->telemetry.max_stack_len,
            row->telemetry.max_value,
            row->telemetry.double_pushes,
            row->telemetry.odd_pushes);
}

static void print_plan_header(FILE *f) {
    fprintf(f, "mode\tn\tM\tdepth\tchunk\tfirst_index\tlast_index\tprefix_count\tfrontier_count\troots_in_chunk\tstatus\n");
}

static void print_segment_header(FILE *f) {
    fprintf(f, "mode\tn\tM\tdepth\tfirst_index\tlast_index\tprefix_count\tfrontier_count\troots_processed\tcount\tstatus\telapsed_sec\tnodes_per_sec\tmax_stack_len\tmax_value\tdouble_pushes\todd_pushes\n");
}

static void print_segment_row(FILE *f, const SegmentRun *seg, const char *status) {
    fprintf(f,
            "segment\t%" PRIu64 "\t%" PRIu64 "\t%u\t%" PRIu64 "\t%" PRIu64
            "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
            "\t%s\t%.9f\t%.3f\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n",
            seg->n, seg->m, seg->depth, seg->first_index, seg->last_index,
            seg->prefix_count, seg->frontier_count, seg->roots_processed,
            seg->count, status, seg->telemetry.elapsed_sec,
            nodes_per_sec(&seg->telemetry), seg->telemetry.max_stack_len,
            seg->telemetry.max_value, seg->telemetry.double_pushes,
            seg->telemetry.odd_pushes);
}

static void print_aggregate_header(FILE *f) {
    fprintf(f, "mode\tn\tM\tdepth\tprefix_count\tfrontier_count\tsegments\troots_processed\tcount\texpected\tstatus\telapsed_sec\tnodes_per_sec\tmax_stack_len\tmax_value\tdouble_pushes\todd_pushes\n");
}

static void print_segment_check_header(FILE *f) {
    fprintf(f, "mode\tn\tM\tdepth\tchunks\tprefix_count\tfrontier_count\tmonolithic_count\tsegmented_count\tstatus\telapsed_sec\n");
}

static void merge_telemetry(CountResult *dst, const CountResult *src) {
    checked_add_u64(&dst->count, src->count, "merged count overflow");
    checked_add_u64(&dst->double_pushes, src->double_pushes, "merged double_pushes overflow");
    checked_add_u64(&dst->odd_pushes, src->odd_pushes, "merged odd_pushes overflow");
    if (src->max_stack_len > dst->max_stack_len) {
        dst->max_stack_len = src->max_stack_len;
    }
    if (src->max_value > dst->max_value) {
        dst->max_value = src->max_value;
    }
}

static void record_prefix_child(CountResult *prefix, uint64_t x, uint64_t m, size_t child_index) {
    if (child_index == 0u && x <= m / 2u) {
        checked_inc_u64(&prefix->double_pushes, "prefix double_pushes overflow");
    } else {
        checked_inc_u64(&prefix->odd_pushes, "prefix odd_pushes overflow");
    }
}

static uint64_t generate_frontier_summary(uint64_t m, unsigned depth, CountResult *prefix) {
    memset(prefix, 0, sizeof(*prefix));
    prefix->max_value = 1u;

    DepthStack stack;
    depth_stack_init(&stack);
    depth_stack_push(&stack, 1u, 0u, prefix);

    uint64_t frontier_count = 0;
    while (stack.len != 0) {
        DepthNode item = depth_stack_pop(&stack);
        if (item.depth == depth) {
            checked_inc_u64(&frontier_count, "frontier count overflow");
            continue;
        }

        checked_inc_u64(&prefix->count, "prefix count overflow");
        if (item.x > prefix->max_value) {
            prefix->max_value = item.x;
        }

        uint64_t child[2];
        size_t num = 0;
        valid_children(item.x, m, child, &num);
        for (size_t i = 0; i < num; i++) {
            record_prefix_child(prefix, item.x, m, i);
            depth_stack_push(&stack, child[i], item.depth + 1u, prefix);
        }
    }

    depth_stack_free(&stack);
    return frontier_count;
}

static SegmentRun count_segment_range(unsigned n, unsigned depth,
                                      uint64_t first_index, uint64_t last_index) {
    if (first_index > last_index) {
        die("segment first index must be <= last index");
    }

    uint64_t m = pow3_checked(n);
    SegmentRun seg;
    memset(&seg, 0, sizeof(seg));
    seg.n = n;
    seg.m = m;
    seg.depth = depth;
    seg.first_index = first_index;
    seg.last_index = last_index;
    seg.telemetry.max_value = 1u;

    CountResult prefix;
    memset(&prefix, 0, sizeof(prefix));
    prefix.max_value = 1u;

    DepthStack stack;
    depth_stack_init(&stack);

    double t0 = now_monotonic();
    depth_stack_push(&stack, 1u, 0u, &prefix);

    uint64_t frontier_index = 0;
    while (stack.len != 0) {
        DepthNode item = depth_stack_pop(&stack);
        if (item.depth == depth) {
            if (frontier_index >= first_index && frontier_index <= last_index) {
                CountResult sub = count_subtree_for_m(item.x, m, false);
                merge_telemetry(&seg.telemetry, &sub);
                checked_inc_u64(&seg.roots_processed, "segment roots_processed overflow");
            }
            checked_inc_u64(&frontier_index, "frontier count overflow");
            continue;
        }

        checked_inc_u64(&prefix.count, "prefix count overflow");
        if (item.x > prefix.max_value) {
            prefix.max_value = item.x;
        }

        uint64_t child[2];
        size_t num = 0;
        valid_children(item.x, m, child, &num);
        for (size_t i = 0; i < num; i++) {
            record_prefix_child(&prefix, item.x, m, i);
            depth_stack_push(&stack, child[i], item.depth + 1u, &prefix);
        }
    }

    depth_stack_free(&stack);

    seg.prefix_count = prefix.count;
    seg.frontier_count = frontier_index;
    seg.count = seg.telemetry.count;
    if (seg.frontier_count == 0u) {
        die("frontier is empty at requested depth");
    }
    if (last_index >= seg.frontier_count) {
        die("segment last index exceeds frontier");
    }
    uint64_t expected_roots = last_index - first_index + 1u;
    if (seg.roots_processed != expected_roots) {
        die("segment did not process the requested number of roots");
    }
    seg.telemetry.elapsed_sec = now_monotonic() - t0;
    return seg;
}

static void segment_record_vec_init(SegmentRecordVec *v) {
    v->cap = 16;
    v->len = 0;
    v->data = (SegmentRecord *)malloc(v->cap * sizeof(*v->data));
    if (v->data == NULL) {
        die_errno("malloc segment records");
    }
}

static void segment_record_vec_free(SegmentRecordVec *v) {
    free(v->data);
    v->data = NULL;
    v->len = 0;
    v->cap = 0;
}

static void segment_record_vec_push(SegmentRecordVec *v, const SegmentRecord *rec) {
    if (v->len == v->cap) {
        if (v->cap > SIZE_MAX / (2u * sizeof(*v->data))) {
            die("segment record capacity overflow");
        }
        size_t new_cap = v->cap * 2u;
        SegmentRecord *new_data = (SegmentRecord *)realloc(v->data, new_cap * sizeof(*v->data));
        if (new_data == NULL) {
            die_errno("realloc segment records");
        }
        v->data = new_data;
        v->cap = new_cap;
    }
    v->data[v->len++] = *rec;
}

static int compare_segment_record(const void *a, const void *b) {
    const SegmentRecord *ra = (const SegmentRecord *)a;
    const SegmentRecord *rb = (const SegmentRecord *)b;
    if (ra->first_index < rb->first_index) {
        return -1;
    }
    if (ra->first_index > rb->first_index) {
        return 1;
    }
    return 0;
}

static void current_time_utc(char *buf, size_t n) {
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        snprintf(buf, n, "unavailable");
        return;
    }
    struct tm tmv;
    if (gmtime_r(&now, &tmv) == NULL) {
        snprintf(buf, n, "unavailable");
        return;
    }
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static void write_manifest(const char *path, int argc, char **argv, const char *mode,
                           const char *status, unsigned min_n, unsigned max_n,
                           const char *data_path) {
    if (path == NULL) {
        return;
    }
    FILE *f = open_output_or_stdout(path);

    char stamp[64];
    current_time_utc(stamp, sizeof(stamp));

    fprintf(f, "program=a224540\n");
    fprintf(f, "sequence=A224540\n");
    fprintf(f, "created_utc=%s\n", stamp);
    fprintf(f, "mode=%s\n", mode);
    fprintf(f, "status=%s\n", status);
    fprintf(f, "n_min=%u\n", min_n);
    fprintf(f, "n_max=%u\n", max_n);
    if (data_path != NULL) {
        fprintf(f, "data_path=%s\n", data_path);
    }
    fprintf(f, "numeric_limit=uint64_t states and counts; 3^n must fit uint64_t\n");
    fprintf(f, "known_replay_cap=%u\n", A224540_MAX_KNOWN_N);
    fprintf(f, "c_standard=%ld\n", (long)__STDC_VERSION__);
    fprintf(f, "compiler=%s\n", __VERSION__);
    fprintf(f, "build_date=%s %s\n", __DATE__, __TIME__);
    fprintf(f, "build_cflags=%s\n", A224540_BUILD_CFLAGS);
    fprintf(f, "command=");
    for (int i = 0; i < argc; i++) {
        fprintf(f, "%s%s", i == 0 ? "" : " ", argv[i]);
    }
    fprintf(f, "\n");

    close_output(f, path);
}

static void write_replay_manifest(const char *path, int argc, char **argv,
                                  const char *status, unsigned rows,
                                  bool have_rows, unsigned min_n, unsigned max_n,
                                  const char *data_path) {
    if (path == NULL) {
        return;
    }
    FILE *f = open_output_or_stdout(path);

    char stamp[64];
    current_time_utc(stamp, sizeof(stamp));

    fprintf(f, "program=a224540\n");
    fprintf(f, "sequence=A224540\n");
    fprintf(f, "created_utc=%s\n", stamp);
    fprintf(f, "mode=replay\n");
    fprintf(f, "status=%s\n", status);
    fprintf(f, "rows_processed=%u\n", rows);
    fprintf(f, "has_n_range=%s\n", have_rows ? "true" : "false");
    if (have_rows) {
        fprintf(f, "n_min=%u\n", min_n);
        fprintf(f, "n_max=%u\n", max_n);
    } else {
        fprintf(f, "n_min=\n");
        fprintf(f, "n_max=\n");
    }
    fprintf(f, "data_path=%s\n", data_path);
    fprintf(f, "numeric_limit=uint64_t states and counts; 3^n must fit uint64_t\n");
    fprintf(f, "known_replay_cap=%u\n", A224540_MAX_KNOWN_N);
    fprintf(f, "c_standard=%ld\n", (long)__STDC_VERSION__);
    fprintf(f, "compiler=%s\n", __VERSION__);
    fprintf(f, "build_date=%s %s\n", __DATE__, __TIME__);
    fprintf(f, "build_cflags=%s\n", A224540_BUILD_CFLAGS);
    fprintf(f, "command=");
    for (int i = 0; i < argc; i++) {
        fprintf(f, "%s%s", i == 0 ? "" : " ", argv[i]);
    }
    fprintf(f, "\n");

    close_output(f, path);
}

static bool expect_children(uint64_t x, uint64_t m, uint64_t a, bool has_a,
                            uint64_t b, bool has_b) {
    uint64_t child[2] = {0, 0};
    size_t num = 0;
    valid_children(x, m, child, &num);
    size_t expected = (has_a ? 1u : 0u) + (has_b ? 1u : 0u);
    if (num != expected) {
        return false;
    }
    if (has_a && child[0] != a) {
        return false;
    }
    if (has_b && child[1] != b) {
        return false;
    }
    return true;
}

static int run_plan(unsigned n, unsigned depth, uint64_t chunks,
                    const char *tsv_path, const char *manifest_path,
                    int argc, char **argv) {
    if (chunks == 0u) {
        die("plan chunk count must be positive");
    }
    uint64_t m = pow3_checked(n);
    CountResult prefix;
    double t0 = now_monotonic();
    uint64_t frontier_count = generate_frontier_summary(m, depth, &prefix);
    prefix.elapsed_sec = now_monotonic() - t0;
    if (frontier_count == 0u) {
        die("frontier is empty at requested depth");
    }
    if (chunks > frontier_count) {
        die("plan chunk count exceeds frontier count");
    }

    FILE *out = open_output_or_stdout(tsv_path);
    print_plan_header(out);
    uint64_t base = frontier_count / chunks;
    uint64_t rem = frontier_count % chunks;
    uint64_t first = 0;
    for (uint64_t chunk = 0; chunk < chunks; chunk++) {
        uint64_t roots = base + (chunk < rem ? 1u : 0u);
        uint64_t last = first + roots - 1u;
        fprintf(out,
                "plan\t%u\t%" PRIu64 "\t%u\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
                "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\tPLAN\n",
                n, m, depth, chunk, first, last, prefix.count, frontier_count, roots);
        first = last + 1u;
    }
    close_output(out, tsv_path);
    write_manifest(manifest_path, argc, argv, "plan", "PLAN", n, n, NULL);
    return 0;
}

static int run_segment(unsigned n, unsigned depth, uint64_t first_index, uint64_t last_index,
                       const char *tsv_path, const char *manifest_path,
                       int argc, char **argv) {
    SegmentRun seg = count_segment_range(n, depth, first_index, last_index);
    FILE *out = open_output_or_stdout(tsv_path);
    print_segment_header(out);
    print_segment_row(out, &seg, "SEGMENT");
    close_output(out, tsv_path);
    write_manifest(manifest_path, argc, argv, "segment", "SEGMENT", n, n, NULL);
    return 0;
}

static int run_segment_check(unsigned n, unsigned depth, uint64_t chunks,
                             const char *tsv_path, const char *manifest_path,
                             int argc, char **argv) {
    if (chunks == 0u) {
        die("segment-check chunk count must be positive");
    }
    uint64_t m = pow3_checked(n);
    CountResult prefix;
    uint64_t frontier_count = generate_frontier_summary(m, depth, &prefix);
    if (frontier_count == 0u) {
        die("frontier is empty at requested depth");
    }
    if (chunks > frontier_count) {
        die("segment-check chunk count exceeds frontier count");
    }

    double t0 = now_monotonic();
    CountResult mono = count_for_m(m, false);
    uint64_t segmented_count = prefix.count;
    uint64_t base = frontier_count / chunks;
    uint64_t rem = frontier_count % chunks;
    uint64_t first = 0;
    for (uint64_t chunk = 0; chunk < chunks; chunk++) {
        uint64_t roots = base + (chunk < rem ? 1u : 0u);
        uint64_t last = first + roots - 1u;
        SegmentRun seg = count_segment_range(n, depth, first, last);
        checked_add_u64(&segmented_count, seg.count, "segment-check total overflow");
        first = last + 1u;
    }
    double elapsed = now_monotonic() - t0;
    bool ok = segmented_count == mono.count;

    FILE *out = open_output_or_stdout(tsv_path);
    print_segment_check_header(out);
    fprintf(out, "segment-check\t%u\t%" PRIu64 "\t%u\t%" PRIu64 "\t%" PRIu64
            "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%s\t%.9f\n",
            n, m, depth, chunks, prefix.count, frontier_count,
            mono.count, segmented_count, ok ? "PASS" : "FAIL", elapsed);
    close_output(out, tsv_path);
    write_manifest(manifest_path, argc, argv, "segment-check", ok ? "PASS" : "FAIL", n, n, NULL);
    return ok ? 0 : 1;
}

static int run_aggregate(int path_count, char **paths, const char *tsv_path,
                         const char *manifest_path, int argc, char **argv) {
    if (path_count <= 0) {
        die("--aggregate requires at least one segment TSV path");
    }
    for (int i = 0; i < path_count; i++) {
        reject_path_alias(tsv_path, paths[i], "--tsv must not overwrite an aggregate input");
        reject_path_alias(manifest_path, paths[i], "--manifest must not overwrite an aggregate input");
    }

    SegmentRecordVec records;
    segment_record_vec_init(&records);
    for (int i = 0; i < path_count; i++) {
        SegmentRecord rec;
        if (!read_segment_record(paths[i], &rec)) {
            fprintf(stderr, "error: malformed segment TSV: %s\n", paths[i]);
            segment_record_vec_free(&records);
            return 1;
        }
        segment_record_vec_push(&records, &rec);
    }

    qsort(records.data, records.len, sizeof(*records.data), compare_segment_record);
    const SegmentRecord *first_rec = &records.data[0];
    uint64_t sum = 0;
    uint64_t roots_processed = 0;
    uint64_t expected_first = 0;
    CountResult telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    telemetry.max_value = 1u;
    bool ok = true;
    double t0 = now_monotonic();

    for (size_t i = 0; i < records.len; i++) {
        const SegmentRecord *r = &records.data[i];
        if (r->n != first_rec->n || r->m != first_rec->m ||
            r->depth != first_rec->depth || r->prefix_count != first_rec->prefix_count ||
            r->frontier_count != first_rec->frontier_count) {
            ok = false;
        }
        if (r->first_index != expected_first || r->last_index < r->first_index) {
            ok = false;
        }
        if (r->last_index >= r->first_index &&
            !(r->last_index == UINT64_MAX && r->first_index == 0u)) {
            uint64_t expected_roots = r->last_index - r->first_index + 1u;
            if (r->roots_processed != expected_roots) {
                ok = false;
            }
        } else {
            ok = false;
        }
        if (r->last_index == UINT64_MAX) {
            ok = false;
        } else {
            expected_first = r->last_index + 1u;
        }
        checked_add_u64(&sum, r->count, "aggregate segment count overflow");
        checked_add_u64(&roots_processed, r->roots_processed, "aggregate roots_processed overflow");
        checked_add_u64(&telemetry.double_pushes, r->double_pushes, "aggregate double_pushes overflow");
        checked_add_u64(&telemetry.odd_pushes, r->odd_pushes, "aggregate odd_pushes overflow");
        if (r->max_stack_len > telemetry.max_stack_len) {
            telemetry.max_stack_len = r->max_stack_len;
        }
        if (r->max_value > telemetry.max_value) {
            telemetry.max_value = r->max_value;
        }
    }
    if (first_rec->frontier_count == 0u || expected_first != first_rec->frontier_count) {
        ok = false;
    }
    CountResult prefix_check;
    uint64_t checked_frontier = generate_frontier_summary(first_rec->m, first_rec->depth, &prefix_check);
    if (checked_frontier != first_rec->frontier_count ||
        prefix_check.count != first_rec->prefix_count) {
        ok = false;
    }
    checked_add_u64(&telemetry.double_pushes, prefix_check.double_pushes,
                    "aggregate prefix double_pushes overflow");
    checked_add_u64(&telemetry.odd_pushes, prefix_check.odd_pushes,
                    "aggregate prefix odd_pushes overflow");
    if (prefix_check.max_stack_len > telemetry.max_stack_len) {
        telemetry.max_stack_len = prefix_check.max_stack_len;
    }
    if (prefix_check.max_value > telemetry.max_value) {
        telemetry.max_value = prefix_check.max_value;
    }
    checked_add_u64(&sum, first_rec->prefix_count, "aggregate total count overflow");
    telemetry.count = sum;
    telemetry.elapsed_sec = now_monotonic() - t0;

    bool has_expected = first_rec->n <= A224540_MAX_KNOWN_N;
    uint64_t expected = has_expected ? known_terms[first_rec->n] : 0u;
    if (has_expected && sum != expected) {
        ok = false;
    }
    const char *status = ok ? (has_expected ? "PASS" : "AGGREGATED_NO_ORACLE") : "FAIL";

    FILE *out = open_output_or_stdout(tsv_path);
    print_aggregate_header(out);
    fprintf(out,
            "aggregate\t%u\t%" PRIu64 "\t%u\t%" PRIu64 "\t%" PRIu64
            "\t%zu\t%" PRIu64 "\t%" PRIu64 "\t",
            first_rec->n, first_rec->m, first_rec->depth, first_rec->prefix_count,
            first_rec->frontier_count, records.len, roots_processed, sum);
    if (has_expected) {
        fprintf(out, "%" PRIu64, expected);
    }
    fprintf(out, "\t%s\t%.9f\t%.3f\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n",
            status, telemetry.elapsed_sec, nodes_per_sec(&telemetry),
            telemetry.max_stack_len, telemetry.max_value,
            telemetry.double_pushes, telemetry.odd_pushes);
    close_output(out, tsv_path);
    write_manifest(manifest_path, argc, argv, "aggregate", status,
                   first_rec->n, first_rec->n, NULL);
    segment_record_vec_free(&records);
    return ok ? 0 : 1;
}

static int run_self_test(void) {
    bool ok = true;
    ok = ok && pow3_checked(0) == 1u;
    ok = ok && pow3_checked(1) == 3u;
    ok = ok && pow3_checked(22) == 31381059609ULL;

    ok = ok && expect_children(1u, 100u, 2u, true, 0u, false);
    ok = ok && expect_children(2u, 100u, 4u, true, 0u, false);
    ok = ok && expect_children(4u, 100u, 8u, true, 0u, false);
    ok = ok && expect_children(10u, 100u, 20u, true, 3u, true);
    ok = ok && expect_children(12u, 24u, 24u, true, 0u, false);
    ok = ok && expect_children(12u, 23u, 0u, false, 0u, false);

    for (unsigned n = 0; n <= 14u; n++) {
        CountResult fwd = count_for_n(n, false);
        CountResult rev = count_for_n(n, true);
        if (fwd.count != known_terms[n] || rev.count != known_terms[n] ||
            fwd.double_pushes != rev.double_pushes ||
            fwd.odd_pushes != rev.odd_pushes) {
            ok = false;
            fprintf(stderr, "self-test mismatch at n=%u: %" PRIu64 " %" PRIu64 " expected %" PRIu64 "\n",
                    n, fwd.count, rev.count, known_terms[n]);
        }
    }

    SegmentRun seg_a = count_segment_range(10u, 6u, 0u, 0u);
    SegmentRun seg_b = count_segment_range(10u, 6u, 1u, 1u);
    uint64_t segmented = seg_a.prefix_count;
    checked_add_u64(&segmented, seg_a.count, "self-test segmented total overflow");
    checked_add_u64(&segmented, seg_b.count, "self-test segmented total overflow");
    ok = ok && seg_a.frontier_count == 2u && seg_b.frontier_count == 2u &&
         segmented == known_terms[10];

    printf("self_test\t%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int run_single(unsigned n, const char *tsv_path, const char *manifest_path,
                      int argc, char **argv) {
    uint64_t m = pow3_checked(n);
    CountResult r = count_for_n(n, false);
    Row row = {
        .mode = "single",
        .n = n,
        .m = m,
        .count = r.count,
        .has_expected = n <= A224540_MAX_KNOWN_N,
        .expected = n <= A224540_MAX_KNOWN_N ? known_terms[n] : 0u,
        .ok = n <= A224540_MAX_KNOWN_N ? r.count == known_terms[n] : true,
        .telemetry = r
    };

    FILE *out = open_output_or_stdout(tsv_path);
    print_tsv_header(out);
    print_row(out, &row);
    close_output(out, tsv_path);
    write_manifest(manifest_path, argc, argv, "single", row_status(&row), n, n, NULL);
    return row.ok ? 0 : 1;
}

static int run_range(unsigned start_n, unsigned end_n, const char *row_mode,
                     const char *range_name, const char *tsv_path,
                     const char *manifest_path, int argc, char **argv) {
    if (start_n > end_n) {
        if (strcmp(range_name, "calibrate") == 0) {
            die("calibration start must be <= end");
        }
        die("range start must be <= end");
    }
    (void)pow3_checked(end_n);
    bool ok = true;
    FILE *out = open_output_or_stdout(tsv_path);
    print_tsv_header(out);
    for (unsigned n = start_n; n <= end_n; n++) {
        uint64_t m = pow3_checked(n);
        CountResult r = count_for_n(n, false);
        Row row = {
            .mode = row_mode,
            .n = n,
            .m = m,
            .count = r.count,
            .has_expected = n <= A224540_MAX_KNOWN_N,
            .expected = n <= A224540_MAX_KNOWN_N ? known_terms[n] : 0u,
            .ok = n <= A224540_MAX_KNOWN_N ? r.count == known_terms[n] : true,
            .telemetry = r
        };
        if (!row.ok) {
            ok = false;
        }
        print_row(out, &row);
        fflush(out);
    }
    close_output(out, tsv_path);
    const char *status = ok ? (end_n <= A224540_MAX_KNOWN_N ? "PASS" : "COMPUTED") : "FAIL";
    write_manifest(manifest_path, argc, argv, range_name, status, start_n, end_n, NULL);
    return ok ? 0 : 1;
}

static int run_replay(const char *bfile_path, const char *tsv_path, const char *manifest_path,
                      int argc, char **argv) {
    FILE *in = fopen(bfile_path, "r");
    if (in == NULL) {
        die_errno(bfile_path);
    }
    FILE *out = open_output_or_stdout(tsv_path);
    print_tsv_header(out);

    bool ok = true;
    unsigned min_n = UINT_MAX;
    unsigned max_n = 0;
    unsigned rows = 0;
    unsigned rows_seen = 0;
    char line[256];
    unsigned expected_n = 0;
    while (fgets(line, sizeof(line), in) != NULL) {
        unsigned n = 0;
        uint64_t expected = 0;
        if (path_is_blank_or_comment(line)) {
            continue;
        }
        if (strchr(line, '\n') == NULL && !feof(in)) {
            fprintf(stderr, "error: b-file row is too long\n");
            ok = false;
            break;
        }
        if (!parse_bfile_line(line, &n, &expected)) {
            fprintf(stderr, "error: malformed b-file row: %s", line);
            ok = false;
            break;
        }
        if (n != expected_n) {
            fprintf(stderr, "error: b-file expected row n=%u, got n=%u\n", expected_n, n);
            ok = false;
            break;
        }
        rows_seen++;
        expected_n++;
        if (n > A224540_MAX_KNOWN_N) {
            continue;
        }
        if (expected != known_terms[n]) {
            fprintf(stderr, "error: b-file n=%u expected value %" PRIu64
                    " does not match compiled oracle %" PRIu64 "\n",
                    n, expected, known_terms[n]);
            ok = false;
            break;
        }
        uint64_t m = pow3_checked(n);
        CountResult r = count_for_n(n, false);
        Row row = {
            .mode = "replay",
            .n = n,
            .m = m,
            .count = r.count,
            .has_expected = true,
            .expected = expected,
            .ok = r.count == expected,
            .telemetry = r
        };
        if (!row.ok) {
            ok = false;
        }
        if (n < min_n) {
            min_n = n;
        }
        if (n > max_n) {
            max_n = n;
        }
        rows++;
        print_row(out, &row);
        fflush(out);
    }
    if (ferror(in)) {
        die_errno("read b-file");
    }
    if (fclose(in) != 0) {
        die_errno("fclose b-file");
    }
    close_output(out, tsv_path);

    bool have_rows = rows != 0u;
    if (rows != A224540_MAX_KNOWN_N + 1u ||
        min_n != 0u || max_n != A224540_MAX_KNOWN_N) {
        if (have_rows) {
            fprintf(stderr,
                    "error: replay expected contiguous n=0..%u, got rows=%u rows_seen=%u min=%u max=%u\n",
                    A224540_MAX_KNOWN_N, rows, rows_seen, min_n, max_n);
        } else {
            fprintf(stderr, "error: replay expected contiguous n=0..%u, got rows=0\n",
                    A224540_MAX_KNOWN_N);
        }
        ok = false;
    }
    write_replay_manifest(manifest_path, argc, argv, ok ? "PASS" : "FAIL",
                          rows, have_rows, min_n, max_n, bfile_path);
    return ok ? 0 : 1;
}

static void usage(FILE *f) {
    fprintf(f,
            "usage:\n"
            "  a224540 --self-test\n"
            "  a224540 --n N [--tsv PATH] [--manifest PATH]\n"
            "  a224540 --replay-bfile PATH [--tsv PATH] [--manifest PATH]\n"
            "  a224540 --calibrate START END [--tsv PATH] [--manifest PATH]\n"
            "  a224540 --range START END [--tsv PATH] [--manifest PATH]\n"
            "  a224540 --plan N DEPTH CHUNKS [--tsv PATH] [--manifest PATH]\n"
            "  a224540 --segment N DEPTH FIRST LAST [--tsv PATH] [--manifest PATH]\n"
            "  a224540 --aggregate SEGMENT_TSV... [--tsv PATH] [--manifest PATH]\n"
            "  a224540 --segment-check N DEPTH CHUNKS [--tsv PATH] [--manifest PATH]\n"
            "  a224540 --help\n");
}

static void reject_duplicate(bool seen, const char *flag) {
    if (seen) {
        fprintf(stderr, "error: duplicate option: %s\n", flag);
        exit(2);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    const char *tsv_path = NULL;
    const char *manifest_path = NULL;
    const char *bfile_path = NULL;
    bool self_test = false;
    bool single = false;
    bool replay = false;
    bool calibrate = false;
    bool range = false;
    bool plan = false;
    bool segment = false;
    bool aggregate = false;
    bool segment_check = false;
    bool saw_tsv = false;
    bool saw_manifest = false;
    unsigned n = 0;
    unsigned start_n = 0;
    unsigned end_n = 0;
    unsigned depth = 0;
    uint64_t chunks = 0;
    uint64_t first_index = 0;
    uint64_t last_index = 0;
    char **aggregate_paths = NULL;
    int aggregate_path_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--self-test") == 0) {
            reject_duplicate(self_test, "--self-test");
            self_test = true;
        } else if (strcmp(argv[i], "--n") == 0 && i + 1 < argc) {
            reject_duplicate(single, "--n");
            single = true;
            n = parse_unsigned(argv[++i], "n");
        } else if (strcmp(argv[i], "--replay-bfile") == 0 && i + 1 < argc) {
            reject_duplicate(replay, "--replay-bfile");
            replay = true;
            bfile_path = argv[++i];
        } else if (strcmp(argv[i], "--calibrate") == 0 && i + 2 < argc) {
            reject_duplicate(calibrate, "--calibrate");
            calibrate = true;
            start_n = parse_unsigned(argv[++i], "calibration start");
            end_n = parse_unsigned(argv[++i], "calibration end");
        } else if (strcmp(argv[i], "--range") == 0 && i + 2 < argc) {
            reject_duplicate(range, "--range");
            range = true;
            start_n = parse_unsigned(argv[++i], "range start");
            end_n = parse_unsigned(argv[++i], "range end");
        } else if (strcmp(argv[i], "--plan") == 0 && i + 3 < argc) {
            reject_duplicate(plan, "--plan");
            plan = true;
            n = parse_unsigned(argv[++i], "plan n");
            depth = parse_unsigned(argv[++i], "plan depth");
            chunks = parse_u64(argv[++i], "plan chunks");
        } else if (strcmp(argv[i], "--segment") == 0 && i + 4 < argc) {
            reject_duplicate(segment, "--segment");
            segment = true;
            n = parse_unsigned(argv[++i], "segment n");
            depth = parse_unsigned(argv[++i], "segment depth");
            first_index = parse_u64(argv[++i], "segment first index");
            last_index = parse_u64(argv[++i], "segment last index");
        } else if (strcmp(argv[i], "--segment-check") == 0 && i + 3 < argc) {
            reject_duplicate(segment_check, "--segment-check");
            segment_check = true;
            n = parse_unsigned(argv[++i], "segment-check n");
            depth = parse_unsigned(argv[++i], "segment-check depth");
            chunks = parse_u64(argv[++i], "segment-check chunks");
        } else if (strcmp(argv[i], "--aggregate") == 0 && i + 1 < argc) {
            reject_duplicate(aggregate, "--aggregate");
            aggregate = true;
            aggregate_paths = &argv[i + 1];
            aggregate_path_count = 0;
            while (i + 1 < argc && strncmp(argv[i + 1], "--", 2u) != 0) {
                i++;
                aggregate_path_count++;
            }
            if (aggregate_path_count == 0) {
                die("--aggregate requires at least one segment TSV path");
            }
        } else if (strcmp(argv[i], "--tsv") == 0 && i + 1 < argc) {
            reject_duplicate(saw_tsv, "--tsv");
            saw_tsv = true;
            tsv_path = argv[++i];
        } else if (strcmp(argv[i], "--manifest") == 0 && i + 1 < argc) {
            reject_duplicate(saw_manifest, "--manifest");
            saw_manifest = true;
            manifest_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "error: unknown or incomplete argument: %s\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }

    unsigned selected = (self_test ? 1u : 0u) + (single ? 1u : 0u) +
                        (replay ? 1u : 0u) + (calibrate ? 1u : 0u) +
                        (range ? 1u : 0u) + (plan ? 1u : 0u) +
                        (segment ? 1u : 0u) + (aggregate ? 1u : 0u) +
                        (segment_check ? 1u : 0u);
    if (selected != 1u) {
        die("select exactly one mode");
    }
    reject_path_alias(tsv_path, manifest_path, "--tsv and --manifest must be different paths");
    if (replay) {
        reject_path_alias(tsv_path, bfile_path, "--tsv must not overwrite --replay-bfile");
        reject_path_alias(manifest_path, bfile_path, "--manifest must not overwrite --replay-bfile");
    }

    if (self_test) {
        if (tsv_path != NULL || manifest_path != NULL) {
            die("--self-test does not write TSV or manifest");
        }
        return run_self_test();
    }
    if (single) {
        return run_single(n, tsv_path, manifest_path, argc, argv);
    }
    if (replay) {
        return run_replay(bfile_path, tsv_path, manifest_path, argc, argv);
    }
    if (calibrate) {
        return run_range(start_n, end_n, "calibrate", "calibrate",
                         tsv_path, manifest_path, argc, argv);
    }
    if (range) {
        return run_range(start_n, end_n, "range", "range",
                         tsv_path, manifest_path, argc, argv);
    }
    if (plan) {
        return run_plan(n, depth, chunks, tsv_path, manifest_path, argc, argv);
    }
    if (segment) {
        return run_segment(n, depth, first_index, last_index, tsv_path, manifest_path, argc, argv);
    }
    if (aggregate) {
        return run_aggregate(aggregate_path_count, aggregate_paths, tsv_path, manifest_path, argc, argv);
    }
    return run_segment_check(n, depth, chunks, tsv_path, manifest_path, argc, argv);
}
