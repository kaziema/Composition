#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ruby/core/Expressions.h"
#include "ruby/script/Sandbox.h"

namespace ruby::script {

// Plugs the sandbox into core's expression seam.
//
// One of these per thread, installed by whoever owns that thread. It is not installed
// automatically anywhere: a module that quietly starts an interpreter on first use is a
// module you cannot reason about, and there are legitimate callers (a migration tool, a
// test) that should never have one at all.
class LuaHost : public core::ExpressionHost {
public:
    ~LuaHost() override;

    [[nodiscard]] static std::unique_ptr<LuaHost> create();

    [[nodiscard]] bool evaluate(const core::Property& prop, double seconds,
                                const core::TimeContext& ctx, const core::Value& fallback,
                                std::uint64_t seed, core::Value& out) override;

    // Expressions that failed, most recent first, capped. The UI wants to show these
    // next to the property rather than in a log nobody opens, and a broken expression
    // that silently returns its keyframed value is otherwise invisible.
    struct Failure {
        std::string source;
        std::string error;
    };
    [[nodiscard]] const std::vector<Failure>& failures() const noexcept;
    void clearFailures();

    [[nodiscard]] Sandbox& sandbox() noexcept { return *box_; }

private:
    LuaHost();

    std::unique_ptr<Sandbox> box_;
    std::vector<Failure> failures_;
};

// Installs `host` for the calling thread and removes it again on destruction. Scoped
// rather than a bare setter because forgetting to clear it leaves core holding a pointer
// to something that has been destroyed.
class ScopedHost {
public:
    explicit ScopedHost(core::ExpressionHost* host);
    ~ScopedHost();

    ScopedHost(const ScopedHost&) = delete;
    ScopedHost& operator=(const ScopedHost&) = delete;

private:
    core::ExpressionHost* previous_ = nullptr;
};

}  // namespace ruby::script
