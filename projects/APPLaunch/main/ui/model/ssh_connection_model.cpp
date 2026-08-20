#include "ssh_connection_model.hpp"
#include "../keyboard_text_input.hpp"
#include "integer_parse_policy.hpp"

#include <algorithm>
#include <cctype>

SshConnectionModel::SshConnectionModel()
    : values_{"192.168.1.1", "22", "pi"}
{
}

const char *SshConnectionModel::label(size_t index) const
{
    static constexpr const char *labels[FIELD_COUNT] = {"Host", "Port", "User"};
    return index < FIELD_COUNT ? labels[index] : "";
}

const std::string &SshConnectionModel::value(size_t index) const
{
    static const std::string empty;
    return index < FIELD_COUNT ? values_[index] : empty;
}

bool SshConnectionModel::select_previous()
{
    if (active_index_ == 0) return false;
    --active_index_;
    return true;
}

bool SshConnectionModel::select_next()
{
    if (active_index_ + 1 >= FIELD_COUNT) return false;
    ++active_index_;
    return true;
}

bool SshConnectionModel::append(const std::string &text)
{
    return launcher_ui::text_input::append_limited(values_[active_index_], text,
                                                    MAX_FIELD_BYTES);
}

bool SshConnectionModel::erase_last()
{
    return launcher_ui::text_input::erase_last_codepoint(values_[active_index_]);
}

bool SshConnectionModel::valid() const
{
    return validate().error == Error::NONE;
}

SshConnectionModel::Validation SshConnectionModel::validate() const
{
    const std::string &host = values_[static_cast<size_t>(Field::HOST)];
    const std::string &port = values_[static_cast<size_t>(Field::PORT)];
    const std::string &user = values_[static_cast<size_t>(Field::USER)];
    auto invalid_identity = [](const std::string &value) {
        return (!value.empty() && value.front() == '-') ||
               std::any_of(value.begin(), value.end(), [](unsigned char character) {
                   return std::isspace(character) != 0;
               });
    };
    if (host.empty()) return {Error::EMPTY_HOST, Field::HOST, "Host is required"};
    if (invalid_identity(host)) return {Error::INVALID_HOST, Field::HOST, "Invalid host"};
    if (invalid_identity(user)) return {Error::INVALID_USER, Field::USER, "Invalid user"};
    if (port.empty()) return {Error::NONE, Field::HOST, ""};
    unsigned int number = 0;
    if (!launcher_ui::integer_parse::complete(port, 1U, 65535U, number))
        return {Error::INVALID_PORT, Field::PORT, "Port must be 1-65535"};
    return {Error::NONE, Field::HOST, ""};
}

bool SshConnectionModel::set_values(std::string host, std::string port, std::string user)
{
    if (host.size() > MAX_FIELD_BYTES || port.size() > MAX_FIELD_BYTES ||
        user.size() > MAX_FIELD_BYTES) return false;
    const auto previous = values_;
    values_ = {std::move(host), std::move(port), std::move(user)};
    if (!valid()) {
        values_ = previous;
        return false;
    }
    return true;
}

std::list<std::string> SshConnectionModel::arguments() const
{
    if (!valid()) return {};
    const std::string &host = values_[static_cast<size_t>(Field::HOST)];
    const std::string &port = values_[static_cast<size_t>(Field::PORT)];
    const std::string &user = values_[static_cast<size_t>(Field::USER)];
    std::list<std::string> arguments = {
        "-o", "StrictHostKeyChecking=no",
        "-o", "ConnectTimeout=" + std::to_string(CONNECTION_TIMEOUT_SECONDS),
    };
    if (!port.empty() && port != "22") {
        arguments.push_back("-p");
        arguments.push_back(port);
    }
    arguments.push_back(user.empty() ? host : user + "@" + host);
    return arguments;
}
