/* XXX keep allocs small for now; rolling heap allocations more than a
   page are b0rked */
#define ALLOC_EXTEND_BITS	(1 << 12)

typedef struct bitmap {
    u64 maxbits;
    u64 mapbits;
    heap h;
    buffer alloc_map;
} *bitmap;

u64 bitmap_alloc(bitmap b, int order);
boolean bitmap_dealloc(bitmap b, u64 bit, u64 order);
bitmap allocate_bitmap(heap h, u64 length);
void deallocate_bitmap(bitmap b);

#define bitmap_foreach_set(__b, __i)					\
    for (u64 __wi = 0, * __wp = bitmap_base(__b); __wi < __b->mapbits; __wi += 64, __wp++) \
	for (u64 __w = *__wp, __bit = lsb(__w), __i = __wi + __bit; __w; \
	     __w &= ~(1ull << __bit), __bit = lsb(__w), __i = __wi + __bit)

static inline u64 *bitmap_base(bitmap b)
{
    return buffer_ref(b->alloc_map, 0);
}

/* no-op if i is within existing bounds, returns true if extended */
static inline boolean bitmap_extend(bitmap b, u64 i)
{
    if (i >= b->mapbits) {
	b->mapbits = pad(i + 1, ALLOC_EXTEND_BITS);
	extend_total(b->alloc_map, b->mapbits >> 3);
	return true;
    }
    return false;
}

static inline boolean bitmap_get(bitmap b, u64 i)
{
    if (i >= b->mapbits)
	return false;
    return (bitmap_base(b)[i >> 6] & (1 << (i & 63))) != 0;
}

static inline void bitmap_set(bitmap b, u64 i, int val)
{
    if (i >= b->mapbits)
	bitmap_extend(b, i);
    u64 mask = 1ull << (i & 63);
    u64 * p = bitmap_base(b) + (i >> 6);
    if (val)
	*p |= mask;
    else
	*p &= ~mask;
}
