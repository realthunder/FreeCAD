// Is flat_map the right container for the render cache's draw list?
//
// SoFCRenderCache::VertexCacheMap is a boost flat_map<Material,
// VertexCacheArray> -- a sorted VECTOR. It decides the order the scene is
// submitted in, and it is rebuilt every time a cache is published, so
// the question is whether its O(n) insert costs more than the locality
// and the index addressing buy back.
//
// The surrounding code does lean on both of those: SoFCRenderCacheP
// addresses buckets by INDEX (vcachemap.nth(bucket), and the child
// slices record `it - vcachemap.begin()`), which a node-based map cannot
// answer at all. So this measures what that choice costs rather than
// proposing a swap, and it prints the crossover so the answer can be
// re-checked when scenes get bigger.
//
// Measured 2026-09-01, conda-relwithdebinfo-801, sizeof(value_type) 600:
//
//      n   flat build   std::map   vec+sort   flat walk   node walk
//      8      0.007ms    0.002ms    0.003ms     0.000ms     0.001ms
//     32      0.032ms    0.010ms    0.018ms     0.001ms     0.003ms
//    128      0.231ms    0.048ms    0.051ms     0.002ms     0.011ms
//    512      1.997ms    0.253ms    0.389ms     0.014ms     0.080ms
//   2048     29.957ms    1.734ms    1.891ms     0.071ms     0.478ms
//   8192    472.051ms    8.819ms    9.037ms     0.625ms     4.574ms
//
// The build is QUADRATIC and says so: 4x the materials costs 16x the
// time, because every insert that is not at the end memmoves 600 bytes
// per bucket after it. The walk is where it is paid back -- 7.3x faster
// than the node map at n=8192, and that walk happens every frame while
// a build happens only when the cache is republished.
//
// So the shape of the answer: flat_map is right up to a few hundred
// materials (a build under a quarter of a millisecond at n=128) and
// wrong in the thousands (half a second at n=8192 is a visible hitch).
// What the table also shows is that the choice is not flat-vs-node at
// all: vec+sort is 52x faster to build than flat_map at n=8192 AND
// walks identically, because it IS the same array. What stops it being
// a drop-in is that SoFCRenderCacheP looks buckets up WHILE it builds
// (vcachemap.find(), vcachemap[material]), which a sort-at-the-end
// cannot answer. Sorting once at the end of a publish would mean
// staging those lookups some other way -- worth doing only if a profile
// on a scene with thousands of distinct materials says the rebuild
// hurts. Re-run this before deciding.
//
// DISABLED by default: it is a measurement, not an assertion, and the
// large-n legs are slow on purpose. Run it with
//
//   ./tests/src/Gui/RenderCacheMapBench_tests_run \
//       --gtest_also_run_disabled_tests

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <numeric>
#include <random>
#include <vector>

#include <FCGlobal.h>

#include <Gui/Inventor/SoFCVertexCache.h>
#include <Gui/Inventor/SoFCRenderCache.h>

namespace
{

using Material = SoFCRenderCache::Material;
using VertexCacheArray = SoFCRenderCache::VertexCacheArray;
using FlatMap = SoFCRenderCache::VertexCacheMap;
using NodeMap = std::map<Material, VertexCacheArray>;

/// n distinct materials, in the order a traversal would hand them over
/// -- which bears no relation to the order they sort in, so every insert
/// but the luckiest lands in the middle of the vector.
std::vector<Material> materials(int n, unsigned seed)
{
    std::vector<Material> out;
    out.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        Material m;
        m.init(nullptr);
        // diffuse is compared well down the Triangle branch, so a
        // comparison walks most of the fields before it decides -- which
        // is what a scene of same-shaded, differently-coloured parts
        // does.
        m.diffuse = uint32_t(i) * 2654435761u;
        m.type = Material::Triangle;
        out.push_back(m);
    }
    std::shuffle(out.begin(), out.end(), std::mt19937(seed));
    return out;
}

double msOf(const std::function<void()> &fn)
{
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

/// Touch every bucket in order, which is what submitting the frame does.
template<class Map>
size_t walk(const Map &map)
{
    size_t acc = 0;
    for (const auto &v : map)
        acc += v.first.diffuse + v.second.size();
    return acc;
}

}  // namespace

TEST(RenderCacheMapBench, DISABLED_Containers)
{
    std::printf("\nsizeof(Material) = %zu, sizeof(value_type) = %zu\n",
                sizeof(Material), sizeof(FlatMap::value_type));
    std::printf("%6s  %12s  %12s  %12s  %12s\n",
                "n", "flat build", "std::map", "vec+sort", "flat walk");

    for (int n : {8, 32, 128, 512, 2048, 8192}) {
        const std::vector<Material> mats = materials(n, 12345u);

        size_t sink = 0;
        FlatMap flat;
        const double flatms = msOf([&] {
            // Exactly how mergeChildCache inserts: a hint carried from
            // the previous insert.
            auto it = flat.end();
            for (const auto &m : mats)
                it = flat.insert(it, FlatMap::value_type(m, {}));
        });

        NodeMap node;
        const double nodems = msOf([&] {
            auto it = node.end();
            for (const auto &m : mats)
                it = node.insert(it, NodeMap::value_type(m, VertexCacheArray()));
        });

        std::vector<std::pair<Material, VertexCacheArray>> vec;
        const double vecms = msOf([&] {
            vec.reserve(mats.size());
            for (const auto &m : mats)
                vec.emplace_back(m, VertexCacheArray());
            std::sort(vec.begin(), vec.end(),
                      [](const std::pair<Material, VertexCacheArray> &a,
                         const std::pair<Material, VertexCacheArray> &b) {
                          return a.first < b.first;
                      });
        });

        // The walk is the other half of the bargain: a flat_map submits
        // the frame straight down one array.
        const double walkflat = msOf([&] {
            for (int r = 0; r < 32; ++r)
                sink += walk(flat);
        });
        const double walknode = msOf([&] {
            for (int r = 0; r < 32; ++r)
                sink += walk(node);
        });

        std::printf("%6d  %10.3fms  %10.3fms  %10.3fms  %8.3fms (node walk %.3fms)\n",
                    n, flatms, nodems, vecms, walkflat, walknode);
        EXPECT_EQ(flat.size(), node.size());
        EXPECT_GT(sink, 0u);
    }
    std::printf("\n");
}
