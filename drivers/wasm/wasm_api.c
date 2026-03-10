#include <stdint.h>
#include <stdio.h>
#include <fs.h>
#include <pit.h>
#include <wasm_api.h>

#include "wasm3.h"
#include "m3_env.h"

m3ApiRawFunction(wasm_api_print)
{
    m3ApiGetArgMem(const char *, ptr)
    m3ApiGetArg(uint32_t, len)
    m3ApiCheckMem(ptr, len);
    for (uint32_t i = 0; i < len; i++)
        putchar(ptr[i]);
    m3ApiSuccess();
}

m3ApiRawFunction(wasm_api_putchar)
{
    m3ApiGetArg(int32_t, c)
    putchar(c);
    m3ApiSuccess();
}

m3ApiRawFunction(wasm_api_get_ticks)
{
    m3ApiReturnType(uint64_t)
    m3ApiReturn(pit_ticks);
}

void wasm_link_api(IM3Module module)
{
    m3_LinkRawFunction(module, "env", "print", "v(*i)", &wasm_api_print);
    m3_LinkRawFunction(module, "env", "putchar", "v(i)", &wasm_api_putchar);
    m3_LinkRawFunction(module, "env", "get_ticks", "I()", &wasm_api_get_ticks);
}
