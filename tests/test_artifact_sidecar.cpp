#include "app/artifacts/ArtifactSidecar.h"
#include "io/IFormatReader.h"

#include <filesystem>
#include <iostream>

namespace {
int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}
} // namespace

int main()
{
    using dolphin::app::sidecarArtifactPath;
    using namespace dolphin::io;

    check(sidecarArtifactPath("survey.dlpd", "layer-1", kArtifactRoleOriginal)
              == "survey_layer-1.dlpd",
          "original store should produce a per-layer sidecar");
    check(sidecarArtifactPath("survey_layer-1.dlpd", "layer-1", kArtifactRoleOriginal)
              == "survey_layer-1.dlpd",
          "legacy suffix-named sidecar should be reused");
    check(sidecarArtifactPath("survey.dlpd", "layer-1", kArtifactRoleSidecar)
              == "survey.dlpd",
          "formally marked sidecar should be reused");
    check(sidecarArtifactPath("survey_layer-10.dlpd", "layer-1", kArtifactRoleOriginal)
              == "survey_layer-10_layer-1.dlpd",
          "layer suffix matching must reject a partial id");
    check(std::filesystem::path(sidecarArtifactPath(
              "data/survey.dpcache", "sss", kArtifactRoleOriginal)).filename()
              == "survey_sss.dlpd",
          "new sidecars should use the current extension");

    return failures == 0 ? 0 : 1;
}
