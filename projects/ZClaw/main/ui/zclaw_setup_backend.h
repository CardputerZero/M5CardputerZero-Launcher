#pragma once

#include "zclaw_types.h"

#include <functional>
#include <string>

namespace zclaw {

class SetupBackend {
public:
    using ProgressHandler = std::function<void(const SetupProgress &)>;

    virtual ~SetupBackend() = default;

    virtual bool ensure_installed(std::string *error,
                                  const ProgressHandler &progress_handler) const = 0;
    virtual bool apply_config(UiConfig *config, const ProviderConfig &provider,
                              std::string *error) const = 0;
    virtual bool start_service(std::string *error) const = 0;
    virtual bool restart_service(std::string *error) const = 0;
    virtual std::string generate_pairing_code() const = 0;
};

}  // namespace zclaw
