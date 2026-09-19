/* Wire-protocol test host for the sandbox image: send one fcx_call
 * eval op (CBOR), print the CBOR reply as JSON text.
 *
 * Build:  g++ -O1 -std=c++17 -I$WASMTIME_CAPI/include \
 *             -I<fcad>/src/3rdParty/json/single_include \
 *             -o wiretest wiretest.cpp \
 *             $WASMTIME_CAPI/lib/libwasmtime.so -Wl,-rpath,$WASMTIME_CAPI/lib
 * Usage:  wiretest <image.wasm> <stdlib-dir> <expression> [bindings-json]
 *
 * bindings-json example (typed values per FcxWire.h):
 *   {"a": {"t":"vec","v":[1,2,3]}, "q": {"t":"quantity","v":10,
 *    "u":[1,0,0,0,0,0,0,0]}, "flag": true}
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>
#include <wasm.h>
#include <wasmtime.h>

using nlohmann::json;

static void bail(const char* msg, wasmtime_error_t* err, wasm_trap_t* trap)
{
    wasm_byte_vec_t text;
    text.data = nullptr;
    text.size = 0;
    if (err)
        wasmtime_error_message(err, &text);
    else if (trap)
        wasm_trap_message(trap, &text);
    fprintf(stderr, "FAIL %s: %.*s\n", msg, (int)text.size,
            text.data ? text.data : "");
    exit(1);
}

static wasmtime_func_t getFunc(wasmtime_context_t* ctx,
                               wasmtime_instance_t* inst, const char* name)
{
    wasmtime_extern_t ext;
    if (!wasmtime_instance_export_get(ctx, inst, name, strlen(name), &ext)
            || ext.kind != WASMTIME_EXTERN_FUNC) {
        fprintf(stderr, "FAIL export %s\n", name);
        exit(1);
    }
    return ext.of.func;
}

// The image imports fcx.host_call/host_fetch (ImageBridge.cpp); this
// test host has no bridge, so satisfy them with -1 stubs ("host bridge
// unavailable" in-image), as smokehost.c does.
static wasm_trap_t* stubBridge(void*, wasmtime_caller_t*,
                               const wasmtime_val_t*, size_t,
                               wasmtime_val_t* results, size_t nresults)
{
    if (nresults >= 1) {
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = -1;
    }
    return nullptr;
}

int main(int argc, char** argv)
{
    if (argc < 4 || argc > 8) {
        fprintf(stderr,
                "usage: wiretest <image.wasm> <stdlib-dir> <expr> "
                "[bindings-json [lang [doc obj]]]\n");
        return 2;
    }

    json req;
    req["op"] = "eval";
    req["src"] = argv[3];
    if (argc >= 5 && argv[4][0])
        req["bindings"] = json::parse(argv[4]);
    if (argc >= 6 && argv[5][0])
        req["lang"] = argv[5];
    if (argc >= 8) {
        json ctx;
        ctx["doc"] = argv[6];
        ctx["obj"] = argv[7];
        req["ctx"] = ctx;
    }
    std::vector<uint8_t> reqBytes = json::to_cbor(req);

    wasm_config_t* cfg = wasm_config_new();
    wasmtime_config_wasm_exceptions_set(cfg, true);
    wasm_engine_t* engine = wasm_engine_new_with_config(cfg);
    wasmtime_store_t* store = wasmtime_store_new(engine, nullptr, nullptr);
    wasmtime_context_t* ctx = wasmtime_store_context(store);

    wasi_config_t* wasi = wasi_config_new();
    wasi_config_inherit_stdout(wasi);
    wasi_config_inherit_stderr(wasi);
    if (!wasi_config_preopen_dir(wasi, argv[2], "/Lib", false)) {
        fprintf(stderr, "FAIL preopen %s\n", argv[2]);
        return 1;
    }
    wasmtime_error_t* err = wasmtime_context_set_wasi(ctx, wasi);
    if (err)
        bail("set_wasi", err, nullptr);

    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        fprintf(stderr, "FAIL reading %s\n", argv[1]);
        return 1;
    }

    wasmtime_module_t* module = nullptr;
    err = wasmtime_module_new(engine, bytes.data(), bytes.size(), &module);
    if (err)
        bail("module_new", err, nullptr);

    wasmtime_linker_t* linker = wasmtime_linker_new(engine);
    {
        wasm_functype_t* ft = wasm_functype_new_2_1(
            wasm_valtype_new_i32(), wasm_valtype_new_i32(),
            wasm_valtype_new_i32());
        wasmtime_linker_define_func(linker, "fcx", 3, "host_call", 9, ft,
                                    stubBridge, nullptr, nullptr);
        wasm_functype_delete(ft);
        ft = wasm_functype_new_2_1(wasm_valtype_new_i32(),
                                   wasm_valtype_new_i32(),
                                   wasm_valtype_new_i32());
        wasmtime_linker_define_func(linker, "fcx", 3, "host_fetch", 10, ft,
                                    stubBridge, nullptr, nullptr);
        wasm_functype_delete(ft);
    }
    err = wasmtime_linker_define_wasi(linker);
    if (err)
        bail("define_wasi", err, nullptr);

    wasmtime_instance_t instance;
    wasm_trap_t* trap = nullptr;
    err = wasmtime_linker_instantiate(linker, ctx, module, &instance, &trap);
    if (err || trap)
        bail("instantiate", err, trap);

    wasmtime_extern_t memExt;
    if (!wasmtime_instance_export_get(ctx, &instance, "memory", 6, &memExt)
            || memExt.kind != WASMTIME_EXTERN_MEMORY) {
        fprintf(stderr, "FAIL memory export\n");
        return 1;
    }
    wasmtime_memory_t memory = memExt.of.memory;

    wasmtime_func_t fInitialize = getFunc(ctx, &instance, "_initialize");
    wasmtime_func_t fInit = getFunc(ctx, &instance, "fcx_init");
    wasmtime_func_t fAlloc = getFunc(ctx, &instance, "fcx_alloc");
    wasmtime_func_t fCall = getFunc(ctx, &instance, "fcx_call");

    err = wasmtime_func_call(ctx, &fInitialize, nullptr, 0, nullptr, 0, &trap);
    if (err || trap)
        bail("_initialize", err, trap);

    wasmtime_val_t args[2], results[1];
    err = wasmtime_func_call(ctx, &fInit, nullptr, 0, results, 1, &trap);
    if (err || trap)
        bail("fcx_init", err, trap);
    if (results[0].of.i32 != 0) {
        fprintf(stderr, "FAIL fcx_init -> %d\n", results[0].of.i32);
        return 1;
    }

    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = (int32_t)reqBytes.size();
    err = wasmtime_func_call(ctx, &fAlloc, args, 1, results, 1, &trap);
    if (err || trap)
        bail("fcx_alloc", err, trap);
    uint32_t guestPtr = (uint32_t)results[0].of.i32;

    uint8_t* mem = wasmtime_memory_data(ctx, &memory);
    size_t memSize = wasmtime_memory_data_size(ctx, &memory);
    if (guestPtr + reqBytes.size() > memSize) {
        fprintf(stderr, "FAIL alloc out of range\n");
        return 1;
    }
    memcpy(mem + guestPtr, reqBytes.data(), reqBytes.size());

    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = (int32_t)guestPtr;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = (int32_t)reqBytes.size();
    err = wasmtime_func_call(ctx, &fCall, args, 2, results, 1, &trap);
    if (err || trap)
        bail("fcx_call", err, trap);
    uint32_t reply = (uint32_t)results[0].of.i32;

    mem = wasmtime_memory_data(ctx, &memory);
    memSize = wasmtime_memory_data_size(ctx, &memory);
    if (reply == 0 || reply + 4 > memSize) {
        fprintf(stderr, "FAIL reply pointer\n");
        return 1;
    }
    uint32_t rlen = 0;
    memcpy(&rlen, mem + reply, 4);
    if (reply + 4 + rlen > memSize) {
        fprintf(stderr, "FAIL reply length\n");
        return 1;
    }
    json rj = json::from_cbor(mem + reply + 4, mem + reply + 4 + rlen);
    printf("%s\n", rj.dump().c_str());

    wasmtime_module_delete(module);
    wasmtime_store_delete(store);
    wasm_engine_delete(engine);
    return rj.value("ok", false) ? 0 : 3;
}
