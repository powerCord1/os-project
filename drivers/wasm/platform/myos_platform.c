#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

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
    void *addr;
    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;

    if (size >= UINT32_MAX)
        return NULL;

    if ((addr = BH_MALLOC((uint32)size)))
        memset(addr, 0, (uint32)size);

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
    (void)size;
    BH_FREE(addr);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    (void)addr;
    (void)size;
    (void)prot;
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
