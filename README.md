# Bionic Hand

A five-finger bionic hand built on an **STM32 Black Pill (STM32F401CCU6)**. It can be controlled in two ways:

- **Manual:** send commands from a phone over Bluetooth (HC-05).
- **Tilt:** wear an **MPU6050** on your hand and bend your wrist; the bionic hand copies the motion.

A 16x2 **I2C LCD** shows the Bluetooth status, the last command, the current mode and the wrist tilt angle.
> *This project was developed as part of the **23CSE304 - Embedded Systems** course.*
---

## Features

- Five servo-driven fingers, controlled individually or all together
- Bluetooth control with a simple one-character protocol
- Wrist-tilt control with hysteresis, so the hand does not flutter
- Live status on a 16x2 LCD
- LCD and MPU6050 share a single I2C bus
- Self-contained LCD driver (no external library)

---

## Hardware

| Part | Purpose |
|---|---|
| STM32 Black Pill (STM32F411) | Main controller |
| 5x hobby servos | Fingers |
| HC-05 Bluetooth module | Wireless commands |
| MPU6050 (GY-521) | Wrist tilt sensor |
| 16x2 LCD with PCF8574 I2C backpack | Status display |
| Separate 5 V supply (3 to 5 A) | Servo power |
| Capacitor (e.g. 470 to 1000 uF) | Smooths servo supply |
| ST-Link V2 | Flashing and debugging |

---

## Pin Connections

### Servos

| Finger | Command | Timer channel | Pin |
|---|---|---|---|
| 1 | `1` | TIM4 CH1 | PB6 |
| 2 | `2` | TIM4 CH2 | PB7 |
| 3 | `3` | TIM4 CH3 | PB8 |
| 4 | `4` | TIM4 CH4 | PB9 |
| 5 | `5` | TIM3 CH1 | PB4 |

Servo power comes from the **separate 5 V supply**, not from the Black Pill. Join the supply's GND to the Black Pill's GND.

### Bluetooth (HC-05)

| HC-05 | Black Pill |
|---|---|
| VCC | 5 V |
| GND | GND |
| TXD | PA3 (USART2 RX) |
| RXD | PA2 (USART2 TX) |

Baud rate: 9600, 8N1.

### I2C bus (LCD and MPU6050 share it)

| Signal | Black Pill |
|---|---|
| SCL | PB10 (I2C2_SCL) |
| SDA | PB3 (I2C2_SDA) |

| Device | VCC | GND | I2C address |
|---|---|---|---|
| LCD backpack | 5 V | GND | `0x27` (some are `0x3F`) |
| MPU6050 | 3V3 | GND | `0x68` (AD0 to GND) |

---

## Bluetooth Commands

Use any Bluetooth serial app (for example *Serial Bluetooth Terminal* on Android). Set newline to **None** so only one character is sent.

| Send | Action |
|---|---|
| `C` | Close all fingers |
| `O` | Open all fingers |
| `1` to `5` | Close one finger |
| `T` | Switch to **tilt mode** |
| `M` | Switch to **manual mode** |

Every received character is echoed back to the phone.

---

## Tilt Control

1. Strap the MPU6050 flat on the **back of your hand**, just above the wrist, chip facing up.
2. The Y axis (printed on the board) points along the forearm toward the fingers; the X axis points across the wrist.
3. Send `T` to enable tilt mode.
4. Bend your wrist:
   - Tilt **above 45 degrees**: the hand closes.
   - Tilt **below 10 degrees**: the hand opens.
   - In between: no change (hysteresis).
5. Send `M` to return to manual control.

If the hand closes when you bend the wrong way, negate the angle in `mpu_read()`:

```c
tilt = -(int)(atan2f((float)ay, (float)az) * 57.2958f);
```

Keep the sensor wires under about 30 cm, since I2C is unreliable over long wires.

---

## LCD Output

| Line | Content |
|---|---|
| 1 | `BT:waiting` before any command, `BT:OK` after the first one |
| 2 | Mode (`M:` or `T:`), tilt angle, and hand state (`OPEN` or `CLOSE`) |

Example: `T:  52 CLOSE`

---

## Software Setup

**Tools:** STM32CubeMX and Keil MDK-ARM (the free version has a 32 KB code limit, which this project fits within).

### CubeMX configuration

| Peripheral | Setting |
|---|---|
| Clock | HSE 25 MHz, SYSCLK 84 MHz |
| TIM3 | PWM CH1, prescaler 83, period 19999 (50 Hz, 1 us ticks) |
| TIM4 | PWM CH1 to CH4, prescaler 83, period 19999 |
| USART2 | Asynchronous, 9600 baud, global interrupt enabled |
| I2C2 | I2C mode, 100 kHz, SCL on PB10, SDA on PB3 |
| SYS | Debug: **Serial Wire** (PB3 is shared with debug pins) |

### Build and flash

1. Open the project in Keil (`MDK-ARM`).
2. Press **F7** to build (expect 0 errors).
3. Connect the ST-Link and press **F8** to flash.
4. Press **NRST** on the Black Pill.

---

## Servo Tuning

In `main.c`:

```c
#define OPEN_ANGLE    0
#define CLOSE_ANGLE   180
#define SERVO_MIN_US  500    // pulse at 0 degrees
#define SERVO_MAX_US  2500   // pulse at 180 degrees
```

- A servo buzzes at its end stop: narrow the range, for example 600 to 2400.
- Fingers curl too far: lower `CLOSE_ANGLE`.
- Per-finger angles: use arrays of open and close angles instead of one value for all fingers.

---

## Troubleshooting

| Problem | Likely cause and fix |
|---|---|
| LCD backlight off | The Black Pill's 5 V pin only has 5 V when powered through USB-C. Check the backlight jumper too. |
| LCD blank or blinking | Wrong address. Watch `lcd27` and `lcd3F` in the debugger and set `LCD_ADDR` to the one that reads 1. |
| LCD shows only boxes | Turn the blue contrast pot on the backpack. |
| `mpu_ok = 0` | Check SDA/SCL wiring, power on 3V3, and AD0 on GND. |
| `who = 0x70` | Normal on MPU6500-type clones; the code works the same. |
| Servos jitter or reset the board | Use a separate 5 V supply of at least 3 A and share GND with the Black Pill. |
| Build fails with `-{` or undefined symbols | Check for stray characters near `int main(void)` and make sure all new variables are declared. |
| Code disappears after CubeMX regenerates | Keep all edits inside `USER CODE BEGIN ... END` blocks. |

---

## Project Structure

```
bionic-hand/
├── Core/
│   ├── Inc/
│   └── Src/
│       └── main.c        # LCD driver, MPU6050 code, servo control, UART callback
├── Drivers/              # STM32 HAL (generated by CubeMX)
├── MDK-ARM/              # Keil project
├── bionic-hand.ioc       # CubeMX configuration
└── README.md
```

---

## Future Ideas

- Per-finger open and close angles for natural grips
- Smooth, gradual finger motion instead of instant jumps
- Proportional control: map tilt angle directly to finger angle
- Preset grips (pinch, point, peace) as extra Bluetooth commands
- Limit switches or current sensing as finger feedback

---

## License
