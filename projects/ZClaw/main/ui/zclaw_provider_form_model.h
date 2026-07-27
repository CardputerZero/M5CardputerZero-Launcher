#pragma once

#include "zclaw_types.h"

#include <string>

namespace zclaw {

enum class ProviderEditField {
    None,
    Alias,
    Family,
    Model,
    Uri,
    ApiKey,
    WireApi,
};

enum class SetupEditField {
    None,
    Uri,
    ApiKey,
    Model,
    WireApi,
};

struct SetupValidation {
    bool valid = true;
    int row = 0;
    std::string error;
};

ProviderEditField provider_edit_field_for_row(int row);
const char *provider_field_name(ProviderEditField field);
std::string *provider_field_value(ProviderConfig *provider, ProviderEditField field);

SetupEditField setup_edit_field_for_row(const ProviderConfig &provider, int row);
const char *setup_field_name(SetupEditField field);
std::string *setup_field_value(ProviderConfig *provider, SetupEditField field);
SetupValidation validate_setup_provider(const ProviderConfig &provider);
void toggle_wire_api(ProviderConfig *provider);

}  // namespace zclaw
