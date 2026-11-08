# STM32 System Monitor

Firmware for the **STM32F407G-DISC1** board that runs four periodic FreeRTOS tasks, measures its own behaviour (memory, stack, execution time, CPU load, task liveness) and sends the results to a PC over **USB CDC**.

## Hardware and tools

- Board: STM32F407G-DISC1 (STM32F407VG, 168 MHz, 8 MHz HSE)
- Firmware: C, STM32 HAL, FreeRTOS (CMSIS-RTOS v2, heap_4)
- Configuration: STM32CubeMX
- Build: CMake, VS Code, WSL
- Flash/debug: ST-LINK (CN1)
- PC link: USB OTG FS (CN5)

## Tasks

| Task | Priority | Period | Role |
|---|---|---|---|
| `SupervisorTask` | 4 | 100 ms | Checks heartbeats, stacks and heap, builds the health bitmask |
| `MonitorTask` | 3 | 500 ms | Collects heap, stack and CPU-share statistics |
| `TelemetryTask` | 2 | 1000 ms | Sends one telemetry line over USB CDC |
| `WorkloadTask` | 1 | 200 ms | Simulated load, measures its own execution time |

## How it works

- **Heartbeat:** each task stores the current tick every cycle. The Supervisor flags a task as late if its heartbeat is older than 3 periods.
- **Execution time:** the Workload task reads the DWT cycle counter (`DWT->CYCCNT`) before and after its loop.
- **CPU load:** a trace hook (`traceTASK_SWITCHED_IN`) bills the elapsed cycles to the task that just ran at every context switch. Shares are computed over the last 500 ms, in permille.
- **Memory:** `xPortGetFreeHeapSize()`, `xPortGetMinimumEverFreeHeapSize()` and `uxTaskGetStackHighWaterMark()`.

## Telemetry output

One line per second:

```
t=3002 cpu=17 pm[sup=0 mon=0 tel=0 wrk=3 usb=11] heap=10176 min=10176 stk[872 768 480 872] wl=954/982us hp=0x00 OK
```

| Field | Meaning |
|---|---|
| `t` | Tick count (1 ms per tick) |
| `cpu` | Total CPU load in permille (1000 = 100%) |
| `pm[...]` | CPU share per task; `usb` is the default task |
| `heap` / `min` | Current and lowest-ever free heap (bytes) |
| `stk[...]` | Free stack bytes: Supervisor, Monitor, Telemetry, Workload |
| `wl` | Workload loop time in µs, last / worst case |
| `hp` | Health bitmask, then `OK` or `FAULT` |

Health bitmask: `0x01` Supervisor late, `0x02` Monitor late, `0x04` Telemetry late, `0x08` Workload late, `0x10` stack under 128 bytes, `0x20` heap minimum under 1 KB, `0x40` CPU above 90%.

LEDs: PD12 Supervisor (blinking = healthy, solid = fault), PD13 Monitor, PD14 Workload, PD15 Telemetry.

## Setup

1. **CubeMX:** clock 168 MHz (PLLM 8, PLLN 336, PLLP 2, PLLQ 7), `USB_OTG_FS` in Device_Only, `USB_DEVICE` as Virtual Port Com, PD12 to PD15 as outputs, FreeRTOS heap_4 with at least 12 KB, and `INCLUDE_uxTaskGetStackHighWaterMark` and `INCLUDE_xTaskGetIdleTaskHandle` enabled.
2. **Trace hook:** add this in the USER CODE block of `FreeRTOSConfig.h`:

   ```c
   extern void Trace_SwitchedIn(void *tcb);
   #define traceTASK_SWITCHED_IN()  Trace_SwitchedIn((void *)pxCurrentTCB)
   ```

3. **`main.c`:** create `defaultTask` (it starts USB), call `App_Init()` before `osKernelStart()`, and start the scheduler only once.
4. **Build and flash** with CMake through ST-LINK on CN1.
5. **Read the data:** plug a data-capable cable into the USB OTG connector (CN5). A new COM port appears in Windows; open it in Serial Monitor (the baud rate is ignored).

## Known limitations

- Interrupt time is counted against the task that was running.
- `wl` is wall time (includes preemption); the real CPU time per task is in `pm`.
- Shares below 0.1% show as 0 in permille.
