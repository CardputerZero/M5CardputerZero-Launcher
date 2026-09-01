#include "wizard_service.h"
#include "account_migration.h"
#include "apply_checkpoint.h"
#include "command_runner.h"
#include "first_boot_policy.h"
#include "service_handoff.h"

#include "global_config.h"
#include "cp0_lvgl_app.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <grp.h>
#include <fcntl.h>
#include <pwd.h>
#include <shadow.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char **environ;

#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifndef LAUNCH_WIZARD_DRY_RUN
#if defined(CONFIG_V9_5_LV_USE_SDL)
#define LAUNCH_WIZARD_DRY_RUN 1
#else
#define LAUNCH_WIZARD_DRY_RUN 0
#endif
#endif

#if !LAUNCH_WIZARD_DRY_RUN
#include <crypt.h>
#endif

namespace launch_wizard {

constexpr uid_t kDefaultUserUid = 1000;
constexpr const char *kFirstBootWizardUser = "rpi-first-boot-wizard";
// Factory account baked into every image by pi-gen (build.yml FIRST_USER_NAME /
// FIRST_USER_PASS). Keep in sync with the pi-gen workflow configuration.
constexpr const char *kFactoryDefaultUser = "pi";
constexpr const char *kFactoryDefaultPassword = "raspberry";
#if LAUNCH_WIZARD_DRY_RUN
constexpr const char *kAccountJournalDir = "/tmp/LaunchWizard-dry-run";
constexpr const char *kAccountJournalPath =
    "/tmp/LaunchWizard-dry-run/account-migration.state";
#else
constexpr const char *kAccountJournalDir = "/var/lib/LaunchWizard";
constexpr const char *kAccountJournalPath =
    "/var/lib/LaunchWizard/account-migration.state";
#endif

// Dropped by APPLaunch's "Run Setup Wizard" settings entry (re-run on demand).
constexpr const char *kRearmOobeMarker = "/var/lib/applaunch/run-oobe";
// Baked into every factory image by pi-gen; removed once first boot finishes.
constexpr const char *kFactoryOobeMarker = "/var/lib/LaunchWizard/run-oobe";
// One-shot keyboard tutorial marker baked into the image by pi-gen.
constexpr const char *kKeyboardGuideMarker =
    "/var/lib/LaunchWizard/run-keyboard-guide";
constexpr const char *kKeyboardGuideBinary =
    "/usr/share/APPLaunch/bin/M5CardputerZero-Keyboard-Guide";

void remove_oobe_markers(std::string *first_error = nullptr)
{
    static const char *marker_paths[] = {kRearmOobeMarker, kFactoryOobeMarker};
    for (const char *path : marker_paths) {
        if (remove(path) != 0 && errno != ENOENT && first_error &&
            first_error->empty())
            *first_error = std::string("Failed to remove OOBE marker: ") +
                           strerror(errno);
    }
}

void print_command(const std::vector<std::string> &args, const std::string *stdin_text)
{
    printf("LaunchWizard dry-run:");
    for (const std::string &arg : args) {
        bool needs_quotes = arg.empty();
        for (char ch : arg) {
            if (isspace(static_cast<unsigned char>(ch)) || ch == '\'' || ch == '"' || ch == '\\') {
                needs_quotes = true;
                break;
            }
        }
        if (!needs_quotes) {
            printf(" %s", arg.c_str());
            continue;
        }
        printf(" '");
        for (char ch : arg) {
            if (ch == '\'')
                printf("'\\''");
            else
                putchar(ch);
        }
        putchar('\'');
    }
    if (stdin_text)
        printf(" <stdin:%zu bytes>", stdin_text->size());
    putchar('\n');
    fflush(stdout);
}

thread_local std::function<bool()> command_cancelled;

CommandResult run_command(const std::vector<std::string> &args,
                          const std::string *stdin_text = nullptr)
{
#if LAUNCH_WIZARD_DRY_RUN
    print_command(args, stdin_text);
    return {};
#else
    CommandOptions options;
    options.cancelled = command_cancelled;
    return run_command_process(args, stdin_text, options);
#endif
}

std::string trim_copy(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && isspace(static_cast<unsigned char>(value[start])))
        ++start;
    size_t end = value.size();
    while (end > start && isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(start, end - start);
}

void read_lightdm_autologin_file(const char *path, std::string &autologin_user)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return;

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), fp)) {
        std::string line = trim_copy(buffer);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos)
            line = trim_copy(line.substr(0, comment));
        size_t sep = line.find('=');
        if (sep == std::string::npos)
            continue;
        std::string key = trim_copy(line.substr(0, sep));
        if (key == "autologin-user")
            autologin_user = trim_copy(line.substr(sep + 1));
    }
    fclose(fp);
}

std::string lightdm_autologin_user()
{
#if LAUNCH_WIZARD_DRY_RUN
    return kFirstBootWizardUser;
#else
    std::string autologin_user;
    read_lightdm_autologin_file("/etc/lightdm/lightdm.conf", autologin_user);

    glob_t matches;
    memset(&matches, 0, sizeof(matches));
    if (glob("/etc/lightdm/lightdm.conf.d/*.conf", 0, nullptr, &matches) == 0) {
        for (size_t i = 0; i < matches.gl_pathc; ++i)
            read_lightdm_autologin_file(matches.gl_pathv[i], autologin_user);
    }
    globfree(&matches);
    return autologin_user;
#endif
}

bool piwiz_autostart_enabled()
{
#if LAUNCH_WIZARD_DRY_RUN
    return true;
#else
    FILE *fp = fopen("/etc/xdg/autostart/piwiz.desktop", "r");
    if (!fp)
        return false;

    bool hidden = false;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), fp)) {
        std::string line = trim_copy(buffer);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos)
            line = trim_copy(line.substr(0, comment));
        size_t sep = line.find('=');
        if (sep == std::string::npos)
            continue;
        std::string key = trim_copy(line.substr(0, sep));
        std::string value = trim_copy(line.substr(sep + 1));
        if (key == "Hidden" && value == "true")
            hidden = true;
    }
    fclose(fp);
    return !hidden;
#endif
}

bool command_ok(const std::vector<std::string> &args, std::string &error)
{
    CommandResult result = run_command(args);
    if (result.code == 0)
        return true;
    error = args.empty() ? "command failed" : args[0] + " failed";
    if (!result.output.empty())
        error += ": " + result.output;
    return false;
}

bool uid_has_processes(uid_t uid, bool &has_processes, std::string &error)
{
    CommandResult result = run_command(
        {"pgrep", "-u", std::to_string(static_cast<unsigned int>(uid))});
    if (result.code == 0) {
        has_processes = true;
        return true;
    }
    if (result.code == 1) {
        has_processes = false;
        return true;
    }
    error = result.output.empty() ? "Failed to inspect user processes"
                                  : result.output;
    return false;
}

bool stop_user_sessions_for_rename(uid_t uid, std::string &error)
{
    // pi-gen autologs UID 1000 into tty1 while LaunchWizard runs as root.
    // Stopping only user@1000.service leaves that login shell alive, and
    // usermod correctly refuses to rename a user that still owns processes.
    if (!command_ok({"systemctl", "stop", "getty@tty1.service"}, error))
        return false;

    CommandResult terminated = run_command(
        {"loginctl", "terminate-user",
         std::to_string(static_cast<unsigned int>(uid))});

    for (int attempt = 0; attempt < 50; ++attempt) {
        bool has_processes = false;
        if (!uid_has_processes(uid, has_processes, error))
            return false;
        if (!has_processes)
            return true;
        usleep(100 * 1000);
    }

    error = terminated.output.empty()
        ? "UID 1000 still owns processes after terminating login sessions"
        : "Failed to terminate UID 1000 login sessions: " + terminated.output;
    return false;
}

// ---------------------------------------------------------------------------
// Validation helpers (preserved).
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
bool user_exists(const std::string &name)
{
#if LAUNCH_WIZARD_DRY_RUN
    (void)name;
    return false;
#else
    return getpwnam(name.c_str()) != nullptr;
#endif
}

bool uid_exists(uid_t uid)
{
#if LAUNCH_WIZARD_DRY_RUN
    (void)uid;
    return false;
#else
    return getpwuid(uid) != nullptr;
#endif
}

// Name of the existing UID 1000 user (pi-gen always pre-creates one, e.g. "pi").
std::string current_first_user()
{
#if LAUNCH_WIZARD_DRY_RUN
    return "pi";
#else
    struct passwd *pw = getpwuid(kDefaultUserUid);
    return (pw && pw->pw_name) ? std::string(pw->pw_name) : std::string();
#endif
}

std::string current_first_group()
{
#if LAUNCH_WIZARD_DRY_RUN
    return "pi";
#else
    struct group *gr = getgrgid(kDefaultUserUid);
    return (gr && gr->gr_name) ? std::string(gr->gr_name) : std::string();
#endif
}

// True if the UID 1000 user has a real (unlocked) password set. This mirrors
// pi-gen's own rule ("FIRST_USER_NAME=pi with no FIRST_USER_PASS launches the
// wizard"): a factory image leaves the user --disabled-login (shadow "!"/"*"),
// whereas a configured one (Imager / a baked password / a finished OOBE) stores
// a real "$..." hash.
bool first_user_has_password()
{
#if LAUNCH_WIZARD_DRY_RUN
    return false;
#else
    struct passwd *pw = getpwuid(kDefaultUserUid);
    if (!pw || !pw->pw_name)
        return false;
    struct spwd *sp = getspnam(pw->pw_name);
    if (!sp || !sp->sp_pwdp)
        return false;
    return sp->sp_pwdp[0] == '$';
#endif
}

// True while the UID 1000 user is still named after the pi-gen factory
// default. userconf/Imager renames the user in passwd, so a different name
// alone proves the device was provisioned.
bool first_user_has_factory_name()
{
#if LAUNCH_WIZARD_DRY_RUN
    return true;
#else
    struct passwd *pw = getpwuid(kDefaultUserUid);
    return pw && pw->pw_name && strcmp(pw->pw_name, kFactoryDefaultUser) == 0;
#endif
}

// True while the UID 1000 account still carries the factory credentials that
// pi-gen bakes into the image (FIRST_USER_NAME=pi / FIRST_USER_PASS=raspberry).
// Verification follows crypt(5): hash the candidate with the stored hash as
// the setting string; a byte-identical result means the password matches. Any
// crypt failure yields NULL or a "*" failure token that never compares equal,
// so errors safely count as "not factory".
bool first_user_has_factory_credentials()
{
#if LAUNCH_WIZARD_DRY_RUN
    return false;
#else
    struct passwd *pw = getpwuid(kDefaultUserUid);
    if (!pw || !pw->pw_name || strcmp(pw->pw_name, kFactoryDefaultUser) != 0)
        return false;
    struct spwd *sp = getspnam(pw->pw_name);
    if (!sp || !sp->sp_pwdp || sp->sp_pwdp[0] != '$')
        return false;
    // struct crypt_data is ~32 KiB; keep it off the stack. should_run() is
    // called once from the single-threaded startup path.
    static struct crypt_data data;
    memset(&data, 0, sizeof(data));
    const char *hash = crypt_r(kFactoryDefaultPassword, sp->sp_pwdp, &data);
    return hash && hash[0] == '$' && strcmp(hash, sp->sp_pwdp) == 0;
#endif
}

std::vector<std::string> initial_user_groups()
{
#if LAUNCH_WIZARD_DRY_RUN
    return {"adm", "dialout", "cdrom", "sudo", "audio", "video", "plugdev",
            "games", "users", "input", "render", "netdev", "gpio", "i2c", "spi"};
#else
    static const char *candidates[] = {
        "adm", "dialout", "cdrom", "sudo", "audio", "video", "plugdev",
        "games", "users", "input", "render", "netdev", "gpio", "i2c", "spi",
    };
    std::vector<std::string> groups;
    for (const char *group : candidates) {
        if (getgrnam(group))
            groups.emplace_back(group);
    }
    return groups;
#endif
}

std::string join_groups(const std::vector<std::string> &groups)
{
    std::string joined;
    for (const std::string &group : groups) {
        if (!joined.empty())
            joined += ",";
        joined += group;
    }
    return joined;
}

bool create_user(const std::string &user, std::string &error)
{
    if (user_exists(user)) {
        error = "Target user already exists";
        return false;
    }
    if (uid_exists(kDefaultUserUid)) {
        error = "UID 1000 already exists";
        return false;
    }

    std::vector<std::string> args = {
        "useradd",
        "--create-home",
        "--user-group",
        "--uid", std::to_string(kDefaultUserUid),
        "--shell", "/bin/bash",
    };
    std::vector<std::string> groups = initial_user_groups();
    if (!groups.empty()) {
        args.push_back("--groups");
        args.push_back(join_groups(groups));
    }
    args.push_back(user);
    return command_ok(args, error);
}

bool set_user_password(const std::string &user, const std::string &password, std::string &error)
{
    std::string chpasswd_input = user + ":" + password + "\n";
    CommandResult pass_result = run_command({"chpasswd"}, &chpasswd_input);
    if (pass_result.code == 0)
        return true;

    error = "chpasswd failed";
    if (!pass_result.output.empty())
        error += ": " + pass_result.output;
    return false;
}

bool file_has_identity(const char *path, const std::string &user, char separator = ':')
{
    std::ifstream input(path);
    std::string line;
    const std::string prefix = user + separator;
    while (std::getline(input, line))
        if (line.rfind(prefix, 0) == 0)
            return true;
    return false;
}

bool account_user_is(const std::string &user)
{
    const passwd *entry = getpwnam(user.c_str());
    return entry && entry->pw_uid == kDefaultUserUid;
}

bool account_home_is(const AccountMigrationRecord &state)
{
    const passwd *entry = getpwnam(state.target_user.c_str());
    struct stat info {};
    const std::string home = "/home/" + state.target_user;
    if (!entry || entry->pw_uid != kDefaultUserUid ||
        std::string(entry->pw_dir ? entry->pw_dir : "") != home ||
        stat(home.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
        return false;
    if (state.source_user == state.target_user) return true;
    struct stat old_info {};
    return stat(("/home/" + state.source_user).c_str(), &old_info) != 0 && errno == ENOENT;
}

bool account_group_is(const std::string &group)
{
    const passwd *user = getpwnam(group.c_str());
    const ::group *entry = getgrnam(group.c_str());
    return user && entry && user->pw_gid == entry->gr_gid;
}

bool identity_migrated(const char *path, const AccountMigrationRecord &state,
                       char separator = ':')
{
    return file_has_identity(path, state.target_user, separator) &&
           (state.source_user == state.target_user ||
            !file_has_identity(path, state.source_user, separator));
}

std::string serialize_account_record(const AccountMigrationRecord &record)
{
    return "version=1\nsource=" + record.source_user +
           "\ntarget=" + record.target_user +
           "\nstage=" + std::to_string(static_cast<int>(record.stage)) +
           "\nrename_group=" + (record.rename_group ? "1" : "0") +
           "\nupdate_subuid=" + (record.update_subuid ? "1" : "0") +
           "\nupdate_subgid=" + (record.update_subgid ? "1" : "0") +
           "\nupdate_sudoers=" + (record.update_sudoers ? "1" : "0") + "\n";
}

bool parse_account_record(AccountMigrationRecord &record)
{
    std::ifstream input(kAccountJournalPath);
    if (!input) return false;
    std::string version, source, target, stage, rename_group;
    std::string update_subuid, update_subgid, update_sudoers;
    if (!std::getline(input, version) || !std::getline(input, source) ||
        !std::getline(input, target) || !std::getline(input, stage) ||
        !std::getline(input, rename_group) || !std::getline(input, update_subuid) ||
        !std::getline(input, update_subgid) || !std::getline(input, update_sudoers))
        return false;
    if (version != "version=1" || source.rfind("source=", 0) != 0 ||
        target.rfind("target=", 0) != 0 || stage.rfind("stage=", 0) != 0 ||
        rename_group.rfind("rename_group=", 0) != 0 ||
        update_subuid.rfind("update_subuid=", 0) != 0 ||
        update_subgid.rfind("update_subgid=", 0) != 0 ||
        update_sudoers.rfind("update_sudoers=", 0) != 0)
        return false;
    char *end = nullptr;
    const long stage_value = strtol(stage.c_str() + 6, &end, 10);
    if (!end || *end || stage_value < 0 ||
        stage_value > static_cast<long>(AccountMigrationStage::Complete))
        return false;
    record.source_user = source.substr(7);
    record.target_user = target.substr(7);
    record.stage = static_cast<AccountMigrationStage>(stage_value);
    record.rename_group = rename_group == "rename_group=1";
    record.update_subuid = update_subuid == "update_subuid=1";
    record.update_subgid = update_subgid == "update_subgid=1";
    record.update_sudoers = update_sudoers == "update_sudoers=1";
    return !record.source_user.empty() && !record.target_user.empty();
}

bool save_account_record(const AccountMigrationRecord &record, std::string &error)
{
    if (!command_ok({"install", "-d", "-m", "0700", kAccountJournalDir}, error))
        return false;
    const std::string temporary = std::string(kAccountJournalPath) + ".tmp." +
                                  std::to_string(static_cast<long long>(getpid()));
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        error = std::string("cannot create account checkpoint: ") + strerror(errno);
        return false;
    }
    const std::string data = serialize_account_record(record);
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = write(fd, data.data() + offset, data.size() - offset);
        if (count > 0) { offset += static_cast<size_t>(count); continue; }
        if (count < 0 && errno == EINTR) continue;
        error = std::string("cannot write account checkpoint: ") + strerror(errno);
        close(fd);
        unlink(temporary.c_str());
        return false;
    }
    if (fsync(fd) != 0 || close(fd) != 0 ||
        rename(temporary.c_str(), kAccountJournalPath) != 0) {
        error = std::string("cannot commit account checkpoint: ") + strerror(errno);
        unlink(temporary.c_str());
        return false;
    }
    const int dir_fd = open(kAccountJournalDir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd >= 0) { (void)fsync(dir_fd); close(dir_fd); }
    return true;
}

bool clear_account_record(std::string &error)
{
    if (unlink(kAccountJournalPath) != 0 && errno != ENOENT) {
        error = std::string("cannot remove account checkpoint: ") + strerror(errno);
        return false;
    }
    const int dir_fd = open(kAccountJournalDir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd >= 0) { (void)fsync(dir_fd); close(dir_fd); }
    return true;
}

// Provision the owner account. pi-gen always pre-creates the UID 1000 user
// (default "pi", --disabled-login). Mirroring the official userconf tool we
// rename that existing user to the chosen name and set its password instead of
// running useradd (which would collide on UID 1000). Returns the account UID, or
// 0 on failure with `error` populated.
uid_t configure_account(const std::string &new_user, const std::string &password,
                        std::string &error)
{
#if LAUNCH_WIZARD_DRY_RUN
    std::string input = new_user + ":" + password + "\n";
    run_command({"chpasswd"}, &input);
    error.clear();
    return kDefaultUserUid;
#else
    AccountMigrationRecord record;
    const bool journal_exists = access(kAccountJournalPath, F_OK) == 0;
    if (journal_exists && !parse_account_record(record)) {
        error = "Account migration checkpoint is damaged; recovery is required";
        return 0;
    }
    if (journal_exists && record.target_user != new_user) {
        error = "Finish account migration for " + record.target_user + " before changing username";
        return 0;
    }

    std::string old_user = current_first_user();
    if (!journal_exists && old_user.empty()) {
        // No pre-created first user (unusual image): create one at UID 1000.
        if (!create_user(new_user, error))
            return 0;
        old_user = new_user;
    }
    if (!journal_exists) {
        if (old_user != new_user && user_exists(new_user)) {
            error = "Target username already in use";
            return 0;
        }
        record.source_user = old_user;
        record.target_user = new_user;
        record.rename_group = current_first_group() == old_user;
        record.update_subuid = file_has_identity("/etc/subuid", old_user);
        record.update_subgid = file_has_identity("/etc/subgid", old_user);
        record.update_sudoers = file_has_identity(
            "/etc/sudoers.d/010_pi-nopasswd", old_user, ' ');
    }

    if (record.source_user != record.target_user &&
        !stop_user_sessions_for_rename(kDefaultUserUid, error))
        return 0;

    bool password_applied = false;
    AccountMigrationOps ops;
    ops.save = save_account_record;
    ops.clear = clear_account_record;
    ops.complete = [&](const AccountMigrationRecord &state, AccountMigrationStage stage) {
        switch (stage) {
        case AccountMigrationStage::RenameLogin:
            return account_user_is(state.target_user);
        case AccountMigrationStage::MoveHome:
            return account_home_is(state);
        case AccountMigrationStage::RenameGroup:
            return !state.rename_group || account_group_is(state.target_user);
        case AccountMigrationStage::UpdateSubuid:
            return !state.update_subuid || identity_migrated("/etc/subuid", state);
        case AccountMigrationStage::UpdateSubgid:
            return !state.update_subgid || identity_migrated("/etc/subgid", state);
        case AccountMigrationStage::UpdateSudoers:
            if (!state.update_sudoers) return true;
            if (!identity_migrated(
                    "/etc/sudoers.d/010_pi-nopasswd", state, ' '))
                return false;
            return run_command(
                {"visudo", "-cf", "/etc/sudoers.d/010_pi-nopasswd"}).code == 0;
        case AccountMigrationStage::SetPassword:
            return password_applied;
        case AccountMigrationStage::Complete:
            return true;
        }
        return false;
    };
    ops.execute = [&](const AccountMigrationRecord &state, AccountMigrationStage stage,
                      std::string &step_error) {
        switch (stage) {
        case AccountMigrationStage::RenameLogin:
            return command_ok({"usermod", "-l", state.target_user, state.source_user}, step_error);
        case AccountMigrationStage::MoveHome:
            return command_ok({"usermod", "-m", "-d", "/home/" + state.target_user,
                               state.target_user}, step_error);
        case AccountMigrationStage::RenameGroup:
            return !state.rename_group || command_ok(
                {"groupmod", "-n", state.target_user, state.source_user}, step_error);
        case AccountMigrationStage::UpdateSubuid:
            return !state.update_subuid || command_ok(
                {"sed", "-i", "s/^" + state.source_user + ":/" + state.target_user + ":/",
                 "/etc/subuid"}, step_error);
        case AccountMigrationStage::UpdateSubgid:
            return !state.update_subgid || command_ok(
                {"sed", "-i", "s/^" + state.source_user + ":/" + state.target_user + ":/",
                 "/etc/subgid"}, step_error);
        case AccountMigrationStage::UpdateSudoers:
            return !state.update_sudoers || command_ok(
                {"sed", "-i", "s/^" + state.source_user + " /" + state.target_user + " /",
                 "/etc/sudoers.d/010_pi-nopasswd"}, step_error);
        case AccountMigrationStage::SetPassword:
            password_applied = set_user_password(state.target_user, password, step_error);
            return password_applied;
        case AccountMigrationStage::Complete:
            return true;
        }
        return false;
    };
    error = run_account_migration(record, ops);
    if (!error.empty())
        return 0;
    return kDefaultUserUid;
#endif
}

std::string disable_piwiz(const std::string &user)
{
    std::string error;
    CommandResult killed = run_command({"pkill", "-x", "piwiz"});
    if (killed.code != 0 && killed.code != 1)
        return killed.output.empty() ? "Failed to stop piwiz" : killed.output;
    if (!command_ok({"rm", "-f",
                     "/etc/xdg/autostart/piwiz.desktop.dpkg-new",
                     "/etc/xdg/autostart/piwiz.desktop.dpkg-dist",
                     "/etc/xdg/autostart/piwiz.desktop.dpkg-old"}, error) ||
        !command_ok({"install", "-d", "-m", "0755", "/etc/xdg/autostart"}, error))
        return error;
    const std::string hidden_entry =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=piwiz\n"
        "Hidden=true\n";
    CommandResult written = run_command(
        {"tee", "/etc/xdg/autostart/piwiz.desktop"}, &hidden_entry);
    if (written.code != 0)
        return written.output.empty() ? "Failed to disable piwiz autostart" : written.output;
    if (!command_ok({"chmod", "0644", "/etc/xdg/autostart/piwiz.desktop"}, error))
        return error;

    const std::string user_autostart = "/home/" + user + "/.config/autostart";
    const std::string user_piwiz = user_autostart + "/piwiz.desktop";
    if (!command_ok({"install", "-d", "-m", "0755", "-o", user, "-g", user,
                     user_autostart}, error))
        return error;
    written = run_command({"tee", user_piwiz}, &hidden_entry);
    if (written.code != 0)
        return written.output.empty() ? "Failed to disable user piwiz autostart" : written.output;
    if (!command_ok({"chown", user + ":" + user, user_piwiz}, error) ||
        !command_ok({"chmod", "0644", user_piwiz}, error))
        return error;
    return {};
}

std::string configure_desktop_startup(const std::string &user)
{
    std::string warning;

    warning = disable_piwiz(user);
    if (!warning.empty()) return warning;

    if (!command_ok({"systemctl", "set-default", "graphical.target"}, warning))
        return warning;

    CommandResult raspi_config = run_command({"raspi-config", "nonint", "do_boot_behaviour", "B4"});
    if (raspi_config.code != 0) {
        warning = "raspi-config desktop autologin failed";
        if (!raspi_config.output.empty())
            warning += ": " + raspi_config.output;
        return warning;
    }

    if (!command_ok({"install", "-d", "-m", "0755", "/etc/lightdm/lightdm.conf.d"},
                    warning))
        return warning;
    const std::string lightdm_conf =
        "[Seat:*]\n"
        "autologin-user=" + user + "\n"
        "autologin-user-timeout=0\n";
    CommandResult write_conf = run_command(
        {"tee", "/etc/lightdm/lightdm.conf.d/50-launchwizard-autologin.conf"},
        &lightdm_conf);
    if (write_conf.code != 0) {
        warning = "LightDM autologin config failed";
        if (!write_conf.output.empty())
            warning += ": " + write_conf.output;
        return warning;
    }
    if (!command_ok({"chmod", "0644",
                     "/etc/lightdm/lightdm.conf.d/50-launchwizard-autologin.conf"}, warning))
        return warning;

    return warning;
}

std::string enable_applaunch_service(const std::string &user, unsigned int uid)
{
    return enable_applaunch_after_reboot(
        user, uid,
        [](const std::vector<std::string> &args) {
            CommandResult result = run_command(args);
            return HandoffCommandResult{result.code, result.output};
        });
}

// ---------------------------------------------------------------------------
// System integration -- OOBE steps (timezone / hostname / wifi / ssh).
// ---------------------------------------------------------------------------
std::string apply_timezone(const std::string &timezone)
{
    if (timezone.empty())
        return "Timezone is required";
    CommandResult result = run_command({"timedatectl", "set-timezone", timezone});
    return result.code == 0 ? std::string() :
        (result.output.empty() ? "Failed to set timezone" : result.output);
}

std::string apply_hostname(const std::string &hostname)
{
    if (hostname.empty())
        return "Hostname is required";
    CommandResult result = run_command({"hostnamectl", "set-hostname", hostname});
    if (result.code != 0)
        return result.output.empty() ? "Failed to set hostname" : result.output;
    const std::string hosts =
        "127.0.0.1\tlocalhost\n"
        "127.0.1.1\t" + hostname + "\n";
    result = run_command({"tee", "/etc/hostname"}, &hostname);
    if (result.code != 0)
        return result.output.empty() ? "Failed to write /etc/hostname" : result.output;
    result = run_command({"tee", "/etc/hosts.launchwizard"}, &hosts);
    return result.code == 0 ? std::string() :
        (result.output.empty() ? "Failed to write hostname hosts file" : result.output);
}

std::string WizardService::set_manual_time(const std::string &date,
                                           const std::string &time)
{
    if (date.empty() || time.empty())
        return "Date and time are required";
    CommandResult result = run_command({"date", "-s", date + " " + time + ":00"});
    if (result.code != 0)
        return result.output.empty() ? "Failed to set system time" : result.output;
    return "";
}

std::string apply_ssh(bool enabled)
{
    CommandResult configured = run_command(
        {"raspi-config", "nonint", "do_ssh", enabled ? "0" : "1"});
    if (configured.code != 0)
        return configured.output.empty()
            ? std::string("Failed to configure SSH")
            : std::string("Failed to configure SSH: ") + configured.output;

    CommandResult state = run_command({
        "systemctl", enabled ? "is-active" : "is-enabled", "--quiet", "ssh.service",
    });
    if (enabled && state.code != 0)
        return state.output.empty()
            ? std::string("SSH was enabled but did not become active")
            : std::string("SSH was enabled but did not become active: ") + state.output;
    if (!enabled && state.code == 0)
        return "SSH remained enabled after it was disabled";
    return {};
}

std::string WizardService::connect_wifi(const std::string &ssid, const std::string &password,
                                        std::string *connected_ip, bool hidden)
{
    if (ssid.empty())
        return "";
#if LAUNCH_WIZARD_DRY_RUN
    print_command({hidden ? "nmcli-hidden-wifi-connect" : "cp0_wifi_connect", ssid,
                   password.empty() ? "<saved/open>" : "<password>"}, nullptr);
    if (connected_ip)
        *connected_ip = "192.168.1.100";
#else
    // Raspberry Pi Imager can provision and activate this profile before the
    // first-boot wizard starts. Treat selecting that same SSID as success;
    // asking NetworkManager to activate it again can return a transient error
    // even though the device is already online.
    cp0_wifi_status_t current{};
    if (cp0_wifi_status_read(&current) == 0 && current.connected &&
        std::string(current.ssid) == ssid) {
        if (connected_ip)
            *connected_ip = current.ip;
        return "";
    }
    if (cp0_wifi_radio_set_enabled(1) != 0)
        return "Wi-Fi radio could not be enabled";
    if (hidden) {
        std::vector<std::string> args = {
            "nmcli", "--wait", "20", "dev", "wifi", "connect", ssid,
        };
        if (!password.empty())
            args.insert(args.end(), {"password", password});
        args.insert(args.end(), {"hidden", "yes"});
        const CommandResult result = run_command(args);
        if (result.code != 0)
            return result.output.empty() ? "Hidden Wi-Fi connect failed" : result.output;
    } else {
        const int connect_result = cp0_wifi_connect(
            ssid.c_str(), password.empty() ? nullptr : password.c_str());
        if (connect_result != 0) {
            switch (connect_result) {
            case CP0_WIFI_ERROR_AUTH: return "Incorrect Wi-Fi password";
            case CP0_WIFI_ERROR_NOT_FOUND: return "Wi-Fi network is no longer available";
            case CP0_WIFI_ERROR_IP_CONFIG: return "Wi-Fi connected but IP setup failed";
            case CP0_WIFI_ERROR_RADIO_OFF: return "Wi-Fi radio is disabled";
            case CP0_WIFI_ERROR_TIMEOUT: return "Wi-Fi connection timed out; retry";
            default: return "Network service could not connect; retry";
            }
        }
    }

    // Hidden connections are created directly through nmcli, while
    // cp0_wifi_status_read() serves a cache refreshed every three seconds.
    // Give that cache time to observe the successful NetworkManager change.
    cp0_wifi_status_t status{};
    const auto status_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(6);
    bool active = false;
    do {
        status = {};
        active = cp0_wifi_status_read(&status) == 0 && status.connected &&
                 ssid == status.ssid;
        if (active)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    } while (std::chrono::steady_clock::now() < status_deadline);
    if (!active)
        return "Wi-Fi did not become active";
    if (connected_ip)
        *connected_ip = status.ip;
#endif
    return "";
}


std::string apply_ethernet(const WizardModel &g)
{
    run_command({"nmcli", "connection", "delete", "LaunchWizard-eth0"});
    std::vector<std::string> args = {
        "nmcli", "connection", "add", "type", "ethernet", "ifname", "eth0",
        "con-name", "LaunchWizard-eth0", "ipv4.method",
        g.ethernet_dhcp ? "auto" : "manual",
    };
    if (!g.ethernet_dhcp) {
        args.insert(args.end(), {"ipv4.addresses", g.ethernet_address,
                                 "ipv4.gateway", g.ethernet_gateway});
        if (!g.ethernet_dns.empty())
            args.insert(args.end(), {"ipv4.dns", g.ethernet_dns});
    }
    CommandResult add = run_command(args);
    if (add.code != 0)
        return add.output.empty() ? "Ethernet configuration failed" : add.output;
    CommandResult up = run_command({"nmcli", "connection", "up", "LaunchWizard-eth0"});
    return up.code == 0 ? "" : (up.output.empty() ? "Ethernet activation failed" : up.output);
}

WifiScanResult WizardService::scan_wifi()
{
    WifiScanResult result;
#if LAUNCH_WIZARD_DRY_RUN
    result.networks.push_back({"Studio_2.4G", 90, "WPA2"});
    result.networks.push_back({"CardputerLab", 70, ""});
    result.networks.push_back({"Home-5G", 45, "WPA3"});
#else
    if (cp0_wifi_radio_set_enabled(1) != 0) {
        result.error = CP0_WIFI_ERROR_RADIO_OFF;
        return result;
    }

    cp0_wifi_ap_t access_points[CP0_WIFI_AP_MAX]{};
    const int count = cp0_wifi_scan(access_points, CP0_WIFI_AP_MAX);
    if (count < 0) {
        result.error = count;
        return result;
    }
    for (int index = 0; index < count && index < CP0_WIFI_AP_MAX; ++index) {
        const cp0_wifi_ap_t &access_point = access_points[index];
        if (access_point.ssid[0] != '\0')
            result.networks.push_back(
                {access_point.ssid, access_point.signal, access_point.security});
    }
#endif
    return result;
}

WifiConnectionStatus WizardService::read_wifi_status()
{
    WifiConnectionStatus result;
#if !LAUNCH_WIZARD_DRY_RUN
    cp0_wifi_status_t status{};
    if (cp0_wifi_status_read(&status) == 0) {
        result.available = true;
        result.connected = status.connected != 0;
        result.ssid = status.ssid;
        result.ip = status.ip;
    }
#endif
    return result;
}

// ---------------------------------------------------------------------------
// Full apply (runs on a worker thread when the user confirms on the Done page).
// ---------------------------------------------------------------------------
std::string WizardService::apply(
    const WizardModel &g,
    const std::function<void(const ProgressEvent &)> &progress,
    const std::function<bool()> &cancelled)
{
    const std::string timezone = g.current_timezone().name;
    const std::string hostname = g.hostname;
    const std::string username = g.username;
    const std::string password = g.password;
    const bool network_skipped = g.network_skipped;
    const std::string wifi_ssid = g.wifi_ssid;
    const std::string wifi_password = g.wifi_password;
    const bool wifi_hidden = g.wifi_hidden;
    const bool ssh_enabled = g.ssh_enabled;

#if !LAUNCH_WIZARD_DRY_RUN
    if (geteuid() != 0)
        return "LaunchWizard must run as root";
#endif

    struct CancelScope {
        ~CancelScope() { command_cancelled = {}; }
    } cancel_scope;
    command_cancelled = cancelled;
    const auto best_effort = [](const char *label, std::function<std::string()> execute) {
        return ApplyStep{label, [label, execute = std::move(execute)] {
            const std::string error = execute();
            if (!error.empty())
                fprintf(stderr, "LaunchWizard: %s failed: %s; continuing setup\n",
                        label, error.c_str());
            return std::string{};
        }};
    };
    std::vector<ApplyStep> steps = {
        best_effort("Setting timezone...", [timezone] {
            const std::string error = apply_timezone(timezone);
            return error.empty() ? error : "Timezone failed: " + error;
        }),
        best_effort("Setting hostname...", [hostname] {
            const std::string error = apply_hostname(hostname);
            return error.empty() ? error : "Hostname failed: " + error;
        }),
        best_effort("Configuring network...", [&g, network_skipped, wifi_ssid,
                                                wifi_password, wifi_hidden] {
            if (!network_skipped && g.use_ethernet)
                return apply_ethernet(g);
            if (!network_skipped && !g.wifi_connected && !wifi_ssid.empty())
                return WizardService::connect_wifi(
                    wifi_ssid, wifi_password, nullptr, wifi_hidden);
            return std::string{};
        }),
        best_effort("Configuring SSH...", [ssh_enabled] {
            return apply_ssh(ssh_enabled);
        }),
        // The account migration has its own sub-step journal. The outer
        // checkpoint advances only after that migration is fully complete.
        {"Configuring user account...", [username, password] {
            std::string error;
            return configure_account(username, password, error) == 0 ? error : std::string{};
        }},
        {"Configuring desktop login...", [username] {
            return configure_desktop_startup(username);
        }},
        {"Enabling APPLaunch service...", [username] {
            return enable_applaunch_service(username, kDefaultUserUid);
        }},
        {"Finalizing configuration...", [] {
            CommandResult disabled =
                run_command({"systemctl", "disable", "LaunchWizard.service"});
            if (disabled.code != 0)
                return disabled.output.empty()
                    ? std::string("Failed to disable LaunchWizard.service")
                    : disabled.output;

            std::string error;
            remove_oobe_markers(&error);
            if (!error.empty())
                return error;

#if !LAUNCH_WIZARD_DRY_RUN
            sync();
            sync();
            sync();
#endif
            return std::string{};
        }},
    };

    const ApplyCheckpointStore checkpoint(default_apply_checkpoint_path());
    return run_apply_steps(
        wizard_configuration_fingerprint(g), steps, checkpoint,
        [&progress](int step, int total, const std::string &label) {
            if (progress) progress({step, total, label});
        },
        cancelled);
}

std::string WizardService::reboot()
{
#if !LAUNCH_WIZARD_DRY_RUN
    if (geteuid() != 0) return "LaunchWizard must run as root";
#endif
    CommandResult result = run_command({"systemctl", "reboot"});
    return result.code == 0 ? std::string() :
        (result.output.empty() ? "Failed to restart device" : result.output);
}

}  // namespace launch_wizard

bool launch_wizard::WizardService::should_run()
{
#if LAUNCH_WIZARD_DRY_RUN
    // In the SDL emulator always show the OOBE so it can be developed/previewed.
    return true;
#else
    FirstBootState state;
    // APPLaunch's "Run Setup Wizard" settings entry drops this file and
    // reboots, letting an already-configured device replay the OOBE on demand.
    // apply_all() clears it on completion, so the wizard still runs exactly
    // once.
    state.rearm_marker = access(kRearmOobeMarker, F_OK) == 0;
    // pi-gen bakes the factory marker into every image. It must only trigger
    // the OOBE while the account is exactly in factory state: still named
    // "pi", with no password or the baked "raspberry" default. A renamed user
    // or a user-chosen password means Raspberry Pi Imager provisioned the
    // device, so first boot skips straight to the launcher
    // (finish_configured_system() then removes the marker).
    state.factory_marker = access(kFactoryOobeMarker, F_OK) == 0;
    state.factory_username = first_user_has_factory_name();
    state.user_has_password = first_user_has_password();
    state.factory_credentials = first_user_has_factory_credentials();
    state.legacy_piwiz_active =
        lightdm_autologin_user() == kFirstBootWizardUser && piwiz_autostart_enabled();
    return should_run_wizard(state);
#endif
}

void launch_wizard::WizardService::run_keyboard_guide()
{
#if LAUNCH_WIZARD_DRY_RUN
    // The guide is a separate on-device binary; nothing to preview in SDL.
    return;
#else
    const bool marker_present = access(kKeyboardGuideMarker, F_OK) == 0;
    const bool binary_present = access(kKeyboardGuideBinary, X_OK) == 0;
    if (!should_run_keyboard_guide(marker_present, binary_present)) {
        if (marker_present)
            fprintf(stderr,
                    "LaunchWizard: keyboard guide binary missing; keeping marker\n");
        return;
    }

    // Consume before starting the interactive process. A power cycle or crash
    // must not trap the device in the guide on every subsequent boot.
    if (remove(kKeyboardGuideMarker) != 0) {
        fprintf(stderr, "LaunchWizard: failed to consume keyboard guide marker: %s\n",
                strerror(errno));
        return;
    }
    sync();

    // The guide's key sounds use miniaudio's PulseAudio backend, which needs
    // the UID 1000 user's pipewire-pulse socket. Spawned as root (the wizard's
    // own identity) it has no XDG_RUNTIME_DIR, initialises no backend and runs
    // silently (bug #262). Drop to the first user with runuser and point it at
    // the user's session runtime dir; the user is in the video/input groups,
    // exactly like APPLaunch, so rendering and input keep working.
    std::vector<std::string> args;
    struct passwd *pw = getpwuid(kDefaultUserUid);
    if (pw && pw->pw_name) {
        const std::string username = pw->pw_name;
        const std::string runtime_dir =
            "/run/user/" + std::to_string(kDefaultUserUid);
        const std::string pulse_socket = runtime_dir + "/pulse/native";
        // A factory image ships without linger (pi-gen strips it; the wizard
        // only enables it after the OOBE), so on true first boot no user
        // session exists and pipewire-pulse would never come up. Start the
        // session explicitly; when linger already started it this is a no-op
        // join.
        const CommandResult session_start =
            run_command({"systemctl", "start", "--no-block",
                         "user@" + std::to_string(kDefaultUserUid) + ".service"});
        if (session_start.code != 0)
            fprintf(stderr,
                    "LaunchWizard: failed to request user session startup: %s\n",
                    session_start.output.empty() ? "unknown error"
                                                 : session_start.output.c_str());
        // Bound the blank-screen delay while still giving pipewire-pulse time
        // to create its socket. On timeout the guide still runs without sound.
        for (int attempt = 0;
             attempt < 15 && access(pulse_socket.c_str(), F_OK) != 0; ++attempt)
            usleep(200 * 1000);
        if (access(pulse_socket.c_str(), F_OK) != 0)
            fprintf(stderr,
                    "LaunchWizard: %s not ready; keyboard guide may be silent\n",
                    pulse_socket.c_str());
        args = {"/usr/sbin/runuser", "-u", username, "--", "/usr/bin/env",
                "-u", "PULSE_SERVER", "-u", "PULSE_RUNTIME_PATH",
                "XDG_RUNTIME_DIR=" + runtime_dir, kKeyboardGuideBinary};
    } else {
        fprintf(stderr,
                "LaunchWizard: UID 1000 user missing; running guide as root\n");
        args = {kKeyboardGuideBinary};
    }

    printf("LaunchWizard: starting keyboard guide\n");
    fflush(stdout);

    // The guide is interactive and can stay open for minutes. Intentionally
    // not run_command(): that captures stdout/stderr into a bounded buffer
    // (hiding the guide's logs from journald), busy-polls at 20ms, and is
    // built around a timeout. posix_spawn + blocking waitpid inherits our
    // stdio, costs nothing while waiting, and reports exec errors directly.
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (std::string &arg : args)
        argv.push_back(arg.data());
    argv.push_back(nullptr);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // Give the guide default signal dispositions regardless of what the
    // wizard's runtime may have changed (e.g. an ignored SIGPIPE).
    sigset_t default_signals;
    sigfillset(&default_signals);
    posix_spawnattr_setsigdefault(&attr, &default_signals);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);

    pid_t pid = -1;
    const int spawn_error =
        posix_spawn(&pid, argv[0], nullptr, &attr, argv.data(), environ);
    posix_spawnattr_destroy(&attr);
    if (spawn_error != 0) {
        fprintf(stderr, "LaunchWizard: failed to start keyboard guide: %s\n",
                strerror(spawn_error));
        return;
    }

    int status = 0;
    pid_t wait_result;
    do {
        wait_result = waitpid(pid, &status, 0);
    } while (wait_result < 0 && errno == EINTR);
    if (wait_result < 0) {
        fprintf(stderr, "LaunchWizard: wait for keyboard guide failed: %s\n",
                strerror(errno));
        return;
    }
    if (WIFSIGNALED(status))
        fprintf(stderr, "LaunchWizard: keyboard guide killed by signal %d\n",
                WTERMSIG(status));
    else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        fprintf(stderr, "LaunchWizard: keyboard guide exited with %d\n",
                WEXITSTATUS(status));
#endif
}

int launch_wizard::WizardService::finish_configured_system()
{
#if !LAUNCH_WIZARD_DRY_RUN
    if (geteuid() != 0) {
        fprintf(stderr, "LaunchWizard: configured-system finish must run as root\n");
        return 1;
    }
#endif

    std::string user = current_first_user();
    if (user.empty() || user == "root") {
        fprintf(stderr, "LaunchWizard: UID 1000 user not found; cannot start APPLaunch\n");
        return 1;
    }

    std::string service_warning = handoff_to_applaunch(
        user, kDefaultUserUid, [](const std::vector<std::string> &args) {
            CommandResult result = run_command(args);
            return HandoffCommandResult{result.code, result.output};
        });
    if (!service_warning.empty()) {
        fprintf(stderr, "LaunchWizard: %s\n", service_warning.c_str());
        return 1;
    }

    // The device counts as configured (Imager provisioning or a finished
    // OOBE), so drop any leftover factory marker to keep future boots clean.
    remove_oobe_markers();
#if !LAUNCH_WIZARD_DRY_RUN
    sync();
#endif

    printf("LaunchWizard: started APPLaunch for %s and disabled LaunchWizard.service\n",
           user.c_str());
    fflush(stdout);
    return 0;
}
