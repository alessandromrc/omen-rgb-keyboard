# HP OMEN RGB Keyboard Driver

Linux kernel driver for HP OMEN laptop RGB keyboard lighting. Controls 4-zone RGB lighting with brightness control.

Inspired by the original [hp-omen-linux-module](https://github.com/pelrun/hp-omen-linux-module) by James Churchill (@pelrun).

## Features

- 4-Zone RGB Control - Individual control over each keyboard zone
- All-Zone Control - Set all zones to the same color at once
- Brightness Control - Adjust brightness from 0-100%
- Real-time Updates - Changes apply immediately
- Hex Color Format - Use standard RGB hex values

## Supported Hardware

- HP OMEN laptops with 4-zone RGB keyboard lighting
- Tested on Omen 16 u0000sl

## Installation

### Prerequisites
```bash
# Install kernel headers and build tools
sudo pacman -S linux-headers base-devel  # Arch Linux
# or
sudo apt install linux-headers-$(uname -r) build-essential  # Ubuntu/Debian
```

### Build and Install
```bash
# Clone the repository
git clone https://github.com/alessandromrc/omen-rgb-keyboard.git
cd omen-rgb-keyboard

# Build and install
sudo make install
```

The module will be built and installed using DKMS, which will automatically rebuild it on kernel updates.

## Usage

### Loading the Module
```bash
# Load the module
sudo modprobe hp_wmi

# Check if it loaded successfully
lsmod | grep hp_wmi
```

### Controlling RGB Lighting

The driver creates sysfs attributes in `/sys/devices/platform/omen-rgb-keyboard/rgb_zones/`:

#### Individual Zone Control
```bash
# Set zone 0 to red
echo "FF0000" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/zone00

# Set zone 1 to green  
echo "00FF00" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/zone01

# Set zone 2 to blue
echo "0000FF" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/zone02

# Set zone 3 to purple
echo "FF00FF" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/zone03
```

#### All-Zone Control
```bash
# Set all zones to the same color
echo "FFFFFF" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/all
```

#### Brightness Control
```bash
# Set brightness to 50%
echo "50" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/brightness

# Set brightness to 100% (maximum)
echo "100" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/brightness

# Turn off lighting (0% brightness)
echo "0" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/brightness
```

#### Reading Current Values
```bash
# Check current brightness
cat /sys/devices/platform/omen-rgb-keyboard/rgb_zones/brightness

# Check current zone colors
cat /sys/devices/platform/omen-rgb-keyboard/rgb_zones/zone00
cat /sys/devices/platform/omen-rgb-keyboard/rgb_zones/zone01
# etc...
```

### Color Format

Colors are specified in RGB hex format:
- `FF0000` = Red
- `00FF00` = Green  
- `0000FF` = Blue
- `FFFFFF` = White
- `000000` = Black (off)

### Brightness Range

Brightness is specified as a percentage (0-100):
- `0` = Completely off
- `50` = 50% brightness
- `100` = Maximum brightness

## Examples

### Gaming Setup
```bash
# Red gaming theme
echo "FF0000" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/all
echo "75" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/brightness
```

### Rainbow Effect
```bash
echo "FF0000" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/zone00  # Red
echo "FF8000" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/zone01  # Orange
echo "FFFF00" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/zone02  # Yellow
echo "00FF00" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/zone03  # Green
```

### Subtle White Lighting
```bash
echo "FFFFFF" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/all
echo "25" | sudo tee /sys/devices/platform/omen-rgb-keyboard/rgb_zones/brightness
```

## Troubleshooting

### Module Not Loading
```bash
# Check if WMI is supported
sudo dmesg | grep -i wmi

# Check for errors
sudo dmesg | grep -i hp_wmi
```

### No RGB Zones Found
```bash
# Verify the module loaded
lsmod | grep hp_wmi

# Check sysfs path
ls -la /sys/devices/platform/omen-rgb-keyboard/rgb_zones/
```

### Colors Not Changing
- Ensure you're using the correct hex format (6 characters, uppercase)
- Check that brightness is not set to 0
- Verify the module loaded without errors

## Technical Details

- Driver Name: `omen-rgb-keyboard`
- WMI Interface: Uses HP's native WMI commands for maximum compatibility
- Buffer Layout: Matches HP's Windows implementation exactly
- Kernel Compatibility: Linux 5.0+

## License

GPL-3.0

## Contributing

Feel free to submit issues and pull requests.

## Disclaimer

This driver is provided as-is, use at your own risk. The author is not responsible for any damage to your hardware.