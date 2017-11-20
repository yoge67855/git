#ifndef MEM_POOL_H
#define MEM_POOL_H

struct mem_pool {
	struct mp_block *mp_block;
	size_t alloc_size;
	size_t total_allocd;
};

void mem_pool_init(struct mem_pool **mem_pool, size_t alloc_growth_size, size_t initial_size);
void mem_pool_combine(struct mem_pool *dst, struct mem_pool *src);
void mem_pool_discard(struct mem_pool *mem_pool);

void *mem_pool_alloc(struct mem_pool *pool, size_t len);
void *mem_pool_calloc(struct mem_pool *pool, size_t count, size_t size);
int mem_pool_contains(struct mem_pool *mem_pool, void *mem);

#endif
