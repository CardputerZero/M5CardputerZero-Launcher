#include "zclaw_setup_service.h"

#include "zclaw_quickstart_model.h"
#include "zclaw_setup_backend.h"

#include <utility>

namespace zclaw {
namespace {

void report_progress(const SetupService::ProgressHandler &handler,
                     const char *status, int percent)
{
    if (handler)
        handler({status ? status : "", percent});
}

}  // namespace

SetupService::SetupService(const SetupBackend &backend)
    : backend_(&backend)
{
}

PreparedSetup SetupService::prepare(
    UiConfig config, ProviderConfig provider,
    const ProgressHandler &progress_handler) const
{
    PreparedSetup result;
    result.config = std::move(config);
    const bool reconfigure = result.config.setup_complete;
    provider = normalize_setup_provider(std::move(provider));

    if (!backend_->ensure_installed(&result.error, progress_handler))
        return result;

    report_progress(progress_handler, "Configuring provider", 72);
    if (!backend_->apply_config(&result.config, provider, &result.error))
        return result;

    report_progress(progress_handler,
                    reconfigure ? "Restarting ZeroClaw service" :
                                  "Starting ZeroClaw service", 86);
    if (reconfigure ? !backend_->restart_service(&result.error) :
                      !backend_->start_service(&result.error))
        return result;

    report_progress(progress_handler, "Pairing local client", 94);
    result.pairing_code = backend_->generate_pairing_code();
    if (result.pairing_code.empty()) {
        result.error = "ZeroClaw started, but pairing code generation failed.";
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace zclaw
