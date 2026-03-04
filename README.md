# LwM2M GPIO Client (Anjay + Leshan)

An [Anjay](https://github.com/AVSystem/Anjay)-based LwM2M client that exposes GPIO pins as a custom LwM2M Object (`/26241`), controllable from a [Leshan](https://github.com/eclipse/leshan) server via CoAP.

## Architecture

```
┌──────────────────────────────┐         CoAP/UDP         ┌─────────────────────┐
│       Linux Board / RPi      │ ◄──────────────────────► │   Leshan Server     │
│                              │                           │                     │
│  ┌────────────────────────┐  │   Register / Observe /    │  ┌───────────────┐  │
│  │  Anjay LwM2M Client    │  │   Read / Write / Execute  │  │  Web UI       │  │
│  │                        │  │                           │  │  :8080        │  │
│  │  /0  Security          │  │                           │  └───────────────┘  │
│  │  /1  Server            │  │                           │                     │
│  │  /26241 GPIO Controller│──┼───── Execute /26241/0/2 ──┤  Click "Exe" on     │
│  │         │              │  │      ──────────────────►  │  Resource 2         │
│  │         ▼              │  │                           │                     │
│  │   sysfs GPIO write     │  │                           └─────────────────────┘
│  │   /sys/class/gpio/...  │  │
│  └─────────┬──────────────┘  │
│            │                 │
│            ▼                 │
│     BCM Pin 17 → HIGH       │
│     (LED / Relay / Buzzer)  │
└──────────────────────────────┘
```

## Custom Object: GPIO Controller (`/26241`)

| Resource ID | Name           | Operations | Type    | Description                                     |
|-------------|----------------|------------|---------|------------------------------------------------ |
| 0           | GPIO Pin       | RW         | Integer | BCM pin number (0–27)                           |
| 1           | GPIO State     | RW         | Boolean | Current HIGH/LOW state                          |
| 2           | Activate       | E          | –       | Set HIGH for `Pulse Duration` ms, then go LOW   |
| 3           | Deactivate     | E          | –       | Immediately force LOW                           |
| 4           | Pulse Duration | RW         | Integer | Duration in ms for Activate pulse (default 1000)|
| 5           | Description    | R          | String  | Human-readable label                            |

## Project Structure

```
.
├── CMakeLists.txt
├── README.md
├── 26241-gpio-controller.xml   # Object model XML for Leshan
├── deps/
│   └── Anjay/                  # Anjay library (cloned during setup)
└── src/
    ├── main.c                  # Client entry point & event loop
    ├── gpio_object.c           # Custom GPIO object implementation
    └── gpio_object.h           # GPIO object header
```

---

## Prerequisites

- Linux system (tested on Ubuntu 24.04, UP Board / Raspberry Pi)
- GCC, CMake, Make
- OpenSSL dev headers
- Python 3.8+ (for Anjay build)
- Java 17+ (for Leshan server)

Install build dependencies:

```bash
sudo apt-get update
sudo apt-get install -y git cmake gcc g++ make libssl-dev python3 python3-venv zlib1g-dev
```

---

## Step 1: Clone and Build Anjay

```bash
mkdir -p deps && cd deps
git clone --recurse-submodules https://github.com/AVSystem/Anjay.git
cd Anjay
```

### Important: Python Virtual Environment

Anjay 3.12+ **requires** Python to run inside a virtual environment. Without this, CMake will fail with:

```
CMake Error: Python3_EXECUTABLE=/usr/bin/python3 is not from a virtualenv.
```

Create and activate a venv:

```bash
python3 -m venv .venv
source .venv/bin/activate
```

### Build Anjay

```bash
mkdir -p build && cd build
```

**Critical:** If you've run CMake before without the venv, the old Python path is cached and will keep failing even after activating the venv. You must either clear the cache or pass the Python path explicitly:

```bash
# Option A: Explicit Python path (recommended)
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DWITH_LIBRARY_SHARED=ON \
    -DPython3_EXECUTABLE=$(which python3)

# Option B: Clear cache first
rm -rf CMakeCache.txt CMakeFiles/
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DWITH_LIBRARY_SHARED=ON
```

Alternatively, skip the demo entirely (you don't need it):

```bash
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DWITH_LIBRARY_SHARED=ON \
    -DWITHOUT_DEMO=ON
```

Then compile and install:

```bash
make -j$(nproc)
sudo make install
sudo ldconfig
```

---

## Step 2: Build the GPIO Client

From the project root directory:

```bash
cd /path/to/project
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/usr/local
make -j$(nproc)
```

This produces the `lwm2m_gpio_client` binary in the `build/` directory.

---

## Step 3: Setup Leshan Server

### Install the Object Model

Copy `26241-gpio-controller.xml` to Leshan's models directory:

```bash
cp 26241-gpio-controller.xml /path/to/leshan/leshan-demo-server/models/
```

### Important: XML Model Requirements

The Leshan DDF parser is strict about the XML format. The included XML file has been validated, but if you modify it, watch out for these issues:

1. **XML declaration must be version `1.0`** (not `1.1`):
   ```xml
   <?xml version="1.0" encoding="UTF-8"?>
   ```

2. **`<LWM2MVersion>` must match the schema URL**. If the schema is `LWM2M-v1_1.xsd`, then:
   ```xml
   <LWM2MVersion>1.1</LWM2MVersion>
   ```

3. **`<Description2>` element is required** (even if empty):
   ```xml
   <Description2></Description2>
   ```
   Place it after `</Resources>` and before `</Object>`.

### Start Leshan with Custom Models

You **must** pass the models directory with `-m`:

```bash
cd /path/to/leshan
java -jar leshan-demo-server/target/leshan-demo-server-*-SNAPSHOT-jar-with-dependencies.jar \
    -m leshan-demo-server/models/
```

Without `-m`, Leshan won't load custom object definitions and `/26241` will appear as an unknown object with no visible resources.

---

## Step 4: Run the Client

```bash
./build/lwm2m_gpio_client -s <LESHAN_SERVER_IP> -p 5683
```

If running on the same machine:

```bash
./build/lwm2m_gpio_client -s 127.0.0.1 -p 5683
```

### Command Line Options

| Flag | Description              | Default           |
|------|--------------------------|-------------------|
| `-s` | Leshan server IP         | `localhost`       |
| `-p` | CoAP port                | `5683`            |
| `-n` | Client endpoint name     | `rpi-gpio-client` |
| `-g` | BCM GPIO pin number      | `17`              |

### Expected Startup Output

```
INFO [lwm2m_main] LwM2M GPIO Client starting
INFO [lwm2m_main]   Server:   coap://127.0.0.1:5683
INFO [anjay]      Initializing Anjay 3.12.0
INFO [anjay_dm]   successfully registered object /0
INFO [anjay_dm]   successfully registered object /1
INFO [anjay_dm]   successfully registered object /26241
INFO [gpio_obj]   GPIO Object /26241 installed (instance 0, pin 17)
INFO [anjay]      registration successful
```

> **Note:** If you see `Cannot open /sys/class/gpio/export: No such file or directory`, your board doesn't use sysfs GPIO. This is normal on non-Raspberry Pi boards — the object still registers, but GPIO hardware control won't work until you adapt the GPIO backend (see [GPIO Backend](#gpio-backend-adaptation) below).

---

## Step 5: Control GPIO from Leshan

1. Open `http://<leshan-ip>:8080`
2. Click on `rpi-gpio-client` in the client list
3. Scroll down to `/26241 - GPIO Controller`
4. Expand **Instance 0**
5. Available operations:
   - **Read** Resource 0 → shows the BCM pin number
   - **Write** Resource 4 → set pulse duration (e.g. `2000` for 2 seconds)
   - **Execute** Resource 2 (Activate) → GPIO goes HIGH for the set duration
   - **Execute** Resource 3 (Deactivate) → forces GPIO LOW immediately
   - **Write** Resource 1 → directly set HIGH (`true`) or LOW (`false`)

---

## Hardware Wiring (Raspberry Pi Example)

### LED

```
RPi BCM 17 (pin 11) ──── 330Ω resistor ──── LED (+) ──── GND (pin 9)
```

### Relay Module

Connect the relay signal pin to BCM 17, power the relay from 5V/GND.

---

## GPIO Backend Adaptation

The client uses Linux sysfs GPIO (`/sys/class/gpio/`), which works on Raspberry Pi but may not be available on all boards. If your board uses a different GPIO interface:

- **libgpiod (recommended for modern kernels):** Replace the `gpio_export`/`gpio_write`/`gpio_read` functions in `gpio_object.c` with `libgpiod` calls.
- **Memory-mapped GPIO:** Use `/dev/gpiomem` for direct register access.
- **Other SBCs (UP Board, BeagleBone, etc.):** Check if sysfs GPIO is enabled or use the board's specific GPIO library.

---

## Running Without Root

On Raspberry Pi, create a udev rule for GPIO access:

```bash
sudo tee /etc/udev/rules.d/99-gpio.rules << 'EOF'
SUBSYSTEM=="gpio", KERNEL=="gpiochip*", ACTION=="add", \
    PROGRAM="/bin/sh -c 'chown root:gpio /sys/class/gpio/export /sys/class/gpio/unexport; chmod 220 /sys/class/gpio/export /sys/class/gpio/unexport'"
SUBSYSTEM=="gpio", KERNEL=="gpio*", ACTION=="add", \
    PROGRAM="/bin/sh -c 'chown root:gpio /sys%p/active_low /sys%p/direction /sys%p/edge /sys%p/value; chmod 660 /sys%p/active_low /sys%p/direction /sys%p/edge /sys%p/value'"
EOF

sudo usermod -aG gpio $(whoami)
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Reboot or re-login for group changes to take effect. If you still get permission errors, run with `sudo`.

---

## Troubleshooting

| Issue | Cause | Fix |
|-------|-------|-----|
| `Python3 is not from a virtualenv` | Anjay CMake requires venv | Create venv: `python3 -m venv .venv && source .venv/bin/activate` |
| Venv active but CMake still fails | Cached old Python path | Pass `-DPython3_EXECUTABLE=$(which python3)` or delete `CMakeCache.txt` |
| `Default Max Period is non-positive` | Anjay 3.12 rejects `0` for period fields | Use `-1` for `default_min_period` and `default_max_period` |
| `Cannot find source file: src/main.c` | Source files not in project directory | Ensure `src/main.c`, `src/gpio_object.c`, `src/gpio_object.h` exist |
| `Cannot open /sys/class/gpio/export` | Board doesn't support sysfs GPIO | Normal on non-RPi boards; object still registers. Adapt GPIO backend. |
| Leshan: `Invalid DDF file` + `Description2 expected` | Missing `<Description2>` tag in XML | Add `<Description2></Description2>` after `</Resources>` |
| Leshan: `LWM2MVersion not consistent` | Version mismatch between schema and tag | XML decl: `version="1.0"`, LWM2MVersion: `1.1` to match v1_1 schema |
| Object `/26241` not visible in Leshan | Model XML not loaded | Start Leshan with `-m models/` flag pointing to the XML directory |
| `/26241` shows but no instances | Pointer cast bug in handlers | Use `(gpio_object_t *)obj_ptr` not `(gpio_object_t *)*obj_ptr` |
| Client doesn't appear in Leshan | Firewall blocking CoAP | `sudo ufw allow 5683/udp` on both machines |

---

## License

MIT
