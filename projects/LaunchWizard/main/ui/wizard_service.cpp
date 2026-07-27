#include "wizard_service.h"

#include "global_config.h"
#include "cp0_lvgl_app.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
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

namespace launch_wizard {

constexpr uid_t kDefaultUserUid = 1000;
constexpr const char *kFirstBootWizardUser = "rpi-first-boot-wizard";

struct CommandResult {
    int code = 0;
    std::string output;
};

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

CommandResult run_command(const std::vector<std::string> &args, const std::string *stdin_text = nullptr)
{
    CommandResult result;
#if LAUNCH_WIZARD_DRY_RUN
    print_command(args, stdin_text);
    return result;
#else
    int out_pipe[2] = {-1, -1};
    int in_pipe[2] = {-1, -1};

    if (pipe(out_pipe) != 0) {
        result.code = 127;
        result.output = strerror(errno);
        return result;
    }
    if (stdin_text && pipe(in_pipe) != 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        result.code = 127;
        result.output = strerror(errno);
        return result;
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (stdin_text) {
            dup2(in_pipe[0], STDIN_FILENO);
            close(in_pipe[0]);
            close(in_pipe[1]);
        }
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const std::string &arg : args)
            argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(out_pipe[1]);
    if (stdin_text) {
        close(in_pipe[0]);
        const char *data = stdin_text->c_str();
        size_t left = stdin_text->size();
        while (left > 0) {
            ssize_t written = write(in_pipe[1], data, left);
            if (written <= 0)
                break;
            data += written;
            left -= static_cast<size_t>(written);
        }
        close(in_pipe[1]);
    }

    char buffer[256];
    ssize_t read_count = 0;
    while ((read_count = read(out_pipe[0], buffer, sizeof(buffer))) > 0) {
        if (result.output.size() < 4096)
            result.output.append(buffer, static_cast<size_t>(read_count));
    }
    close(out_pipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        result.code = 127;
        result.output = strerror(errno);
    } else if (WIFEXITED(status)) {
        result.code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.code = 128 + WTERMSIG(status);
    } else {
        result.code = 127;
    }

    while (!result.output.empty() &&
           (result.output.back() == '\n' || result.output.back() == '\r'))
        result.output.pop_back();
    return result;
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

// Provision the owner account. pi-gen always pre-creates the UID 1000 user
// (default "pi", --disabled-login). Mirroring the official userconf tool we
// rename that existing user to the chosen name and set its password instead of
// running useradd (which would collide on UID 1000). Returns the account UID, or
// 0 on failure with `error` populated.
uid_t configure_account(const std::string &new_user, const std::string &password,
                        std::string &error)
{
    std::string old_user = current_first_user();

    if (old_user.empty()) {
        // No pre-created first user (unusual image): create one at UID 1000.
        if (!create_user(new_user, error))
            return 0;
    } else if (old_user != new_user) {
        if (user_exists(new_user)) {
            error = "Target username already in use";
            return 0;
        }
        // Rename the existing UID 1000 user/group (same steps as userconf).
        if (!command_ok({"usermod", "-l", new_user, old_user}, error))
            return 0;
        run_command({"usermod", "-m", "-d", "/home/" + new_user, new_user});
        std::string old_group = current_first_group();
        if (!old_group.empty() && old_group == old_user)
            run_command({"groupmod", "-n", new_user, old_group});
        // Keep subuid/subgid and the nopasswd sudoers entry consistent.
        run_command({"sed", "-i", "s/^" + old_user + ":/" + new_user + ":/",
                     "/etc/subuid"});
        run_command({"sed", "-i", "s/^" + old_user + ":/" + new_user + ":/",
                     "/etc/subgid"});
        run_command({"sed", "-i", "s/^" + old_user + " /" + new_user + " /",
                     "/etc/sudoers.d/010_pi-nopasswd"});
    }

    if (!set_user_password(new_user, password, error))
        return 0;
    return kDefaultUserUid;
}

void disable_piwiz(const std::string &user)
{
    run_command({"pkill", "-x", "piwiz"});
    run_command({"rm", "-f",
                 "/etc/xdg/autostart/piwiz.desktop.dpkg-new",
                 "/etc/xdg/autostart/piwiz.desktop.dpkg-dist",
                 "/etc/xdg/autostart/piwiz.desktop.dpkg-old"});
    run_command({"install", "-d", "-m", "0755", "/etc/xdg/autostart"});
    const std::string hidden_entry =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=piwiz\n"
        "Hidden=true\n";
    run_command({"tee", "/etc/xdg/autostart/piwiz.desktop"}, &hidden_entry);
    run_command({"chmod", "0644", "/etc/xdg/autostart/piwiz.desktop"});

    const std::string user_autostart = "/home/" + user + "/.config/autostart";
    const std::string user_piwiz = user_autostart + "/piwiz.desktop";
    run_command({"install", "-d", "-m", "0755", "-o", user, "-g", user, user_autostart});
    run_command({"tee", user_piwiz}, &hidden_entry);
    run_command({"chown", user + ":" + user, user_piwiz});
    run_command({"chmod", "0644", user_piwiz});
}

std::string configure_desktop_startup(const std::string &user)
{
    std::string warning;

    disable_piwiz(user);

    run_command({"systemctl", "set-default", "graphical.target"});

    CommandResult raspi_config = run_command({"raspi-config", "nonint", "do_boot_behaviour", "B4"});
    if (raspi_config.code != 0) {
        warning = "raspi-config desktop autologin failed";
        if (!raspi_config.output.empty())
            warning += ": " + raspi_config.output;
    }

    run_command({"install", "-d", "-m", "0755", "/etc/lightdm/lightdm.conf.d"});
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
    }
    run_command({"chmod", "0644", "/etc/lightdm/lightdm.conf.d/50-launchwizard-autologin.conf"});

    run_command({"systemctl", "enable", "lightdm.service"});

    return warning;
}

std::string enable_applaunch_service(const std::string &user, uid_t uid)
{
    std::string warning;
    std::string uid_text = std::to_string(uid);
    std::string runtime_dir = "XDG_RUNTIME_DIR=/run/user/" + uid_text;
    std::string bus_address = "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/" + uid_text + "/bus";

    run_command({"loginctl", "enable-linger", user});
    run_command({"systemctl", "daemon-reload"});
    run_command({"systemctl", "start", "user@" + uid_text + ".service"});
    run_command({"runuser", "-u", user, "--", "env",
                 runtime_dir, bus_address,
                 "systemctl", "--user", "daemon-reload"});
    CommandResult enabled = run_command({"runuser", "-u", user, "--", "env",
                                         runtime_dir, bus_address,
                                         "systemctl", "--user", "enable",
                                         "APPLaunch.service"});
    if (enabled.code != 0) {
        warning = "APPLaunch service enable failed";
        if (!enabled.output.empty()) warning += ": " + enabled.output;
    }
    // Do not start the UI during OOBE. The reboot starts the enabled user unit
    // in the newly configured autologin session.
    return warning;
}

// ---------------------------------------------------------------------------
// System integration -- new OOBE steps (timezone / hostname / wifi / time / ssh).
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
    std::string warning;
    if (enabled) {
        run_command({"systemctl", "unmask", "ssh.service"});
        CommandResult ok = run_command({"systemctl", "enable", "--now", "ssh.service"});
        if (ok.code != 0) {
            CommandResult alt = run_command({"systemctl", "enable", "--now", "sshd.service"});
            if (alt.code != 0)
                warning = "SSH enable failed";
        }
    } else {
        run_command({"systemctl", "disable", "--now", "ssh.service"});
        run_command({"systemctl", "disable", "--now", "sshd.service"});
    }
    return warning;
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
    } else if (cp0_wifi_connect(ssid.c_str(), password.empty() ? nullptr : password.c_str()) != 0) {
        return "Wi-Fi connect failed";
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

std::vector<WifiNetwork> WizardService::scan_wifi()
{
    std::vector<WifiNetwork> networks;
#if LAUNCH_WIZARD_DRY_RUN
    networks.push_back({"Studio_2.4G", 90});
    networks.push_back({"CardputerLab", 70});
    networks.push_back({"Home-5G", 45});
#else
    if (cp0_wifi_radio_set_enabled(1) != 0)
        return networks;

    cp0_wifi_ap_t access_points[CP0_WIFI_AP_MAX]{};
    const int count = cp0_wifi_scan(access_points, CP0_WIFI_AP_MAX);
    for (int index = 0; index < count && index < CP0_WIFI_AP_MAX; ++index) {
        const cp0_wifi_ap_t &access_point = access_points[index];
        if (access_point.ssid[0] != '\0')
            networks.push_back({access_point.ssid, access_point.signal});
    }
#endif
    return networks;
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
    const std::function<bool(const std::string &)> &progress)
{
    const std::string timezone = g.current_timezone().name;
    const std::string hostname = g.hostname;
    const std::string username = g.username;
    const std::string password = g.password.empty() ? std::string("pi") : g.password;
    const bool network_skipped = g.network_skipped;
    const std::string wifi_ssid = g.wifi_ssid;
    const std::string wifi_password = g.wifi_password;
    const bool wifi_hidden = g.wifi_hidden;
    const bool ssh_enabled = g.ssh_enabled;

#if !LAUNCH_WIZARD_DRY_RUN
    if (geteuid() != 0)
        return "LaunchWizard must run as root";
#endif

    auto step = [&](const char *message) { return progress && !progress(message); };

    if (step("Setting timezone...")) return "Configuration cancelled";
    std::string error = apply_timezone(timezone);
    if (!error.empty()) return "Timezone failed: " + error;
    if (step("Setting date and time...")) return "Configuration cancelled";
    error = set_manual_time(g.manual_date, g.manual_time);
    if (!error.empty()) return "System time failed: " + error;
    if (step("Setting hostname...")) return "Configuration cancelled";
    error = apply_hostname(hostname);
    if (!error.empty()) return "Hostname failed: " + error;

    if (step("Configuring network...")) return "Configuration cancelled";
    if (!network_skipped && g.use_ethernet) {
        const std::string ethernet_error = apply_ethernet(g);
        if (!ethernet_error.empty())
            return ethernet_error;
    } else if (!network_skipped && !g.wifi_connected && !wifi_ssid.empty()) {
        std::string wifi_error = WizardService::connect_wifi(
            wifi_ssid, wifi_password, nullptr, wifi_hidden);
        if (!wifi_error.empty()) return wifi_error;
    }

    if (step("Configuring SSH...")) return "Configuration cancelled";
    const std::string ssh_error = apply_ssh(ssh_enabled);
    if (!ssh_error.empty()) return ssh_error;

    // Provision the owner account by renaming/repurposing the pre-created
    // UID 1000 user (pi-gen always ships one) and setting its password.
    if (step("Configuring user account...")) return "Configuration cancelled";
    error.clear();
    uid_t uid = configure_account(username, password, error);
    if (uid == 0)
        return error;

    if (step("Configuring desktop login...")) return "Configuration cancelled";
    std::string desktop_warning = configure_desktop_startup(username);
    if (!desktop_warning.empty()) return desktop_warning;
    if (step("Enabling APPLaunch service...")) return "Configuration cancelled";
    std::string service_warning = enable_applaunch_service(username, uid);
    if (!service_warning.empty())
        return service_warning;

    if (step("Finalizing configuration...")) return "Configuration cancelled";
    CommandResult disabled = run_command({"systemctl", "disable", "LaunchWizard.service"});
    if (disabled.code != 0)
        return disabled.output.empty() ? "Failed to disable LaunchWizard.service" : disabled.output;
    static const char *marker_paths[] = {
        "/var/lib/applaunch/run-oobe",
        "/var/lib/LaunchWizard/run-oobe",
    };
    for (const char *path : marker_paths) {
        if (remove(path) != 0 && errno != ENOENT)
            return std::string("Failed to remove OOBE marker: ") + strerror(errno);
    }
#if !LAUNCH_WIZARD_DRY_RUN
    sync();
    sync();
    sync();
#endif

    return "";
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
    // Explicit re-arm marker. APPLaunch's "Run Setup Wizard" settings entry
    // drops this file and reboots, letting an already-configured device replay
    // the OOBE on demand. apply_all() clears it on completion, so the wizard
    // still runs exactly once.
    static const char *kRearmPaths[] = {
        "/var/lib/applaunch/run-oobe",
        "/var/lib/LaunchWizard/run-oobe",
    };
    for (const char *path : kRearmPaths) {
        if (access(path, F_OK) == 0)
            return true;
    }

    return lightdm_autologin_user() == kFirstBootWizardUser && piwiz_autostart_enabled();
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

    std::string service_warning = enable_applaunch_service(user, kDefaultUserUid);
    if (!service_warning.empty()) {
        fprintf(stderr, "LaunchWizard: %s\n", service_warning.c_str());
        return 1;
    }

    run_command({"systemctl", "disable", "--now", "LaunchWizard.service"});
    printf("LaunchWizard: started APPLaunch for %s and disabled LaunchWizard.service\n",
           user.c_str());
    fflush(stdout);
    return 0;
}
