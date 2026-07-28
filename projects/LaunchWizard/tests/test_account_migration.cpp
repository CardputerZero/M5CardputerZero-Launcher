#include "account_migration.h"

#include <array>
#include <string>

using namespace launch_wizard;

namespace {

bool expect(bool condition)
{
    return condition;
}

AccountMigrationRecord make_record()
{
    AccountMigrationRecord value;
    value.source_user = "pi";
    value.target_user = "alice";
    value.rename_group = true;
    value.update_subuid = true;
    value.update_subgid = true;
    value.update_sudoers = true;
    return value;
}

} // namespace

bool test_account_migration()
{
    bool passed = true;
    constexpr int stage_count = static_cast<int>(AccountMigrationStage::Complete);
    for (int failure = 0; failure < stage_count; ++failure) {
        AccountMigrationRecord value = make_record();
        std::array<bool, stage_count> completed{};
        bool failed_once = false;
        bool cleared = false;
        AccountMigrationRecord journal;
        AccountMigrationOps ops;
        ops.save = [&](const AccountMigrationRecord &saved, std::string &) {
            journal = saved;
            return true;
        };
        ops.complete = [&](const AccountMigrationRecord &, AccountMigrationStage stage) {
            return completed[static_cast<int>(stage)];
        };
        ops.execute = [&](const AccountMigrationRecord &, AccountMigrationStage stage,
                          std::string &error) {
            const int index = static_cast<int>(stage);
            if (index == failure && !failed_once) {
                failed_once = true;
                error = "injected";
                return false;
            }
            completed[index] = true;
            return true;
        };
        ops.clear = [&](std::string &) { cleared = true; return true; };

        passed &= expect(!run_account_migration(value, ops).empty());
        passed &= expect(journal.stage == static_cast<AccountMigrationStage>(failure));
        value = journal;
        passed &= expect(run_account_migration(value, ops).empty());
        passed &= expect(cleared);
        for (bool done : completed) passed &= expect(done);
    }

    AccountMigrationRecord value = make_record();
    std::array<bool, stage_count> completed{};
    int save_calls = 0;
    AccountMigrationRecord durable = value;
    AccountMigrationOps ops;
    ops.save = [&](const AccountMigrationRecord &saved, std::string &error) {
        ++save_calls;
        if (save_calls == 3) { error = "disk full"; return false; }
        durable = saved;
        return true;
    };
    ops.complete = [&](const AccountMigrationRecord &, AccountMigrationStage stage) {
        return completed[static_cast<int>(stage)];
    };
    ops.execute = [&](const AccountMigrationRecord &, AccountMigrationStage stage, std::string &) {
        completed[static_cast<int>(stage)] = true;
        return true;
    };
    ops.clear = [](std::string &) { return true; };
    const std::string error = run_account_migration(value, ops);
    passed &= expect(error.find("disk full") != std::string::npos);
    passed &= expect(completed[0] && completed[1]);
    passed &= expect(durable.stage == AccountMigrationStage::MoveHome);
    return passed;
}
