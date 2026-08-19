#pragma once

#include <array>
#include <cstddef>
#include <list>
#include <string>

class SshConnectionModel
{
public:
    enum class Field : size_t { HOST = 0, PORT = 1, USER = 2 };
    enum class Error { NONE, EMPTY_HOST, INVALID_HOST, INVALID_PORT, INVALID_USER };
    struct Validation { Error error; Field field; const char *message; };
    static constexpr size_t FIELD_COUNT = 3;
    static constexpr int CONNECTION_TIMEOUT_SECONDS = 20;

    SshConnectionModel();

    const char *label(size_t index) const;
    const std::string &value(size_t index) const;
    size_t active_index() const { return active_index_; }

    bool select_previous();
    bool select_next();
    bool append(const std::string &text);
    bool erase_last();

    bool valid() const;
    Validation validate() const;
    std::list<std::string> arguments() const;
    bool set_values(std::string host, std::string port, std::string user);

private:
    static constexpr size_t MAX_FIELD_BYTES = 128;
    std::array<std::string, FIELD_COUNT> values_;
    size_t active_index_ = 0;
};
