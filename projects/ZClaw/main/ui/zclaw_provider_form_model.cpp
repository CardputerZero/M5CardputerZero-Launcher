#include "zclaw_provider_form_model.h"

namespace zclaw {

ProviderEditField provider_edit_field_for_row(int row)
{
    switch (row) {
    case 0: return ProviderEditField::Alias;
    case 1: return ProviderEditField::Family;
    case 2: return ProviderEditField::Model;
    case 3: return ProviderEditField::Uri;
    case 4: return ProviderEditField::ApiKey;
    case 5: return ProviderEditField::WireApi;
    default: return ProviderEditField::None;
    }
}

const char *provider_field_name(ProviderEditField field)
{
    switch (field) {
    case ProviderEditField::Alias: return "Alias";
    case ProviderEditField::Family: return "Family";
    case ProviderEditField::Model: return "Model";
    case ProviderEditField::Uri: return "URI";
    case ProviderEditField::ApiKey: return "API Key";
    case ProviderEditField::WireApi: return "API Type";
    case ProviderEditField::None: return "";
    }
    return "";
}

std::string *provider_field_value(ProviderConfig *provider, ProviderEditField field)
{
    if (!provider)
        return nullptr;
    switch (field) {
    case ProviderEditField::Alias: return &provider->alias;
    case ProviderEditField::Family: return &provider->family;
    case ProviderEditField::Model: return &provider->model;
    case ProviderEditField::Uri: return &provider->uri;
    case ProviderEditField::ApiKey: return &provider->api_key;
    case ProviderEditField::WireApi: return &provider->wire_api;
    case ProviderEditField::None: return nullptr;
    }
    return nullptr;
}

SetupEditField setup_edit_field_for_row(const ProviderConfig &provider, int selected_row)
{
    if (selected_row <= 0)
        return SetupEditField::None;

    int row = 1;
    if (provider.family == "custom" && selected_row == row++)
        return SetupEditField::Uri;
    if (provider.family != "ollama" && selected_row == row++)
        return SetupEditField::ApiKey;
    if (provider.family == "custom" && selected_row == row++)
        return SetupEditField::Model;
    if (provider.family == "custom" && selected_row == row)
        return SetupEditField::WireApi;
    return SetupEditField::None;
}

const char *setup_field_name(SetupEditField field)
{
    switch (field) {
    case SetupEditField::Uri: return "API URL";
    case SetupEditField::ApiKey: return "API Key";
    case SetupEditField::Model: return "API Model Name";
    case SetupEditField::WireApi: return "API Type";
    case SetupEditField::None: return "";
    }
    return "";
}

std::string *setup_field_value(ProviderConfig *provider, SetupEditField field)
{
    if (!provider)
        return nullptr;
    switch (field) {
    case SetupEditField::Uri: return &provider->uri;
    case SetupEditField::ApiKey: return &provider->api_key;
    case SetupEditField::Model: return &provider->model;
    case SetupEditField::WireApi: return &provider->wire_api;
    case SetupEditField::None: return nullptr;
    }
    return nullptr;
}

SetupValidation validate_setup_provider(const ProviderConfig &provider)
{
    if (provider.family != "ollama" && provider.api_key.empty())
        return {false, provider.family == "custom" ? 2 : 1, "API Key is required."};

    if (provider.family == "custom") {
        if (provider.uri.rfind("http://", 0) != 0 &&
            provider.uri.rfind("https://", 0) != 0)
            return {false, 1, "API URL must start with http:// or https://."};
        if (provider.model.empty())
            return {false, 3, "API Model Name is required."};
    }
    return {};
}

void toggle_wire_api(ProviderConfig *provider)
{
    if (provider)
        provider->wire_api = provider->wire_api == "responses" ? "completions" : "responses";
}

}  // namespace zclaw
