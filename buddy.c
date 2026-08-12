#include "buddy.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE   4096
#define MAX_RANK    16

static void *pool_start = NULL;
static int   pool_pages = 0;
static int   pool_max_rank = 0;
static void *free_list[MAX_RANK + 1];
static int8_t *page_rank = NULL;

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

static void add_to_free_list(int rank, void *block)
{
    *(void **)block = free_list[rank];
    free_list[rank] = block;
}

static void *remove_head(int rank)
{
    void *block = free_list[rank];
    if (block) {
        free_list[rank] = *(void **)block;
    }
    return block;
}

static int remove_from_free_list(int rank, void *block)
{
    void **cur = &free_list[rank];
    while (*cur) {
        if (*cur == block) {
            *cur = *(void **)*cur;
            return 1;
        }
        cur = (void **)*cur;
    }
    return 0;
}

static inline void *buddy_addr(void *p, int rank)
{
    int page = ptr_to_page(p);
    int buddy_page = page ^ block_pages(rank);
    return page_to_ptr(buddy_page);
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

    if (page_rank) {
        free(page_rank);
    }
    page_rank = (int8_t *)malloc(pgcount * sizeof(int8_t));
    if (!page_rank) {
        return ENOSPC;
    }
    memset(page_rank, 0, pgcount * sizeof(int8_t));

    memset(free_list, 0, sizeof(free_list));

    int page = 0;
    int remaining = pgcount;
    for (int r = pool_max_rank; r >= 1; r--) {
        int size = block_pages(r);
        while (remaining >= size) {
            void *block = page_to_ptr(page);
            add_to_free_list(r, block);
            page_rank[page] = (int8_t)(-r);
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
    while (chosen_rank <= pool_max_rank && !free_list[chosen_rank]) {
        chosen_rank++;
    }
    if (chosen_rank > pool_max_rank) {
        return ERR_PTR(-ENOSPC);
    }

    void *block = remove_head(chosen_rank);
    int page = ptr_to_page(block);
    page_rank[page] = 0;

    while (chosen_rank > rank) {
        int half_rank = chosen_rank - 1;
        int half_pages = block_pages(half_rank);
        void *buddy = page_to_ptr(page + half_pages);
        add_to_free_list(half_rank, buddy);
        page_rank[page + half_pages] = (int8_t)(-half_rank);
        chosen_rank = half_rank;
    }

    page_rank[page] = (int8_t)rank;
    return block;
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
    void *block = p;

    while (rank < pool_max_rank) {
        void *buddy = buddy_addr(block, rank);
        int buddy_page = ptr_to_page(buddy);
        if (buddy_page < 0 || buddy_page >= pool_pages) {
            break;
        }
        if (page_rank[buddy_page] != (int8_t)(-rank)) {
            break;
        }
        if (!remove_from_free_list(rank, buddy)) {
            break;
        }
        page_rank[buddy_page] = 0;
        if (buddy < block) {
            block = buddy;
            page = buddy_page;
        }
        rank++;
    }

    page_rank[page] = (int8_t)(-rank);
    add_to_free_list(rank, block);
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

    int count = 0;
    void *p = free_list[rank];
    while (p) {
        count++;
        p = *(void **)p;
    }
    return count;
}
