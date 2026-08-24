#include "../main/ui/model/adb_state.hpp"
#include <cassert>
#include <limits>

int main()
{
    using namespace setting;
    AdbStatus active = parse_adb_status("peripheral=yes\nadbd=active\nenabled=enabled\n");
    assert(active.valid && active.active && active.enabled);
    AdbStatus pending = parse_adb_status("peripheral=no\nadbd=inactive\nenabled=enabled\n");
    assert(pending.valid && !pending.active && pending.enabled);
    AdbStatus off = parse_adb_status("peripheral=no\nadbd=inactive\nenabled=disabled\n");
    assert(off.valid && !off.active && !off.enabled);
    assert(!parse_adb_status("unrelated=active\n").valid);
    assert(!parse_adb_status(nullptr).valid);
    assert(adb_state_after_failure(active, false));
    assert(adb_state_after_failure(pending, false));
    assert(!adb_state_after_failure(off, true));
    AdbStatus transient = parse_adb_status("adbd=active\nenabled=disabled\n");
    assert(!adb_state_after_failure(transient, true));
    assert(adb_state_after_failure(AdbStatus{}, true));
    assert(!adb_state_after_failure(AdbStatus{}, false));
    AdbStatus paired = parse_adb_status("adbd=inactive\nenabled=disabled\nauthorizations=2\n");
    assert(paired.valid && paired.authorizations == 2);
    assert(parse_adb_status("adbd=active\nauthorizations=0\n").authorizations == 0);
    assert(parse_adb_status(
               ("adbd=active\nauthorizations=" +
                std::to_string(std::numeric_limits<int>::max()) + "\n").c_str())
               .authorizations == std::numeric_limits<int>::max());
    assert(parse_adb_status("adbd=active\nauthorizations=-1\n").authorizations == 0);
    assert(parse_adb_status("adbd=active\nauthorizations=+2\n").authorizations == 0);
    assert(parse_adb_status("adbd=active\nauthorizations=2junk\n").authorizations == 0);
    assert(parse_adb_status("adbd=active\nauthorizations=2147483648\n").authorizations == 0);
    assert(parse_adb_status("adbd=active\nxauthorizations=9\n").authorizations == 0);
    assert(parse_adb_status("adbd=active\nauthorizations=bad\nauthorizations=3\n")
               .authorizations == 3);
    const std::string blob = "QAAAA" + std::string(694, 'A') + "=";
    assert(adb_public_key_valid(blob + " workstation@user"));
    assert(!adb_public_key_valid(blob));
    assert(!adb_public_key_valid("not-a-key host"));
    assert(!adb_public_key_valid(blob + " host\nsecond"));
    auto authorizations = parse_adb_authorizations(
        ("authorization=" + std::string(64, 'a') + "\thost-one@workstation\n"
         "authorization=bad\tignored\n").c_str());
    assert(authorizations.size() == 1);
    assert(authorizations[0].fingerprint == std::string(64, 'a'));
    assert(authorizations[0].label == "host-one@workstation");
}
