#pragma once

#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"
#include "settings_system_model.hpp"

#include <functional>
#include <list>
#include <string>

namespace settings_system {

using ApiCallback = std::function<void(int, std::string)>;

void request(std::list<std::string> arguments, ApiCallback callback) noexcept;
void request_background(UpdateAction action, ApiCallback callback) noexcept;

int read_network_default(NetworkInfo &result);
int read_ethernet(NetworkInfo &result);
int read_account(AccountInfo &result);
int read_launcher_state(std::string &state);

int start_update(UpdateAction action, std::string &job_id);
int update_status(const std::string &job_id, std::string &state);
int cancel_update(const std::string &job_id);

int apt_update_background();
int update_launcher_background();

} // namespace settings_system
