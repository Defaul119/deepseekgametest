// Minimal self-contained test harness for the engine's math and world code.
// No external framework is required: tests register themselves and the runner
// reports each result, returning non-zero on failure for CTest.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/doors.h"
#include "math/fixed.h"
#include "math/trig.h"
#include "math/vec2.h"
#include "render/renderer.h"
#include "render/texture.h"
#include "render/visplane.h"
#include "world/bsp.h"
#include "world/mapfile.h"
#include "world/world.h"

#ifndef RF_SOURCE_DIR
#define RF_SOURCE_DIR "."
#endif

using namespace rf;

namespace {

int g_checks = 0;
int g_failures = 0;

struct TestFailure {};

void reportFailure(const char* expr, const char* file, int line) {
    ++g_failures;
    std::printf("    FAIL %s:%d  %s\n", file, line, expr);
}

#define CHECK(cond)                                             \
    do {                                                        \
        ++g_checks;                                             \
        if (!(cond)) reportFailure(#cond, __FILE__, __LINE__);  \
    } while (0)

#define REQUIRE(cond)                                                    \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) {                                                   \
            reportFailure(#cond, __FILE__, __LINE__);                    \
            throw TestFailure{};                                         \
        }                                                                \
    } while (0)

using TestFn = void (*)();

struct TestCase {
    const char* name;
    TestFn fn;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, TestFn fn) { registry().push_back({name, fn}); }
};

#define TEST(name)                                           \
    static void name();                                      \
    static Registrar registrar_##name(#name, name);          \
    static void name()

fixed_t units(double v) { return fromFloat(static_cast<float>(v)); }

bool nearFixed(fixed_t actual, fixed_t expected, fixed_t tolerance) {
    const int64_t diff = static_cast<int64_t>(actual) - static_cast<int64_t>(expected);
    return diff >= -tolerance && diff <= tolerance;
}

bool nearFloat(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

std::string sampleMapPath() { return std::string(RF_SOURCE_DIR) + "/assets/sample.rmap"; }

// Builds an L-shaped room with one sector and six one-sided walls. Its
// concavity forces partition lines to split wall segments.
World makeLShapedWorld() {
    World w;
    Sector s;
    s.floorHeight = 0;
    s.ceilHeight = fromInt(128);
    s.floorTex = 0;
    s.ceilTex = 0;
    w.sectors.push_back(s);

    w.addVertex(0, 0);
    w.addVertex(fromInt(512), 0);
    w.addVertex(fromInt(512), fromInt(256));
    w.addVertex(fromInt(256), fromInt(256));
    w.addVertex(fromInt(256), fromInt(512));
    w.addVertex(0, fromInt(512));

    for (int i = 0; i < 6; ++i) {
        Line l;
        l.v1 = i;
        l.v2 = (i + 1) % 6;
        l.frontSector = 0;
        l.backSector = -1;
        l.texMiddle = 0;
        w.lines.push_back(l);
    }
    w.computeBounds();
    return w;
}

World makeSampleWorld(const TextureSet& textures, Bsp* bspOut, std::string* error) {
    World world;
    if (!loadMap(sampleMapPath(), textures, world, error)) return {};
    if (bspOut && !bspOut->build(world, error)) return {};
    return world;
}

// ---------------------------------------------------------------- fixed point

TEST(fixed_conversions) {
    CHECK(fromInt(5) == 5 * FRACUNIT);
    CHECK(fromFloat(1.25f) == 81920);
    CHECK(fromFloat(-1.25f) == -81920);
    CHECK(toInt(fromInt(5)) == 5);
    CHECK(toIntRound(fromFloat(2.49f)) == 2);
    CHECK(toIntRound(fromFloat(2.5f)) == 3);
    CHECK(toIntRound(fromFloat(-2.5f)) == -3);
    CHECK(toInt(fromFloat(-2.9f)) == -2);  // truncation toward zero
    CHECK(toFloat(fromInt(5)) == 5.0f);
    CHECK(fromIntSat(40000) == FIXED_MAX);
    CHECK(fromIntSat(-40000) == FIXED_MIN);
    CHECK(fromIntSat(3) == fromInt(3));
}

TEST(fixed_signed_mul_div) {
    CHECK(mul(fromFloat(1.5f), fromFloat(2.25f)) == fromFloat(3.375f));
    CHECK(mul(fromFloat(-2.5f), fromFloat(1.5f)) == fromFloat(-3.75f));
    CHECK(mul(fromFloat(0.5f), fromFloat(-0.25f)) == fromFloat(-0.125f));
    CHECK(mul(fromInt(123), FRACUNIT) == fromInt(123));
    CHECK(mul(fromInt(0), fromInt(99)) == 0);

    CHECK(rf::div(fromInt(7), fromInt(3)) == 152917);  // 458752 / 3, truncated
    CHECK(rf::div(-fromInt(7), fromInt(3)) == -152917);
    CHECK(rf::div(fromInt(7), -fromInt(3)) == -152917);
    CHECK(rf::div(fromInt(1), fromFloat(0.5f)) == fromInt(2));
    CHECK(rf::div(fromInt(-1), fromFloat(0.5f)) == fromInt(-2));
    CHECK(rf::div(fromInt(7), 0) == FIXED_MAX);
    CHECK(rf::div(-fromInt(7), 0) == FIXED_MIN);
    CHECK(rf::div(0, 0) == 0);

    CHECK(mulDiv(fromInt(3), fromInt(4), fromInt(2)) == fromInt(6));
    CHECK(mulDiv(fromInt(1), fromInt(1), 0) == FIXED_MAX);
}

TEST(fixed_precision_and_overflow) {
    // One FRACUNIT step survives repeated multiplication by 1.0.
    CHECK(mul(fromInt(12345) + 1, FRACUNIT) == fromInt(12345) + 1);

    CHECK(mul(FIXED_MAX, fromInt(2)) == FIXED_MAX);
    CHECK(mul(FIXED_MAX, fromInt(-2)) == FIXED_MIN);
    CHECK(rf::div(FIXED_MAX, fromFloat(0.5f)) == FIXED_MAX);
    CHECK(rf::div(FIXED_MIN, fromFloat(0.5f)) == FIXED_MIN);
    CHECK(absv(FIXED_MIN) == FIXED_MAX);
    CHECK(absv(fromInt(-7)) == fromInt(7));
    CHECK(clampv(fromInt(50), fromInt(-10), fromInt(10)) == fromInt(10));
    CHECK(clampv(fromInt(-50), fromInt(-10), fromInt(10)) == fromInt(-10));
    CHECK(clampv(fromInt(4), fromInt(-10), fromInt(10)) == fromInt(4));

    // sqrt uses integer Newton iterations: exact for perfect squares.
    CHECK(sqrtFixed(0) == 0);
    CHECK(sqrtFixed(-10) == 0);
    CHECK(sqrtFixed(fromInt(9)) == fromInt(3));
    CHECK(sqrtFixed(fromInt(144)) == fromInt(12));
    CHECK(nearFixed(sqrtFixed(fromInt(2)), 92682, 2));
    CHECK(nearFixed(sqrtFixed(fromInt(10000)), 6553600, 4));
}

TEST(trig_accuracy_and_wrapping) {
    CHECK(sinTableAt(0) == 0);
    CHECK(sinTableAt(ANG90) == FRACUNIT);
    CHECK(cosTableAt(0) == FRACUNIT);
    CHECK(cosTableAt(ANG90) == 0);
    CHECK(nearFixed(sinTableAt(angleFromDegrees(30.0f)), fromFloat(0.5f), 64));
    CHECK(nearFixed(sinTableAt(ANG45), fromFloat(0.70710678f), 100));

    CHECK(angleFromDegrees(450.0f) == ANG90);
    CHECK(nearFloat(angleToDegrees(angleFromDegrees(-90.0f)), 270.0f, 0.001f));
    CHECK(nearFloat(angleToDegrees(atan2Fixed(fromInt(1), fromInt(1))), 45.0f, 0.05f));
    CHECK(nearFloat(angleToDegrees(atan2Fixed(fromInt(1), -fromInt(1))), 135.0f, 0.05f));
    CHECK(nearFloat(angleToDegrees(atan2Fixed(-fromInt(1), -fromInt(1))), 225.0f, 0.05f));
    CHECK(nearFloat(angleToDegrees(atan2Fixed(-fromInt(1), fromInt(1))), 315.0f, 0.05f));
    CHECK(nearFloat(angleToDegrees(atan2Fixed(fromInt(1), 0)), 90.0f, 0.05f));
    CHECK(nearFloat(angleToDegrees(atan2Fixed(-fromInt(1), 0)), 270.0f, 0.05f));
    CHECK(atan2Fixed(0, 0) == 0);

    const Vec2 dir0 = angleVector(0);
    const Vec2 right0 = rightVector(0);
    CHECK(dir0.x == FRACUNIT && dir0.y == 0);
    CHECK(right0.x == 0 && right0.y == FRACUNIT);
    CHECK(dot(dir0, right0) == 0);
    const Vec2 dir90 = angleVector(ANG90);
    const Vec2 right90 = rightVector(ANG90);
    CHECK(nearFixed(dir90.x, 0, 1));
    CHECK(nearFixed(dir90.y, FRACUNIT, 1));
    CHECK(nearFixed(right90.x, -FRACUNIT, 1));
    CHECK(nearFixed(right90.y, 0, 1));
}

// ------------------------------------------------------------------------ BSP

TEST(bsp_point_side_and_intersection) {
    const Vec2 origin{0, 0};
    const Vec2 east{fromInt(1), 0};
    CHECK(Bsp::pointSide(Vec2{0, fromInt(1)}, origin, east) == 0);   // left
    CHECK(Bsp::pointSide(Vec2{0, -fromInt(1)}, origin, east) == 1);  // right
    CHECK(Bsp::pointSide(Vec2{0, 0}, origin, east) == 0);            // on line

    const Vec2 a{0, 0};
    const Vec2 b{fromInt(4), 0};
    const Vec2 verticalOrigin{fromInt(2), 0};
    const Vec2 north{0, fromInt(1)};
    fixed_t t = -1;
    REQUIRE(Bsp::intersectionParameter(a, b, verticalOrigin, north, &t));
    CHECK(t == FRACUNIT / 2);

    fixed_t endpoint = -1;
    REQUIRE(Bsp::intersectionParameter(a, Vec2{fromInt(2), 0}, verticalOrigin, north, &endpoint));
    CHECK(endpoint == FRACUNIT);

    fixed_t parallel = -1;
    CHECK(!Bsp::intersectionParameter(a, b, origin, east, &parallel));

    fixed_t diagonal = -1;
    CHECK(Bsp::intersectionParameter(a, Vec2{fromInt(4), fromInt(4)}, Vec2{0, fromInt(2)}, north,
                                     &diagonal));
    CHECK(diagonal == 0);  // segment starts on the partition
}

TEST(bsp_l_shape_construction_splitting) {
    World world = makeLShapedWorld();
    Bsp bsp;
    std::string error;
    REQUIRE(bsp.build(world, &error));
    CHECK(!bsp.empty());
    CHECK(bsp.root() >= 0 || bsp.subsectors().size() == 1);
    CHECK(bsp.subsectors().size() > 1);
    CHECK(bsp.numSplits() > 0);  // the concave L forces at least one split
    CHECK(bsp.badLeaves() == 0);
    CHECK(bsp.segs().size() == 6 + static_cast<size_t>(bsp.numSplits()));

    for (const Subsector& sub : bsp.subsectors()) {
        CHECK(sub.numSegs > 0);
        CHECK(sub.sector == 0);
        CHECK(sub.polygon.size() >= 3);
    }
    CHECK(bsp.sectorAtPoint(Vec2{fromInt(64), fromInt(64)}) == 0);
    CHECK(bsp.sectorAtPoint(Vec2{fromInt(128), fromInt(384)}) == 0);
    CHECK(bsp.sectorAtPoint(Vec2{fromInt(384), fromInt(384)}) == -1);  // notch
    CHECK(bsp.sectorAtPoint(Vec2{fromInt(-64), fromInt(-64)}) == -1);  // outside
}

TEST(bsp_traversal_orders_are_reverses) {
    World world = makeLShapedWorld();
    Bsp bsp;
    REQUIRE(bsp.build(world));
    const Vec2 camera{fromInt(64), fromInt(64)};

    std::vector<int> front;
    std::vector<int> back;
    bsp.traverse(camera, true, [&](int leaf) { front.push_back(leaf); });
    bsp.traverse(camera, false, [&](int leaf) { back.push_back(leaf); });

    REQUIRE(front.size() == bsp.subsectors().size());
    REQUIRE(back.size() == front.size());
    for (size_t i = 0; i < front.size(); ++i) {
        CHECK(front[i] == back[front.size() - 1 - i]);
    }

    std::vector<int> visits(bsp.subsectors().size(), 0);
    for (int leaf : front) {
        REQUIRE(leaf >= 0);
        REQUIRE(leaf < static_cast<int>(visits.size()));
        ++visits[static_cast<size_t>(leaf)];
    }
    for (int v : visits) CHECK(v == 1);
}

TEST(bsp_degenerate_inputs) {
    {
        Bsp bsp;
        World empty;
        std::string error;
        CHECK(!bsp.build(empty, &error));
        CHECK(!error.empty());
    }
    {
        World w;
        Sector s;
        s.ceilHeight = fromInt(64);
        s.floorTex = 0;
        s.ceilTex = 0;
        w.addSector(s);
        w.addVertex(0, 0);
        w.addVertex(fromInt(64), 0);
        Line l;
        l.v1 = 0;
        l.v2 = 1;
        l.frontSector = 0;
        l.texMiddle = 0;
        w.addLine(l);
        Bsp bsp;
        REQUIRE(bsp.build(w));
        CHECK(bsp.subsectors().size() == 1);
        int visits = 0;
        bsp.traverse(Vec2{0, 0}, true, [&](int leaf) {
            CHECK(leaf == 0);
            ++visits;
        });
        CHECK(visits == 1);
        CHECK(bsp.subsectorAtPoint(Vec2{fromInt(1), 0}) == 0 ||
              bsp.subsectorAtPoint(Vec2{fromInt(1), 0}) == -1);
    }
    {
        World w;
        Sector s;
        s.ceilHeight = fromInt(64);
        s.floorTex = 0;
        s.ceilTex = 0;
        w.addSector(s);
        w.addVertex(0, 0);
        w.addVertex(0, 0);
        Line l;
        l.v1 = 0;
        l.v2 = 1;
        l.frontSector = 0;
        l.texMiddle = 0;
        w.addLine(l);
        Bsp bsp;
        CHECK(!bsp.build(w));  // zero-length line
    }
}

// ---------------------------------------------------------------- map loading

TEST(sample_map_loads_and_builds) {
    TextureSet textures;
    Bsp bsp;
    std::string error;
    World world = makeSampleWorld(textures, &bsp, &error);
    if (world.sectors.empty()) {
        std::printf("    map/BSP error: %s\n", error.c_str());
        REQUIRE(false);
    }
    CHECK(world.sectors.size() == 9);
    CHECK(world.lines.size() == 122);
    CHECK(world.vertices.size() == 112);
    CHECK(world.things.size() == 9);
    CHECK(world.minX == fromInt(64));
    CHECK(world.maxX == fromInt(1024));
    CHECK(world.minY == fromInt(64));
    CHECK(world.maxY == fromInt(768));

    CHECK(!bsp.empty());
    CHECK(bsp.badLeaves() == 0);
    CHECK(bsp.subsectors().size() >= 9);
    CHECK(bsp.segs().size() >= world.lines.size());

    int sided = 0;
    for (const Line& l : world.lines) {
        if (l.backSector >= 0) ++sided;
        CHECK(l.frontSector >= 0 && l.frontSector < static_cast<int>(world.sectors.size()));
        CHECK(l.frontSector != l.backSector);
        CHECK(world.lineLength(static_cast<int>(&l - world.lines.data())) > 0);
    }
    CHECK(sided > 0);

    // Each leaf must belong to exactly one valid sector.
    for (const Subsector& sub : bsp.subsectors()) {
        if (sub.sector >= 0) {
            CHECK(sub.sector < static_cast<int>(world.sectors.size()));
            CHECK(sub.polygon.size() >= 3);
        }
    }
}

TEST(sample_map_sector_location) {
    TextureSet textures;
    std::string error;
    World world;
    REQUIRE(loadMap(sampleMapPath(), textures, world, &error));
    Bsp bsp;
    REQUIRE(bsp.build(world, &error));

    struct PointSector {
        double x;
        double y;
        int sector;
    };
    const PointSector points[] = {
        {160, 288, 0},  // room A, player start
        {600, 160, 1},  // room B
        {900, 300, 2},  // room C
        {700, 700, 3},  // corridor F
        {416, 288, 4},  // door D
        {800, 288, 5},  // door E
        {352, 608, 6},  // door H
        {544, 608, 7},  // door G
        {864, 608, 8},  // door I
    };
    for (const PointSector& p : points) {
        CHECK(bsp.sectorAtPoint(Vec2{units(p.x), units(p.y)}) == p.sector);
    }

    // Solid pillars and outside the map must not resolve to a sector.
    CHECK(bsp.sectorAtPoint(Vec2{units(224), units(224)}) == -1);
    CHECK(bsp.sectorAtPoint(Vec2{units(608), units(416)}) == -1);
    CHECK(bsp.sectorAtPoint(Vec2{units(928), units(352)}) == -1);
    CHECK(bsp.sectorAtPoint(Vec2{units(32), units(32)}) == -1);

    // Every thing stands in a valid sector.
    for (const Thing& t : world.things) {
        CHECK(bsp.sectorAtPoint(Vec2{t.x, t.y}) >= 0);
    }
}

TEST(sample_map_bsp_traversal_complete) {
    TextureSet textures;
    std::string error;
    World world;
    REQUIRE(loadMap(sampleMapPath(), textures, world, &error));
    Bsp bsp;
    REQUIRE(bsp.build(world, &error));

    // Thing 0 is the player start.
    REQUIRE(!world.things.empty());
    const Thing& start = world.things[0];
    REQUIRE(bsp.sectorAtPoint(Vec2{start.x, start.y}) == 0);

    std::vector<int> front;
    bsp.traverse(Vec2{start.x, start.y}, true, [&](int leaf) { front.push_back(leaf); });
    REQUIRE(front.size() == bsp.subsectors().size());
    std::vector<int> visits(bsp.subsectors().size(), 0);
    for (int leaf : front) {
        REQUIRE(leaf >= 0);
        REQUIRE(leaf < static_cast<int>(visits.size()));
        ++visits[static_cast<size_t>(leaf)];
    }
    for (int v : visits) CHECK(v == 1);

    std::vector<int> back;
    bsp.traverse(Vec2{start.x, start.y}, false, [&](int leaf) { back.push_back(leaf); });
    REQUIRE(back.size() == front.size());
    for (size_t i = 0; i < front.size(); ++i) {
        CHECK(front[i] == back[front.size() - 1 - i]);
    }

    // An outside camera must still traverse the whole tree safely.
    std::vector<int> outside;
    bsp.traverse(Vec2{-fromInt(1000), -fromInt(1000)}, true,
                 [&](int leaf) { outside.push_back(leaf); });
    CHECK(outside.size() == bsp.subsectors().size());
}

TEST(sample_map_round_trip) {
    TextureSet textures;
    std::string error;
    World original;
    REQUIRE(loadMap(sampleMapPath(), textures, original, &error));

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "retrofps_roundtrip_test.rmap";
    REQUIRE(saveMap(tmp.string(), original, textures, &error));
    World loaded;
    const bool ok = loadMap(tmp.string(), textures, loaded, &error);
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    REQUIRE(ok);

    REQUIRE(loaded.sectors.size() == original.sectors.size());
    REQUIRE(loaded.vertices.size() == original.vertices.size());
    REQUIRE(loaded.lines.size() == original.lines.size());
    REQUIRE(loaded.things.size() == original.things.size());

    for (size_t i = 0; i < original.sectors.size(); ++i) {
        const Sector& a = original.sectors[i];
        const Sector& b = loaded.sectors[i];
        CHECK(a.floorHeight == b.floorHeight);
        CHECK(a.ceilHeight == b.ceilHeight);
        CHECK(a.floorTex == b.floorTex);
        CHECK(a.ceilTex == b.ceilTex);
        CHECK(a.light == b.light);
        CHECK(a.tag == b.tag);
        CHECK(a.special == b.special);
        CHECK(a.closedCeil == b.closedCeil);
        CHECK(a.openCeil == b.openCeil);
    }
    for (size_t i = 0; i < original.vertices.size(); ++i) {
        CHECK(original.vertices[i].x == loaded.vertices[i].x);
        CHECK(original.vertices[i].y == loaded.vertices[i].y);
    }
    for (size_t i = 0; i < original.lines.size(); ++i) {
        const Line& a = original.lines[i];
        const Line& b = loaded.lines[i];
        CHECK(a.v1 == b.v1);
        CHECK(a.v2 == b.v2);
        CHECK(a.frontSector == b.frontSector);
        CHECK(a.backSector == b.backSector);
        CHECK(a.texMiddle == b.texMiddle);
        CHECK(a.texUpper == b.texUpper);
        CHECK(a.texLower == b.texLower);
        CHECK(a.flags == b.flags);
        CHECK(a.special == b.special);
        CHECK(a.tag == b.tag);
    }
    for (size_t i = 0; i < original.things.size(); ++i) {
        const Thing& a = original.things[i];
        const Thing& b = loaded.things[i];
        CHECK(a.x == b.x);
        CHECK(a.y == b.y);
        CHECK(a.type == b.type);
        CHECK(std::abs(angleDelta(a.angle, b.angle)) < 2000);  // < 0.0002 degree
    }
}

TEST(map_loader_rejects_malformed_input) {
    TextureSet textures;
    World world;
    std::string error;

    CHECK(!loadMapText("sector 0 0 128 FLOOR CEIL 160 0 0\n", textures, world, &error));
    CHECK(!error.empty());

    error.clear();
    const std::string missingTexture =
        "version 1\n"
        "sector 0 0 128 NOPE CEIL 160 0 0\n"
        "vertex 0 0 0\n"
        "vertex 1 64 0\n"
        "linedef 0 0 1 0 -1 NOPE - - 0 0 0\n";
    CHECK(!loadMapText(missingTexture, textures, world, &error));
    CHECK(error.find("unknown texture") != std::string::npos);

    error.clear();
    const std::string badIndices =
        "version 1\n"
        "sector 0 0 128 FLOOR CEIL 160 0 0\n"
        "vertex 0 0 0\n"
        "vertex 1 64 0\n"
        "linedef 0 0 9 0 -1 BRICK - - 0 0 0\n";
    CHECK(!loadMapText(badIndices, textures, world, &error));
    CHECK(!error.empty());

    error.clear();
    const std::string gap =
        "version 1\n"
        "sector 0 0 128 FLOOR CEIL 160 0 0\n"
        "sector 2 0 128 FLOOR CEIL 160 0 0\n"
        "vertex 0 0 0\n"
        "vertex 1 64 0\n"
        "linedef 0 0 1 0 -1 BRICK - - 0 0 0\n";
    CHECK(!loadMapText(gap, textures, world, &error));
    CHECK(error.find("non-contiguous") != std::string::npos);

    error.clear();
    CHECK(!loadMapText("version 1\nsector 0 0 128 FLOOR CEIL 160 0 0\n"
                       "vertex 0 0 0\nvertex 1 64 0\n"
                       "linedef 0 0 1 0 -1 BRICK - - 0 0\n",
                       textures, world, &error));
    CHECK(!error.empty());
}

// ----------------------------------------------------------------- visplanes

TEST(visplane_span_merge_and_columns) {
    VisplaneManager vm;
    vm.beginFrame(32, 24);
    CHECK(vm.planeCount() == 0);
    CHECK(vm.addSpan(0, 0, 160, false, 4, 10, 15) == 0);
    CHECK(vm.planeCount() == 1);
    CHECK(vm.planes()[0].top[4] == 10);
    CHECK(vm.planes()[0].bottom[4] == 15);
    CHECK(vm.planes()[0].minx == 4 && vm.planes()[0].maxx == 4);

    // Touching spans in the same column merge (10..15 then 16..20).
    CHECK(vm.addSpan(0, 0, 160, false, 4, 16, 20) == 0);
    CHECK(vm.planeCount() == 1);
    CHECK(vm.planes()[0].top[4] == 10);
    CHECK(vm.planes()[0].bottom[4] == 20);
    CHECK(vm.mergedSpans() == 1);

    // Overlapping spans merge and extend both ends.
    CHECK(vm.addSpan(0, 0, 160, false, 4, 5, 12) == 0);
    CHECK(vm.planes()[0].top[4] == 5);
    CHECK(vm.planes()[0].bottom[4] == 20);

    // An empty column in a matching plane is absorbed and widens its extent.
    CHECK(vm.addSpan(0, 0, 160, false, 9, 2, 3) == 0);
    CHECK(vm.planeCount() == 1);
    CHECK(vm.planes()[0].top[9] == 2);
    CHECK(vm.planes()[0].minx == 4 && vm.planes()[0].maxx == 9);
    CHECK(vm.columnsAdded() == 4);
}

TEST(visplane_disjoint_spans_split) {
    VisplaneManager vm;
    vm.beginFrame(32, 24);
    CHECK(vm.addSpan(0, 0, 160, false, 3, 4, 6) == 0);
    // Same key, same column, disjoint interval: a second visplane is required.
    CHECK(vm.addSpan(0, 0, 160, false, 3, 10, 12) == 1);
    CHECK(vm.planeCount() == 2);
    CHECK(vm.splitCount() == 1);
    CHECK(vm.planes()[0].top[3] == 4 && vm.planes()[0].bottom[3] == 6);
    CHECK(vm.planes()[1].top[3] == 10 && vm.planes()[1].bottom[3] == 12);

    // A span touching the first interval (6+1 == 7) must join plane 0, not the
    // disjoint second one.
    CHECK(vm.addSpan(0, 0, 160, false, 3, 7, 9) == 0);
    CHECK(vm.planes()[0].bottom[3] == 9);
    CHECK(vm.planes()[1].top[3] == 10);

    // A column empty in both planes is claimed by the first match.
    CHECK(vm.addSpan(0, 0, 160, false, 5, 1, 2) == 0);
    CHECK(vm.planes()[1].top[5] == -1);
}

TEST(visplane_plane_keys_separate) {
    VisplaneManager vm;
    vm.beginFrame(16, 16);
    CHECK(vm.addSpan(0, 0, 160, false, 2, 5, 10) == 0);
    CHECK(vm.addSpan(8, 0, 160, false, 2, 5, 10) == 1);   // height differs
    CHECK(vm.addSpan(0, 1, 160, false, 2, 5, 10) == 2);   // texture differs
    CHECK(vm.addSpan(0, 0, 100, false, 2, 5, 10) == 3);   // light differs
    CHECK(vm.addSpan(0, 0, 160, true, 2, 5, 10) == 4);    // ceiling differs
    CHECK(vm.planeCount() == 5);
    // Distinct keys are separate planes, not visplane splits.
    CHECK(vm.splitCount() == 0);
}

TEST(visplane_span_boundaries) {
    VisplaneManager vm;
    vm.beginFrame(8, 8);
    CHECK(vm.addSpan(0, 0, 160, false, -1, 0, 3) == -1);  // column out of range
    CHECK(vm.addSpan(0, 0, 160, false, 8, 0, 3) == -1);
    CHECK(vm.addSpan(0, 0, 160, false, 3, 5, 2) == -1);   // empty span
    CHECK(vm.planeCount() == 0);

    // Spans are clamped to the framebuffer instead of being rejected.
    CHECK(vm.addSpan(0, 0, 160, false, 3, -5, 12) == 0);
    CHECK(vm.planes()[0].top[3] == 0);
    CHECK(vm.planes()[0].bottom[3] == 7);
    CHECK(vm.planes()[0].top[0] == -1);  // untouched column

    // Column 0 and the rightmost column are both legal.
    CHECK(vm.addSpan(0, 0, 160, false, 0, 0, 0) >= 0);
    CHECK(vm.addSpan(0, 0, 160, false, 7, 2, 2) >= 0);
}

TEST(visplane_split_renders_both_regions) {
    TextureSet textures;
    Framebuffer fb(8, 8);
    fb.clear(0);
    std::vector<fixed_t> depth(8 * 8, 0);
    Stats stats;

    VisplaneManager vm;
    vm.beginFrame(8, 8);
    // Same key and column but disjoint: the manager must split, and the
    // rasteriser must fill both intervals.
    vm.addSpan(0, 0, 160, false, 2, 4, 4);
    vm.addSpan(0, 0, 160, false, 2, 6, 7);
    CHECK(vm.planeCount() == 2);

    FlatCamera cam;
    cam.pos = Vec2{0, 0};
    cam.forward = Vec2{0, -FRACUNIT};
    cam.right = Vec2{FRACUNIT, 0};
    cam.eyeZ = fromInt(41);
    cam.focal = fromInt(4);
    cam.horizon = fromInt(3);
    cam.width = 8;
    cam.height = 8;
    vm.render(fb, cam, textures, depth, stats);

    CHECK(fb.row(4)[2] != 0);
    CHECK(fb.row(5)[2] == 0);  // the gap between the two spans stays empty
    CHECK(fb.row(6)[2] != 0);
    CHECK(fb.row(7)[2] != 0);
    CHECK(depth[5 * 8 + 2] == 0);
    CHECK(depth[4 * 8 + 2] > 0);
}

TEST(visplane_render_perspective_and_depth) {
    TextureSet textures;
    Framebuffer fb(100, 100);
    fb.clear(0);
    std::vector<fixed_t> depth(100 * 100, 0);
    Stats stats;

    VisplaneManager vm;
    vm.beginFrame(100, 100);
    for (int x = 10; x < 90; ++x) vm.addSpan(0, 0, 200, false, x, 51, 99);

    FlatCamera cam;
    cam.pos = Vec2{0, 0};
    cam.forward = Vec2{0, -FRACUNIT};  // looking toward -y
    cam.right = Vec2{FRACUNIT, 0};
    cam.eyeZ = fromInt(41);
    cam.focal = fromInt(50);  // 90 degree horizontal FOV at this width
    cam.horizon = fromInt(50);
    cam.width = 100;
    cam.height = 100;
    vm.render(fb, cam, textures, depth, stats);

    auto depthAt = [&](int x, int y) {
        return toFloat(depth[static_cast<size_t>(y) * 100 + static_cast<size_t>(x)]);
    };

    // Outside the per-column span nothing is written.
    CHECK(fb.row(40)[50] == 0);
    CHECK(depth[40 * 100 + 50] == 0);
    CHECK(fb.row(75)[5] == 0);
    CHECK(fb.row(75)[95] == 0);

    // Distance follows z = (eyeZ - planeZ) * focal / (y + 0.5 - horizon).
    CHECK(nearFloat(depthAt(50, 76), 41.0f * 50.0f / 26.5f, 0.02f));
    CHECK(nearFloat(depthAt(50, 60), 41.0f * 50.0f / 10.5f, 0.02f));
    CHECK(nearFloat(depthAt(50, 99), 41.0f * 50.0f / 49.5f, 0.02f));
    CHECK(depthAt(50, 99) < depthAt(50, 76));
    CHECK(depthAt(50, 76) < depthAt(50, 60));

    // Perspective-correct mapping: for every pixel the sampled texel is the
    // one under the world point pos + forward*z + right*((x+0.5-cx)*z/focal).
    const Texture& tex = textures.get(0);
    const int shade = shadeFactor(200);
    auto expectedTexel = [&](int x, int y) -> uint32_t {
        const double z = depthAt(x, y);
        const double lateral = ((x + 0.5) - 50.0) * z / 50.0;
        const double wx = lateral;  // right = (1,0)
        const double wy = -z;       // forward = (0,-1)
        const int tx = static_cast<int>(std::floor(wx)) & (tex.w - 1);
        const int ty = static_cast<int>(std::floor(wy)) & (tex.h - 1);
        return shadeRGB(tex.pixels[static_cast<size_t>(ty) * static_cast<size_t>(tex.w) + tx],
                        shade);
    };
    CHECK(fb.row(76)[50] == expectedTexel(50, 76));
    CHECK(fb.row(76)[30] == expectedTexel(30, 76));
    CHECK(fb.row(90)[70] == expectedTexel(70, 90));
    CHECK(fb.row(55)[20] == expectedTexel(20, 55));
}

TEST(visplane_render_depth_rejects_nearer_wall) {
    TextureSet textures;
    Framebuffer fb(40, 40);
    fb.clear(0);
    std::vector<fixed_t> depth(40 * 40, 0);
    Stats stats;

    VisplaneManager vm;
    vm.beginFrame(40, 40);
    for (int x = 0; x < 40; ++x) vm.addSpan(0, 0, 160, false, x, 21, 39);

    // Pretend a wall one map unit away already covered this pixel.
    depth[30 * 40 + 20] = fromInt(1);
    const uint32_t wallColor = colorRGB(200, 30, 30);
    fb.row(30)[20] = wallColor;

    FlatCamera cam;
    cam.pos = Vec2{0, 0};
    cam.forward = Vec2{0, -FRACUNIT};
    cam.right = Vec2{FRACUNIT, 0};
    cam.eyeZ = fromInt(41);
    cam.focal = fromInt(20);
    cam.horizon = fromInt(20);
    cam.width = 40;
    cam.height = 40;
    vm.render(fb, cam, textures, depth, stats);

    CHECK(fb.row(30)[20] == wallColor);        // nearer wall wins
    CHECK(depth[30 * 40 + 20] == fromInt(1));  // depth untouched
    CHECK(fb.row(30)[19] != 0);                // neighbours still rasterised
    CHECK(stats.pixelsDepthRejected == 1);
}

// --------------------------------------------------------------------- doors

TEST(doors_find_activate_animate_and_block) {
    World world;
    Sector roomA;
    roomA.ceilHeight = fromInt(128);
    roomA.floorTex = 0;
    roomA.ceilTex = 0;
    Sector doorSector;
    doorSector.special = 1;
    doorSector.ceilHeight = 0;
    doorSector.closedCeil = 0;
    doorSector.openCeil = fromInt(112);
    Sector roomB = roomA;
    world.addSector(roomA);
    world.addSector(doorSector);
    world.addSector(roomB);

    // A short doorway line at x = 100 between room A and the door sector,
    // plus the door sector's far boundary.
    const int v0 = world.addVertex(fromInt(100), 0);
    const int v1 = world.addVertex(fromInt(100), fromInt(64));
    Line toDoor;
    toDoor.v1 = v0;
    toDoor.v2 = v1;
    toDoor.frontSector = 0;
    toDoor.backSector = 1;
    world.addLine(toDoor);
    Line fromDoor = toDoor;
    fromDoor.frontSector = 1;
    fromDoor.backSector = 2;
    world.addLine(fromDoor);

    DoorSystem doors;
    doors.reset(world);
    REQUIRE(doors.doorCount() == 1);
    CHECK(doors.door(0).sector == 1);
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Closed);
    CHECK(doors.blocked(world, 1));
    CHECK(!doors.blocked(world, 0));  // ordinary sectors are never blocked
    CHECK(!doors.blocked(world, -1));
    CHECK(!doors.blocked(world, 99));

    // Out of reach, and a sector that shares no line with the door at all.
    CHECK(doors.use(world, Vec2{units(0), units(32)}, 0) == -1);
    CHECK(doors.use(world, Vec2{units(80), units(32)}, 7) == -1);
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Closed);

    CHECK(doors.use(world, Vec2{units(80), units(32)}, 0) == 1);
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Opening);
    CHECK(world.sectors[1].ceilHeight == 0);

    auto advance = [&](double seconds, int playerSector) {
        for (double t = 0.0; t < seconds - 1e-9; t += 0.05) doors.update(world, 0.05, playerSector);
    };

    advance(0.5, 0);
    CHECK(nearFixed(world.sectors[1].ceilHeight, fromInt(48), fromInt(2)));
    CHECK(doors.blocked(world, 1));  // 48 < 56: still too low to pass
    advance(0.1, 0);
    CHECK(!doors.blocked(world, 1));  // ~57.6: passable
    advance(0.7, 0);
    CHECK(world.sectors[1].ceilHeight == fromInt(112));
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Open);

    // Use on a fully open door closes it...
    CHECK(doors.use(world, Vec2{units(80), units(32)}, 0) == 1);
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Closing);

    // ...but a player standing in the doorway makes it reopen instead.
    doors.update(world, 0.05, 1);
    CHECK(world.sectors[1].ceilHeight == fromInt(112));
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Opening);
    doors.update(world, 0.05, 1);
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Open);

    // Close it with the player outside.
    CHECK(doors.use(world, Vec2{units(80), units(32)}, 0) == 1);
    advance(1.5, 0);
    CHECK(world.sectors[1].ceilHeight == 0);
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Closed);

    // Reopen and let the automatic hold timer close it again.
    CHECK(doors.use(world, Vec2{units(80), units(32)}, 0) == 1);
    advance(1.2, 0);
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Open);
    advance(4.5, 0);
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Closing);
    advance(1.2, 0);
    CHECK(doors.door(0).phase == DoorRuntime::Phase::Closed);
    CHECK(world.sectors[1].ceilHeight == 0);
}

TEST(doors_open_in_sample_map) {
    TextureSet textures;
    World world;
    std::string error;
    REQUIRE(loadMap(sampleMapPath(), textures, world, &error));
    Bsp bsp;
    REQUIRE(bsp.build(world, &error));

    DoorSystem doors;
    doors.reset(world);
    REQUIRE(doors.doorCount() == 5);  // sectors 4..8 are special 1
    const int doorIndex = doors.indexForSector(4);
    REQUIRE(doorIndex >= 0);

    // Door 4 is the 64-unit-wide doorway east of the start room; the player
    // start faces it.
    const Vec2 pos{units(320), units(288)};
    CHECK(bsp.sectorAtPoint(pos) == 0);
    CHECK(doors.blocked(world, 4));
    CHECK(doors.use(world, pos, 0) == 4);
    for (int i = 0; i < 40; ++i) doors.update(world, 0.05, 0);  // 2 seconds
    CHECK(world.sectors[4].ceilHeight == world.sectors[4].openCeil);
    CHECK(doors.door(doorIndex).phase == DoorRuntime::Phase::Open);
    CHECK(!doors.blocked(world, 4));
}

// ------------------------------------------------------------------ renderer

TEST(renderer_covers_viewport_from_sample_map) {
    TextureSet textures;
    World world;
    Bsp bsp;
    std::string error;
    REQUIRE(loadMap(sampleMapPath(), textures, world, &error));
    REQUIRE(bsp.build(world, &error));

    Renderer renderer;
    Framebuffer fb(320, 200);
    const uint32_t sentinel = colorRGB(255, 0, 255);  // cannot be produced by shading
    struct View {
        double x, y, degrees;
    };
    const View views[] = {
        {160, 288, 0},   {160, 288, 90},  {300, 288, 270}, {150, 500, 90},
        {500, 130, 180}, {900, 140, 0},   {600, 700, 0},   {700, 700, 180},
    };
    int viewsWithPlanes = 0;
    for (const View& v : views) {
        fb.clear(sentinel);
        Stats stats;
        CameraView cam;
        cam.pos = Vec2{units(v.x), units(v.y)};
        cam.yaw = angleFromDegrees(static_cast<float>(v.degrees));
        const int sector = bsp.sectorAtPoint(cam.pos);
        REQUIRE(sector >= 0);
        cam.eyeZ = world.sectors[static_cast<size_t>(sector)].floorHeight + fromInt(41);
        cam.focal = fromInt(fb.width() / 2);
        renderer.render(world, bsp, textures, cam, fb, stats, false);

        int uncovered = 0;
        for (int y = 0; y < fb.height(); ++y) {
            for (int x = 0; x < fb.width(); ++x) {
                if (fb.row(y)[x] == sentinel) ++uncovered;
            }
        }
        // The sample map is fully enclosed: every pixel of the viewport must be
        // filled by a wall or a flat (this catches seam cracks and polygon
        // clip bugs).
        CHECK(uncovered == 0);
        CHECK(stats.pixelsRendered > 0);
        // A close wall can legitimately hide every flat (the room B west view
        // is fully covered by one wall), but most views must use visplanes.
        if (stats.visplanesCreated > 0) ++viewsWithPlanes;
    }
    CHECK(viewsWithPlanes >= 6);
}

}  // namespace

int main() {    rf::initTrigTables();

    int failedTests = 0;
    for (const TestCase& test : registry()) {
        const int failuresBefore = g_failures;
        std::printf("[ RUN  ] %s\n", test.name);
        try {
            test.fn();
        } catch (const TestFailure&) {
            // Failure already reported; continue with the next test.
        } catch (const std::exception& ex) {
            ++g_failures;
            std::printf("    FAIL uncaught exception: %s\n", ex.what());
        }
        if (g_failures == failuresBefore) {
            std::printf("[ PASS ] %s\n", test.name);
        } else {
            ++failedTests;
            std::printf("[ FAIL ] %s\n", test.name);
        }
    }

    std::printf("\n%d checks, %d failure(s), %d/%zu test(s) passed\n", g_checks, g_failures,
                static_cast<int>(registry().size()) - failedTests, registry().size());
    return failedTests == 0 ? 0 : 1;
}
