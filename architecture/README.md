# Clean Architecture Overview

This project follows a clean‑architecture inspired layout. The main layers are:

1. **Domain** – pure business logic, data structures, and core types.
   - `libqcode/include/qcode/core` – core concepts, models, utilities.
   - `libqcode/include/qcode/config` – configuration structures.
   - `libqcode/include/qcode/compaction` – domain‑level compaction request types.

2. **Use Cases** – application‑specific operations that orchestrate domain objects.
   - `libqcode/src/compaction` – compaction request handling.
   - `libqcode/src/generation` – generation controller and service.

3. **Interface Adapters** – adapt external systems to the use‑case layer.
   - **Providers** (external AI services) – `libqcode/include/qcode/providers` and `libqcode/src/providers`.
   - **Tools** (CLI, bash, image) – `libqcode/include/qcode/tools` and `libqcode/src/tools`.
   - **Session & UI** – `libqcode/include/qcode/session`, `libqcode/include/qcode/ui` and corresponding src.

4. **Frameworks / Drivers** – concrete entry points (apps, servers, CLI).
   - `apps/android` – Android UI driver.
   - `apps/cli` – command‑line driver.
   - `apps/server` – HTTP server driver.
   - `apps/webui` – web UI driver.

### Mapping Existing Structure

| Current Path | Clean‑Architecture Layer | Reasoning |
|--------------|--------------------------|-----------|
| `libqcode/include/qcode/core` | Domain | Core business models and logic. |
| `libqcode/include/qcode/config` | Domain | Configuration data structures. |
| `libqcode/include/qcode/compaction` | Domain | Domain entity for compaction requests. |
| `libqcode/src/compaction` | Use Cases | Implements compaction use‑case. |
| `libqcode/src/generation` | Use Cases | Generation orchestration logic. |
| `libqcode/include/qcode/providers` / `src/providers` | Interface Adapters | External AI provider integrations. |
| `libqcode/include/qcode/tools` / `src/tools` | Interface Adapters | CLI / tool helpers used by the app. |
| `libqcode/include/qcode/session` / `src/session` | Interface Adapters | Session persistence and state handling. |
| `libqcode/include/qcode/ui` / `src/ui` | Interface Adapters | UI rendering helpers used by drivers. |
| `apps/*` | Frameworks / Drivers | Concrete entry points for different platforms. |

### Recommendations

- **Create explicit layer directories** under `libqcode/` (e.g., `libqcode/domain`, `libqcode/use_cases`, `libqcode/adapters`, `libqcode/drivers`). Populate them with symbolic links to the existing files to make the architecture obvious without breaking the current build.
- **Update CMakeLists** to reference the new directories while keeping the original paths for backward compatibility.
- **Add interface contracts** in `include/qcode/interfaces` to define abstract boundaries between layers (e.g., provider interfaces, logger interface).
- **Document** the design in this `architecture/README.md` and ensure developers refer to it when adding new code.

By following this structure, new features can be added to the appropriate layer, keeping the core domain independent of external frameworks and simplifying testing.
