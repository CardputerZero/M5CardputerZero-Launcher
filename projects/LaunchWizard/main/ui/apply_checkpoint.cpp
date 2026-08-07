#include "apply_checkpoint.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace launch_wizard {
namespace {

constexpr const char *kHeader = "LaunchWizard apply checkpoint v1";

std::string parent_path(const std::string &path)
{
    const std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

bool ensure_private_directory(const std::string &directory, std::string &error)
{
    struct stat info {};
    if (stat(directory.c_str(), &info) == 0) {
        if (!S_ISDIR(info.st_mode)) {
            error = directory + " is not a directory";
            return false;
        }
        // Never chmod an arbitrary parent supplied through the test/diagnostic
        // path override (it could be /tmp). Refuse a broadly accessible
        // directory and let the caller select or create a private one.
        if ((info.st_mode & 0077) != 0) {
            error = "checkpoint directory permissions must be 0700";
            return false;
        }
        return true;
    }
    if (errno != ENOENT) {
        error = "cannot inspect checkpoint directory: " + std::string(strerror(errno));
        return false;
    }
    if (mkdir(directory.c_str(), 0700) != 0) {
        error = "cannot create checkpoint directory: " + std::string(strerror(errno));
        return false;
    }
    return true;
}

bool write_all(int fd, const std::string &data, std::string &error)
{
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = write(fd, data.data() + offset, data.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            error = "cannot write checkpoint: " + std::string(strerror(errno));
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

void hash_bytes(std::uint64_t &hash, const void *bytes, std::size_t size)
{
    const auto *data = static_cast<const unsigned char *>(bytes);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= UINT64_C(1099511628211);
    }
}

void hash_string(std::uint64_t &hash, const std::string &value)
{
    const std::uint64_t size = value.size();
    unsigned char encoded_size[sizeof(size)] = {};
    for (std::size_t index = 0; index < sizeof(size); ++index)
        encoded_size[index] = static_cast<unsigned char>(size >> (index * 8));
    hash_bytes(hash, encoded_size, sizeof(encoded_size));
    hash_bytes(hash, value.data(), value.size());
}

void hash_bool(std::uint64_t &hash, bool value)
{
    const unsigned char byte = value ? 1 : 0;
    hash_bytes(hash, &byte, sizeof(byte));
}

} // namespace

ApplyCheckpointStore::ApplyCheckpointStore(std::string path) : path_(std::move(path)) {}

ApplyCheckpointLoad ApplyCheckpointStore::load(std::uint64_t fingerprint,
                                               int &next_step,
                                               std::string &error) const
{
    next_step = 0;
    error.clear();
    FILE *file = fopen(path_.c_str(), "r");
    if (!file) {
        if (errno == ENOENT) return ApplyCheckpointLoad::Missing;
        error = "cannot read apply checkpoint: " + std::string(strerror(errno));
        return ApplyCheckpointLoad::Invalid;
    }

    char header[80] = {};
    unsigned long long stored_fingerprint = 0;
    int stored_step = -1;
    char trailing = '\0';
    const bool valid = fgets(header, sizeof(header), file) != nullptr &&
        std::string(header) == std::string(kHeader) + "\n" &&
        fscanf(file, "fingerprint=%llx\nnext_step=%d\n%c",
               &stored_fingerprint, &stored_step, &trailing) == 2 &&
        stored_step >= 0 && stored_step <= kApplyStepCount;
    const int close_result = fclose(file);
    if (!valid || close_result != 0) {
        error = "apply checkpoint is damaged; remove " + path_ + " and retry";
        return ApplyCheckpointLoad::Invalid;
    }
    if (static_cast<std::uint64_t>(stored_fingerprint) != fingerprint)
        return ApplyCheckpointLoad::ConfigurationChanged;
    next_step = stored_step;
    return ApplyCheckpointLoad::Resume;
}

bool ApplyCheckpointStore::save(std::uint64_t fingerprint, int next_step,
                                std::string &error) const
{
    error.clear();
    if (next_step < 0 || next_step > kApplyStepCount) {
        error = "invalid apply checkpoint step";
        return false;
    }
    const std::string directory = parent_path(path_);
    if (!ensure_private_directory(directory, error)) return false;

    std::ostringstream content;
    content << kHeader << '\n' << "fingerprint=" << std::hex << fingerprint
            << '\n' << std::dec << "next_step=" << next_step << '\n';
    const std::string temporary = path_ + ".tmp." + std::to_string(getpid());
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        error = "cannot create apply checkpoint: " + std::string(strerror(errno));
        return false;
    }
    bool ok = write_all(fd, content.str(), error);
    if (ok && fsync(fd) != 0) {
        error = "cannot sync apply checkpoint: " + std::string(strerror(errno));
        ok = false;
    }
    if (close(fd) != 0 && ok) {
        error = "cannot close apply checkpoint: " + std::string(strerror(errno));
        ok = false;
    }
    if (ok && rename(temporary.c_str(), path_.c_str()) != 0) {
        error = "cannot replace apply checkpoint: " + std::string(strerror(errno));
        ok = false;
    }
    if (!ok) {
        unlink(temporary.c_str());
        return false;
    }
    const int directory_fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0 || fsync(directory_fd) != 0) {
        error = "cannot sync checkpoint directory: " + std::string(strerror(errno));
        if (directory_fd >= 0) close(directory_fd);
        return false;
    }
    close(directory_fd);
    return true;
}

bool ApplyCheckpointStore::clear(std::string &error) const
{
    error.clear();
    if (unlink(path_.c_str()) != 0 && errno != ENOENT) {
        error = "cannot clear apply checkpoint: " + std::string(strerror(errno));
        return false;
    }
    const std::string directory = parent_path(path_);
    const int directory_fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) {
        if (errno == ENOENT) return true;
        error = "cannot open checkpoint directory: " + std::string(strerror(errno));
        return false;
    }
    if (fsync(directory_fd) != 0) {
        error = "cannot sync checkpoint directory: " + std::string(strerror(errno));
        close(directory_fd);
        return false;
    }
    close(directory_fd);
    return true;
}

std::uint64_t wizard_configuration_fingerprint(const WizardModel &model)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    hash_string(hash, "oobe-no-manual-time-v1");
    hash_string(hash, model.current_timezone().name);
    hash_string(hash, model.hostname);
    hash_string(hash, model.username);
    hash_string(hash, model.password);
    hash_bool(hash, model.network_skipped);
    hash_bool(hash, model.use_ethernet);
    hash_bool(hash, model.ethernet_dhcp);
    hash_string(hash, model.ethernet_address);
    hash_string(hash, model.ethernet_gateway);
    hash_string(hash, model.ethernet_dns);
    hash_string(hash, model.wifi_ssid);
    hash_string(hash, model.wifi_security);
    hash_string(hash, model.wifi_password);
    hash_bool(hash, model.wifi_hidden);
    hash_bool(hash, model.wifi_connected);
    hash_bool(hash, model.ssh_enabled);
    return hash;
}

std::string default_apply_checkpoint_path()
{
    const char *override_path = getenv("LAUNCH_WIZARD_APPLY_CHECKPOINT");
    if (override_path && override_path[0] != '\0') return override_path;
#if defined(CONFIG_V9_5_LV_USE_SDL) || \
    (defined(LAUNCH_WIZARD_DRY_RUN) && LAUNCH_WIZARD_DRY_RUN)
    return "/tmp/LaunchWizard-dry-run/apply.checkpoint";
#else
    return "/var/lib/LaunchWizard/apply.checkpoint";
#endif
}

std::string run_apply_steps(
    std::uint64_t fingerprint,
    const std::vector<ApplyStep> &steps,
    const ApplyCheckpointStore &store,
    const std::function<void(int, int, const std::string &)> &progress,
    const std::function<bool()> &cancelled)
{
    if (steps.size() != kApplyStepCount)
        return "Apply runner requires exactly 9 steps";
    for (const ApplyStep &step : steps) {
        if (!step.execute || step.label.empty())
            return "Apply runner contains an invalid step";
    }

    int next_step = 0;
    std::string detail;
    const ApplyCheckpointLoad loaded = store.load(fingerprint, next_step, detail);
    if (loaded == ApplyCheckpointLoad::Invalid)
        return detail;
    if (loaded == ApplyCheckpointLoad::Missing ||
        loaded == ApplyCheckpointLoad::Resume ||
        loaded == ApplyCheckpointLoad::ConfigurationChanged) {
        next_step = 0;
        if (!store.save(fingerprint, next_step, detail))
            return "Apply checkpoint failed: " + detail;
    }

    for (int index = next_step; index < kApplyStepCount; ++index) {
        if (cancelled && cancelled()) return "Configuration cancelled";
        if (progress) progress(index + 1, kApplyStepCount, steps[index].label);
        const std::string step_error = steps[index].execute();
        if (!step_error.empty()) return step_error;
        if (cancelled && cancelled()) return "Configuration cancelled";
        if (!store.save(fingerprint, index + 1, detail))
            return "Apply checkpoint failed after step " + std::to_string(index + 1) +
                   ": " + detail;
    }
    if (!store.clear(detail)) return "Apply checkpoint failed: " + detail;
    return {};
}

} // namespace launch_wizard
