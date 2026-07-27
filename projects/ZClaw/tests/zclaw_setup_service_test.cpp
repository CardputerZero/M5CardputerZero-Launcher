#include "zclaw_setup_backend.h"
#include "zclaw_setup_service.h"

#include <cassert>
#include <string>
#include <vector>

namespace {

class FakeSetupBackend : public zclaw::SetupBackend {
public:
    bool install_ok = true;
    bool config_ok = true;
    bool service_ok = true;
    std::string pairing_code = "123456";
    mutable std::vector<std::string> calls;
    mutable zclaw::ProviderConfig configured_provider;

    bool ensure_installed(std::string *error,
                          const ProgressHandler &progress_handler) const override
    {
        calls.push_back("install");
        if (progress_handler)
            progress_handler({"Fake install", 50});
        if (!install_ok && error)
            *error = "install failed";
        return install_ok;
    }

    bool apply_config(zclaw::UiConfig *config,
                      const zclaw::ProviderConfig &provider,
                      std::string *error) const override
    {
        calls.push_back("config");
        configured_provider = provider;
        if (!config_ok) {
            if (error)
                *error = "config failed";
            return false;
        }
        config->agent_alias = "configured-agent";
        config->webhook_url = "http://configured/webhook";
        return true;
    }

    bool start_service(std::string *error) const override
    {
        calls.push_back("start");
        if (!service_ok && error)
            *error = "service failed";
        return service_ok;
    }

    bool restart_service(std::string *error) const override
    {
        calls.push_back("restart");
        if (!service_ok && error)
            *error = "service failed";
        return service_ok;
    }

    std::string generate_pairing_code() const override
    {
        calls.push_back("pair-code");
        return pairing_code;
    }
};

}  // namespace

int main()
{
    FakeSetupBackend backend;
    std::vector<int> progress;
    zclaw::UiConfig config;
    zclaw::ProviderConfig provider;
    const zclaw::PreparedSetup prepared = zclaw::SetupService(backend).prepare(
        config, provider, [&progress](const zclaw::SetupProgress &update) {
            progress.push_back(update.percent);
        });

    assert(prepared.ok);
    assert(prepared.pairing_code == "123456");
    assert(prepared.config.agent_alias == "configured-agent");
    assert(prepared.config.webhook_url == "http://configured/webhook");
    assert((backend.calls ==
            std::vector<std::string>{"install", "config", "start", "pair-code"}));
    assert(backend.configured_provider.alias == "zclaw");
    assert(backend.configured_provider.family == "openai");
    assert(backend.configured_provider.model == "gpt-4.1-mini");
    assert((progress == std::vector<int>{50, 72, 86, 94}));

    backend.calls.clear();
    config.setup_complete = true;
    const zclaw::PreparedSetup reconfigured =
        zclaw::SetupService(backend).prepare(config, provider, {});
    assert(reconfigured.ok);
    assert((backend.calls == std::vector<std::string>{
        "install", "config", "restart", "pair-code"}));

    FakeSetupBackend failed_backend;
    failed_backend.service_ok = false;
    config.setup_complete = false;
    const zclaw::PreparedSetup failed =
        zclaw::SetupService(failed_backend).prepare(config, provider, {});
    assert(!failed.ok);
    assert(failed.error == "service failed");
    assert((failed_backend.calls ==
            std::vector<std::string>{"install", "config", "start"}));

    FakeSetupBackend missing_code_backend;
    missing_code_backend.pairing_code.clear();
    const zclaw::PreparedSetup missing_code =
        zclaw::SetupService(missing_code_backend).prepare(config, provider, {});
    assert(!missing_code.ok);
    assert(missing_code.error ==
           "ZeroClaw started, but pairing code generation failed.");
    return 0;
}
