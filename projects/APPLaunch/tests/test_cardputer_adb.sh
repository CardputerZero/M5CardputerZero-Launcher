#!/bin/sh
set -eu

helper=$(dirname "$0")/../APPLaunch/adb/cardputer-adb
hotplug_source=$(dirname "$0")/../../../scripts/cardputer_adb_hotplug.c
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
hotplug="$test_dir/cardputer-adb-hotplug"
keys="$test_dir/adb_keys"
mkdir -p "$test_dir/root" "$test_dir/bin"
cat > "$test_dir/bin/systemctl" <<'EOF'
#!/bin/sh
echo "$*" >> "$CARDPUTER_ADB_TEST_ROOT/systemctl.log"
case "$1 $2" in
    "is-active --quiet") [ "${MOCK_ADBD_ACTIVE:-0}" = 1 ] ;;
    "is-enabled --quiet")
        [ "${MOCK_ADBD_ENABLED:-0}" = 1 ] &&
            [ ! -e "$CARDPUTER_ADB_TEST_ROOT/hotplug.stop" ]
        ;;
    *) exit 0 ;;
esac
EOF
chmod +x "$test_dir/bin/systemctl"
PATH="$test_dir/bin:$PATH"
export PATH
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror "$hotplug_source" -o "$hotplug"
key_bin="$test_dir/key.bin"
printf '\100\000\000\000' > "$key_bin"
dd if=/dev/zero bs=1 count=516 >> "$key_bin" 2>/dev/null
printf '\001\000\001\000' >> "$key_bin"
blob=$(base64 -w0 "$key_bin")
test_env="CARDPUTER_ADB_TEST_ROOT=$test_dir/root"

keys="$test_dir/root/adb_keys"
env $test_env "$helper" migrate
env $test_env "$helper" migrate
[ -f "$keys" ]
env $test_env "$helper" authorize "$blob host-one"
[ "$(stat -c %a "$keys")" = 600 ]
[ "$(wc -l < "$keys")" -eq 1 ]
env $test_env "$helper" authorize "$blob host-one"
[ "$(wc -l < "$keys")" -eq 1 ]
printf '\002' | dd of="$key_bin" bs=1 seek=8 conv=notrunc 2>/dev/null
second_blob=$(base64 -w0 "$key_bin")
env $test_env "$helper" authorize "$second_blob host-two"
[ "$(wc -l < "$keys")" -eq 2 ]
fingerprint=$(printf '%s' "$blob" | sha256sum | awk '{print $1}')
env $test_env "$helper" revoke "$fingerprint"
! grep -q "^$blob " "$keys"
grep -q "^$second_blob " "$keys"

printf '%s\n%s\n' "$blob host-one" "$blob cardputer-zero-default" > "$keys"
env $test_env "$helper" status | grep -q '^authorizations=2$'
env $test_env "$helper" authorize "$blob host-one"
! grep -q 'cardputer-zero-default' "$keys"

env $test_env "$helper" clear-authorizations
[ ! -s "$keys" ]
set +e
env $test_env "$helper" enable >/dev/null 2>&1
enable_rc=$?
set -e
[ "$enable_rc" -eq 11 ]
if env $test_env "$helper" authorize "invalid key"; then
    echo "invalid key was accepted" >&2
    exit 1
fi

# Package upgrades must remove the legacy key and request a restart when the
# mocked service reports active. No real systemd service is contacted.
printf '%s\n' "$blob cardputer-zero-default" > "$keys"
MOCK_ADBD_ACTIVE=1 env $test_env "$helper" migrate
! grep -q 'cardputer-zero-default' "$keys"
grep -q '^restart adbd.service$' "$test_dir/root/systemctl.log"

# Root executions must ignore all test-path environment overrides.
grep -q '\[ "$(id -u)" -ne 0 \]' "$helper"
# The product UI remains usable while its pairing views are intentionally hidden.
grep -q 'cmd_enable ui' "$helper"

# Reconnecting a previously detached UDC restarts adbd exactly once.
mkdir -p "$test_dir/root/sys/class/udc/mock"
printf '%s\n' 'not attached' > "$test_dir/root/sys/class/udc/mock/state"
: > "$test_dir/root/systemctl.log"
env $test_env CARDPUTER_ADB_HOTPLUG_TIMEOUT_MS=100 "$hotplug" &
hotplug_pid=$!
sleep 0.2
printf '%s\n' configured > "$test_dir/root/sys/class/udc/mock/state"
tries=0
while ! grep -q '^restart adbd.service$' "$test_dir/root/systemctl.log"; do
    tries=$((tries + 1))
    [ "$tries" -lt 30 ] || { kill "$hotplug_pid"; exit 1; }
    sleep 0.1
done
kill "$hotplug_pid"
wait "$hotplug_pid" || true
[ "$(grep -c '^restart adbd.service$' "$test_dir/root/systemctl.log")" -eq 1 ]
