#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Cp0SystemSoundPlayer
{
public:
    using PlayCallback = std::function<void(bool)>;

    Cp0SystemSoundPlayer();
    ~Cp0SystemSoundPlayer();

    Cp0SystemSoundPlayer(const Cp0SystemSoundPlayer &) = delete;
    Cp0SystemSoundPlayer &operator=(const Cp0SystemSoundPlayer &) = delete;

    int reload(const std::vector<std::string> &names);
    bool play_index(std::size_t index, PlayCallback callback = nullptr);
    bool play_named(const std::string &name);
    bool contains(const std::string &name) const;
    void set_enabled(bool enabled);
    bool enabled() const;
    std::size_t sound_count() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
