/* For copyright notice, see oscar.h. */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "oscar.h"
#include "greatest.h"

typedef struct link {
    void *d;                    /* data */
    pool_id n;                  /* next */
} link;

static int mark_from_zero(oscar *p, void *udata) {
    int *zero_is_live = (int *) udata;

    if (*zero_is_live) {
        pool_id id = 0;         /* assume 0 is the root. */
        link *n = (link *) oscar_get(p, id);
        if (n == NULL) return 0;
        oscar_mark(p, id);
        id = n->n;

        while (id != 0) {
            oscar_mark(p, id);
            n = (link *) oscar_get(p, id);
            id = n->n;
        }
    }
    return 0;
}

static void basic_free_hook(oscar *pool, pool_id id, void *udata) {
    int *freed_flags = (int *) udata;
    freed_flags[id] = 1;
}

/* In a dynamically allocated 5-cell pool, check that live values
 * persist and unreachable values are swept as expected. */
TEST basic_dynamic(int pad) {
    int zero_is_live = 1;
    int basic_freed[] = {0,0,0,0,0};
    oscar *p = oscar_new(sizeof(link) + pad, 5, oscar_generic_mem_cb, NULL,
        mark_from_zero, &zero_is_live,
        basic_free_hook, basic_freed);
    ASSERT(p);
    unsigned int count = oscar_count(p);
    pool_id id = oscar_alloc(p);
    ASSERT_EQ(0, id);
    link *l = (link *) oscar_get(p, id);
    link *l1 = NULL;
    ASSERT(l->d == NULL);
    ASSERT_EQ(0, l->n);         /* [0] */
    ASSERT(l);

    id = oscar_alloc(p);
    ASSERT_EQ(1, id);
    l->n = id;                  /* [0] -> [1] */
    l = (link *) oscar_get(p, id);
    ASSERT(l);
    l1 = l;

    id = oscar_alloc(p);
    ASSERT_EQ(2, id);
    l->n = id;                  /* [0] -> [1] -> [2] */
    l = (link *) oscar_get(p, id);
    ASSERT(l);

    /* Allocate a couple cells that aren't kept live, to force GC */
    for (int i=0; i<count; i++) (void) oscar_alloc(p);
    id = oscar_alloc(p);
    ASSERT_EQ(4, id);
    l1->n = id;                 /* [0] -> [1] -> [n], 2 is garbage */
    
    /* Allocate a couple cells that aren't kept live, to force GC */
    for (int i=0; i<count; i++) (void) oscar_alloc(p);
    ASSERT_EQ(1, basic_freed[2]);

    for (int i=0; i<count; i++) basic_freed[i] = 0;

    zero_is_live = 0;           /* [0] is no longer root, all are garbage */
    oscar_force_gc(p);

    for (int i=0; i<count; i++) {
        ASSERT_EQ(1, basic_freed[i]);
    }
    
    oscar_free(p);
    PASS();
}

static void count_coll(oscar *pool, pool_id id, void *udata) {
    int *collections = (int *) udata;
    assert(id == 0);
    (*collections)++;
}

/* In the smallest possible valid pool, check that the cell count is 1
 * and repeatedly allocating keeps sweeping and returning the same cell
 * (without corrupting anything interally, etc.). */
TEST fixed_small() {
    int zero_is_live = 0;
    int collections = 0;

#define SZ (88 /* sizeof(oscar) */ + 2*sizeof(link))
    static char raw_mem[SZ];
    oscar *p = oscar_new_fixed(sizeof(link), SZ, raw_mem,
        mark_from_zero, &zero_is_live, count_coll, &collections);
    ASSERTm("no oscar *", p);
#undef SZ
    unsigned int count = oscar_count(p);
    ASSERT_EQ(1, count);

    /* Repeatedly alloc; should get cell 0 every time, because it
     * isn't marked live and should be collected. */
    for (int i=0; i<50; i++) {
        pool_id id = oscar_alloc(p); /* -> garbage */
        ASSERT_EQ(0, id);
    }

    /* cell 0 should have been swept every time, since it was never live. */
    ASSERT_EQ(50, collections);

    oscar_free(p);
    PASS();
}

/* Roughly the same as basic_dynamic, but build the GC pool
 * in statically allocated memory. */
TEST basic_static(int pad) {
    int zero_is_live = 1;
    int SZ = (88 + 10*(sizeof(link) + pad));
    int basic_freed[SZ];
    char raw_mem[SZ];
    oscar *p = oscar_new_fixed(sizeof(link), SZ, raw_mem,
        mark_from_zero, &zero_is_live,
        basic_free_hook, basic_freed);
    ASSERTm("no oscar *", p);
    bzero(basic_freed, SZ * sizeof(int));
    unsigned int count = oscar_count(p);
    pool_id id = oscar_alloc(p);
    ASSERT_EQ(0, id);
    link *l = (link *) oscar_get(p, id);
    link *l1 = NULL;
    ASSERT(l->d == NULL);
    ASSERT_EQ(0, l->n);         /* [0] */
    ASSERT(l);

    id = oscar_alloc(p);
    ASSERT_EQ(1, id);
    l->n = id;                  /* [0] -> [1] */
    l = (link *) oscar_get(p, id);
    ASSERT(l);
    l1 = l;

    id = oscar_alloc(p);
    ASSERT_EQ(2, id);
    l->n = id;                  /* [0] -> [1] -> [2] */
    l = (link *) oscar_get(p, id);
    ASSERT(l);

    /* Allocate a couple cells that aren't kept live, to force GC */
    for (int i=0; i<count; i++) (void) oscar_alloc(p);
    id = oscar_alloc(p);
    l1->n = id;                 /* [0] -> [1] -> [n], 2 is garbage */
   
    /* Allocate a couple cells that aren't kept live, to force GC */
    for (int i=0; i<count; i++) (void) oscar_alloc(p);
    ASSERT_EQ(1, basic_freed[2]);

    for (int i=0; i<5; i++) basic_freed[i] = 0;

    zero_is_live = 0;           /* [0] is no longer root, all are garbage */
    oscar_force_gc(p);

    for (int i=0; i<5; i++) {
        ASSERT_EQ(1, basic_freed[i]);
    }
    
    oscar_free(p);
    PASS();
}

/* Write '\0'..(pad-1) in the pad bytes after the link. */
static void scribble(link *l, int pad) {
    if (pad == 0) return;
    char *raw = (char *) l + sizeof(*l);
    for (int i=0; i<pad; i++) {
        raw[i] = i % 256;
    }
}

/* Check the padding bytes after the link for consistency. */
static int check(oscar *p, pool_id root, int pad) {
    pool_id id = root;
    do {
        link *l = (link *) oscar_get(p, id);
        if (l == NULL) { fprintf(stderr, "NULL link\n"); return 0; }
        intptr_t data = (intptr_t) l->d;
        if (data != id) {
            fprintf(stderr, "id mismatch\n");
            return 0;
        }
        char *raw = (char *) l + sizeof(*l);
        for (int i=0; i<pad; i++) {
            if (raw[i] != i % 256) {
                fprintf(stderr, "corruption, pad %d, id %d, %d: %d\n",
                    pad, id, i, raw[i]);
                return 0;
            }
        }
        id = l->n;
    } while (id != 0);
    return 1;
}

/* Make a linked list of (limit) cells, growing the GC pool on demand, then
 * set 0 to unreachable and force a collection. */
TEST growth(int pad) {
    int zero_is_live = 1;
    int limit = 100000;
    int freed[2*limit];         /* 2x to not segfault due to growth past limit */
    oscar *p = oscar_new(sizeof(link) + pad, 2,
        oscar_generic_mem_cb, NULL,
        mark_from_zero, &zero_is_live,
        basic_free_hook, freed);
    ASSERT(p);

    unsigned int count = oscar_count(p);
    ASSERT_EQ(2, count);

    pool_id id = oscar_alloc(p);
    ASSERT_EQ(0, id);
    pool_id last_id = id;

    for (int i=0; i<limit; i++) {
        pool_id id = oscar_alloc(p);
        ASSERTm("allocation failed", id != OSCAR_ID_NONE);
        link *last = (link *) oscar_get(p, last_id);
        last->d = (void *) ((intptr_t) last_id);
        scribble(last, pad);
        ASSERT(last);
        ASSERT_EQ(0, last->n);         /* [n] -> NULL */
        last->n = id;
        last_id = id;
        ASSERT(oscar_count(p) >= i);
    }

    link *final = (link *) oscar_get(p, limit);
    scribble(final, pad);
    final->d = (void *) ((intptr_t) limit);
    final->n = 0;

    ASSERT_EQ(1, check(p, 0, pad));

    zero_is_live = 0;
    oscar_force_gc(p);          /* GC & ensure all are freed */
    for (int i=0; i<limit; i++) ASSERT_EQ(1, freed[i]);

    oscar_free(p);
    PASS();
}

SUITE(suite) {
    for (int i=0; i<8; i++) {
        int pad = i*sizeof(void *);
        RUN_TESTp(basic_dynamic, pad);
        RUN_TESTp(basic_static, pad);
        RUN_TESTp(growth, pad);
    }
    RUN_TEST(fixed_small);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();      /* command-line arguments, initialization. */
    RUN_SUITE(suite);
    GREATEST_MAIN_END();        /* display results */
}
