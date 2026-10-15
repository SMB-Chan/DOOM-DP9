//----------------------------------------------------------------
// Statically-allocated memory manager
//
// by Eli Bendersky (eliben@gmail.com)
//
// This code is in the public domain.
//----------------------------------------------------------------
#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "memmgr.h"

typedef ulong Align;

union mem_header_union
{
    struct
    {
        union mem_header_union* next;
        ulong size;
    } s;
    Align align_dummy;
};

typedef union mem_header_union mem_header_t;

static mem_header_t base;
static mem_header_t* freep = 0;
static byte * pool;
static ulong pool_size = 0;
static ulong pool_free_pos = 0;

void set_mem_pool()
{
    /* CY700 / DP9+ (XD-K4800) 64MB SDRAM:
     * OS + .d01 live in 0x8C000000..0x8C9FFFFF, stack at 0x8D200000.
     * Safe 16MB cached heap pool at 0x8D400000..0x8E400000. */
    pool_size = 4096 * 1024;
    pool = (byte *)0x8d800000UL;
}

void memmgr_init()
{
    base.s.next = 0;
    base.s.size = 0;
    freep = 0;
    pool_free_pos = 0;
    set_mem_pool();
}

void memmgr_print_stats()
{
}

static mem_header_t* get_mem_from_pool(ulong nquantas)
{
    ulong total_req_size;
    mem_header_t* h;

    if (nquantas < MIN_POOL_ALLOC_QUANTAS)
        nquantas = MIN_POOL_ALLOC_QUANTAS;

    total_req_size = nquantas * sizeof(mem_header_t);

    if (pool_free_pos + total_req_size <= pool_size)
    {
        h = (mem_header_t*) (pool + pool_free_pos);
        h->s.size = nquantas;
        memmgr_free((void*) (h + 1));
        pool_free_pos += total_req_size;
    }
    else
    {
        return 0;
    }

    return freep;
}

void* memmgr_alloc(ulong nbytes)
{
    mem_header_t* p;
    mem_header_t* prevp;

    if (nbytes == 0)
        return 0;

    ulong nquantas = (nbytes + sizeof(mem_header_t) - 1) / sizeof(mem_header_t) + 1;

    if ((prevp = freep) == 0)
    {
        base.s.next = freep = prevp = &base;
        base.s.size = 0;
    }

    for (p = prevp->s.next; ; prevp = p, p = p->s.next)
    {
        if (p->s.size >= nquantas)
        {
            if (p->s.size == nquantas)
            {
                prevp->s.next = p->s.next;
            }
            else
            {
                p->s.size -= nquantas;
                p += p->s.size;
                p->s.size = nquantas;
            }

            freep = prevp;
            return (void*) (p + 1);
        }
        else if (p == freep)
        {
            if ((p = get_mem_from_pool(nquantas)) == 0)
            {
                return 0;
            }
        }
    }
}

void *memmgr_realloc(void *ptr, ulong size)
{
    mem_header_t* block;
    void *newptr = 0;

    if (!ptr)
        return memmgr_alloc(size);
    if (!size) {
        memmgr_free(ptr);
        return memmgr_alloc(0);
    }

    newptr = memmgr_alloc(size);
    if (newptr) {
        block = ((mem_header_t*) ptr) - 1;
        ulong old_size = (block->s.size - 1) * sizeof(mem_header_t);
        memcpy(newptr, ptr, (old_size < size ? old_size : size));
        memmgr_free(ptr);
    }
    return newptr;
}

void memmgr_free(void* ap)
{
    mem_header_t* block;
    mem_header_t* p;

    if (ap == 0)
        return;

    block = ((mem_header_t*) ap) - 1;

    for (p = freep; !(block > p && block < p->s.next); p = p->s.next)
    {
        if (p >= p->s.next && (block > p || block < p->s.next))
            break;
    }

    if (block + block->s.size == p->s.next)
    {
        block->s.size += p->s.next->s.size;
        block->s.next = p->s.next->s.next;
    }
    else
    {
        block->s.next = p->s.next;
    }

    if (p + p->s.size == block)
    {
        p->s.size += block->s.size;
        p->s.next = block->s.next;
    }
    else
    {
        p->s.next = block;
    }

    freep = p;
}
