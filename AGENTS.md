# AGENTS.md

## Cursor Cloud specific instructions

### Project overview

Flow.IO is an ESP32-based pool automation firmware built with PlatformIO (Arduino framework on ESP-IDF/FreeRTOS). It compiles multiple firmware profiles from a shared C++17 codebase:

| Environment | Target |
|---|---|
| `FlowIO` | Main controller (pool logic, MQTT, HA, sensors) |
| `Supervisor` | Supervision board (config, TFT display, web interface) |
| `FlowConnectDisplay` | Remote display |
| `Micronova` | Pellet stove controller |
| `FlowIOWokwi` / `SupervisorWokwi` | Wokwi simulator variants |

### Build system

- **Toolchain**: PlatformIO Core CLI (`pio`)
- **PATH**: PlatformIO installs to `~/.local/bin` — ensure this is on PATH
- **Build command**: `pio run -e <environment>` (e.g. `pio run -e FlowIO`)
- **Pre-build scripts** (auto-invoked): `scripts/generate_build_version.py`, `scripts/generate_datamodel.py`, `scripts/generate_runtimeui_manifest.py`, `scripts/generate_config_docs.py`, `scripts/prepare_spiffs_data.py`

### Running tests

There is no `[env:native]` defined in `platformio.ini`. To run the native unit test:

```bash
g++ -std=c++17 -Isrc -Iinclude -I/tmp/Unity/src \
  test/test_poollogic_filtration_window/test_main.cpp \
  src/Modules/PoolLogicModule/FiltrationWindow.cpp \
  /tmp/Unity/src/unity.c \
  -c  # compile objects separately, then link with setUp/tearDown stubs
```

Or more practically, clone Unity once (`git clone --depth 1 https://github.com/ThrowTheSwitch/Unity.git /tmp/Unity`) and compile+link with stub setUp/tearDown functions. See the test runner pattern in the setup session.

### Linting

No explicit linter config (`.clang-tidy`, `.clang-format`) exists. The primary lint check is successful compilation with `-std=c++17` and strict build flags. `pio check` (cppcheck) is available but very slow.

### Known issues on main

- `src/Modules/ElectrolysisModule/ElectrolysisModule.cpp` references `LogModuleIdValue::ElectrolysisModule` which is not defined in `src/Core/LogModuleIds.h`. This causes all firmware builds to fail. The enum entry needs to be added to fix compilation.

### Running the application

This is embedded firmware — it runs on ESP32 hardware or the Wokwi simulator. There is no traditional "run locally" workflow. The development loop is: edit -> `pio run` -> flash to board or simulate with Wokwi.
