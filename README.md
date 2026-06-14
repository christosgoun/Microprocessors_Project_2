# Non-Blocking Industrial Traffic Signaling Controller Firmware

This repository contains the firmware implementation for an asynchronous, event-driven industrial optical signaling controller developed for the **Microprocessors and Peripherals (Spring 2026)** course at the Aristotle University of Thessaloniki (AUTh).

## 👥 Authors (Group 33)
* **Angeliki Loulouda** (AEM: 10976)
* **Christos Gounaris** (AEM: 10638)

---

## 🛠️ System Specifications & Architecture

The system operates entirely asynchronously (**non-blocking**) without any spin-locks, continuous polling (`while` loop checking), or crude delay loops (`Delay()`) in the main pipeline. 

### 1. Peripherals & Concurrency Model
* **System Timebase:** A `SysTick` timer generates high-priority interrupts every $1\text{ ms}$, incrementing a volatile global counter (`sys_ticks`) used for timestamp comparisons.
* **Serial Communication (UART):** Configured at 115200 baud. Character reception is completely handled inside the UART RX Interrupt Service Routine (ISR) via a dedicated buffer.
* **External Interrupts (EXTI):** The onboard user button acts as a safety-critical Emergency Stop switch.

### 2. NVIC Priority Assignment
To guarantee predictable behavior during safety-critical events, priorities are strict:
1. **Priority 1 (Highest):** Hardware Emergency Stop (`EXTI15_10_IRQn`)
2. **Priority 2 (Medium):** System Timebase (`SysTick_IRQn`)
3. **Priority 3 (Lowest):** Communication Interface (`USART2_IRQn`)

---

## ⚙️ Functional Features

### A. Dynamic Profile Execution
* Numerical strings (e.g., `4152`) represent blink frequencies in Hz (e.g., `4` = 4 toggles/sec). A `0` forces the LED off.
* Each profile state executes for exactly 2 seconds before shifting to the next digit.
* **Looping Option:** Appending a dash (e.g., `415-`) configures the profile to loop infinitely until overridden by a new sequence. New sequences immediately abort currently running profiles.

### B. Inactivity & Security Hardening
* **Input Timeout:** If an unfinished sequence is left idle in the buffer for $> 4\text{ seconds}$ without an Enter (`\r` or `\n`) termination, the buffer clears automatically and alerts the user.
* **Emergency Stop (E-Stop):** Instantly triggers via the user button, halts execution timers, forces the LED permanently ON, and locks the UART interface against incoming commands.
* **Two-Step Secure Override:** To recover from an E-Stop, the operator must press the hardware button a second time and transmit the exact ASCII string `UNLOCK` via UART within a strict $5\text{ second}$ time-window. Failure to do so relocks the node.

---

## 📂 Project Structure

* `main.c` - Core application logic, finite state machine (FSM), and interrupt callbacks.
* `platform.h`, `gpio.h`, `uart.h`, `timer.h` - Peripheral hardware abstraction definitions.
* `Report_Group33.pdf` - Two-page technical report describing architecture, testing, and driver workarounds.

---

## ⚠️ Errata & Hardware Implementation Notes

* **Active-Low Button Inversion:** The standard peripheral driver library (`gpio.c`) checks for a logical `HIGH` state inside `EXTI15_10_IRQHandler` to invoke callbacks, conflicting with the physical active-low configuration of the onboard Nucleo button. To remedy this cleanly without altering core driver dependencies, the EXTI trigger is configured on the **Rising Edge**, causing the interrupt to execute reliably upon button *release*.
