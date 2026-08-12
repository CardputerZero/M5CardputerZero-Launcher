#pragma once

#include "zclaw_setup_backend.h"

#include <memory>

namespace zclaw {

class HttpCancellationRegistry;
class ProcessExecutor;
class LhvHttpClient;

class LocalSetupBackend final : public SetupBackend {
public:
    LocalSetupBackend() = default;
    explicit LocalSetupBackend(
        std::shared_ptr<HttpCancellationRegistry> cancellation,
        std::shared_ptr<ProcessExecutor> processes = nullptr,
        std::shared_ptr<LhvHttpClient> http_client = nullptr);

    bool ensure_installed(std::string *error,
                          const ProgressHandler &progress_handler) const override;
    bool apply_config(UiConfig *config, const ProviderConfig &provider,
                      std::string *error) const override;
    bool start_service(std::string *error) const override;
    bool restart_service(std::string *error) const override;
    std::string generate_pairing_code() const override;

private:
    std::shared_ptr<HttpCancellationRegistry> cancellation_;
    std::shared_ptr<ProcessExecutor> processes_;
    std::shared_ptr<LhvHttpClient> http_client_;
};

}  // namespace zclaw
