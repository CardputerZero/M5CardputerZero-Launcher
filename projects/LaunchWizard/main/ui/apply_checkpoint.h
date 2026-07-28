#ifndef LAUNCH_WIZARD_APPLY_CHECKPOINT_H
#define LAUNCH_WIZARD_APPLY_CHECKPOINT_H

#include "wizard_model.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace launch_wizard {

inline constexpr int kApplyStepCount = 9;

enum class ApplyCheckpointLoad {
    Missing,
    Resume,
    ConfigurationChanged,
    Invalid,
};

class ApplyCheckpointStore {
public:
    explicit ApplyCheckpointStore(std::string path);

    ApplyCheckpointLoad load(std::uint64_t fingerprint, int &next_step,
                             std::string &error) const;
    bool save(std::uint64_t fingerprint, int next_step, std::string &error) const;
    bool clear(std::string &error) const;
    const std::string &path() const { return path_; }

private:
    std::string path_;
};

struct ApplyStep {
    std::string label;
    std::function<std::string()> execute;
};

std::uint64_t wizard_configuration_fingerprint(const WizardModel &model);
std::string default_apply_checkpoint_path();

std::string run_apply_steps(
    std::uint64_t fingerprint,
    const std::vector<ApplyStep> &steps,
    const ApplyCheckpointStore &store,
    const std::function<void(int, int, const std::string &)> &progress = {},
    const std::function<bool()> &cancelled = {});

} // namespace launch_wizard

#endif
