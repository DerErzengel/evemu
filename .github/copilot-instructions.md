# evemu - Kernel Device Emulation

## Project Overview

evemu is a library and toolset for Linux kernel input device emulation via the evdev subsystem. It enables recording, describing, creating, and replaying input events from devices like touchscreens, mice, and keyboards.

**Core Architecture:**

- **C Library (`src/libevemu.la`)**: Wraps libevdev to create virtual uinput devices and manage event I/O
- **CLI Tools (`tools/`)**: Binaries for device operations (describe, record, play, device, event, echo)
- **Python Bindings (`python/evemu/`)**: ctypes wrapper around libevemu with testing framework
- **Data Format**: Custom text format (EVEMU 1.3) for device properties and event streams

## Build System (Autotools)

**Initial Setup:**

```bash
./autogen.sh           # Generate configure script
./configure            # Optional: --disable-python-bindings, --disable-tests
make                   # Build library, tools, and Python bindings
sudo make install      # Install to /usr/local (or --prefix location)
```

**Key Dependencies:**

- `libevdev >= 1.2.99.902` (required, wraps kernel evdev API)
- `xmlto` + `asciidoc` (optional, for man page generation)
- Python 2.6+ (optional, for bindings)

**Build Structure:**

- Library version controlled by `LIB_VERSION` in `configure.ac` (format: C:R:A for libtool)
- Tests require static linking (`test-evemu-create` uses `-static` flag)
- Man pages shadow each other: `evemu-describe` → `evemu-record`, etc.

## Critical Architecture Patterns

### 1. Dual Licensing

Files have **two** license blocks:

- LGPL v3 for library code (`src/evemu.c`, `src/evemu.h`)
- GPL v3 for tools (`tools/*.c`)
- Some files also include MIT license (Henrik Rydberg's original code)

Always preserve both when editing core files.

### 2. libevdev Delegation Pattern

`evemu_device` is a thin wrapper around `struct libevdev` and `struct libevdev_uinput`:

```c
struct evemu_device {
    unsigned int version;
    struct libevdev *evdev;           // Device description
    struct libevdev_uinput *uidev;     // uinput handle
    int pbytes, mbytes[EV_CNT];       // Format parsing state
};
```

Nearly all getters/setters delegate to libevdev macros (see `src/evemu.c:171-213`). Device creation uses `libevdev_uinput_create_from_device()`.

### 3. File Format Versioning

Current: **EVEMU 1.3** (`EVEMU_FILE_MAJOR.EVEMU_FILE_MINOR` in `src/evemu.c:64-65`)

Format evolution:

- 1.0: Comments only at file top
- 1.1: Comments anywhere, including end-of-line (except device name line)
- 1.2: Added resolution field to ABS axis info
- 1.3: Added LED/switch state (L:/S: lines)

**When bumping version**: Update `README.md` format docs and both defines in `evemu.c`.

### 4. \_GNU_SOURCE Requirement

All `.c` files requiring GNU extensions (e.g., `asprintf`, `O_CLOEXEC`) must define:

```c
#define _GNU_SOURCE  // BEFORE any includes
```

See `src/evemu.c:43`, `tools/evemu-record.c:42`, etc.

## Common Workflows

### Recording/Replaying Devices

```bash
# Describe device capabilities
sudo ./tools/evemu-describe /dev/input/event5 > device.prop

# Record event stream
sudo ./tools/evemu-record /dev/input/event5 > events.data

# Create virtual device and replay
sudo ./tools/evemu-device device.prop        # Outputs /dev/input/eventN
sudo ./tools/evemu-play /dev/input/eventN < events.data
```

**Note**: Tools require root for `/dev/input` and `/dev/uinput` access.

### Python Testing

```bash
cd python
python -m evemu.tests.test_device  # Run specific test module
./evemu-test-runner                # Run full test suite (after configure)
```

Python bindings use ctypes (`evemu/base.py:LibraryWrapper`) with error checking callbacks (`expect_ge_zero`, etc.).

## Key Files & Conventions

**Device Descriptions** (`data/*.prop`):

- Lines: `N:` (name), `I:` (bus/vendor/product/version hex), `P:` (properties), `B:` (event bits), `A:` (absinfo), `L:`/`S:` (LED/switch state)
- Event data: `E: <sec>.<usec> <type> <code> <value>` (hex for type/code)
- Comments: `#` prefix, allowed anywhere except device name line

**Version Control**:

- Library API version: `src/version.h` + `LIB_VERSION` in `configure.ac`
- File format version: `EVEMU_FILE_MAJOR/MINOR` in `src/evemu.c`
- Python `__version__` not currently exposed

**Error Handling**:

- C library: Returns negative errno values, uses `error()` helper (see `src/evemu.c:75`)
- Python: Raises `evemu.exception.ExecutionError` via `errcheck` callbacks

## Testing Notes

- C tests (`test/test-evemu-create.c`) validate file format parsing with bit flags
- Python tests assume `data/` directory is two levels up from `evemu` package
- No CI/CD config present; tests run via `make check`

## Integration Points

- **Kernel Interface**: `/dev/uinput` for device creation, `/dev/input/eventX` for I/O
- **libevdev**: All device property access goes through libevdev API
- **Python ctypes**: Bindings dynamically load `libevemu.so` via `ctypes.util.find_library()`

## When Modifying Code

- Device properties: Delegate to libevdev unless tracking internal state (`pbytes`, `mbytes`)
- New event types: Update `max[]` array in `test-evemu-create.c` if adding to format
- File format changes: Bump minor (new fields) or major (incompatible), document in README
- Man pages: Edit `.txt` sources in `tools/`, rebuild requires `asciidoc` + `xmlto`
