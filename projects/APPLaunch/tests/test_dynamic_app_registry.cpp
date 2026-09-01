#include "../main/ui/model/dynamic_app_registry.hpp"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace {

void assert_entry(const DynamicAppRegistration &entry,
                  const std::string &label,
                  const std::string &icon,
                  const std::string &config_key)
{
    assert(entry.label == label);
    assert(entry.icon == icon);
    assert(entry.config_key == config_key);
    assert(entry.desc.label == entry.label.c_str());
    assert(entry.desc.icon == entry.icon.c_str());
    assert(entry.desc.config_key == entry.config_key.c_str());
}

} // namespace

int main(int argc, char **argv)
{
    DynamicAppRegistry registry;
    assert(registry.entries().empty());
    assert(registry.register_pending("Ignored", "ignored.png", "app_ignored") == nullptr);

    registry.begin_refresh();
    assert(registry.register_pending("Old", "old.png", "app_old") != nullptr);
    assert(registry.entries().empty());
    registry.commit_refresh();
    assert(registry.entries().size() == 1);
    assert_entry(registry.entries()[0], "Old", "old.png", "app_old");

    registry.begin_refresh();
    assert(registry.register_pending("Discarded", "discarded.png", "app_discarded") != nullptr);
    assert(registry.entries().size() == 1);
    assert_entry(registry.entries()[0], "Old", "old.png", "app_old");
    registry.cancel_refresh();
    assert(registry.entries().size() == 1);
    assert_entry(registry.entries()[0], "Old", "old.png", "app_old");

    registry.begin_refresh();
    assert(registry.register_pending("New One", "one.png", "app_one") != nullptr);
    assert(registry.register_pending("New Two", "two.png", "app_two") != nullptr);
    assert(registry.entries().size() == 1);
    registry.commit_refresh();
    assert(registry.entries().size() == 2);
    assert_entry(registry.entries()[0], "New One", "one.png", "app_one");
    assert_entry(registry.entries()[1], "New Two", "two.png", "app_two");

    registry.begin_refresh();
    registry.commit_refresh();
    assert(registry.entries().empty());

    assert(argc == 2);
    std::ifstream loader_file(argv[1]);
    assert(loader_file);
    const std::string loader((std::istreambuf_iterator<char>(loader_file)),
                             std::istreambuf_iterator<char>());
    const std::size_t list_failure = loader.find("if (list_code != 0) return;");
    const std::size_t begin_refresh = loader.find("launcher_app_registry_begin_dynamic_refresh();");
    const std::size_t commit_refresh = loader.find("launcher_app_registry_commit_dynamic_refresh();");
    const std::size_t cancel_refresh = loader.find("launcher_app_registry_cancel_dynamic_refresh();");
    assert(list_failure != std::string::npos);
    assert(begin_refresh != std::string::npos);
    assert(commit_refresh != std::string::npos);
    assert(cancel_refresh != std::string::npos);
    assert(list_failure < begin_refresh);
    assert(begin_refresh < commit_refresh);
    assert(loader.find("std::filesystem::last_write_time") != std::string::npos);
    assert(loader.find("sort_desktop_candidates(candidates);") != std::string::npos);
    assert(loader.find("desktop_config_key(candidate.filename)") != std::string::npos);
}
