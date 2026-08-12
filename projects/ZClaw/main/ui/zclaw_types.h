#pragma once

#include <cstddef>
#include <string>
#include <utility>

namespace zclaw {

struct ProviderConfig {
    std::string alias;
    std::string family;
    std::string model;
    std::string uri;
    std::string api_key;
    std::string wire_api = "completions";
};

struct UiConfig {
    std::string webhook_url = "http://127.0.0.1:42617/webhook";
    std::string agent_alias = "zclaw";
    std::string webhook_secret;
    std::string bearer_token;
    bool setup_complete = false;
    bool ui_sounds_enabled = true;
};

struct ApprovalRequest {
    std::string request_id;
    std::string tool;
    std::string summary;
    int timeout_secs = 120;
};

struct OperationResult {
    std::string text;
    bool ok = false;
    UiConfig config;
};

struct SetupProgress {
    SetupProgress() = default;
    SetupProgress(std::string status_value, int percent_value,
                  std::size_t downloaded_value = 0,
                  std::size_t total_value = 0, double speed_value = 0.0,
                  std::string source_value = {},
                  std::string destination_value = {})
        : status(std::move(status_value)), percent(percent_value),
          downloaded_bytes(downloaded_value), total_bytes(total_value),
          bytes_per_second(speed_value), source_url(std::move(source_value)),
          destination_path(std::move(destination_value))
    {
    }

    std::string status;
    int percent = 0;
    std::size_t downloaded_bytes = 0;
    std::size_t total_bytes = 0;
    double bytes_per_second = 0.0;
    std::string source_url;
    std::string destination_path;
};

}  // namespace zclaw
