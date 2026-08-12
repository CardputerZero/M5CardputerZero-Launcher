#include "zclaw_local_cli_service.h"

#include "zclaw_process_executor.h"

namespace zclaw {

CliService make_local_cli_service(
    const std::shared_ptr<ProcessExecutor> &processes)
{
    if (!processes)
        return CliService();
    return CliService(
        [processes](const CliService::Command &command) {
            return processes->run(command);
        },
        [processes](unsigned int seconds) { processes->wait(seconds); },
        [processes](const CliService::Command &command,
                    const std::string &secret) {
            return processes->run_with_secret_input(command, secret);
        });
}

}  // namespace zclaw
