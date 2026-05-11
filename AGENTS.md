# Repository Guidelines

## Project Structure & Module Organization

This repository contains PlatformIO firmware for an ESP32 train-departure display using a Waveshare Pico-ePaper-4.2-B panel. Keep the layout simple and predictable:

- `src/` for Arduino C++ firmware code.
- `include/` for headers and local configuration templates.
- `test/` for PlatformIO tests when behavior is split into testable modules.
- `README.md` for setup, wiring, and flashing instructions.

Do not commit generated output, `.pio/`, dependency caches, or `include/secrets.h`.

## Build, Test, and Development Commands

Use PlatformIO as the canonical build and upload tool:

- `pio run`: compile the ESP32 firmware.
- `pio run --target upload`: flash the connected ESP32.
- `pio device monitor`: open the serial monitor at `115200` baud.
- `pio test`: run PlatformIO tests once tests are added.

Copy `include/secrets.example.h` to `include/secrets.h` before flashing.

## Coding Style & Naming Conventions

Use Arduino C++ with 2-space indentation, `constexpr` for fixed configuration, and descriptive names for pin assignments and timing constants. Keep secrets out of source control.

Use lowercase, hyphenated names for documentation files, for example `docs/data-flow.md`. Match test filenames to the module under test when adding testable modules.

## Testing Guidelines

Add tests with new parsing, filtering, or formatting behavior. Tests should be deterministic and should not depend on live NS API calls, local Wi-Fi, or real credentials. Put sample API responses under `test/fixtures/`.

Regression fixes should include a test that demonstrates the repaired behavior before flashing to hardware.

## Commit & Pull Request Guidelines

This repository has no commit history yet, so no existing commit convention can be inferred. Use concise, imperative commit subjects such as `Add departure rendering` or `Document setup workflow`.

Pull requests should include a summary, testing performed, linked issue or task when available, and screenshots for visible UI changes. Call out migrations, configuration changes, and known follow-up work.

## Agent-Specific Instructions

Inspect the tree before editing, keep changes scoped to the request, and do not overwrite unrelated user work. Update this guide when real project structure, commands, or conventions are introduced.
