/* Standalone smoke host for the sandbox image (until the real wasmtime
 * embedding lands in FreeCADApp): instantiate fcx_image.wasm, preopen
 * the CPython stdlib read-only at /Lib, eval one expression.
 *
 * Build:  gcc -O1 -I$WASMTIME_CAPI/include -o smokehost smokehost.c \
 *             $WASMTIME_CAPI/lib/libwasmtime.so -Wl,-rpath,$WASMTIME_CAPI/lib
 * Usage:  smokehost <image.wasm> <stdlib-dir> <expression>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wasm.h>
#include <wasmtime.h>

static void bail(const char *msg, wasmtime_error_t *err, wasm_trap_t *trap)
{
    wasm_byte_vec_t text;
    text.data = NULL;
    text.size = 0;
    if (err)
        wasmtime_error_message(err, &text);
    else if (trap)
        wasm_trap_message(trap, &text);
    fprintf(stderr, "FAIL %s: %.*s\n", msg, (int)text.size,
            text.data ? text.data : "");
    exit(1);
}

static wasm_trap_t *stub_bridge(void *env, wasmtime_caller_t *caller,
                                const wasmtime_val_t *args, size_t nargs,
                                wasmtime_val_t *results, size_t nresults)
{
    (void)env;
    (void)caller;
    (void)args;
    (void)nargs;
    if (nresults >= 1) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = -1;
    }
    return NULL;
}

static wasmtime_func_t get_func(wasmtime_context_t *ctx,
                                wasmtime_instance_t *inst, const char *name)
{
    wasmtime_extern_t ext;
    if (!wasmtime_instance_export_get(ctx, inst, name, strlen(name), &ext)
            || ext.kind != WASMTIME_EXTERN_FUNC) {
        fprintf(stderr, "FAIL export %s\n", name);
        exit(1);
    }
    return ext.of.func;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: host <image.wasm> <stdlib-dir> <expr>\n");
        return 2;
    }
    const char *expr = argv[3];

    wasm_config_t *cfg = wasm_config_new();
    wasmtime_config_wasm_exceptions_set(cfg, true);
    wasm_engine_t *engine = wasm_engine_new_with_config(cfg);
    wasmtime_store_t *store = wasmtime_store_new(engine, NULL, NULL);
    wasmtime_context_t *ctx = wasmtime_store_context(store);

    wasi_config_t *wasi = wasi_config_new();
    wasi_config_inherit_stdout(wasi);
    wasi_config_inherit_stderr(wasi);
    if (!wasi_config_preopen_dir(wasi, argv[2], "/Lib", false)) {
        fprintf(stderr, "FAIL preopen %s\n", argv[2]);
        return 1;
    }
    wasmtime_error_t *err = wasmtime_context_set_wasi(ctx, wasi);
    if (err)
        bail("set_wasi", err, NULL);

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("image");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = malloc(size);
    if (fread(bytes, 1, size, f) != (size_t)size) {
        perror("read");
        return 1;
    }
    fclose(f);

    wasmtime_module_t *module = NULL;
    err = wasmtime_module_new(engine, bytes, size, &module);
    if (err)
        bail("module_new", err, NULL);
    free(bytes);

    wasmtime_linker_t *linker = wasmtime_linker_new(engine);
    err = wasmtime_linker_define_wasi(linker);
    if (err)
        bail("define_wasi", err, NULL);

    /* The image imports fcx.host_call/host_fetch (ImageBridge.cpp).
     * This smoke host has no bridge: satisfy them with stubs returning
     * -1, which the image surfaces as "host bridge unavailable". */
    {
        wasm_functype_t *ft = wasm_functype_new_2_1(
            wasm_valtype_new_i32(), wasm_valtype_new_i32(),
            wasm_valtype_new_i32());
        err = wasmtime_linker_define_func(linker, "fcx", 3, "host_call", 9,
                                          ft, stub_bridge, NULL, NULL);
        if (err)
            bail("define host_call", err, NULL);
        wasm_functype_delete(ft);
        ft = wasm_functype_new_2_1(wasm_valtype_new_i32(),
                                   wasm_valtype_new_i32(),
                                   wasm_valtype_new_i32());
        err = wasmtime_linker_define_func(linker, "fcx", 3, "host_fetch", 10,
                                          ft, stub_bridge, NULL, NULL);
        if (err)
            bail("define host_fetch", err, NULL);
        wasm_functype_delete(ft);
    }

    wasmtime_instance_t instance;
    wasm_trap_t *trap = NULL;
    err = wasmtime_linker_instantiate(linker, ctx, module, &instance, &trap);
    if (err || trap)
        bail("instantiate", err, trap);

    wasmtime_extern_t mem_ext;
    if (!wasmtime_instance_export_get(ctx, &instance, "memory", 6, &mem_ext)
            || mem_ext.kind != WASMTIME_EXTERN_MEMORY) {
        fprintf(stderr, "FAIL memory export\n");
        return 1;
    }
    wasmtime_memory_t memory = mem_ext.of.memory;

    wasmtime_func_t f_initialize = get_func(ctx, &instance, "_initialize");
    wasmtime_func_t f_init = get_func(ctx, &instance, "fcx_init");
    wasmtime_func_t f_alloc = get_func(ctx, &instance, "fcx_alloc");
    wasmtime_func_t f_eval = get_func(ctx, &instance, "fcx_eval");

    err = wasmtime_func_call(ctx, &f_initialize, NULL, 0, NULL, 0, &trap);
    if (err || trap)
        bail("_initialize", err, trap);

    wasmtime_val_t args[2], results[1];
    err = wasmtime_func_call(ctx, &f_init, NULL, 0, results, 1, &trap);
    if (err || trap)
        bail("fcx_init", err, trap);
    if (results[0].of.i32 != 0) {
        fprintf(stderr, "FAIL fcx_init -> %d\n", results[0].of.i32);
        return 1;
    }

    uint32_t n = (uint32_t)strlen(expr);
    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = (int32_t)n;
    err = wasmtime_func_call(ctx, &f_alloc, args, 1, results, 1, &trap);
    if (err || trap)
        bail("fcx_alloc", err, trap);
    uint32_t guest_ptr = (uint32_t)results[0].of.i32;

    uint8_t *mem = wasmtime_memory_data(ctx, &memory);
    size_t mem_size = wasmtime_memory_data_size(ctx, &memory);
    if (guest_ptr + n > mem_size) {
        fprintf(stderr, "FAIL alloc out of range\n");
        return 1;
    }
    memcpy(mem + guest_ptr, expr, n);

    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = (int32_t)guest_ptr;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = (int32_t)n;
    err = wasmtime_func_call(ctx, &f_eval, args, 2, results, 1, &trap);
    if (err || trap)
        bail("fcx_eval", err, trap);
    uint32_t reply = (uint32_t)results[0].of.i32;

    mem = wasmtime_memory_data(ctx, &memory);  /* may have grown */
    mem_size = wasmtime_memory_data_size(ctx, &memory);
    if (reply == 0 || reply + 4 > mem_size) {
        fprintf(stderr, "FAIL reply pointer\n");
        return 1;
    }
    uint32_t rlen;
    memcpy(&rlen, mem + reply, 4);
    if (reply + 4 + rlen > mem_size) {
        fprintf(stderr, "FAIL reply length\n");
        return 1;
    }
    printf("reply: %.*s\n", (int)rlen, mem + reply + 4);

    wasmtime_module_delete(module);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
    return 0;
}
