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
// But that table is the WORST CASE: it gives every child a material of
// its own, so n children means n buckets. A real scene shares: many
// shapes, far fewer distinct materials. DISABLED_BuildStrategy varies
// that ratio, and the answer turns over inside it (2026-09-01):
//
//   children  distinct   as-you-go   no fixup   append+sort
//       2000        64     1.018ms    0.958ms       2.187ms
//       2000       512     3.156ms    2.619ms       2.373ms
//       8000       512     7.083ms    6.450ms      10.534ms
//       8000      4000   100.810ms   86.090ms      11.930ms
//
// Sorted-as-you-go WINS by 2x when materials are few, and loses by 8.5x
// only when nearly every child brings its own. The reason is that the
// flat_map holds one record per DISTINCT material and the carried hint
// makes a repeat of the previous material free, while append-then-sort
// must sort one record per CHILD. So the lookups during a build are
// already minimal -- the hint is the minimization -- and what costs is a
// NEW bucket, which memmoves. The "no fixup" column also settles where
// that cost sits: removing the slice-index repair (a new bucket shifts
// every index already recorded) saves only about 15%, so it is the
// container's memmove that dominates, not the bookkeeping.
//
// Conclusion: KEEP the flat_map. It is the right structure for scenes
// that share materials, which is the ordinary case; the regime where it
// loses is thousands of distinct materials, which is a problem for the
// draw list generally and not just for its container. A swap would also
// have to replace the index addressing the surrounding code needs
// (nth(bucket), it - begin(), slices naming buckets by index), and
// append-then-sort cannot answer the find()/operator[] the build makes
// while it builds. Re-run both legs before revisiting.
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
#include <Gui/Renderer/Renderer.h>
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


// What a stable node serial costs: one memoized lookup behind a mutex,
// paid per CAPTURED NODE per traversal (a light, a clip plane, a
// texture) -- not per shape, and never in a comparison, which reads the
// value straight out of the info.
TEST(RenderCacheMapBench, DISABLED_NodeSerial)
{
    std::vector<int> nodes(4096);
    std::iota(nodes.begin(), nodes.end(), 1);

    // Cold: every address is new, so every call inserts.
    const double coldms = msOf([&] {
        for (int &n : nodes)
            (void)Render::CacheSerial::forNode(&n);
    });

    // Warm: what the steady state actually pays.
    const int reps = 250;
    size_t sink = 0;
    const double warmms = msOf([&] {
        for (int r = 0; r < reps; ++r)
            for (int &n : nodes)
                sink += Render::CacheSerial::forNode(&n);
    });

    std::printf("\nnode serial: cold %.1f ns/call, warm %.1f ns/call\n",
                coldms * 1e6 / double(nodes.size()),
                warmms * 1e6 / double(nodes.size() * size_t(reps)));
    EXPECT_GT(sink, 0u);
}

// Building the draw list: sorted-as-you-go against append-then-sort.
//
// The real build (SoFCRenderCacheP::mergeChildCache) walks children,
// finds or creates each child's bucket, and appends the child's entries
// to it. Two costs ride on keeping the vector sorted THROUGHOUT: the
// insert memmoves every bucket after it, and a new bucket shifts every
// bucket index already recorded in the child slices, which the real code
// repairs by walking them all. Both vanish if the order is imposed once
// at the end.
TEST(RenderCacheMapBench, DISABLED_BuildStrategy)
{
    std::printf("\n%8s %8s  %12s  %12s  %12s\n",
                "children", "distinct", "as-you-go", "no fixup", "append+sort");

    for (auto shape : {std::make_pair(2000, 64), std::make_pair(2000, 512),
                       std::make_pair(8000, 512), std::make_pair(8000, 4000)}) {
        const int children = shape.first;
        const int distinct = shape.second;
        const std::vector<Material> mats = materials(distinct, 999u);
        std::vector<int> pick(size_t(children), 0);
        std::mt19937 rng(7u);
        for (int &p : pick)
            p = int(rng() % unsigned(distinct));

        // (a) what the code does today
        FlatMap flat;
        std::vector<int> slices;
        const double sortedms = msOf([&] {
            auto it = flat.end();
            for (int i = 0; i < children; ++i) {
                const size_t before = flat.size();
                it = flat.insert(it, FlatMap::value_type(mats[size_t(pick[size_t(i)])], {}));
                if (flat.size() != before) {
                    // A new bucket shifts every index already recorded.
                    const int at = int(it - flat.begin());
                    for (int &sl : slices)
                        if (sl >= at)
                            ++sl;
                }
                slices.push_back(int(it - flat.begin()));
            }
        });

        // (a2) the same inserts with the slice repair removed, to
        // separate the container's cost from the bookkeeping's.
        FlatMap flat2;
        const double nofixms = msOf([&] {
            auto it = flat2.end();
            for (int i = 0; i < children; ++i)
                it = flat2.insert(it, FlatMap::value_type(mats[size_t(pick[size_t(i)])], {}));
        });

        // (b) append, then impose the order once
        std::vector<std::pair<Material, VertexCacheArray>> vec;
        std::vector<int> vslices;
        const double appendms = msOf([&] {
            vec.reserve(size_t(children));
            for (int i = 0; i < children; ++i) {
                vec.emplace_back(mats[size_t(pick[size_t(i)])], VertexCacheArray());
                vslices.push_back(int(vec.size()) - 1);
            }
            std::vector<int> order(vec.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                return vec[size_t(a)].first < vec[size_t(b)].first;
            });
            // Equal keys become one bucket; every recorded index is
            // rewritten once, in place of the running repair above.
            std::vector<int> bucketof(vec.size(), 0);
            int buckets = 0;
            for (size_t k = 0; k < order.size(); ++k) {
                if (k && (vec[size_t(order[k - 1])].first < vec[size_t(order[k])].first))
                    ++buckets;
                bucketof[size_t(order[k])] = buckets;
            }
            for (int &sl : vslices)
                sl = bucketof[size_t(sl)];
        });

        std::printf("%8d %8d  %10.3fms  %10.3fms  %10.3fms\n",
                    children, distinct, sortedms, nofixms, appendms);
        EXPECT_EQ(flat2.size(), flat.size());
        EXPECT_GT(slices.size(), 0u);
        EXPECT_EQ(vslices.size(), slices.size());
    }
    std::printf("\n");
}
