# 🐦 Synthesizing Birdsong with the RP2040

**Author:** Eduardo Diaz

This project implements a birdsong synthesizer using the Raspberry Pi RP2040 microcontroller. It generates dynamic audio waveforms (chirps and swoops) using Direct Digital Synthesis (DDS), controlled via a keypad and replayable through a recording feature.

Inspired by: https://vanhunteradams.com/Pico/Birds/Birdsong.html

---

## 📌 Features

* 🎹 **Keypad-controlled sound generation**
* 🔊 **Real-time audio synthesis via SPI DAC**
* 🎵 **Two sound profiles:**

  * **Swoop:** sinusoidal frequency modulation
  * **Chirp:** quadratic frequency increase
* ⏺️ **Record & Replay functionality**
* ⚡ **Interrupt-driven audio generation (50 kHz sample rate)**
* 🧵 **Protothreads-based multitasking**

---

## 🧠 System Overview

### Audio Generation

* Uses **Direct Digital Synthesis (DDS)** with:

  * Phase accumulator
  * Sine lookup table (256 entries)
* Output sent to a **12-bit SPI DAC**
* Sampling rate: **50 kHz**

### Sound Profiles

* **Swoop (Key 1)**
  Frequency varies sinusoidally:

  ```
  f(x) ≈ -260sin(-π/6500 * x) + 1740
  ```

* **Chirp (Key 2)**
  Frequency increases quadratically:

  ```
  f(x) ≈ 1.18×10⁻⁴ x² + 2000
  ```

### Envelope (Amplitude Shaping)

* Attack, sustain, and decay implemented in ISR:

  * Smooth fade-in and fade-out
  * Prevents audio clicks

---

## 🎛️ Hardware Connections

### Keypad

| GPIO  | Connection                |
| ----- | ------------------------- |
| 9–12  | Rows (via 330Ω resistors) |
| 13–15 | Columns                   |

---

### SPI DAC

| GPIO | Function         |
| ---- | ---------------- |
| 5    | Chip Select (CS) |
| 6    | SCK              |
| 7    | MOSI             |
| 2    | ISR timing debug |
| 3.3V | VCC              |
| GND  | Ground           |

---

### Other

| GPIO | Purpose                                |
| ---- | -------------------------------------- |
| 0    | External switch (Record/Replay toggle) |
| 25   | Onboard LED                            |
| 1    | Debug GPIO                             |
| 8    | LDAC                                   |

---

## 🎮 Controls

| Input           | Action                          |
| --------------- | ------------------------------- |
| Key 1           | Play **swoop** sound            |
| Key 2           | Play **chirp** sound            |
| External Switch | Toggle **Record / Replay mode** |

---

## 🔁 Modes of Operation

### ▶️ Play Mode (default)

* Press keys to generate sounds in real time

### ⏺️ Record Mode

* Activated via external switch
* Stores:

  * Key presses
  * Timing between presses (silence)

### 🔁 Replay Mode

* Automatically plays back recorded sequence
* Maintains original timing and order

---

## ⚙️ Software Architecture

### Core Components

* **Timer Interrupt (Core 0)**

  * Runs at 50 kHz
  * Generates waveform samples
  * Handles:

    * DDS updates
    * Envelope shaping
    * Replay logic

* **Protothread**

  * Handles keypad scanning
  * Implements debouncing state machine

* **GPIO Interrupt**

  * Toggles record/replay mode

---

## 🧮 Key Implementation Details

### Fixed-Point Arithmetic

* Uses **Q15 format** (`fix15`) for performance
* Avoids floating-point overhead in ISR

### Lookup Tables

* `sin_table[256]` → waveform generation
* `swoop_sin_table[6501]` → frequency modulation

### Timing

* Interrupt period: **Max 15 µs**
* Total sound duration: **6500 samples (~130 ms)**

---

## 🚀 How to Run

1. Flash code onto RP2040 (e.g., Raspberry Pi Pico)
2. Connect:

   * Keypad
   * SPI DAC
   * External switch
3. Open serial monitor (optional for debug output)
4. Press keys to generate birdsong 🎶

---

## 🛠️ Dependencies

* Pico SDK
* Protothreads library:

  ```
  pt_cornell_rp2040_v1_4.h
  ```

---

## 📈 Future Improvements

* Add more bird call profiles
* Polyphonic playback (multiple simultaneous tones)
* SD card storage for longer recordings
* UI display (OLED/LCD)

---

## 🙌 Acknowledgments

* Bruce Land / Cornell ECE 4760 resources
* Van Hunter Adams for DDS birdsong inspiration

---

## 📜 License

This project is open-source and intended for educational use.
