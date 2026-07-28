#include "../src/cp0_launcher_updater.hpp"

#include <cassert>
#include <string>
#include <vector>

namespace {

struct Fixture {
    std::string hash = std::string(64, 'a');
    std::string actual = std::string(64, 'a') + "  package\n";
    std::string package = "applaunch\n";
    std::string architecture = "arm64\n";
    std::string candidate = "1.2.0\n";
    std::string installed = "1.1.0\n";
    std::string fail_operation;

    int run(const std::vector<std::string> &arguments)
    {
        const std::string operation = arguments[0] == "dpkg" && arguments.size() > 1
            ? arguments[0] + ":" + arguments[1] : arguments[0];
        if (!fail_operation.empty() && operation == fail_operation) return 9;
        return 0;
    }

    int capture(const std::vector<std::string> &arguments, std::string &output)
    {
        if (arguments[0] == "cat") output = hash + "  applaunch_arm64.deb\n";
        else if (arguments[0] == "sha256sum") output = actual;
        else if (arguments[0] == "dpkg-query") output = installed;
        else if (arguments.size() >= 4 && arguments[3] == "Package") output = package;
        else if (arguments.size() >= 4 && arguments[3] == "Architecture") output = architecture;
        else if (arguments.size() >= 4 && arguments[3] == "Version") output = candidate;
        else return -1;
        return 0;
    }
};

cp0::update::Result execute(Fixture &fixture)
{
    return cp0::update::launcher(
        [&](const auto &arguments) { return fixture.run(arguments); },
        [&](const auto &arguments, auto &output) { return fixture.capture(arguments, output); });
}

} // namespace

int main()
{
    std::string hash;
    assert(cp0::update::checksum_value(std::string(64, 'a') + "  file\n", hash));
    assert(hash == std::string(64, 'a'));
    assert(!cp0::update::checksum_value("not-a-checksum", hash));

    Fixture fixture;
    auto result = execute(fixture);
    assert(result.code == 0 && result.stage == "submitted");

    fixture.actual = std::string(64, 'b');
    result = execute(fixture);
    assert(result.code != 0 && result.stage == "checksum");
    fixture.actual = std::string(64, 'a');

    fixture.package = "other\n";
    result = execute(fixture);
    assert(result.stage == "package-name");
    fixture.package = "applaunch\n";

    fixture.architecture = "amd64\n";
    result = execute(fixture);
    assert(result.stage == "architecture");
    fixture.architecture = "arm64\n";

    fixture.fail_operation = "dpkg:--compare-versions";
    result = execute(fixture);
    assert(result.stage == "version-not-newer");
    fixture.fail_operation.clear();

    fixture.fail_operation = "systemctl";
    result = execute(fixture);
    assert(result.stage == "submit");
}
