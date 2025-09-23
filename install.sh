#!/bin/bash

# Installation script for HP OMEN RGB Keyboard Driver

set -e

echo "Installing HP OMEN RGB Keyboard Driver..."

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (use sudo)"
    exit 1
fi

# Install DKMS if not present
if ! command -v dkms &> /dev/null; then
    echo "Installing DKMS..."
    if command -v pacman &> /dev/null; then
        pacman -S dkms
    elif command -v apt &> /dev/null; then
        apt update && apt install -y dkms
    elif command -v dnf &> /dev/null; then
        dnf install -y dkms
    else
        echo "Please install DKMS manually for your distribution"
        exit 1
    fi
fi

# Install the module
echo "Installing module with DKMS..."
make install

# Create modprobe configuration (for module options)
echo "Creating modprobe configuration..."
cp hp-wmi.conf /etc/modprobe.d/

# Create systemd module loading configuration
echo "Creating systemd module loading configuration..."
echo "hp-wmi" > /etc/modules-load.d/hp-wmi.conf

# Create the state directory
echo "Creating state directory..."
mkdir -p /var/lib/omen-rgb-keyboard
chmod 755 /var/lib/omen-rgb-keyboard

# Load the module immediately
echo "Loading module..."
modprobe hp-wmi

echo "Installation complete!"
echo "The driver will now load automatically on boot."
echo ""
echo "You can control the RGB keyboard using:"
echo "  echo 'rainbow' | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/animation_mode"
echo "  echo '5' | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/animation_speed"
echo ""
echo "See README.md for more examples and controls."
