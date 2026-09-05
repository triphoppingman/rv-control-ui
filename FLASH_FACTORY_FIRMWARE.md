# Flash the Factory Firmware

This guide explains how to flash the prebuilt factory firmware for the CrowPanel 1.28" Rotary HMI display.

## 1) Prepare the hardware

- USB-C cable
- PC or laptop
- CrowPanel Advance HMI ESP32 AI Display
- Flash Download Tool

Check the board before flashing to make sure there are no obvious soldering or connection issues.

## 2) Connect the display

Connect the display to your PC using the USB-C cable.

- The power indicator should turn on.
- The board should appear as a COM port on the computer.

## 3) Open the Flash Download Tool

Open the flash tool included in the project bundle:

- `factory_firmware/flash_download_tool_3.9.7.rar`

If needed, extract it first and run the executable.

Select the correct chip type:

- ESP32-S3

## 4) Load the firmware files

Use the firmware files in:

- `../factory_firmware/Factory_firmware/`

The required files are:

- `boot_app0.bin` -> address `0xe000`
- `RotaryScreen_1_28.ino.bin` -> address `0x10000`
- `RotaryScreen_1_28.ino.bootloader.bin` -> address `0x0`
- `RotaryScreen_1_28.ino.partitions.bin` -> address `0x8000`

Make sure the files are selected correctly and match the addresses exactly.

## 5) Start the flash

1. Choose the required firmware files.
2. Enter the addresses listed above.
3. Confirm the files are correct.
4. Select the correct COM port.
5. Click `Start` to begin flashing.

Wait until the tool finishes the upload.

## 6) Finish

When the flash is complete:

- The tool will show `FINISH`
- The progress bar will reach the end

Then press the reset button on the display.

The factory demo should start running.

## 7) PlatformIO / esptool command

If you prefer to flash from a PlatformIO terminal instead of the GUI tool, use this exact command from the project directory:

```bash
~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 \
  --port /dev/ttyACM0 \
  --baud 460800 \
  --before default_reset \
  --after hard_reset \
  write_flash \
  -z \
  --flash_mode qio \
  --flash_freq 80m \
  --flash_size 16MB \
  0x0  "../factory_firmware/Factory_firmware/RotaryScreen_1_28.ino.bootloader.bin" \
  0x8000 "../factory_firmware/Factory_firmware/RotaryScreen_1_28.ino.partitions.bin" \
  0xe000 "../factory_firmware/Factory_firmware/boot_app0.bin" \
  0x10000 "../factory_firmware/Factory_firmware/RotaryScreen_1_28.ino.bin"
```

Notes:

- On Windows, the COM port is usually `COM3`, `COM4`, etc., so replace `/dev/ttyACM0` with the matching port.
- If PlatformIO is already installed and `esptool.py` is available in PATH, this also works:

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash -z --flash_mode qio --flash_freq 80m --flash_size 16MB 0x0 ../factory_firmware/Factory_firmware/RotaryScreen_1_28.ino.bootloader.bin 0x8000 ../factory_firmware/Factory_firmware/RotaryScreen_1_28.ino.partitions.bin 0xe000 ../factory_firmware/Factory_firmware/boot_app0.bin 0x10000 ../factory_firmware/Factory_firmware/RotaryScreen_1_28.ino.bin
```

After the command finishes successfully, press the reset button and the stock demo should start.

## 8) Troubleshooting

- If the port is not detected, reconnect the USB cable or reinstall the USB driver.
- If flashing fails, check that you selected the correct ESP32-S3 target.
- Make sure the firmware files and addresses match exactly.

## Notes

This is the official factory firmware for the device and is the recommended starting point before loading custom Arduino or PlatformIO sketches.
