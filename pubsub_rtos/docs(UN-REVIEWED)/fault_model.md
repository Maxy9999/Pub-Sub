# Fault Model

Faults are classified by `fault_manager.c`.

Severity levels:

- Info: normal status and recovery signals.
- Warning: power, network, or threshold degradation.
- Error: security or invalid-command condition.
- Critical: watchdog/system task fault.

Device states:

- BOOTING
- NORMAL
- DEGRADED
- FAULT
- RECOVERY

Repeated identical fault signals are debounced for two seconds. Non-info faults are recorded in a bounded ring history.
