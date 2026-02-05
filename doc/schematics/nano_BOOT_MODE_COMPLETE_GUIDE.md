# NanoSDR Boot Mode and System Signals - Final Documentation

## Complete Signal Analysis

### Summary Table

| Signal | Pin | Type | Purpose | Controlled By | XDC Status |
|--------|-----|------|---------|---------------|------------|
| **Boot Mode (C8)** | C8 = MIO[4] | Input | DFU vs QSPI boot selection | PS (U-Boot) | ✅ Correct |
| **System Reset (B10)** | B10 = MIO[11] | PS MIO | System control/reset | PS Software | ✅ Correct |
| **PS Reset (B11)** | B11 | PS Reset | Hardware PS reset | PS System | ✅ Correct |

---

## 1. Boot Mode Selection (C8 / MIO[4])

### Purpose
**MIO[4] on pin C8** is read by U-Boot during the boot process to determine the boot source:
- **LOW (0)** = Boot into DFU (Device Firmware Update) mode via USB
- **HIGH (1)** = Boot from QSPI Flash

### XDC Configuration
```tcl
# MIO[4] on C8 - Boot mode selection (DFU vs QSPI)
# Read by U-Boot during boot process
# Low = DFU mode, High = QSPI boot
set_property -dict {PACKAGE_PIN C8 PULLTYPE PULLUP} [get_ports fixed_io_mio[4]]
```

### Hardware Connection
```
Pin C8 (MIO[4]) ──┬── Switch/Jumper ──┬── GND (DFU mode)
                  │                    │
                  └── Pull-up ─────────┴── VCC (QSPI boot, default)
```

### U-Boot Detection
In your U-Boot code, the boot mode is typically detected like this:

```c
// U-Boot boot mode detection (pseudo-code)
#define BOOT_MODE_GPIO  4  // MIO[4]

int get_boot_mode(void) {
    int boot_mode_pin = gpio_get_value(BOOT_MODE_GPIO);
    
    if (boot_mode_pin == 0) {
        // Pin pulled low - DFU mode
        return BOOT_MODE_DFU;
    } else {
        // Pin pulled high (default) - QSPI boot
        return BOOT_MODE_QSPI;
    }
}
```

### Boot Flow Diagram
```
Power On
   |
   v
FSBL reads MIO[4] (C8)
   |
   ├─── LOW (0) ────> U-Boot DFU Mode
   |                  - Initialize USB
   |                  - Wait for DFU commands
   |                  - Allow firmware update
   |
   └─── HIGH (1) ───> U-Boot QSPI Boot (Normal)
                      - Load kernel from QSPI
                      - Boot Linux
                      - Run application
```

### Device Tree Configuration
In your device tree, MIO[4] should be configured as input with pull-up:

```dts
&gpio0 {
    status = "okay";
};

/ {
    boot_mode_gpio {
        compatible = "gpio-keys";
        boot-mode {
            label = "boot-mode-select";
            gpios = <&gpio0 4 GPIO_ACTIVE_LOW>;  /* MIO[4] */
            linux,code = <BTN_0>;
        };
    };
};
```

### PS7 TCL Configuration
In your PS7.tcl, MIO[4] should be configured:

```tcl
ad_ip_parameter sys_ps7 CONFIG.PCW_MIO_4_DIRECTION in
ad_ip_parameter sys_ps7 CONFIG.PCW_MIO_4_PULLUP enabled
ad_ip_parameter sys_ps7 CONFIG.PCW_MIO_4_IOTYPE LVCMOS18
ad_ip_parameter sys_ps7 CONFIG.PCW_MIO_4_SLEW slow
```

---

## 2. System Reset (B10 / MIO[11])

### Purpose
**MIO[11] on pin B10** is a PS MIO pin that can be used for:
- System reset button
- General purpose input/output (controlled by PS software)
- Part of a peripheral function (UART, SPI, I2C, etc.)

### XDC Configuration
```tcl
# MIO[11] on B10 - Could be used for system reset or other PS function
set_property -dict {PACKAGE_PIN B10 PULLTYPE PULLUP} [get_ports fixed_io_mio[11]]
```

### Usage Options

#### Option A: As GPIO Input (Reset Button)
```c
// Linux userspace GPIO access
#define RESET_BUTTON_GPIO  (906 + 11)  // Base + MIO[11]

// In application code
int fd = open("/sys/class/gpio/export", O_WRONLY);
write(fd, "917", 3);  // Export GPIO 917 (906 + 11)
close(fd);

// Read button state
fd = open("/sys/class/gpio/gpio917/value", O_RDONLY);
char value;
read(fd, &value, 1);
if (value == '0') {
    // Button pressed (active low with pullup)
    perform_reset();
}
```

#### Option B: As GPIO Output (Status/Control)
```c
// Configure as output
fd = open("/sys/class/gpio/gpio917/direction", O_WRONLY);
write(fd, "out", 3);
close(fd);

// Set value
fd = open("/sys/class/gpio/gpio917/value", O_WRONLY);
write(fd, "1", 1);  // Set high
```

### PS7 TCL Configuration
```tcl
# Configure MIO[11] as GPIO input with pull-up
ad_ip_parameter sys_ps7 CONFIG.PCW_MIO_11_DIRECTION in
ad_ip_parameter sys_ps7 CONFIG.PCW_MIO_11_PULLUP enabled
ad_ip_parameter sys_ps7 CONFIG.PCW_MIO_11_IOTYPE LVCMOS18
ad_ip_parameter sys_ps7 CONFIG.PCW_MIO_11_SLEW slow
```

---

## 3. PS Hardware Reset (B11 / PS_SRST_B)

### Purpose
**Pin B11** is the **PS_SRST_B** (Processing System Reset) - hardware reset input for the PS.
- Active LOW reset
- Resets the entire Processing System
- Controlled by external reset circuitry

### XDC Configuration
```tcl
set_property PACKAGE_PIN B11 [get_ports fixed_io_ps_srstb]
```

### Hardware Connection
```
Reset Button ─── Reset IC ─── B11 (PS_SRST_B) ──┐
                    |                             |
                    └─── RC circuit               PS7
                    └─── Power supervisor         |
                                                   └─── System Reset
```

### Behavior
- **LOW**: PS is held in reset (does not boot)
- **HIGH**: PS runs normally
- Typically controlled by power-on-reset (POR) circuitry
- May have RC delay circuit for proper power-up sequencing

---

## Boot Mode Configuration Summary

### Complete Boot Configuration

| Component | Pin | MIO/PS | Function | Configuration |
|-----------|-----|--------|----------|---------------|
| Boot Mode Select | C8 | MIO[4] | DFU vs QSPI | Input, Pull-up, LVCMOS18 |
| System Control | B10 | MIO[11] | Reset/GPIO | Input, Pull-up, LVCMOS18 |
| PS Hardware Reset | B11 | PS_SRST_B | PS Reset | PS system signal |

### Testing Boot Modes

#### Test DFU Mode
1. Ground MIO[4] (C8) - via jumper or switch
2. Power cycle the board
3. USB should enumerate as DFU device
4. Use dfu-util to upload firmware:
   ```bash
   dfu-util -l  # List DFU devices
   dfu-util -a 0 -D firmware.bin  # Upload firmware
   ```

#### Test QSPI Boot (Normal)
1. Leave MIO[4] (C8) floating or pulled high (default)
2. Power cycle the board
3. System should boot from QSPI flash
4. Linux should start normally

---

## Software Integration

### U-Boot Environment Variables
```bash
# Set boot mode detection in U-Boot
setenv bootcmd 'run detect_boot_mode; run boot_${boot_mode}'

setenv detect_boot_mode '
    if gpio input 4; then
        setenv boot_mode qspi;
        echo "Booting from QSPI Flash";
    else
        setenv boot_mode dfu;
        echo "Entering DFU mode";
    fi
'

setenv boot_qspi 'sf probe; sf read ${kernel_addr} 0x100000 0x500000; bootm ${kernel_addr}'
setenv boot_dfu 'dfu 0 mmc 0'

saveenv
```

### Device Tree GPIO Mapping
```dts
&gpio0 {
    status = "okay";
    
    boot_mode_pin {
        gpio-hog;
        gpios = <4 GPIO_ACTIVE_HIGH>;  /* MIO[4] */
        input;
        line-name = "boot-mode-select";
    };
    
    reset_button {
        gpio-hog;
        gpios = <11 GPIO_ACTIVE_LOW>;  /* MIO[11] */
        input;
        line-name = "system-reset";
    };
};
```

---

## Troubleshooting

### DFU Mode Not Working
1. **Check C8 is actually grounded**
   ```bash
   # In U-Boot console
   gpio input 4
   gpio status  # Should show MIO4 = 0
   ```

2. **Verify USB is enabled in U-Boot**
   ```bash
   # U-Boot should have CONFIG_USB_DFU enabled
   # Check with: printenv | grep dfu
   ```

3. **Check USB connection**
   ```bash
   # On host PC
   lsusb  # Should show Xilinx DFU device
   ```

### QSPI Boot Failing
1. **Verify QSPI flash is programmed**
   ```bash
   # In U-Boot
   sf probe
   sf read 0x1000000 0 0x100000  # Read first 1MB
   md 0x1000000 0x100  # Display memory
   ```

2. **Check boot mode pin**
   ```bash
   gpio input 4
   gpio status  # Should show MIO4 = 1
   ```

---

## Final Configuration Checklist

- [x] **C8 (MIO[4])**: Boot mode selection - Configured with pull-up
- [x] **B10 (MIO[11])**: System control - Configured with pull-up
- [x] **B11 (PS_SRST_B)**: PS hardware reset - PS system signal
- [ ] **PS7.tcl**: Verify MIO[4] and MIO[11] configuration
- [ ] **U-Boot**: Add boot mode detection code
- [ ] **Device Tree**: Add GPIO pin definitions
- [ ] **Hardware**: Install boot mode jumper/switch on C8

---

## Conclusion

Your system uses a clean boot mode selection architecture:

1. **Hardware**: Simple switch/jumper on C8 (MIO[4])
2. **Firmware**: U-Boot reads MIO[4] at boot time
3. **Result**: 
   - C8 LOW → DFU mode for firmware updates
   - C8 HIGH → Normal QSPI boot

This is a standard and robust approach used in many embedded systems. The XDC configuration is now correct and complete for this boot architecture.
