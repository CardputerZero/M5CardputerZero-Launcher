# CardputerZero ZClaw

ZClaw is an LVGL chat client and setup frontend for running
[ZeroClaw](https://github.com/zeroclaw-labs/zeroclaw) on CardputerZero. It can
bootstrap a local ZeroClaw gateway, configure a model provider, pair the UI,
send chat requests, and answer tool approval prompts from the device keyboard.

See [Usage Guide](docs/usage.md) for the complete interaction flow and
[Configuration Notes](docs/configuration.md) for build profiles, runtime
integration, timeouts, and persistence details.

## Current Features

- Uses a keyboard-first layout designed for the 320×170 display.
- Checks internet connectivity before opening the application.
- Provides first-run Quickstart for OpenAI, OpenRouter, Anthropic, Ollama,
  DeepSeek, and custom OpenAI-compatible endpoints.
- Downloads the pinned ZeroClaw `v0.8.2` AArch64 release when the local binary
  is missing, installs/starts its service, creates the selected agent, and
  configures the local gateway on `127.0.0.1:42617`.
- Generates and submits a pairing code automatically during Quickstart, with a
  manual pairing-code flow available under Authorization settings.
- Uses authenticated WebSocket chat after pairing, including interactive tool
  approvals (`Yes`, `Always`, or `No`). Before pairing, chat falls back to the
  configured webhook endpoint.
- Stores multiple provider profiles and exposes setup, authorization, provider,
  agent, and transport status from the settings panel.

Quickstart requires internet access for the ZeroClaw download and hosted model
providers. Ollama uses `http://127.0.0.1:11434` by default and does not require
an API key.

## Keyboard Controls

- `Enter` opens the chat editor, confirms a selection, or submits text.
- `Shift+Enter` inserts a newline in the text editor.
- `Tab` opens or closes ZClaw Settings.
- Arrow keys navigate settings, scroll chat, move the text cursor, or select an
  approval response. `Z`, `X`, `C`, and `F` also map to left, down, right, and
  up outside the text editor.
- `Esc` or `Backspace` returns from settings; during an approval prompt it
  denies the request.
- `Delete` removes the provider currently open in Provider Detail.
- `Y`, `A`, and `N` answer an approval prompt with approve, always allow, and
  deny respectively.

## Runtime Files

ZClaw keeps its state under `$HOME/.zeroclaw`:

```text
bin/zeroclaw          downloaded ZeroClaw executable
config.toml           ZeroClaw gateway, agent, and provider configuration
zclaw_providers.tsv   provider profiles managed by the UI
zclaw_ui.tsv          webhook, agent, pairing token, and setup state
```

The UI configuration files may contain provider API keys, webhook secrets, and
the gateway bearer token. Protect the user's home directory accordingly.

Packaged image assets are installed below `APPLaunch/share/images`; the build
also stages JetBrains Mono and its OFL license from the APPLaunch project for
the settings interface. The chat interface keeps the regular proportional
font.

## Build

Run builds from this directory. When changing configuration profiles, clean the
existing generated configuration first.

Linux SDL2 simulator:

```bash
cd projects/ZClaw
scons distclean
CONFIG_DEFAULT_FILE=linux_x86_sdl2_config_defaults.mk scons -j8
```

Linux AArch64 cross-build for CardputerZero:

```bash
cd projects/ZClaw
scons distclean
CONFIG_DEFAULT_FILE=linux_x86_cross_cp0_config_defaults.mk scons -j8
```

Setting `CardputerZero=y` selects the Linux cross-build profile automatically.
On macOS, use `mac_cross_cp0_config_defaults.mk`. The legacy
`config_defaults.mk` is a base LVGL profile and does not explicitly select an
SDL or framebuffer display driver.

The build depends on the repository `SDK`, `ext_components`, and APPLaunch font
asset. `main/SConstruct` uses the SDK's lhv component with OpenSSL support for
HTTPS and secure WebSocket connections.

## Tests

Run the warning-clean unit and concurrency suite from this directory:

```bash
tests/run_tests.sh
```

After an SDL build, a short lifecycle smoke test verifies that startup remains
responsive to process termination:

```bash
timeout 3s ./dist/ZClaw
```

Exit status `124` is expected from `timeout`. The current SDL renderer produces
valid 320x170 frames, but XWayland desktop capture tools return a black root
pixmap in this environment. Pixel-level checks must read the SDL renderer
directly rather than relying on `scrot`.

The same test list supports exact target filtering and additional compiler or
linker flags. For example, run representative concurrency tests with ASan and
UBSan using:

```bash
ZCLAW_TEST_FILTER='zclaw_process_executor_test zclaw_async_service_test zclaw_http_cancellation_test' \
ZCLAW_TEST_CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
ZCLAW_TEST_LDFLAGS='-fsanitize=address,undefined' \
ASAN_OPTIONS='detect_leaks=1:halt_on_error=1' \
UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
tests/run_tests.sh
```

## Configuration Notes

`main/Kconfig` currently exposes `COMPILE_FOR_WEB`, `COMPILE_FOR_WIN32`, and
`COMPILE_FOR_LINUX`. These symbols are retained as build options but are not
currently referenced by the ZClaw application sources; the active backend is
selected by the LVGL options in the chosen `*_config_defaults.mk` profile.

The Linux SDL2 profile enables the simulator and loads assets relative to the
built executable. The CardputerZero profiles enable Linux framebuffer and
evdev input and use `/usr/share/APPLaunch/` as the LVGL POSIX asset root.

## Repository Layout

- `main/ui/zclaw_app.cpp` - application composition, asynchronous workflow
  orchestration, and semantic UI action execution
- `main/ui/zclaw_shell_view.*` - root application surface, static top bar, and
  input-bar chrome with parent-delete-aware root ownership
- `main/ui/zclaw_widgets.*` - shared LVGL box, label, and image construction
  with consistent non-interactive base styling
- `main/ui/zclaw_fonts.*` - owned FreeType font loading, fallback chains, and
  explicit teardown after app content removal and before LVGL shutdown
- `main/ui/zclaw_font_path_model.*` - pure environment-override and ordered
  font-candidate selection policy
- `main/ui/zclaw_theme.h` - shared UI palette tokens
- `main/ui/zclaw_pairing_service.*` - HTTP pairing and bearer-token result
- `main/ui/zclaw_chat_transport.*` - authenticated channel selection between
  webhook and WebSocket chat
- `main/ui/zclaw_webhook_transport.*` - HTTP webhook request/response transport
- `main/ui/zclaw_request_id.*` - clock-injectable, concurrent unique Webhook
  idempotency-key generation
- `main/ui/zclaw_websocket_transport.*` and `zclaw_chat_event.*` - WebSocket
  streaming, approval exchange, response aggregation, and typed event parsing
- `main/ui/zclaw_chat_stream_model.*` - pure WebSocket stream accumulation,
  approval signaling, terminal-state, and success/failure semantics
- `main/ui/zclaw_quickstart_service.*` - Setup-to-pairing result orchestration
- `main/ui/zclaw_quickstart_backend.h` and `zclaw_local_quickstart_backend.*` -
  testable Quickstart side-effect boundary and production Setup/pairing adapter
- `main/ui/zclaw_protocol.*` - HTTP URL construction and structured JSON
  encoding for webhook and WebSocket messages; typed response/event models
  parse and validate each inbound payload once
- `main/ui/zclaw_http_client_policy.*` - named HTTP timeout/redirect profiles
  shared by probes, pairing, webhook chat, and release downloads
- `main/ui/zclaw_http_cancellation.*` - shared sticky cancellation registry,
  race-safe request registration, and active Pairing/Webhook request stopping
- `main/ui/zclaw_gateway_response_model.*` - pure pairing/webhook HTTP status,
  JSON payload, business-error, and success-state interpretation
- `main/ui/zclaw_setup_service.*` - high-level Quickstart installation,
  configuration, service-start, and pairing preparation workflow
- `main/ui/zclaw_setup_backend.h` and `zclaw_local_setup_backend.*` - testable
  Setup side-effect boundary and its production installer/CLI implementation
- `main/ui/zclaw_process_runner.*` - legacy explicit shell execution used only
  by test tooling, plus the shared captured-process result contract
- `main/ui/zclaw_process_executor.*` - owned process-group execution, captured
  output, argv-only `execvp`, cancellable retry waits, sticky shutdown, and
  child reaping without implicit shell interpretation
- `main/ui/zclaw_text.*` - shared ASCII whitespace normalization used by CLI
  output and configuration-text models
- `main/ui/zclaw_text_file.*` - typed regular-file loading that distinguishes
  missing files from I/O and invalid-file-type failures
- `main/ui/zclaw_binary_installer.*` - ZeroClaw release acquisition workflow,
  readiness short-circuiting, and Setup progress orchestration
- `main/ui/zclaw_binary_installer_backend.h` and
  `zclaw_local_binary_installer_backend.*` - testable installation side-effect
  boundary and production filesystem/download/archive adapter
- `main/ui/zclaw_file_downloader.*` - cancellable streaming HTTP file transfer,
  timeout policy, byte progress, and throughput measurement independent of Setup UI
- `main/ui/zclaw_temporary_directory.*` - RAII ownership and recursive cleanup
  for private installation work directories
- `main/ui/zclaw_archive_installer.*` - archive extraction, executable discovery,
  symlink-safe atomic deployment, and owner-only executable permissions
- `main/ui/zclaw_cli_service.*` - ZeroClaw CLI configuration, Agent preparation,
  service control, and pairing-code generation behind injectable argv command
  and retry-wait boundaries
- `main/ui/zclaw_cli_config_model.*` - pure staged ZeroClaw configuration plan,
  Agent defaults, Provider references, and resulting local webhook policy
- `main/ui/zclaw_cli_output_model.*` - pure Agent-list parsing and idempotent
  service-command result interpretation
- `main/ui/zclaw_risk_profile_model.*` - pure idempotent default risk-profile
  document transformation
- `main/ui/zclaw_risk_profile_store.*` - permission-preserving ZeroClaw config
  replacement
- `main/ui/zclaw_setup_progress_model.*` and `zclaw_setup_progress_view.*` -
  testable transfer presentation data and a parent-owned LVGL progress subtree
  with delete-event lifetime cleanup
- `main/ui/zclaw_quickstart_model.*` - testable Quickstart defaults and
  pairing-code extraction
- `main/ui/zclaw_paths.*`, `zclaw_path_model.*`, and `zclaw_types.h` -
  thread-safe account lookup, pure portable runtime path policy, and shared
  namespaced data contracts used across UI, persistence, setup, and transport
  modules
- `main/ui/zclaw_runtime_model.*` - pure installed/configured/setup/token
  readiness policy used to decide whether Quickstart is required
- `main/ui/zclaw_runtime_state.*` - filesystem and UI-config readiness probe
- `main/ui/zclaw_connectivity.*` - injectable public-endpoint probes with
  callback-chained asynchronous HTTP fallback
- `main/ui/zclaw_approval_controller.*` - single-session approval isolation,
  request-bound waiting, reusable cancellation, and sticky shutdown
- `main/ui/zclaw_approval_coordinator.*` - approval state/dialog ownership,
  UI-thread prompt delivery, decisions, timeout cleanup, and cancellation
- `main/ui/zclaw_callback_lifetime.h` - mutex-serialized callback invocation
  and owner invalidation for queued UI work across Approval and asynchronous
  Startup, Chat, Authorization, and Setup workflows
- `main/ui/zclaw_approval_dialog.*` - approval prompt layout and selection
  presentation with top-layer delete-event lifetime cleanup
- `main/ui/zclaw_chat_view.*` and `zclaw_chat_layout.*` - chat message rendering,
  explicit borrowed-LVGL callback teardown, pure scroll clamping, bubble sizing,
  and scrollbar geometry
- `main/ui/zclaw_chat_workflow.*` - chat submission state, Quickstart gating,
  approval-enabled asynchronous requests, and result presentation
- `main/ui/zclaw_input_dialog.*` - text editor dialog, cursor styling, edit modes,
  keyboard editing primitives, and top-layer delete-event lifetime cleanup
- `main/ui/zclaw_input_model.*` and `zclaw_input_workflow.*` - testable input
  submission decisions and dispatch to chat, settings edits, or pairing
- `main/ui/zclaw_key_router.*` - testable keyboard mode routing from normalized
  key events to semantic application actions
- `main/ui/zclaw_key_event_adapter.*` - Linux keyboard event normalization kept
  outside the application composition root
- `main/ui/zclaw_ui_action_dispatcher.*` - execution of routed semantic actions
  across input, approval, settings, and chat UI services
- `main/ui/zclaw_settings_panel.*` - settings panel rows, selection styling,
  headers, parent-delete tracking, and explicit mounted/opening/open/closing
  animation lifecycle
- `main/ui/zclaw_startup_model.h` and `zclaw_startup_view.*` - shared startup
  state and parent-delete-aware network-check/offline overlay presentation
- `main/ui/zclaw_startup_workflow.*` - UI task-queue startup, asynchronous
  connectivity checks, and first-run Setup transition
- `main/ui/zclaw_task_inbox.*` and `zclaw_ui_task_queue.*` - thread-safe,
  lifetime-aware delivery of background results to the LVGL thread
- `main/ui/zclaw_task_batch.*` - noexcept per-callback exception isolation for
  UI-thread task batches
- `main/ui/zclaw_lhv_http_client.*` - shared lhv `sendAsync` ownership,
  exactly-once completion, retry policy, and shutdown result delivery
- lhv's global async pool executes blocking setup/process stages and approval
  timeout waits; interactive HTTP and WebSocket traffic uses event callbacks
- `main/ui/zclaw_async_service.*` - asynchronous network, chat, pairing, and
  Setup execution with explicit backend injection and UI-queue-only progress
  and completion callbacks, including backend-exception normalization
- `main/ui/zclaw_async_backend.h` and `zclaw_local_async_backend.*` - testable
  background-operation boundary and production service adapter
- `main/ui/zclaw_ui_task_sink.h` - minimal UI-thread task-delivery boundary
  implemented by the LVGL-backed UiTaskQueue
- `main/ui/zclaw_provider_catalog.*` and `zclaw_*_store.*` - provider defaults
  and persistent UI/provider configuration
- `main/ui/zclaw_provider_collection_model.*` - pure Provider activation,
  replacement, deletion, and active-entry synchronization rules
- `main/ui/zclaw_config_codec.*` - pure escaped-field encoding, decoding, and
  tab-separated line parsing shared by persistent stores
- `main/ui/zclaw_config_document_model.*` - pure Provider/UI configuration
  document parsing, checked validity, DTO mapping, and serialization
- `main/ui/zclaw_atomic_file.*` - unique-temporary-file, permission-aware,
  file-and-parent-directory-fsynced atomic replacement shared by persistent stores
- `main/ui/zclaw_directory_sync.*` - portable parent-directory durability after
  atomic rename, with explicit unsupported-filesystem handling
- `main/ui/zclaw_provider_manager.*` - transactional provider/setup state,
  typed load status, persistence, activation, editing, deletion, and rollback
- `main/ui/zclaw_ui_config_manager.*` - transactional UI configuration loading,
  typed missing/invalid/I/O status, updates, token clearing, and rollback
- `main/ui/zclaw_storage.*` - recursive owner-only configuration-directory
  preparation with ancestor-symlink rejection, shared by persistent managers
- `main/ui/zclaw_provider_form_model.*` - Provider field mapping and Quickstart
  form validation independent of Settings navigation and LVGL
- `main/ui/zclaw_settings_navigation_model.*` - Settings page activation and
  back-navigation decisions independent of form data and LVGL
- `main/ui/zclaw_selection_model.*` - reusable bounded row selection and paged
  list-selection state independent of Settings and LVGL
- `main/ui/zclaw_settings_controller.*` - settings page, row, paged-provider,
  detail, and edit-state ownership
- `main/ui/zclaw_settings_coordinator.*` - settings state/view ownership,
  presentation binding, navigation, and panel lifecycle
- `main/ui/zclaw_settings_workflow.*` - settings action dispatch and
  cross-page back-navigation orchestration
- `main/ui/zclaw_authorization_workflow.*` - pairing input/request completion,
  generation-safe request completion, authorization-token persistence,
  clearing, and page refresh
- `main/ui/zclaw_authorization_session_model.*` - pure pairing request
  generation, invalidation, and stale-completion rejection
- `main/ui/zclaw_provider_workflow.*` - Provider creation, detail editing,
  deletion, selection synchronization, and persistence feedback
- `main/ui/zclaw_setup_workflow.*` - Setup editing, validation, Provider
  selection, asynchronous progress, completion persistence, and page lifecycle
- `main/ui/zclaw_settings_presentation.*` - testable settings headers, hints,
  row labels, values, and paged-list presentation
- `tests/zclaw_core_test.cpp` - persistence, service, controller, concurrency,
  and presentation coverage using a shared temporary fixture
- `tests/zclaw_protocol_model_test.cpp` - isolated protocol encoding/parsing,
  chat layout/events, Quickstart normalization, and setup-progress presentation
- `tests/zclaw_selection_model_test.cpp` - isolated row and paged-selection
  boundary coverage
- `tests/zclaw_settings_rules_test.cpp` - isolated Provider/Setup form rules and
  Settings navigation-decision coverage
- `tests/zclaw_filesystem_test.cpp` - temporary-directory lifetime plus local
  archive discovery, executable deployment, and permission coverage
- `tests/zclaw_cli_config_model_test.cpp` - isolated ZeroClaw CLI configuration
  path, value, staging, and default-Agent coverage
- `tests/zclaw_cli_output_model_test.cpp` - isolated Agent-list and service
  command-output interpretation coverage
- `tests/zclaw_cli_service_test.cpp` - deterministic command sequencing,
  service failure, pairing retry, wait-count, and invalid-input coverage
- `tests/zclaw_config_codec_test.cpp` - isolated persistent field-codec and
  tab-separated line-boundary coverage
- `tests/zclaw_config_document_model_test.cpp` - isolated Provider/UI document
  round trips, malformed-record handling, defaults, and unknown-key coverage
- `tests/zclaw_config_manager_load_test.cpp` - missing, invalid, valid, retained
  in-memory state, and directory I/O failure coverage
- `tests/zclaw_risk_profile_model_test.cpp` - isolated default risk-profile
  transformation and idempotency coverage
- `tests/zclaw_provider_collection_model_test.cpp` - isolated Provider
  collection transitions, invalid-index handling, and active-entry coverage
- `tests/zclaw_runtime_model_test.cpp` - isolated first-run readiness truth-table
  coverage independent of HOME and filesystem state
- `tests/zclaw_binary_installer_test.cpp` - isolated installation sequencing,
  short-circuiting, failure propagation, and progress mapping with a fake backend
- `tests/zclaw_text_test.cpp` - isolated ASCII whitespace normalization coverage
- `tests/zclaw_text_file_test.cpp` - missing, empty, binary-content, and
  invalid-file-type read coverage
- `tests/zclaw_task_batch_test.cpp` - callback ordering, empty-task, standard
  and non-standard exception isolation coverage
- `tests/zclaw_atomic_file_test.cpp` - isolated atomic replacement, content,
  permissions, and invalid-parent failure coverage
- `tests/zclaw_directory_sync_test.cpp` - parent-directory durability success,
  missing-parent, and non-directory failure coverage
- `tests/zclaw_font_path_model_test.cpp` - isolated font override, candidate
  priority, missing-path, and invalid-probe coverage
- `tests/zclaw_path_model_test.cpp` - HOME/account fallback and portable
  `.zeroclaw` path composition coverage
- `tests/zclaw_authorization_session_model_test.cpp` - isolated pairing
  generation, replacement, invalidation, and stale-completion coverage
- `tests/zclaw_request_id_test.cpp` - fixed-clock formatting and concurrent
  Webhook request-ID uniqueness coverage
  shutdown waiting/rejection, and destructor join coverage
- `tests/zclaw_storage_test.cpp` - recursive directory creation, owner-only
  permissions, wide-mode tightening, and blocking-file coverage
- `tests/zclaw_connectivity_cancel_test.cpp` - deterministic in-flight probe
  cancellation using a blocking fake adapter
- `tests/zclaw_http_cancellation_test.cpp` - sticky shutdown, registration
  lifetime, move semantics, and unregister/shutdown race coverage
- `tests/zclaw_lhv_http_client_test.cpp` - localhost async response and
  in-flight shutdown completion coverage
- `tests/zclaw_process_executor_test.cpp` - process output, process-group
  cancellation, child reaping, sticky rejection, and cancellable wait coverage
- `tests/zclaw_setup_service_test.cpp` - Setup sequencing, normalization,
  progress, failure short-circuiting, and pairing-code coverage with a fake backend
- `tests/zclaw_quickstart_service_test.cpp` - Quickstart result composition,
  pairing success criteria, completion state, and progress with a fake backend
- `tests/zclaw_async_service_test.cpp` - callback execution, UI-task deferral,
  backend lifetime, shutdown rejection, Setup progress ordering, and invalid-callback coverage
- `tests/zclaw_input_routing_test.cpp` - isolated input submission, platform-key
  normalization, and semantic key-routing coverage
- `tests/run_tests.sh` - shared warning-clean build policy and explicit minimal
  source dependencies for all test binaries
- `main/src/` - application entry point and LVGL platform loop
- `APPLaunch/` and `assets/` - packaged ZClaw image assets
- `SConstruct`, `main/SConstruct`, and `*_config_defaults.mk` - project build
  definitions and platform profiles

Generated `build/` and `dist/` contents are build artifacts rather than source.
