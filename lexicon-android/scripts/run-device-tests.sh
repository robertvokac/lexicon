#!/usr/bin/env bash
# Runs the instrumented tests on a connected device or emulator against a
# temporary LexiconServer with a throwaway database and user.
#
#   scripts/run-device-tests.sh /path/to/LexiconServer [extra Gradle arguments]
#
# The server listens on 127.0.0.1 of this machine; the Android emulator
# reaches that address as 10.0.2.2, which debug builds may use over plain
# HTTP (src/debug/res/xml/network_security_config.xml). Release builds never
# allow it. For a physical device use `adb reverse tcp:PORT tcp:PORT` and set
# LEXICON_DEVICE_HOST=127.0.0.1.
set -euo pipefail

server_binary=${1:?usage: $0 /path/to/LexiconServer [extra Gradle arguments]}
shift
port=${LEXICON_TEST_PORT:-18628}
device_host=${LEXICON_DEVICE_HOST:-10.0.2.2}
user=android-device-test
password=$(head -c 24 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 24)

workdir=$(mktemp -d)
cleanup() {
    [[ -n "${server_pid:-}" ]] && kill "$server_pid" 2>/dev/null && wait "$server_pid" 2>/dev/null || true
    rm -rf "$workdir"
}
trap cleanup EXIT

printf '%s\n%s\n%s\n' "$user" "$password" "$password" | "$server_binary" auth set-user --database "$workdir/lexicon.db" >/dev/null
"$server_binary" --database "$workdir/lexicon.db" --listen 127.0.0.1 --port "$port" --quiet &
server_pid=$!
for _ in $(seq 1 100); do
    curl -fsS "http://127.0.0.1:$port/api/v1/health" >/dev/null 2>&1 && break
    sleep 0.1
done

if [[ "$device_host" == 127.0.0.1 ]]; then
    adb reverse "tcp:$port" "tcp:$port"
fi

cd "$(dirname "$0")/.."
./gradlew connectedDebugAndroidTest \
    "-Pandroid.testInstrumentationRunnerArguments.lexiconServer=http://$device_host:$port" \
    "-Pandroid.testInstrumentationRunnerArguments.lexiconUser=$user" \
    "-Pandroid.testInstrumentationRunnerArguments.lexiconPassword=$password" \
    "$@"
