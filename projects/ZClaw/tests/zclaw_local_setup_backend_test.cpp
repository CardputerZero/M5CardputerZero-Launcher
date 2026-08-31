#include "zclaw_local_cli_service.h"

#include "zclaw_process_executor.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

}  // namespace

int main()
{
    char home_template[] = "/tmp/zclaw-local-backend-XXXXXX";
    const char *created_home = ::mkdtemp(home_template);
    assert(created_home);
    const std::filesystem::path home(created_home);
    const std::filesystem::path binary = home / ".zeroclaw/bin/zeroclaw";
    const std::filesystem::path argv_log = home / "argv.log";
    const std::filesystem::path secret_log = home / "secret.log";
    std::filesystem::create_directories(binary.parent_path());
    std::filesystem::create_directories(home / ".zeroclaw");
    std::ofstream(home / ".zeroclaw/config.toml").close();

    std::ofstream script(binary);
    script << "#!/bin/sh\n"
              "printf '%s\\n' \"$*\" >>\"$ZCLAW_TEST_ARGV_LOG\"\n"
              "if [ \"$1 $2\" = 'config set' ]; then\n"
              "  case \"$*\" in *.api_key*) "
              "stty -echo; "
              "printf 'Enter value for %s: ' \"$3\"; "
              "IFS= read -r secret; printf '%s' \"$secret\" "
              ">\"$ZCLAW_TEST_SECRET_LOG\";; esac\n"
              "elif [ \"$1 $2\" = 'agents list' ]; then\n"
              "  printf 'zclaw\\n'\n"
              "fi\n";
    script.close();
    assert(::chmod(binary.c_str(), 0700) == 0);
    assert(::setenv("HOME", home.c_str(), 1) == 0);
    assert(::setenv("ZCLAW_TEST_ARGV_LOG", argv_log.c_str(), 1) == 0);
    assert(::setenv("ZCLAW_TEST_SECRET_LOG", secret_log.c_str(), 1) == 0);

    auto processes = std::make_shared<zclaw::ProcessExecutor>();
    zclaw::CliService service = zclaw::make_local_cli_service(processes);
    zclaw::UiConfig config;
    config.agent_alias = "zclaw";
    zclaw::ProviderConfig provider;
    provider.family = "openai";
    provider.alias = "primary";
    provider.model = "test-model";
    provider.uri = "https://example.invalid/v1";
    provider.api_key = "secret-backend-sentinel";
    provider.wire_api = "responses";
    std::string error;
    if (!service.apply_config(&config, provider, &error)) {
        std::cerr << error << '\n';
        assert(false);
    }

    assert(read_file(secret_log) == provider.api_key);
    assert(read_file(argv_log).find(provider.api_key) == std::string::npos);

    std::filesystem::remove_all(home);
    return 0;
}
