#include "apply_checkpoint.h"

#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string make_test_directory()
{
    char pattern[] = "/tmp/launch-wizard-checkpoint-XXXXXX";
    char *directory = mkdtemp(pattern);
    return directory ? directory : std::string{};
}

void remove_test_directory(const std::string &directory)
{
    unlink((directory + "/apply.state").c_str());
    rmdir((directory + "/apply.state").c_str());
    rmdir(directory.c_str());
}

std::vector<launch_wizard::ApplyStep> make_steps(std::vector<int> &calls,
                                                 int &failed_step)
{
    std::vector<launch_wizard::ApplyStep> steps;
    for (int index = 0; index < launch_wizard::kApplyStepCount; ++index) {
        steps.push_back({"step " + std::to_string(index + 1),
                         [&calls, &failed_step, index] {
            ++calls[index];
            return failed_step == index ? std::string("injected failure") : std::string{};
        }});
    }
    return steps;
}

} // namespace

bool test_apply_checkpoint()
{
    using namespace launch_wizard;
    bool passed = true;
    const auto expect = [&passed](bool condition, const std::string &message) {
        if (!condition) {
            std::cerr << message << '\n';
            passed = false;
        }
    };

    WizardModel configuration;
    configuration.password = "private password";
    configuration.wifi_password = "network secret";
    const std::uint64_t fingerprint = wizard_configuration_fingerprint(configuration);
    WizardModel changed;
    changed.password = configuration.password;
    changed.wifi_password = configuration.wifi_password;
    changed.hostname = "different-host";
    expect(wizard_configuration_fingerprint(changed) != fingerprint,
           "apply fingerprint did not include hostname");
    WizardModel manual_time_changed;
    manual_time_changed.password = configuration.password;
    manual_time_changed.wifi_password = configuration.wifi_password;
    manual_time_changed.manual_date = "2030-01-02";
    manual_time_changed.manual_time = "03:04";
    expect(wizard_configuration_fingerprint(manual_time_changed) == fingerprint,
           "inactive manual time still affected the apply fingerprint");
    expect(kApplyStepCount == 8,
           "manual time removal did not reduce the apply sequence to eight steps");

    // A new invocation always starts from the first step, even when a valid
    // checkpoint from an interrupted configuration exists.
    for (int injected = 0; injected < kApplyStepCount; ++injected) {
        const std::string directory = make_test_directory();
        expect(!directory.empty(), "could not create checkpoint test directory");
        if (directory.empty()) continue;
        const std::string path = directory + "/apply.state";
        ApplyCheckpointStore store(path);
        std::vector<int> calls(kApplyStepCount, 0);
        int failed_step = injected;
        auto steps = make_steps(calls, failed_step);
        std::vector<int> progress_steps;
        std::string error = run_apply_steps(
            fingerprint, steps, store,
            [&progress_steps](int step, int total, const std::string &) {
                if (total == kApplyStepCount) progress_steps.push_back(step);
            });
        expect(error == "injected failure",
               "step " + std::to_string(injected + 1) + " failure was not returned");
        for (int index = 0; index < kApplyStepCount; ++index) {
            const int expected = index <= injected ? 1 : 0;
            expect(calls[index] == expected,
                   "first run executed an unexpected step around failure " +
                       std::to_string(injected + 1));
        }
        expect(!progress_steps.empty() && progress_steps.front() == 1,
               "initial progress was not absolute step 1");

        failed_step = -1;
        progress_steps.clear();
        error = run_apply_steps(
            fingerprint, steps, store,
            [&progress_steps](int step, int, const std::string &) {
                progress_steps.push_back(step);
            });
        expect(error.empty(), "retry did not complete after injected failure");
        expect(!progress_steps.empty() && progress_steps.front() == 1,
               "retry progress did not restart at step 1");
        for (int index = 0; index < injected; ++index)
            expect(calls[index] == 2, "completed apply step was not rerun");
        expect(calls[injected] == 2, "failed apply step was not retried");
        expect(access(path.c_str(), F_OK) != 0,
               "successful apply did not clear its checkpoint");
        remove_test_directory(directory);
    }

    // A changed configuration deliberately invalidates the old sequence and
    // starts at step zero rather than mixing two configurations.
    {
        const std::string directory = make_test_directory();
        const std::string path = directory + "/apply.state";
        ApplyCheckpointStore store(path);
        std::vector<int> calls(kApplyStepCount, 0);
        int failed_step = 3;
        auto steps = make_steps(calls, failed_step);
        expect(run_apply_steps(fingerprint, steps, store) == "injected failure",
               "configuration-change setup did not fail");
        failed_step = 0;
        expect(run_apply_steps(fingerprint + 1, steps, store) == "injected failure",
               "changed configuration did not restart at step zero");
        expect(calls[0] == 2, "changed configuration reused the stale checkpoint");
        remove_test_directory(directory);
    }

    // A damaged checkpoint must be explicit and recoverable; silently starting
    // over could repeat non-idempotent system changes.
    {
        const std::string directory = make_test_directory();
        const std::string path = directory + "/apply.state";
        std::ofstream(path) << "not a checkpoint\n";
        ApplyCheckpointStore store(path);
        std::vector<int> calls(kApplyStepCount, 0);
        int failed_step = -1;
        const std::string error = run_apply_steps(
            fingerprint, make_steps(calls, failed_step), store);
        expect(error.find("damaged") != std::string::npos,
               "damaged checkpoint did not produce a recovery error");
        expect(calls[0] == 0, "damaged checkpoint allowed apply to proceed");
        remove_test_directory(directory);
    }

    // Failure to durably record a completed step must stop before step two.
    {
        const std::string directory = make_test_directory();
        const std::string path = directory + "/apply.state";
        ApplyCheckpointStore store(path);
        std::vector<int> calls(kApplyStepCount, 0);
        int failed_step = -1;
        auto steps = make_steps(calls, failed_step);
        steps[0].execute = [&calls, &path] {
            ++calls[0];
            unlink(path.c_str());
            mkdir(path.c_str(), 0700);
            return std::string{};
        };
        const std::string error = run_apply_steps(fingerprint, steps, store);
        expect(error.find("checkpoint failed after step 1") != std::string::npos,
               "checkpoint write failure was not reported at its step");
        expect(calls[0] == 1 && calls[1] == 0,
               "apply continued after checkpoint persistence failed");
        remove_test_directory(directory);
    }

    // Stored state is private and contains only the fingerprint, never either
    // plaintext credential.
    {
        const std::string directory = make_test_directory();
        const std::string path = directory + "/apply.state";
        ApplyCheckpointStore store(path);
        std::string error;
        expect(store.save(fingerprint, 4, error), "could not save checkpoint: " + error);
        struct stat info {};
        expect(stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
               "checkpoint file permissions are not 0600");
        std::ifstream file(path);
        const std::string contents((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        expect(contents.find(configuration.password) == std::string::npos &&
                   contents.find(configuration.wifi_password) == std::string::npos,
               "checkpoint leaked plaintext credentials");
        remove_test_directory(directory);
    }

    return passed;
}
