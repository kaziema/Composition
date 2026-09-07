#include "ruby/beat/Detector.h"

// The public build's detector. Analyses nothing, and says so.
//
// Not an error path: an empty rhythm map is an ordinary state everywhere above this
// (see RhythmMap and its tests). The stub exists so the public tree compiles, links and
// runs with the private module absent, which is the whole D6 seam.

namespace ruby::beat {
namespace {

class StubDetector final : public Detector {
public:
    Result analyze(const media::AudioBuffer&, Lane) override { return {}; }
    [[nodiscard]] bool available() const noexcept override { return false; }
    [[nodiscard]] const char* description() const noexcept override {
        return "rhythm analysis is not included in this build";
    }
};

}  // namespace

#ifndef RUBY_HAVE_PRIVATE_BEAT
std::unique_ptr<Detector> createDetector() {
    return std::make_unique<StubDetector>();
}
#endif

}  // namespace ruby::beat
