#include "account_migration.h"

namespace launch_wizard {
namespace {

AccountMigrationStage next_stage(AccountMigrationStage stage)
{
    return static_cast<AccountMigrationStage>(static_cast<int>(stage) + 1);
}

std::string stage_error(AccountMigrationStage stage, const std::string &detail)
{
    std::string message = "Account migration failed at ";
    message += account_migration_stage_name(stage);
    if (!detail.empty()) message += ": " + detail;
    return message;
}

} // namespace

const char *account_migration_stage_name(AccountMigrationStage stage)
{
    switch (stage) {
    case AccountMigrationStage::RenameLogin: return "rename login";
    case AccountMigrationStage::MoveHome: return "move home";
    case AccountMigrationStage::RenameGroup: return "rename group";
    case AccountMigrationStage::UpdateSubuid: return "update subuid";
    case AccountMigrationStage::UpdateSubgid: return "update subgid";
    case AccountMigrationStage::UpdateSudoers: return "update sudoers";
    case AccountMigrationStage::SetPassword: return "set password";
    case AccountMigrationStage::Complete: return "complete";
    }
    return "unknown stage";
}

std::string run_account_migration(AccountMigrationRecord &record,
                                  const AccountMigrationOps &ops)
{
    if (record.source_user.empty() || record.target_user.empty())
        return "Account migration journal is invalid";
    if (!ops.save || !ops.complete || !ops.execute || !ops.clear)
        return "Account migration operations are incomplete";

    std::string detail;
    if (!ops.save(record, detail))
        return stage_error(record.stage, detail.empty() ? "cannot save checkpoint" : detail);

    while (record.stage != AccountMigrationStage::Complete) {
        const AccountMigrationStage stage = record.stage;
        if (!ops.complete(record, stage)) {
            detail.clear();
            if (!ops.execute(record, stage, detail))
                return stage_error(stage, detail);
            if (!ops.complete(record, stage))
                return stage_error(stage, "post-condition was not reached");
        }

        record.stage = next_stage(stage);
        detail.clear();
        if (!ops.save(record, detail))
            return stage_error(stage, detail.empty() ? "cannot save checkpoint" : detail);
    }

    detail.clear();
    if (!ops.clear(detail))
        return stage_error(AccountMigrationStage::Complete,
                           detail.empty() ? "cannot clear checkpoint" : detail);
    return {};
}

} // namespace launch_wizard
