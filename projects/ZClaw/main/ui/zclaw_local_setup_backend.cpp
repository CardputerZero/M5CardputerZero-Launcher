#include "zclaw_local_setup_backend.h"

#include "zclaw_binary_installer.h"
#include "zclaw_cli_service.h"
#include "zclaw_local_binary_installer_backend.h"
#include "zclaw_process_executor.h"

#include <memory>
#include <utility>

namespace zclaw {
namespace {

CliService cli_service(const std::shared_ptr<ProcessExecutor> &processes)
{
    if (!processes)
        return CliService();
    return CliService(
        [processes](const CliService::Command &command) {
            return processes->run(command);
        },
        [processes](unsigned int seconds) { processes->wait(seconds); });
}

}  // namespace

LocalSetupBackend::LocalSetupBackend(
    std::shared_ptr<HttpCancellationRegistry> cancellation,
    std::shared_ptr<ProcessExecutor> processes,
    std::shared_ptr<LhvHttpClient> http_client)
    : cancellation_(std::move(cancellation)), processes_(std::move(processes)),
      http_client_(std::move(http_client))
{
}

bool LocalSetupBackend::ensure_installed(
    std::string *error, const ProgressHandler &progress_handler) const
{
    return BinaryInstaller(
               std::make_shared<LocalBinaryInstallerBackend>(cancellation_,
                                                              processes_,
                                                              http_client_))
        .ensure_installed(error, progress_handler);
}

bool LocalSetupBackend::apply_config(UiConfig *config,
                                     const ProviderConfig &provider,
                                     std::string *error) const
{
    return cli_service(processes_).apply_config(config, provider, error);
}

bool LocalSetupBackend::start_service(std::string *error) const
{
    return cli_service(processes_).start_service(error);
}

bool LocalSetupBackend::restart_service(std::string *error) const
{
    return cli_service(processes_).restart_service(error);
}

std::string LocalSetupBackend::generate_pairing_code() const
{
    return cli_service(processes_).generate_pairing_code();
}

}  // namespace zclaw
