#pragma once

#include "menu_types.hpp"

#include <cstdint>
#include <vector>

class UISetupPage;

namespace setting {

class About
{
public:
    static void append(UISetupPage &page, std::vector<MenuItem> &menu);
    static void refresh_info(UISetupPage &page);
};

class Help
{
public:
    static void append(UISetupPage &page, std::vector<MenuItem> &menu);
    static void enter_page(UISetupPage &page);
    static bool build_page(UISetupPage &page);
    static void handle_key(UISetupPage &page, uint32_t key);
};

class ExtPort
{
public:
    static void append(UISetupPage &page, std::vector<MenuItem> &menu);
};

class Ethernet
{
public:
    static void append(UISetupPage &page, std::vector<MenuItem> &menu);
    static void refresh_info(UISetupPage &page);
};

class Account
{
public:
    static void append(UISetupPage &page, std::vector<MenuItem> &menu);
    static void refresh_info(UISetupPage &page);
};

class Update
{
public:
    static void append(UISetupPage &page, std::vector<MenuItem> &menu);
    static void refresh_version_info(UISetupPage &page);
    static void check_system_update(UISetupPage &page);
    static void update_launcher(UISetupPage &page);
};

void build_menu(UISetupPage &page);

} // namespace setting
