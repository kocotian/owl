#include <owl/arena.h>

#include <sys/mman.h>
#include <errno.h>

[[nodiscard]]
int
owl_arenaallocator_create(struct owl_ArenaAllocator *this)
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
struct owl_Arena *
owl_arena_get(struct owl_ArenaAllocator *allocator)
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
owl_arena_return(struct owl_Arena *arena)
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

	arena->offset = 0;
	arena->last = 0;
	--aa->position;

	return 0;
}

int
owl_arenaallocator_destroy(struct owl_ArenaAllocator *this)
{
	if (nullptr == this) {
		errno = EINVAL;
		return -1;
	}

	return munmap(this->memory, arena_block_size * arena_block_count);
}

[[nodiscard]]
void *
owl_arena_alloc(struct owl_Arena *this, size_t size)
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
