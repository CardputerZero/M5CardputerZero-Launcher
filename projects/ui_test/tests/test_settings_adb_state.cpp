#include "settings_adb_state.hpp"

#include <cassert>
#include <limits>
#include <string>

int main()
{
    using namespace setting;

    const AdbStatus active = parse_adb_status(
        "adbd=active\nenabled=enabled\nauthorizations=2\n"
        "authorization=" + std::string(64, 'a') + "\thost-one@workstation\n"
        "authorization=" + std::string(64, 'b') + "\thost-two@workstation\n");
    assert(active.valid && active.payload_valid && active.active && active.enabled);
    assert(active.authorizations == 2);
    assert(active.authorization_entries.size() == 2);

    const AdbStatus pending = parse_adb_status("adbd=inactive\nenabled=enabled\n");
    assert(pending.valid && pending.payload_valid && !pending.active && pending.enabled);

    const AdbStatus off = parse_adb_status("adbd=inactive\nenabled=disabled\n");
    assert(off.valid && !off.active && !off.enabled);
    assert(!parse_adb_status("unrelated=active\n").valid);
    assert(!parse_adb_status(nullptr).valid);
    assert(adb_state_after_failure(active, false));
    assert(adb_state_after_failure(pending, false));
    assert(!adb_state_after_failure(off, true));
    assert(adb_state_after_failure(AdbStatus{}, true));

    const AdbStatus malformed = parse_adb_status("adbd=active\nauthorizations=bad\n");
    assert(malformed.valid && !malformed.payload_valid);
    assert(!adb_state_after_failure(malformed, false));

    const AdbStatus mismatched = parse_adb_status(
        "adbd=active\nauthorizations=2\n" +
        std::string("authorization=") + std::string(64, 'a') + "\thost\n");
    assert(mismatched.valid && !mismatched.payload_valid);

    const std::string public_key = "QAAAA" + std::string(694, 'A') + "=";
    assert(adb_public_key_valid(public_key + " workstation@user"));
    assert(!adb_public_key_valid(public_key));
    assert(!adb_public_key_valid("not-a-key host"));
    assert(!adb_public_key_valid(public_key + " host\nsecond"));
    assert(!adb_public_key_valid(public_key + " host\tbad"));
    std::string non_ascii_key = public_key + " host";
    non_ascii_key[10] = static_cast<char>(0xC2);
    assert(!adb_public_key_valid(non_ascii_key));

    assert(adb_fingerprint_valid(std::string(64, 'a')));
    assert(!adb_fingerprint_valid(std::string(63, 'a')));
    assert(!adb_fingerprint_valid(std::string(64, 'A')));

    assert(parse_adb_status(
               ("adbd=active\nauthorizations=" +
                std::to_string(std::numeric_limits<int>::max()) + "\n"))
               .authorizations == std::numeric_limits<int>::max());
    assert(parse_adb_status("adbd=active\nauthorizations=-1\n").authorizations == 0);
    assert(parse_adb_status("adbd=active\nauthorizations=+2\n").authorizations == 0);
    assert(parse_adb_status("adbd=active\nauthorizations=2junk\n").authorizations == 0);
    assert(parse_adb_status("adbd=active\nauthorizations=2147483648\n").authorizations == 0);

    assert(classify_privileged_result(0) == PrivilegedResultKind::SUCCESS);
    assert(classify_privileged_result(1) == PrivilegedResultKind::AUTH_FAILED);
    assert(classify_privileged_result(2) == PrivilegedResultKind::EXEC_FAILED);
    assert(classify_privileged_result(3) == PrivilegedResultKind::CANCELLED);
    assert(classify_privileged_result(4) == PrivilegedResultKind::TIMED_OUT);
    assert(classify_privileged_result(99) == PrivilegedResultKind::EXEC_FAILED);
    return 0;
}
