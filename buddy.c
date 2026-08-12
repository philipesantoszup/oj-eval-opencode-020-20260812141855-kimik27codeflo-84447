#include "buddy.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE   4096
#define MAX_RANK    16
#define NIL         (-1)

static void *pool_start = NULL;
static int   pool_pages = 0;
static int   pool_max_rank = 0;
static int   free_head[MAX_RANK + 1];
static int   free_count[MAX_RANK + 1];
static int8_t *page_rank = NULL;
static int    *next_free = NULL;
static int    *prev_free = NULL;

static inline int ptr_to_page(void *p)
{
    return (int)(((uintptr_t)p - (uintptr_t)pool_start) / PAGE_SIZE);
}

static inline void *page_to_ptr(int page)
{
    return (void *)((uintptr_t)pool_start + (uintptr_t)page * PAGE_SIZE);
}

static inline int block_pages(int rank)
{
    return 1 << (rank - 1);
}

static void add_to_free_list(int rank, int page)
{
    next_free[page] = free_head[rank];
    if (free_head[rank] != NIL) {
        prev_free[free_head[rank]] = page;
    }
    prev_free[page] = NIL;
    free_head[rank] = page;
    page_rank[page] = (int8_t)(-rank);
    free_count[rank]++;
}

static void remove_from_free_list(int rank, int page)
{
    if (prev_free[page] != NIL) {
        next_free[prev_free[page]] = next_free[page];
    } else {
        free_head[rank] = next_free[page];
    }
    if (next_free[page] != NIL) {
        prev_free[next_free[page]] = prev_free[page];
    }
    page_rank[page] = 0;
    free_count[rank]--;
}

static inline int buddy_page(int page, int rank)
{
    return page ^ block_pages(rank);
}

int init_page(void *p, int pgcount)
{
    if (!p || pgcount <= 0) {
        return EINVAL;
    }

    pool_start = p;
    pool_pages = pgcount;

    pool_max_rank = 1;
    while (pool_max_rank < MAX_RANK && block_pages(pool_max_rank + 1) <= pgcount) {
        pool_max_rank++;
    }

    if (page_rank) free(page_rank);
    if (next_free) free(next_free);
    if (prev_free) free(prev_free);

    page_rank = (int8_t *)malloc(pgcount * sizeof(int8_t));
    next_free = (int *)malloc(pgcount * sizeof(int));
    prev_free = (int *)malloc(pgcount * sizeof(int));
    if (!page_rank || !next_free || !prev_free) {
        return ENOSPC;
    }

    memset(page_rank, 0, pgcount * sizeof(int8_t));
    memset(next_free, 0xff, pgcount * sizeof(int));
    memset(prev_free, 0xff, pgcount * sizeof(int));
    memset(free_head, 0xff, sizeof(free_head));
    memset(free_count, 0, sizeof(free_count));

    int page = 0;
    int remaining = pgcount;
    for (int r = pool_max_rank; r >= 1; r--) {
        int size = block_pages(r);
        while (remaining >= size) {
            add_to_free_list(r, page);
            page += size;
            remaining -= size;
        }
    }

    return OK;
}

void *alloc_pages(int rank)
{
    if (rank < 1 || rank > MAX_RANK) {
        return ERR_PTR(-EINVAL);
    }
    if (!pool_start || rank > pool_max_rank) {
        return ERR_PTR(-ENOSPC);
    }

    int chosen_rank = rank;
    while (chosen_rank <= pool_max_rank && free_head[chosen_rank] == NIL) {
        chosen_rank++;
    }
    if (chosen_rank > pool_max_rank) {
        return ERR_PTR(-ENOSPC);
    }

    int page = free_head[chosen_rank];
    remove_from_free_list(chosen_rank, page);

    while (chosen_rank > rank) {
        int half_rank = chosen_rank - 1;
        int half_pages = block_pages(half_rank);
        add_to_free_list(half_rank, page + half_pages);
        chosen_rank = half_rank;
    }

    page_rank[page] = (int8_t)rank;
    return page_to_ptr(page);
}

int return_pages(void *p)
{
    if (!pool_start || !p) {
        return -EINVAL;
    }
    if ((uintptr_t)p < (uintptr_t)pool_start) {
        return -EINVAL;
    }
    if (((uintptr_t)p - (uintptr_t)pool_start) % PAGE_SIZE != 0) {
        return -EINVAL;
    }
    int page = ptr_to_page(p);
    if (page < 0 || page >= pool_pages) {
        return -EINVAL;
    }
    if (page_rank[page] <= 0) {
        return -EINVAL;
    }

    int rank = (int)page_rank[page];
    page_rank[page] = 0;

    while (rank < pool_max_rank) {
        int buddy = buddy_page(page, rank);
        if (buddy < 0 || buddy >= pool_pages) {
            break;
        }
        if (page_rank[buddy] != (int8_t)(-rank)) {
            break;
        }
        remove_from_free_list(rank, buddy);
        if (buddy < page) {
            page = buddy;
        }
        rank++;
    }

    add_to_free_list(rank, page);
    return OK;
}

int query_ranks(void *p)
{
    if (!pool_start || !p) {
        return -EINVAL;
    }
    if ((uintptr_t)p < (uintptr_t)pool_start) {
        return -EINVAL;
    }
    if (((uintptr_t)p - (uintptr_t)pool_start) % PAGE_SIZE != 0) {
        return -EINVAL;
    }
    int page = ptr_to_page(p);
    if (page < 0 || page >= pool_pages) {
        return -EINVAL;
    }

    if (page_rank[page] > 0) {
        return (int)page_rank[page];
    }
    if (page_rank[page] < 0) {
        return -(int)page_rank[page];
    }

    for (int r = pool_max_rank; r >= 1; r--) {
        int size = block_pages(r);
        int start = page & ~(size - 1);
        if (page_rank[start] == (int8_t)(-r)) {
            return r;
        }
    }

    return -EINVAL;
}

int query_page_counts(int rank)
{
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }
    if (!pool_start) {
        return 0;
    }
    return free_count[rank];
}
