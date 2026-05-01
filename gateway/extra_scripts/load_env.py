from pathlib import Path

from SCons.Script import Import

Import("env")


def parse_env_file(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue

        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()

        if not key:
            continue

        if len(value) >= 2 and value[0] == value[-1] and value[0] in ('"', "'"):
            value = value[1:-1]

        values[key] = value

    return values


project_dir = Path(env["PROJECT_DIR"])
env_file = project_dir / ".env"
values = parse_env_file(env_file)

generated_dir = project_dir / ".pio" / "build" / env["PIOENV"] / "generated"
generated_dir.mkdir(parents=True, exist_ok=True)

header_path = generated_dir / "env_config.h"

string_keys = [
    "APP_ENV",
    "WIFI_SSID",
    "WIFI_PASSWORD",
    "API_BASE_URL",
    "INFLUXDB_URL",
    "MQTT_BROKER",
    "API_KEY",
    "MQTT_USERNAME",
    "MQTT_PASSWORD",
    "INFLUXDB_TOKEN",
    "INFLUXDB_ORG",
    "INFLUXDB_BUCKET",
    "DEVICE_ID",
]

integer_keys = ["MQTT_PORT"]

header_lines = ["#pragma once", ""]

for key in string_keys:
    raw_value = values.get(key, "")
    escaped_value = raw_value.replace("\\", r"\\").replace('"', r'\"')
    header_lines.append(f'#define {key} "{escaped_value}"')

for key in integer_keys:
    raw_value = values.get(key, "1883")
    try:
        numeric_value = int(raw_value)
    except ValueError:
        numeric_value = 1883
    header_lines.append(f"#define {key} {numeric_value}")

header_path.write_text("\n".join(header_lines) + "\n", encoding="utf-8")

env.Append(CPPPATH=[str(generated_dir)])
