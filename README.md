# Intelligent Power Distribution Unit (PSU) for Robotic Systems

A custom-designed, high-reliability Power Supply Unit (PSU) engineered to act as the centralized power management hub and hardware safety backbone for an advanced robotics platform. This board safely bridges high-draw actuator systems with sensitive compute electronics, featuring real-time telemetry and integrated fault protection.

---

## 🚀 Core Engineering Features

* **Centralized Voltage Regulation:** Features onboard step-down (buck) converters engineered to deliver clean, highly stable regulated voltage rails dedicated to sensitive compute units and sensor payloads.
* **Dynamic Power Routing:** Implements remotely switchable outputs, enabling upstream software to dynamically toggle power rails on-the-fly to conserve system energy or reset frozen peripherals.
* **Real-Time Current Monitoring:** Integrates inline current sensing architectures across critical nodes to detect electrical deviations, preventing overcurrent conditions from damaging hardware.
* **Hardware-Level Safety Architecture:** Interfaces natively with a physical Emergency Stop (E-Stop) loop, ensuring instantaneous, hardware-level power severance to high-draw chassis drivetrains and robotic arm actuators without relying on software loops.

---

## 🛠️ Technical Design & Specifications

### Hardware Breakdown
* **EDA Tooling:** Designed and routed entirely using KiCad.
* **Target Load Rails:** Dual-domain architecture isolating high-noise inductive motor spikes from digital logic logic rails.
* **Safety Controls:** Low-latency hardware isolation circuit mapped to the master system E-Stop button.

### Target Subsystem Map

| Managed Output Domain | Power Characteristic | Safety / Control Mechanism |
| :--- | :---: | :--- |
| **Compute Payloads** | Stable, Low-Noise Regulated | Upstream Overcurrent Monitored |
| **Sensor Electronics** | High-Precision Filtered | Soft-Switch Controlled |
| **Chassis Drivetrain** | High-Draw / Unregulated | Instantaneous Hardware E-Stop Cutoff |
| **Robotic Arm Drives** | High-Draw / Unregulated | Instantaneous Hardware E-Stop Cutoff |

---

## 💻 Hardware Layout & Physical Assembly

### Manufacturing & Routing Strategy
The PCB layout utilizes broad power planes and deliberate copper pours to handle high-current distribution safely while maintaining a tight, compact form factor. Signal lines for telemetry monitoring are physically isolated away from high-switching buck regulators to mitigate electromagnetic interference (EMI).

### Assembled Module View
Below is the physical validation of the manufactured and populated PSU PCB assembly:

![Custom PSU Hardware Assembly](assets/board_photo.jpg)

---

## 📁 Repository Navigation Guide

* `/hardware`: Contains the complete source schematic (`.kicad_sch`) and multi-layer board layout files (`.kicad_pcb`).
* `/assets`: High-resolution photographs, hardware layout visuals, and documentation figures.
