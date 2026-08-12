#ifndef LAUNCH_WIZARD_ACCOUNT_MIGRATION_H
#define LAUNCH_WIZARD_ACCOUNT_MIGRATION_H

#include <functional>
#include <string>

namespace launch_wizard {

enum class AccountMigrationStage {
    RenameLogin = 0,
    MoveHome,
    RenameGroup,
    UpdateSubuid,
    UpdateSubgid,
    UpdateSudoers,
    SetPassword,
    Complete,
};

struct AccountMigrationRecord {
    std::string source_user;
    std::string target_user;
    AccountMigrationStage stage = AccountMigrationStage::RenameLogin;
    bool rename_group = false;
    bool update_subuid = false;
    bool update_subgid = false;
    bool update_sudoers = false;
};

struct AccountMigrationOps {
    std::function<bool(const AccountMigrationRecord &, std::string &)> save;
    std::function<bool(const AccountMigrationRecord &, AccountMigrationStage)> complete;
    std::function<bool(const AccountMigrationRecord &, AccountMigrationStage, std::string &)> execute;
    std::function<bool(std::string &)> clear;
};

const char *account_migration_stage_name(AccountMigrationStage stage);
std::string run_account_migration(AccountMigrationRecord &record,
                                  const AccountMigrationOps &ops);

} // namespace launch_wizard

#endif
