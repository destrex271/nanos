#define INITIAL_MAP_SIZE (0xa000)

#define _PAGE_NO_EXEC       U64_FROM_BIT(63)
#define _PAGE_NO_PS         0x0200 /* AVL[0] */
#define _PAGE_PS            0x0080
#define _PAGE_DIRTY         0x0040
#define _PAGE_ACCESSED      0x0020
#define _PAGE_CACHE_DISABLE 0x0010
#define _PAGE_WRITETHROUGH  0x0008
#define _PAGE_USER          0x0004
#define _PAGE_READONLY      0
#define _PAGE_WRITABLE      0x0002
#define _PAGE_PRESENT       0x0001

#define PAGEMASK           MASK(PAGELOG)
#define PAGEMASK_2M        MASK(PAGELOG_2M)
#define _PAGE_FLAGS_MASK    (_PAGE_NO_EXEC | PAGEMASK)
#define _PAGE_PROT_FLAGS    (_PAGE_NO_EXEC | _PAGE_USER | _PAGE_WRITABLE)
#define _PAGE_DEV_FLAGS     (_PAGE_WRITABLE | _PAGE_CACHE_DISABLE | _PAGE_NO_EXEC)
#define _PAGE_BACKED_FLAGS  (_PAGE_WRITABLE | _PAGE_NO_EXEC)

/* Though page flags are just a u64, we hide it behind this type to
   emphasize that page flags should be composed using helpers with
   clear semantics, not architecture bits. This is to avoid mistakes
   due to a union of PAGE_* constants on one architecture meaning
   something entirely different on another. */

typedef struct pageflags {
    u64 w;                      /* _PAGE_* flags, keep private to page.[hc] */
} pageflags;

/* Page flags default to minimum permissions:
   - read-only
   - no user access
   - no execute
*/
#define _PAGE_DEFAULT_PERMISSIONS (_PAGE_READONLY | _PAGE_NO_EXEC)

#define PT_FIRST_LEVEL 1
#define PT_PTE_LEVEL   4

#define PT_SHIFT_L1 39
#define PT_SHIFT_L2 30
#define PT_SHIFT_L3 21
#define PT_SHIFT_L4 12

static inline pageflags pageflags_memory(void)
{
    return (pageflags){.w = _PAGE_DEFAULT_PERMISSIONS};
}

static inline pageflags pageflags_memory_writethrough(void)
{
    return (pageflags){.w = _PAGE_DEFAULT_PERMISSIONS | _PAGE_WRITETHROUGH};
}

static inline pageflags pageflags_device(void)
{
    return (pageflags){.w = _PAGE_DEFAULT_PERMISSIONS | _PAGE_CACHE_DISABLE};
}

static inline pageflags pageflags_writable(pageflags flags)
{
    return (pageflags){.w = flags.w | _PAGE_WRITABLE};
}

static inline pageflags pageflags_readonly(pageflags flags)
{
    return (pageflags){.w = flags.w & ~_PAGE_WRITABLE};
}

static inline pageflags pageflags_user(pageflags flags)
{
    return (pageflags){.w = flags.w | _PAGE_USER};
}

static inline pageflags pageflags_noexec(pageflags flags)
{
    return (pageflags){.w = flags.w | _PAGE_NO_EXEC};
}

static inline pageflags pageflags_exec(pageflags flags)
{
    return (pageflags){.w = flags.w & ~_PAGE_NO_EXEC};
}

static inline pageflags pageflags_minpage(pageflags flags)
{
    return (pageflags){.w = flags.w | _PAGE_NO_PS};
}

static inline pageflags pageflags_no_minpage(pageflags flags)
{
    return (pageflags){.w = flags.w & ~_PAGE_NO_PS};
}

/* no-exec, read-only */
static inline pageflags pageflags_default_user(void)
{
    return pageflags_user(pageflags_minpage(pageflags_memory()));
}

static inline boolean pageflags_is_writable(pageflags flags)
{
    return (flags.w & _PAGE_WRITABLE) != 0;
}

static inline boolean pageflags_is_readonly(pageflags flags)
{
    return !pageflags_is_writable(flags);
}

static inline boolean pageflags_is_noexec(pageflags flags)
{
    return (flags.w & _PAGE_NO_EXEC) != 0;
}

static inline boolean pageflags_is_exec(pageflags flags)
{
    return !pageflags_is_noexec(flags);
}

typedef u64 pte;
typedef volatile pte *pteptr;

static inline pte pte_from_pteptr(pteptr pp)
{
    return *pp;
}

static inline void pte_set(pteptr pp, pte p)
{
    *pp = p;
}

static inline boolean pte_is_present(pte entry)
{
    return (entry & _PAGE_PRESENT) != 0;
}

static inline boolean pte_is_block_mapping(pte entry)
{
    return (entry & _PAGE_PS) != 0;
}

static inline int pt_level_shift(int level)
{
    switch (level) {
    case 1:
        return PT_SHIFT_L1;
    case 2:
        return PT_SHIFT_L2;
    case 3:
        return PT_SHIFT_L3;
    case 4:
        return PT_SHIFT_L4;
    }
    return 0;
}

static inline u64 flags_from_pte(u64 pte)
{
    return pte & _PAGE_FLAGS_MASK;
}

static inline u64 page_pte(u64 phys, u64 flags)
{
    return phys | (flags & ~_PAGE_NO_PS) | _PAGE_PRESENT;
}

static inline u64 block_pte(u64 phys, u64 flags)
{
    return phys | flags | _PAGE_PRESENT | _PAGE_PS;
}

static inline u64 new_level_pte(u64 tp_phys)
{
    return tp_phys | _PAGE_WRITABLE | _PAGE_USER | _PAGE_PRESENT;
}

static inline boolean flags_has_minpage(u64 flags)
{
    return (flags & _PAGE_NO_PS) != 0;
}

static inline u64 canonize_address(u64 addr)
{
    if (addr & U64_FROM_BIT(47))
        addr |= 0xffff000000000000;
    return addr;
}

extern u64 pagebase;
static inline u64 get_pagetable_base(u64 vaddr)
{
    return pagebase;
}

u64 *pointer_from_pteaddr(u64 pa);

/* log of mapping size (block or page) if valid leaf, else 0 */
static inline int pte_order(int level, pte entry)
{
    assert(level > 0 && level < 5);
    if (level == 1 || !pte_is_present(entry) ||
        (level != 4 && !(entry & _PAGE_PS)))
        return 0;
    return pt_level_shift(level);
}

static inline u64 pte_map_size(int level, pte entry)
{
    if (pte_is_present(entry)) {
        // XXX take from arm
        if (level == 4)
            return PAGESIZE;
        if ((entry & _PAGE_PS) && level != 1)
            return level == 2 ? HUGE_PAGESIZE : PAGESIZE_2M;
    }
    return INVALID_PHYSICAL;
}

static inline boolean pte_is_mapping(int level, pte entry)
{
    return pte_map_size(level, entry) != INVALID_PHYSICAL;
}

static inline boolean pte_is_dirty(pte entry)
{
    return (entry & _PAGE_DIRTY) != 0;
}

static inline u64 page_from_pte(pte p)
{
    /* page directory pointer base address [51:12] */
    return p & (MASK(52) & ~PAGEMASK);
}

static inline void pt_pte_clean(pteptr pp)
{
    *pp &= ~_PAGE_DIRTY;
}

#ifndef physical_from_virtual
static inline u64 pte_lookup_phys(u64 table, u64 vaddr, int offset)
{
    return table + (((vaddr >> offset) & MASK(9)) << 3);
}

static inline u64 *pte_lookup_ptr(u64 table, u64 vaddr, int offset)
{
    return pointer_from_pteaddr(pte_lookup_phys(table, vaddr, offset));
}

#define _pfv_level(table, vaddr, level)                                 \
    u64 *l ## level = pte_lookup_ptr(table, vaddr, PT_SHIFT_L ## level); \
    if (!(*l ## level & 1))                                             \
        return INVALID_PHYSICAL;

#define _pfv_check_ps(level, vaddr)                                     \
    if (*l ## level & _PAGE_PS)                                         \
        return page_from_pte(*l ## level) | (vaddr & MASK(PT_SHIFT_L ## level));

static inline physical __physical_from_virtual_locked(void *x)
{
    u64 xt = u64_from_pointer(x);
    _pfv_level(pagebase, xt, 1);
    _pfv_level(page_from_pte(*l1), xt, 2);
    _pfv_check_ps(2, xt);
    _pfv_level(page_from_pte(*l2), xt, 3);
    _pfv_check_ps(3, xt);
    _pfv_level(page_from_pte(*l3), xt, 4);
    return page_from_pte(*l4) | (xt & MASK(PT_SHIFT_L4));
}

physical physical_from_virtual(void *x);
#endif

typedef struct flush_entry *flush_entry;

void map_with_complete(u64 virtual, physical p, u64 length, pageflags flags, status_handler complete);

static inline void map(u64 v, physical p, u64 length, pageflags flags)
{
    map_with_complete(v, p, length, flags, 0);
}

void update_map_flags_with_complete(u64 vaddr, u64 length, pageflags flags, status_handler complete);

static inline void update_map_flags(u64 vaddr, u64 length, pageflags flags)
{
    update_map_flags_with_complete(vaddr, length, flags, 0);
}

void unmap(u64 virtual, u64 length);
void unmap_pages_with_handler(u64 virtual, u64 length, range_handler rh);
void unmap_and_free_phys(u64 virtual, u64 length);

static inline void unmap_pages(u64 virtual, u64 length)
{
    unmap_pages_with_handler(virtual, length, 0);
}

void zero_mapped_pages(u64 vaddr, u64 length);
void remap_pages(u64 vaddr_new, u64 vaddr_old, u64 length);
void dump_ptes(void *x);

static inline void map_and_zero(u64 v, physical p, u64 length, pageflags flags, status_handler complete)
{
    assert((v & MASK(PAGELOG)) == 0);
    assert((p & MASK(PAGELOG)) == 0);
    if (pageflags_is_readonly(flags)) {
        map(v, p, length, pageflags_writable(flags));
        zero(pointer_from_u64(v), length);
        update_map_flags_with_complete(v, length, flags, complete);
    } else {
        map_with_complete(v, p, length, flags, complete);
        zero(pointer_from_u64(v), length);
    }
}

typedef closure_type(entry_handler, boolean /* success */, int /* level */,
        u64 /* vaddr */, pteptr /* entry */);
boolean traverse_ptes(u64 vaddr, u64 length, entry_handler eh);
void page_invalidate(flush_entry f, u64 p);
void page_invalidate_sync(flush_entry f, status_handler completion);
flush_entry get_page_flush_entry(void);
void page_invalidate_flush(void);
void flush_tlb();
void init_flush(heap);
void *bootstrap_page_tables(heap initial);
void init_page_early(void *initial_map, range phys);
#ifdef KERNEL
void map_setup_2mbpages(u64 v, physical p, int pages, pageflags flags,
                        u64 *pdpt, u64 *pdt);
void init_mmu(void);
#else
void init_mmu(heap initial);
#endif
void init_page_tables(heap pageheap, id_heap physical);
void init_page_initial_map(void *initial_map, range phys);
