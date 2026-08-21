#include "pipeline/NodeGraph.h"
#include "pipeline/NodeRegistry.h"
#include "pipeline/ProcessingContract.h"
#include "app/contracts/ProcessingProvenance.h"
#include "pipeline/SidescanRadiometryAlgorithms.h"
#include "pipeline/nodes/correction/TvgNode.h"
#include "pipeline/nodes/correction/ArcNode.h"
#include "pipeline/nodes/correction/SlantRangeNode.h"
#include "pipeline/nodes/enhancement/GainNormalizeNode.h"
#include "pipeline/nodes/enhancement/SidescanEnhancementNode.h"
#include "pipeline/nodes/enhancement/ContrastEnhanceNode.h"
#include "pipeline/nodes/enhancement/HistogramEqNode.h"
#include "core/SidescanPing.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

using dolphin::pipeline::NodeGraph;
using dolphin::pipeline::NodeGroup;
using dolphin::pipeline::NodeRegistry;

namespace {

int gPassed = 0;
int gFailed = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    if (condition) {
        ++gPassed;
    } else {
        ++gFailed;
        std::fprintf(stderr, "FAIL  %s:%d  %s\n", file, line, expression);
    }
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

dolphin::pipeline::NodePtr makeNode(const std::string& type,
                                    const std::string& id)
{
    auto node = NodeRegistry::instance().create(type);
    CHECK(node != nullptr);
    if (node) node->instance_id = id;
    return node;
}

NodeGraph makeStableGraph()
{
    NodeGraph graph;
    auto node = makeNode("tvg", "stable");
    if (node) {
        node->params["spreading"] = 12.0f;
        graph.addNode(std::move(node));
        graph.setNodePosition("stable", 5.0f, 7.0f);
        graph.addGroup(NodeGroup{"stable_group", "Stable", {"stable"}});
    }
    return graph;
}

void expectRejectedWithoutMutation(NodeGraph& graph, const std::string& json)
{
    const std::string before = graph.toJson();
    CHECK(!graph.fromJson(json));
    CHECK(graph.toJson() == before);
}

void testRoundTripPreservesPortsGroupsAndLayout()
{
    NodeGraph graph;
    auto input = makeNode("sss_input", "input");
    auto merge = makeNode("merge", "merge");
    if (!input || !merge) return;

    input->params["channel"] = std::string{"Port"};
    graph.addNode(std::move(input));
    graph.addNode(std::move(merge));
    CHECK(graph.addEdge("input", "merge", 1));
    graph.setNodePosition("input", -14.5f, 20.25f);
    graph.setNodePosition("merge", 120.0f, 45.0f);
    graph.addGroup(NodeGroup{"grp_processing", "Processing", {"input", "merge"}});

    const std::string serialized = graph.toJson();
    CHECK(serialized.find("\"to_port\": 1") != std::string::npos);
    CHECK(serialized.find("\"groups\"") != std::string::npos);

    NodeGraph loaded;
    CHECK(loaded.fromJson(serialized));
    CHECK(loaded.nodes().size() == 2);
    CHECK(loaded.edges().size() == 1);
    if (loaded.edges().size() == 1) {
        CHECK(loaded.edges()[0].from_node == "input");
        CHECK(loaded.edges()[0].to_node == "merge");
        CHECK(loaded.edges()[0].to_port == 1);
    }

    const auto loadedInput = loaded.findNode("input");
    CHECK(loadedInput != nullptr);
    if (loadedInput) {
        const auto channel = loadedInput->params.find("channel");
        CHECK(channel != loadedInput->params.end());
        if (channel != loadedInput->params.end())
            CHECK(std::get<std::string>(channel->second) == "Port");
    }

    CHECK(loaded.hasNodePosition("input"));
    const auto inputPosition = loaded.nodePosition("input");
    CHECK(std::fabs(inputPosition.first - (-14.5f)) < 0.001f);
    CHECK(std::fabs(inputPosition.second - 20.25f) < 0.001f);

    CHECK(loaded.groups().size() == 1);
    if (loaded.groups().size() == 1) {
        CHECK(loaded.groups()[0].id == "grp_processing");
        CHECK(loaded.groups()[0].label == "Processing");
        CHECK(loaded.groups()[0].node_ids.size() == 2);
        if (loaded.groups()[0].node_ids.size() == 2) {
            CHECK(loaded.groups()[0].node_ids[0] == "input");
            CHECK(loaded.groups()[0].node_ids[1] == "merge");
        }
    }

    // Legacy optional collections and edge ports remain readable.
    NodeGraph legacy;
    CHECK(legacy.fromJson(R"json({
        "nodes": [
            {"id": "a", "type": "sss_input"},
            {"id": "b", "type": "merge", "params": {}}
        ],
        "edges": [{"from": "a", "to": "b"}]
    })json"));
    CHECK(legacy.edges().size() == 1);
    if (legacy.edges().size() == 1) CHECK(legacy.edges()[0].to_port == 0);
}

void testUnknownTypeDoesNotMutateExistingGraph()
{
    NodeGraph graph = makeStableGraph();
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "unknown", "type": "not_registered", "params": {}}]
    })json");
}

void testMalformedContentAndDanglingReferencesAreRejected()
{
    NodeGraph graph = makeStableGraph();

    expectRejectedWithoutMutation(graph,
        R"json({"version": 2, "nodes": {}})json");
    expectRejectedWithoutMutation(graph,
        R"json({"version": 2, "nodes": [42]})json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "", "type": "tvg"}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [
            {"id": "dup", "type": "tvg"},
            {"id": "dup", "type": "tvg"}
        ]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "bad_params", "type": "tvg", "params": []}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "bad_params", "type": "tvg",
                   "params": {"spreading": "wide"}}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "bad_params", "type": "tvg",
                   "params": {"unknown_setting": 1}}]
    })json");

    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "a", "type": "sss_input"}],
        "edges": [{"from": "a", "to": "missing"}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [
            {"id": "a", "type": "sss_input"},
            {"id": "b", "type": "merge"}
        ],
        "edges": [{"from": "a", "to": "b", "to_port": 2}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "a", "type": "tvg"}],
        "layout": [{"id": "missing", "x": 1, "y": 2}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "a", "type": "tvg"}],
        "groups": [{"id": "g", "label": "Broken", "node_ids": ["missing"]}]
    })json");
    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{"id": "a", "type": "tvg"}],
        "groups": [
            {"id": "g", "label": "One", "node_ids": ["a"]},
            {"id": "g", "label": "Two", "node_ids": []}
        ]
    })json");
}

void testLegacySlantRangeSoundVelocityIsMigrated()
{
    NodeGraph graph;
    CHECK(graph.fromJson(R"json({
        "version": 2,
        "nodes": [{
            "id": "slant",
            "type": "slant_range",
            "params": {"sound_velocity": 1500}
        }],
        "edges": []
    })json"));

    const std::string migrated = graph.toJson();
    CHECK(migrated.find("sound_velocity") == std::string::npos);

    expectRejectedWithoutMutation(graph, R"json({
        "version": 2,
        "nodes": [{
            "id": "slant",
            "type": "slant_range",
            "params": {"unknown_setting": 1}
        }]
    })json");
}

void testRadiometryNodesAreLineStableAndIdempotent()
{
    using namespace dolphin;
    core::SidescanPing dark, bright;
    dark.channel = bright.channel = core::SidescanChannel::Port;
    dark.slant_range_m = bright.slant_range_m = 10.f;
    dark.samples.resize(8);
    bright.samples.resize(8);
    for (auto& sample : dark.samples) sample.amplitude = 1000;
    for (auto& sample : bright.samples) sample.amplitude = 4000;

    pipeline::ArtifactBuffer input;
    input.emplace_back(dark);
    input.emplace_back(bright);
    pipeline::GainNormalizeNode gain;
    pipeline::NodeParams gain_params;
    gain_params["target_mean"] = 10000.f;
    gain_params["noise_floor"] = 0.f;
    gain_params["edge_skip"] = 0.f;
    const auto once = gain.process(input, gain_params);
    const auto twice = gain.process(once, gain_params);
    const auto& out_dark = std::get<core::SidescanPing>(once[0]);
    const auto& out_bright = std::get<core::SidescanPing>(once[1]);
    CHECK(out_bright.samples[0].amplitude == out_dark.samples[0].amplitude);
    CHECK(out_dark.samples[0].amplitude == 10000);
    CHECK(core::hasCorrectionFlag(out_dark.correction_flags,
                                  core::CorrectionFlag::GainNormalized));
    CHECK(std::get<core::SidescanPing>(twice[0]).samples[0].amplitude
          == out_dark.samples[0].amplitude);

    pipeline::TvgNode tvg;
    pipeline::NodeParams tvg_params;
    tvg_params["spreading"] = 20.f;
    auto tvg_once = tvg.process(input, tvg_params);
    auto tvg_twice = tvg.process(tvg_once, tvg_params);
    CHECK(std::get<core::SidescanPing>(tvg_twice[0]).samples.back().amplitude
          == std::get<core::SidescanPing>(tvg_once[0]).samples.back().amplitude);

    dark.bottom_pick.range_m = 5.f;
    dark.bottom_pick.source = 1;
    for (size_t i = 0; i < dark.samples.size(); ++i)
        dark.samples[i].range_m = 5.f + static_cast<float>(i);
    pipeline::ArtifactBuffer arc_input;
    arc_input.emplace_back(dark);
    pipeline::ArcNode arc;
    const auto arc_once = arc.process(arc_input, {});
    const auto arc_twice = arc.process(arc_once, {});
    const auto& arc_ping = std::get<core::SidescanPing>(arc_once[0]);
    CHECK(arc_ping.samples.back().amplitude > dark.samples.back().amplitude);
    CHECK(core::hasCorrectionFlag(arc_ping.correction_flags,
                                  core::CorrectionFlag::Arc));
    CHECK(std::get<core::SidescanPing>(arc_twice[0]).samples.back().amplitude
          == arc_ping.samples.back().amplitude);

    dark.bottom_pick = {};
    dark.nav.altitude_m = 0.f;
    pipeline::ArtifactBuffer no_arc_geometry{dark};
    const auto unchanged = arc.process(no_arc_geometry, {});
    const auto& unchanged_ping = std::get<core::SidescanPing>(unchanged[0]);
    CHECK(!core::hasCorrectionFlag(unchanged_ping.correction_flags,
                                   core::CorrectionFlag::Arc));
    CHECK(unchanged_ping.samples.back().amplitude == dark.samples.back().amplitude);
}

void testRadiometryNodeAdaptersMatchSharedEngine()
{
    using namespace dolphin;
    core::SidescanPing ping;
    ping.channel = core::SidescanChannel::Starboard;
    ping.timestamp_us = 42;
    ping.ping_number = 7;
    ping.slant_range_m = 12.f;
    ping.blanking_m = 2.f;
    ping.bottom_pick = {5.f, 1};
    for (int i = 0; i < 12; ++i) {
        core::SidescanSample sample;
        sample.amplitude = static_cast<uint16_t>(800 + i * 170);
        sample.range_m = 2.f + static_cast<float>(i);
        ping.samples.push_back(sample);
    }

    const auto checkSame = [](const pipeline::ArtifactBuffer& actual,
                              const std::vector<core::SidescanPing>& expected) {
        CHECK(actual.size() == expected.size());
        const auto& got = std::get<core::SidescanPing>(actual.front());
        CHECK(got.correction_flags == expected.front().correction_flags);
        CHECK(got.samples.size() == expected.front().samples.size());
        for (size_t i = 0; i < got.samples.size(); ++i)
            CHECK(got.samples[i].amplitude == expected.front().samples[i].amplitude);
    };

    pipeline::NodeParams tvg_params{{"spreading", 17.f}, {"absorption", 0.2f},
                                    {"blanking_m", 3.f}};
    pipeline::radiometry::TvgSettings tvg_settings{true, 17.f, 0.2f, 3.f};
    auto expected = std::vector<core::SidescanPing>{ping};
    pipeline::radiometry::applyTvg(expected, tvg_settings);
    checkSame(pipeline::TvgNode{}.process({ping}, tvg_params), expected);

    pipeline::NodeParams arc_params{{"exponent", 2.1f}, {"gain_cap_db", 9.f}};
    pipeline::radiometry::ArcSettings arc_settings{true, 2.1f, 9.f};
    expected = {ping};
    pipeline::radiometry::applyArc(expected, arc_settings);
    checkSame(pipeline::ArcNode{}.process({ping}, arc_params), expected);

    pipeline::NodeParams agc_params{{"target_mean", 12000.f}, {"strength", 0.7f},
        {"noise_floor", 1.f}, {"edge_skip", 2.f}, {"gain_cap_db", 15.f}};
    pipeline::radiometry::AgcSettings agc_settings;
    agc_settings.enabled = true;
    agc_settings.target_mean = 12000.f;
    agc_settings.strength = 0.7f;
    agc_settings.noise_floor_pct = 1.f;
    agc_settings.edge_skip_samples = 2;
    agc_settings.gain_cap_db = 15.f;
    expected = {ping};
    pipeline::radiometry::applyAgc(expected, agc_settings);
    checkSame(pipeline::GainNormalizeNode{}.process({ping}, agc_params), expected);
}

void testCentralProcessingContract()
{
    using namespace dolphin;
    pipeline::TvgNode tvg;
    tvg.params["spreading"] = 100.f;
    core::SidescanPing ping;
    ping.samples.resize(2);
    pipeline::ArtifactBuffer sss{ping};
    CHECK(pipeline::validateProcessingInvocation(tvg, sss).find("outside")
          != std::string::npos);

    tvg.params["spreading"] = 20.f;
    ping.correction_flags |= core::CorrectionFlag::SlantRange;
    sss = pipeline::ArtifactBuffer{ping};
    CHECK(pipeline::validateProcessingInvocation(tvg, sss).find("before")
          != std::string::npos);

    core::SubBottomTrace trace;
    pipeline::ArtifactBuffer sbp{trace};
    CHECK(pipeline::validateProcessingInvocation(tvg, sbp).find("modality")
          != std::string::npos);

    const auto contract = pipeline::processingContractFor("gain_normalize");
    CHECK(contract.mutates_samples);
    CHECK(contract.idempotent);
    CHECK(contract.stage == pipeline::ProcessingStage::Normalization);

    pipeline::SlantRangeNode slant;
    pipeline::ArcNode arc;
    CHECK(pipeline::validateProcessingOrder(slant, arc).find("earlier")
          != std::string::npos);
    CHECK(pipeline::validateProcessingOrder(arc, slant).empty());
    CHECK(pipeline::validateProcessingOrder(arc, tvg).find("before")
          != std::string::npos);
    CHECK(pipeline::validateProcessingOrder(tvg, arc).empty());

    pipeline::SidescanEnhancementNode beam(
        pipeline::SidescanEnhancementKind::BeamPattern);
    pipeline::SidescanEnhancementNode arn(
        pipeline::SidescanEnhancementKind::Arn);
    pipeline::SidescanEnhancementNode destripe(
        pipeline::SidescanEnhancementKind::Destripe);
    core::SidescanPing unprojected;
    unprojected.samples.resize(2);
    CHECK(pipeline::validateProcessingInvocation(
              beam, pipeline::ArtifactBuffer{unprojected}).find("requires")
          != std::string::npos);
    unprojected.correction_flags |= core::CorrectionFlag::SlantRange;
    CHECK(pipeline::validateProcessingInvocation(
              beam, pipeline::ArtifactBuffer{unprojected}).empty());
    CHECK(pipeline::validateProcessingOrder(beam, arn).empty());
    CHECK(pipeline::validateProcessingOrder(arn, destripe).empty());
    CHECK(pipeline::validateProcessingOrder(destripe, beam).find("before")
          != std::string::npos);

    pipeline::ArcNode arc_without_geometry;
    core::SidescanPing no_geometry_ping;
    no_geometry_ping.samples.resize(2);
    pipeline::ArtifactBuffer no_geometry_input{no_geometry_ping};
    CHECK(pipeline::validateProcessingInvocation(arc_without_geometry, no_geometry_input)
          .find("requires") != std::string::npos);

    auto graph = NodeGraph{};
    auto late = std::make_shared<pipeline::SlantRangeNode>();
    late->instance_id = "src";
    auto early = std::make_shared<pipeline::ArcNode>();
    early->instance_id = "arc";
    graph.addNode(late);
    graph.addNode(early);
    CHECK(graph.addEdge("src", "arc"));
    pipeline::GraphJob job;
    graph.execute(sss, job);
    CHECK(job.anyFailed());
    CHECK(!job.node_results.empty());
    CHECK(job.node_results.back().error.find("processing contract")
          != std::string::npos);
}

void testProfessionalEnhancementNodes()
{
    using namespace dolphin;
    const auto kinds = {
        pipeline::SidescanEnhancementKind::Arn,
        pipeline::SidescanEnhancementKind::Destripe,
        pipeline::SidescanEnhancementKind::BeamPattern,
        pipeline::SidescanEnhancementKind::AdaptiveContrast,
    };
    for (const auto kind : kinds) {
        pipeline::SidescanEnhancementNode node(kind);
        CHECK(NodeRegistry::instance().isKnown(node.typeId()));
        const auto contract = pipeline::processingContractFor(node.typeId());
        CHECK(contract.mutates_samples);
        CHECK(contract.idempotent);

        pipeline::ArtifactBuffer input;
        for (int row = 0; row < 20; ++row) {
            core::SidescanPing ping;
            ping.channel = core::SidescanChannel::Port;
            ping.timestamp_us = row;
            ping.samples.resize(32);
            for (size_t column = 0; column < ping.samples.size(); ++column)
                ping.samples[column].amplitude = static_cast<uint16_t>(
                    1000 + row * 20 + column * 40 + (row == 10 ? 4000 : 0));
            input.emplace_back(std::move(ping));
        }
        const auto once = node.process(input, {});
        const auto twice = node.process(once, {});
        const auto flag = kind == pipeline::SidescanEnhancementKind::Arn
            ? core::CorrectionFlag::Arn
            : kind == pipeline::SidescanEnhancementKind::Destripe
                ? core::CorrectionFlag::Destriping
                : kind == pipeline::SidescanEnhancementKind::BeamPattern
                    ? core::CorrectionFlag::BeamPattern
                    : core::CorrectionFlag::AdaptiveContrast;
        for (size_t i = 0; i < once.size(); ++i) {
            const auto& first = std::get<core::SidescanPing>(once[i]);
            const auto& second = std::get<core::SidescanPing>(twice[i]);
            CHECK(core::hasCorrectionFlag(first.correction_flags, flag));
            CHECK(first.samples.front().amplitude == second.samples.front().amplitude);
        }
    }

    core::SidescanPing zero_safe;
    zero_safe.channel = core::SidescanChannel::Port;
    zero_safe.samples.resize(4);
    zero_safe.samples[0].amplitude = 0;
    zero_safe.samples[1].amplitude = 1000;
    zero_safe.samples[2].amplitude = 2000;
    zero_safe.samples[3].amplitude = 4000;
    pipeline::ArtifactBuffer generic_input{zero_safe};
    pipeline::ContrastEnhanceNode contrast;
    auto contrast_once = contrast.process(generic_input, {});
    auto contrast_twice = contrast.process(contrast_once, {});
    const auto& contrast_ping = std::get<core::SidescanPing>(contrast_once[0]);
    CHECK(contrast_ping.samples[0].amplitude == 0);
    CHECK(core::hasCorrectionFlag(contrast_ping.correction_flags,
                                  core::CorrectionFlag::ContrastStretch));
    CHECK(std::get<core::SidescanPing>(contrast_twice[0]).samples[1].amplitude
          == contrast_ping.samples[1].amplitude);

    pipeline::HistogramEqNode histogram;
    auto histogram_once = histogram.process(generic_input, {});
    auto histogram_twice = histogram.process(histogram_once, {});
    const auto& histogram_ping = std::get<core::SidescanPing>(histogram_once[0]);
    CHECK(histogram_ping.samples[0].amplitude == 0);
    CHECK(core::hasCorrectionFlag(histogram_ping.correction_flags,
                                  core::CorrectionFlag::HistogramEqualized));
    CHECK(std::get<core::SidescanPing>(histogram_twice[0]).samples[1].amplitude
          == histogram_ping.samples[1].amplitude);
}

void testPersistedProvenanceReplacesPriorState()
{
    using namespace dolphin;
    core::SidescanPing port;
    port.correction_flags = static_cast<uint32_t>(core::CorrectionFlag::Tvg)
                          | static_cast<uint32_t>(core::CorrectionFlag::Arc);
    core::SidescanPing starboard;
    starboard.correction_flags =
        static_cast<uint32_t>(core::CorrectionFlag::SlantRange);
    pipeline::ArtifactBuffer processed{port, starboard};

    const auto provenance =
        app::contracts::deriveProcessingProvenance(processed);
    CHECK(core::hasCorrectionFlag(provenance.baked_correction_flags,
                                  core::CorrectionFlag::Tvg));
    CHECK(core::hasCorrectionFlag(provenance.baked_correction_flags,
                                  core::CorrectionFlag::Arc));
    CHECK(provenance.slant_range_corrected);

    // A subsequent baseline run with no correction flags must produce an empty
    // replacement state; old flags must never leak into History.
    pipeline::ArtifactBuffer unprocessed{core::SidescanPing{}};
    const auto cleared = app::contracts::deriveProcessingProvenance(unprocessed);
    CHECK(cleared.baked_correction_flags == 0);
    CHECK(!cleared.slant_range_corrected);
}

void testEnhancementNoOpDoesNotClaimProvenance()
{
    using namespace dolphin;
    pipeline::SidescanEnhancementNode arn(
        pipeline::SidescanEnhancementKind::Arn);
    core::SidescanPing empty;
    empty.channel = core::SidescanChannel::Port;
    empty.samples.resize(32); // all zero: no valid reference profile
    const pipeline::ArtifactBuffer output = arn.process({empty}, {});
    const auto& ping = std::get<core::SidescanPing>(output.front());
    CHECK(!core::hasCorrectionFlag(ping.correction_flags,
                                   core::CorrectionFlag::Arn));
}

} // namespace

int main()
{
    dolphin::pipeline::registerBuiltinNodes();

    testRoundTripPreservesPortsGroupsAndLayout();
    testUnknownTypeDoesNotMutateExistingGraph();
    testMalformedContentAndDanglingReferencesAreRejected();
    testLegacySlantRangeSoundVelocityIsMigrated();
    testRadiometryNodesAreLineStableAndIdempotent();
    testRadiometryNodeAdaptersMatchSharedEngine();
    testCentralProcessingContract();
    testProfessionalEnhancementNodes();
    testPersistedProvenanceReplacesPriorState();
    testEnhancementNoOpDoesNotClaimProvenance();

    std::printf("%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
