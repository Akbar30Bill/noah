#ifndef NOAH_PAGE_H
#define NOAH_PAGE_H

#include <stdint.h>

#define PTE_P           0x001   // Present
#define PTE_W           0x002   // Writeable
#define PTE_U           0x004   // User
#define PTE_PS          0x080   // Page Size
#define PTE_NX          0x8000000000000000UL // No Execute

typedef enum {
  PAGE_4KB,
  PAGE_2MB,
  PAGE_1GB,
  PAGE_PML4E,
} page_type_t;

#define PAGE_SHIFT(page_type)            (12 + (page_type) * 9)
#define PAGE_SIZE(page_type)             (1ULL << PAGE_SHIFT(page_type))
#define NR_PAGE_ENTRY                    512

static inline int is_page_aligned(void *addr, page_type_t page)
{
  return ((uint64_t)addr & (PAGE_SIZE(page) - 1)) == 0;
}

#endif
