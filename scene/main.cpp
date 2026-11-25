// agentviz-hive: Claude Code sessions as a beehive comb, on the Strata web kit.
//
// The page (web/hive-*.js) owns the data and the layout: it reads the agent
// feed, gives each session a 19-cell patch of the comb and each subagent a
// cell around its lead, and eases them between snapshots. Every frame it packs
// the scene in the agentviz wire format (vendor/agentviz-kit, docs/KIT.md in
// the agentviz repo) and calls agentviz_frame(); this file only draws it:
//   scene floats  comb radius, flower field x/z (comb.h)
//   zones         one per session: its patch, tinted by its repo (comb.h)
//   agents        one bee per session or subagent, and its cell (bee.h, cell.h, props.h)
//   links         scent trails between live sessions in one repo (props.h)
// Every animation is a function of scene time t and the frame.
#define STRATA_KIT_MAIN
#include "strata_kit.h"

#include "cell.h"
#include "props.h"

namespace {

agentviz::Frame frame;
uint32_t combMesh = 0;
int builtRadius = -1;

// A warm fill light over the comb so it reads at the lowest quality tier too.
void addFill(hive::Host& h) {
    StrataPointLight fill{};
    fill.position[1] = 12.0f;
    fill.range = 40.0f;
    fill.color[0] = 1.0f;
    fill.color[1] = 0.85f;
    fill.color[2] = 0.6f;
    fill.intensity = 0.5f;
    h.fill(fill, {0.06f, 0.05f, 0.03f}, 1);
}

void drawAgent(hive::Host& h, const hive::Bee& b, const hive::World& w, float t, hive::Lights& lights) {
    const bool reduced = h.reducedMotion;
    hive::drawCell(h, b, t, reduced, lights);
    if (!(b.lead && b.offline())) {  // an offline session's cell is empty
        const hive::Pose p = hive::beePose(b, w, t, reduced);
        hive::drawBee(h, b, p, reduced ? 0.0f : t);
        hive::drawProps(h, b, p, w, t, reduced);
        hive::drawToolPulse(h, b, p);
    }
    hive::drawSelection(h, b, t, lights);
}

}  // namespace

AGENTVIZ_FRAME_EXPORT(frame)

void strata_kit_configure(strata_kit::Options& o) {
    o.name = "agentviz-hive";
    o.farPlane = 500.0f;
}

void strata_kit_engine_ready(strata_kit::Host& h) {
    hive::setMeadow(h);
    combMesh = 0;  // a restored context starts with no meshes
    builtRadius = -1;
}

void strata_kit_build(strata_kit::Host& h, double seconds) {
    if (!frame.ok) return;
    const float t = static_cast<float>(seconds);
    const hive::World w = hive::worldOf(frame);
    if (w.combRadius > 0 && builtRadius != w.combRadius) {
        hive::buildComb(h, w.combRadius, &combMesh);
        builtRadius = w.combRadius;
    }
    hive::drawFrame(h, w);
    hive::drawFlowers(h, w, t);
    for (const auto& zone : frame.zones) hive::drawPatch(h, zone);
    hive::Lights lights;
    for (const auto& agent : frame.agents) {
        const hive::Bee b = hive::beeOf(agent);
        if (b.appear >= 0.02f) drawAgent(h, b, w, t, lights);
    }
    for (const auto& link : frame.links) hive::drawScentTrail(h, link, t, h.reducedMotion);
    addFill(h);
}
