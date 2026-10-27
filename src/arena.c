#define USING_NAMESPACE_OWL

#include <owl/arena.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>
#include <errno.h>
#include <string.h>

[[nodiscard]]
int
arenaallocator_create(struct ArenaAllocator *const this)
{
	if (nullptr == this) {
		errno = EINVAL;
		return -1;
	}

	if (this->memory) {
		errno = EEXIST;
		return -1;
	}

	this->memory = mmap(nullptr, arena_block_size * arena_block_count,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS,
			-1, 0);

	if (MAP_FAILED == this->memory) {
		errno = ENOMEM;
		return -1;
	}

	for (typeof_unqual (arena_block_count) i = 0; i < arena_block_count; ++i) {
		this->blocks[i].memory = this->memory + (arena_block_size * i);
		this->blocks[i].parent = this;
	}

	return 0;
}

/* Getting arena means we just and return current (position) block
   and move position right. */
[[nodiscard]]
struct Arena *
arena_get(struct ArenaAllocator *const allocator)
{
	if (nullptr == allocator) {
		errno = EINVAL;
		return nullptr;
	}
	if (allocator->position > arena_block_count - 1) {
		errno = ENOMEM;
		return nullptr;
	}
	return allocator->blocks + (allocator->position++);
}

/* Returning arena means we should move position left. */
int
arena_return(struct Arena *arena)
{
	if (nullptr == arena) {
		errno = EINVAL;
		return -1;
	}

	auto aa = arena->parent; /* aa - arena allocator */
	ptrdiff_t this_position = arena - aa->blocks;

	/* We check if given arena fits inside used blocks. */
	if (this_position < 0 || this_position >= aa->position) {
		errno = EFAULT;
		return -1;
	}

	/* We check if arena was last occupied arena block.
	   If not, we should swap that with last occupied arena
	   block. */
	if (this_position < (aa->position - 1)) {
		/* swap */
		typeof_unqual (*arena) atmp =
			aa->blocks[aa->position - 1];
		aa->blocks[aa->position - 1] = *arena;
		aa->blocks[this_position] = atmp;
		arena = aa->blocks + (aa->position - 1);
	}

	if (arena_clear(arena) < 0) /* always false */
		return 0;

	--aa->position;

	return 0;
}

int
arenaallocator_destroy(struct ArenaAllocator *const this)
{
	if (nullptr == this) {
		errno = EINVAL;
		return -1;
	}

	return munmap(this->memory, arena_block_size * arena_block_count);
}

[[nodiscard]]
void *
arena_alloc(struct Arena *const this, size_t size)
{
	if (nullptr == this || 0 == size) {
		errno = EINVAL;
		return nullptr;
	}
	
	/* alignment */
	size = (size + 7) & ~7;

	if (this->offset + size > arena_block_size) {
		errno = ENOMEM;
		return nullptr;
	}

	void *ret = this->memory + this->offset;
	this->last = this->offset;
	this->offset += size;
	return ret;
}

[[nodiscard]]
void *
arena_calloc(struct Arena *const this, size_t size)
{
	/* validation is skipped here, because arena_alloc
	   will validate and set errno if needed */
	auto ret = arena_alloc(this, size);
	if (nullptr == ret)
		return nullptr;
	memset(ret, 0, size);
	return ret;
}

[[nodiscard]]
void *
arena_resize(struct Arena *const this, void *const mem, size_t size)
{
	if (nullptr == this || nullptr == mem || 0 == size) {
		errno = EINVAL;
		return nullptr;
	}

	if (0 > this->last || mem != this->memory + this->last) {
		errno = EADDRNOTAVAIL;
		return nullptr;
	}

	size = (size + 7) & ~7;

	if (this->last + size > arena_block_size) {
		errno = ENOMEM;
		return nullptr;
	}

	this->offset = this->last + size;
	return mem;
}

[[nodiscard]]
void *
arena_realloc(struct Arena *const this, void *const mem, size_t oldsize, size_t newsize)
{
	if (nullptr == this || (0 == oldsize && nullptr != mem) || 0 == newsize) {
		errno = EINVAL;
		return nullptr;
	}

	if (nullptr == mem)
		return arena_alloc(this, newsize);

	void *ret = arena_resize(this, mem, newsize);
	if (nullptr == ret) {
		if (ENOMEM == errno)
			return nullptr;
	} else {
		return ret;
	}

	ret = arena_alloc(this, newsize);
	if (nullptr == ret)
		return nullptr;

	memcpy(ret, mem, newsize > oldsize ? oldsize : newsize);

	return ret;
}

void
arena_free(struct Arena *const this, void *const mem)
{
	if (nullptr == this || nullptr == mem)
		return;

	if (0 > this->last || mem != this->memory + this->last) {
		errno = EADDRNOTAVAIL;
		return;
	}

	this->offset = this->last;
	this->last = -1;
}
