#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

#include <pmm.h>
#include <vmm.h>

int
os_thread_sys_init(void);

void
os_thread_sys_destroy(void);

int
bh_platform_init(void)
{
    return os_thread_sys_init();
}

void
bh_platform_destroy(void)
{
    os_thread_sys_destroy();
}

void *
os_malloc(unsigned size)
{
    return malloc(size);
}

void *
os_realloc(void *ptr, unsigned size)
{
    return realloc(ptr, size);
}

void
os_free(void *ptr)
{
    free(ptr);
}

int
os_dumps_proc_mem_info(char *out, unsigned int size)
{
    (void)out;
    (void)size;
    return -1;
}

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    (void)hint;
    (void)flags;
    (void)file;

    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    void *addr = find_free_virt_pages(num_pages);
    if (!addr)
        return NULL;

    if (prot & (MMAP_PROT_READ | MMAP_PROT_WRITE)) {
        if (os_mprotect(addr, size, prot) != 0)
            return NULL;
    }

    return addr;
}

void *
os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    return os_mremap_slow(old_addr, old_size, new_size);
}

void
os_munmap(void *addr, size_t size)
{
    uintptr_t start = (uintptr_t)addr & ~(PAGE_SIZE - 1);
    uintptr_t end = ((uintptr_t)addr + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
        void *phys = vmm_get_phys((void *)va);
        if (phys) {
            vmm_unmap_page((void *)va);
            pmm_free_page(phys);
        }
    }
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    if (!(prot & (MMAP_PROT_READ | MMAP_PROT_WRITE)))
        return 0;

    uintptr_t start = (uintptr_t)addr & ~(PAGE_SIZE - 1);
    uintptr_t end = ((uintptr_t)addr + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
        if (vmm_get_phys((void *)va) != NULL)
            continue;
        void *phys = pmm_alloc_page();
        if (!phys)
            return -1;
        uint32_t flags = VMM_PRESENT | VMM_WRITE;
        if (!mmap_physical((void *)va, phys, PAGE_SIZE, flags))
            return -1;
        memset((void *)va, 0, PAGE_SIZE);
    }
    return 0;
}

void
os_dcache_flush(void)
{}

void
os_icache_flush(void *start, size_t len)
{
    (void)start;
    (void)len;
}

os_raw_file_handle
os_invalid_raw_handle(void)
{
    return -1;
}

void
wasm_trap_delete(void *trap)
{
    (void)trap;
}

/* Stub for WASM_ENABLE_DUMP_CALL_STACK — the real implementation lives in
   wasm_c_api.c which we don't compile.  Only called from thread_manager
   when propagating exceptions across threads; harmless as a no-op here. */
void
wasm_frame_vec_clone_internal(void *src, void *out)
{
    (void)src;
    (void)out;
}
