// Copyright (c) 2015-2016 Sergio Gonzalez. All rights reserved.
// License: https://github.com/serge-rgb/milton#license

#pragma once



// TODO: out of memory handler.

#define mlt_malloc(sz) malloc(sz)
#define mlt_calloc(n, sz) calloc(n, sz)
#define mlt_free(ptr) do { if (ptr) { free(ptr); ptr = NULL; } else { mlt_assert(!"Freeing null"); } } while(0)
#define mlt_realloc realloc

struct Arena
{
    // Memory:
    size_t  size;
    size_t  count;
    u8*     ptr;

    // For pushing/popping
    Arena*  parent;
    int     id;
    int     num_children;
};

// Create a root arena from a memory block.
Arena arena_init(void* base, size_t size);
Arena arena_spawn(Arena* parent, size_t size);
void  arena_reset(Arena* arena);
void  arena_reset_noclear(Arena* arena);

// ==== Temporary arenas.
// Usage:
//      child = arena_push(my_arena, some_size);
//      use_temporary_arena(&child.arena);
//      arena_pop(child);
Arena  arena_push(Arena* parent, size_t size);
void   arena_pop(Arena* child);
void   arena_pop_noclear(Arena* child);

#define     arena_alloc_elem_(arena, T, flags)          (T *)arena_alloc_bytes((arena), sizeof(T), flags)
#define     arena_alloc_array_(arena, count, T, flags)  (T *)arena_alloc_bytes((arena), (count) * sizeof(T), flags)
#define     arena_alloc_elem(arena, T)                  arena_alloc_elem_(arena, T, Arena_NONE)
#define     arena_alloc_array(arena, count, T)          arena_alloc_array_(arena, count, T, Arena_NONE)
#define     arena_available_space(arena)                ((arena)->size - (arena)->count)
#define     ARENA_VALIDATE(arena)                       mlt_assert ((arena)->num_children == 0)

enum ArenaAllocOpts
{
    Arena_NONE = 0,

    Arena_NOFAIL = 1<<0,
};

u8* arena_alloc_bytes(Arena* arena, size_t num_bytes, int alloc_flags=Arena_NONE);

