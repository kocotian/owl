#ifndef OWL_ARENA_H
#define OWL_ARENA_H

#include <owl/base.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

constexpr size_t owl_arena_block_size = 64 * 1024;
constexpr ptrdiff_t owl_arena_block_count = 1024;

struct owl_Arena {
	uint8_t *memory;
	/* offset - free space */
	ptrdiff_t offset;
	/* last - previously free space */
	ptrdiff_t last;
	struct owl_ArenaAllocator *parent;
};

struct owl_ArenaAllocator {
	uint8_t *memory;
	struct owl_Arena blocks[owl_arena_block_count];
	ptrdiff_t position;
};

[[nodiscard]] int owl_arenaallocator_create(struct owl_ArenaAllocator *);
[[nodiscard]] struct owl_Arena *owl_arena_get(struct owl_ArenaAllocator *);
int owl_arena_return(struct owl_Arena *);
int owl_arenaallocator_destroy(struct owl_ArenaAllocator *);
[[nodiscard]]
static inline int
owl_arena_clear(struct owl_Arena *this)
{
	if (nullptr == this) {
		errno = EINVAL;
		return -1;
	}

	return this->offset = this->last = 0;
}

[[nodiscard]] void *owl_arena_alloc(struct owl_Arena *, size_t);
[[nodiscard]] void *owl_arena_calloc(struct owl_Arena *, size_t);
[[nodiscard]] void *owl_arena_resize(struct owl_Arena *const, void *const, size_t);
[[nodiscard]] void *owl_arena_realloc(struct owl_Arena *const, void *const, size_t, size_t);
void owl_arena_free(struct owl_Arena *const this, void *const mem);

#if defined(USING_NAMESPACE_OWL) || defined(USING_OWL_ARENA)
#	define arena_block_size owl_arena_block_size
#	define arena_block_count owl_arena_block_count

#	define Arena owl_Arena
#	define ArenaAllocator owl_ArenaAllocator

#	define arenaallocator_create owl_arenaallocator_create
#	define arena_get owl_arena_get
#	define arena_return owl_arena_return
#	define arenaallocator_destroy owl_arenaallocator_destroy
#	define arena_clear owl_arena_clear

#	define arena_alloc owl_arena_alloc
#	define arena_calloc owl_arena_calloc
#	define arena_resize owl_arena_resize
#	define arena_realloc owl_arena_realloc
#	define arena_free owl_arena_free
#endif

#endif
