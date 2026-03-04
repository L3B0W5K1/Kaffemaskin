#!/usr/bin/env bash
#
# setup_and_build.sh
# Run this on your Raspberry Pi to install dependencies, build Anjay, and compile the client.
#
# Usage:
#   chmod +x setup_and_build.sh
#   ./setup_and_build.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ANJAY_VERSION="3.5.2"   # Adjust if you need a different version
BUILD_DIR="${SCRIPT_DIR}/build"
DEPS_DIR="${SCRIPT_DIR}/deps"

echo "============================================"
echo "  LwM2M GPIO Client - Setup & Build Script"
echo "============================================"

# ---- 1. Install system dependencies ----
echo ""
echo "[1/4] Installing system packages..."
sudo apt-get update
sudo apt-get install -y \
    git cmake gcc g++ make \
    libssl-dev \
    python3 \
    zlib1g-dev

# ---- 2. Clone and build Anjay library ----
echo ""
echo "[2/4] Building Anjay ${ANJAY_VERSION}..."
mkdir -p "${DEPS_DIR}"
cd "${DEPS_DIR}"

if [ ! -d "Anjay" ]; then
    git clone --recurse-submodules https://github.com/AVSystem/Anjay.git
    cd Anjay
    git checkout "${ANJAY_VERSION}" 2>/dev/null || echo "  (using default branch)"
else
    cd Anjay
    echo "  Anjay source already present, skipping clone."
fi

mkdir -p build && cd build
cmake .. \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DWITH_LIBRARY_SHARED=ON
make -j"$(nproc)"
sudo make install
sudo ldconfig

echo "  Anjay installed to /usr/local"

# ---- 3. Build the GPIO client ----
echo ""
echo "[3/4] Building LwM2M GPIO client..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${SCRIPT_DIR}" \
    -DCMAKE_PREFIX_PATH=/usr/local
make -j"$(nproc)"

echo "  Binary: ${BUILD_DIR}/lwm2m_gpio_client"

# ---- 4. Setup GPIO permissions ----
echo ""
echo "[4/4] Configuring GPIO permissions..."
# Add current user to the gpio group (if it exists)
if getent group gpio > /dev/null 2>&1; then
    sudo usermod -aG gpio "$(whoami)" 2>/dev/null || true
    echo "  Added $(whoami) to 'gpio' group"
fi

# Create a udev rule so /sys/class/gpio is writable without sudo
UDEV_RULE="/etc/udev/rules.d/99-gpio.rules"
if [ ! -f "${UDEV_RULE}" ]; then
    echo 'SUBSYSTEM=="gpio", KERNEL=="gpiochip*", ACTION=="add", PROGRAM="/bin/sh -c '"'"'chown root:gpio /sys/class/gpio/export /sys/class/gpio/unexport; chmod 220 /sys/class/gpio/export /sys/class/gpio/unexport'"'"'"' \
        | sudo tee "${UDEV_RULE}" > /dev/null
    echo 'SUBSYSTEM=="gpio", KERNEL=="gpio*", ACTION=="add", PROGRAM="/bin/sh -c '"'"'chown root:gpio /sys%p/active_low /sys%p/direction /sys%p/edge /sys%p/value; chmod 660 /sys%p/active_low /sys%p/direction /sys%p/edge /sys%p/value'"'"'"' \
        | sudo tee -a "${UDEV_RULE}" > /dev/null
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    echo "  udev rules installed (reboot or re-login for group changes)"
fi

echo ""
echo "============================================"
echo "  BUILD COMPLETE!"
echo "============================================"
echo ""
echo "Quick start:"
echo "  ${BUILD_DIR}/lwm2m_gpio_client -s <LESHAN_SERVER_IP> -g 17"
echo ""
echo "Options:"
echo "  -s <ip>     Leshan server IP       (default: localhost)"
echo "  -p <port>   Leshan CoAP port       (default: 5683)"
echo "  -n <name>   Client endpoint name   (default: rpi-gpio-client)"
echo "  -g <pin>    BCM GPIO pin number    (default: 17)"
echo ""
echo "Don't forget to upload the object model XML to Leshan:"
echo "  ${SCRIPT_DIR}/leshan/26241-gpio-controller.xml"
echo ""
