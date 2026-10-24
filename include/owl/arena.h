#include <owl/base.h>

#include <stddef.h>
#include <stdint.h>

#ifndef OWL_ARENA_H
#define OWL_ARENA_H

constexpr size_t arena_block_size = 64 * 1024;
constexpr ptrdiff_t arena_block_count = 1024;

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
	struct owl_Arena blocks[arena_block_count];
	ptrdiff_t position;
};

[[nodiscard]] int owl_arenaallocator_create(struct owl_ArenaAllocator *);
[[nodiscard]] struct owl_Arena *owl_arena_get(struct owl_ArenaAllocator *);
int owl_arena_return(struct owl_Arena *);
int owl_arenaallocator_destroy(struct owl_ArenaAllocator *);

[[nodiscard]] void *owl_arena_alloc(struct owl_Arena *, size_t);

#endif
