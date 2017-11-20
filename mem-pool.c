/*
 * Memory Pool implementation logic.
 */

#include "cache.h"
#include "mem-pool.h"

#define MIN_ALLOC_GROWTH_SIZE 1024 * 1024

static struct mp_block *mem_pool_alloc_block(struct mem_pool *mem_pool, size_t block_alloc)
{
	struct mp_block *p;

	mem_pool->pool_alloc += sizeof(struct mp_block) + block_alloc;
	p = xmalloc(st_add(sizeof(struct mp_block), block_alloc));
	p->next_block = mem_pool->mp_block;
	p->next_free = (char *)p->space;
	p->end = p->next_free + block_alloc;
	mem_pool->mp_block = p;

	return p;
}

void mem_pool_init(struct mem_pool **mem_pool, size_t alloc_growth_size,
		   size_t initial_size)
{
	if (!(*mem_pool)) {
		if (alloc_growth_size < MIN_ALLOC_GROWTH_SIZE)
			alloc_growth_size = MIN_ALLOC_GROWTH_SIZE;

		*mem_pool = xmalloc(sizeof(struct mem_pool));
		(*mem_pool)->mp_block = NULL;
		(*mem_pool)->block_alloc = alloc_growth_size;
		(*mem_pool)->pool_alloc = 0;

		if (initial_size > 0)
			mem_pool_alloc_block((*mem_pool), initial_size);
	}
}

void mem_pool_combine(struct mem_pool *dst, struct mem_pool *src)
{
	struct mp_block *p, *next_block;

	for (next_block = src->mp_block; next_block;) {
		p = next_block;
		next_block = next_block->next_block;
		p->next_block = dst->mp_block;
		dst->mp_block = p;
	}

	src->mp_block = NULL;
}

void mem_pool_discard(struct mem_pool *mem_pool)
{
	struct mp_block *block, *block_to_free;

	for (block = mem_pool->mp_block; block;) {
		block_to_free = block;
		block = block->next_block;
		free(block_to_free);
	}

	free(mem_pool);
}

void *mem_pool_alloc(struct mem_pool *mem_pool, size_t len)
{
	struct mp_block *p;
	void *r;

	/* round up to a 'uintmax_t' alignment */
	if (len & (sizeof(uintmax_t) - 1))
		len += sizeof(uintmax_t) - (len & (sizeof(uintmax_t) - 1));

	for (p = mem_pool->mp_block; p; p = p->next_block)
		if (p->end - p->next_free >= len)
			break;

	if (!p) {
		if (len >= (mem_pool->block_alloc / 2)) {
			mem_pool->pool_alloc += len;
			return xmalloc(len);
		}

		p = mem_pool_alloc_block(mem_pool, mem_pool->block_alloc);
	}

	r = p->next_free;
	p->next_free += len;
	return r;
}

void *mem_pool_calloc(struct mem_pool *mem_pool, size_t count, size_t size)
{
	size_t len = st_mult(count, size);
	void *r = mem_pool_alloc(mem_pool, len);
	memset(r, 0, len);
	return r;
}
