# qhw-admission Detailed Design

## Purpose

`qhw-admission` is a generic admission-control library for quantum resources.
It is the layer that decides whether a requested quantum workload should be
allowed to reserve capacity on a device. A caller submits a reservation request
with workload metadata. The library evaluates that request against a device
profile, a resource estimator, the selected admission policy, and current
reservations. The result is a structured admission decision.

The module provides quality-of-service control. It limits the active workload
accepted onto a quantum device, so downstream queues do not grow without bound.
It is intended for use by resource managers, runtime services, simulators, and
site-specific control planes.

The standard policy set includes three policies:

- `unlimited`: accept every internally valid request.
- `credit`: admit requests against a finite credit budget.
- `rate`: admit requests against a finite device throughput rate.

The implementation should be written in C. The C ABI is the reference
interface. Python bindings should be generated with SWIG and wrapped in a
small Python package. CMake should build the C library, policy plugins,
estimators, C tests, SWIG extension, and Python tests.

## Scope

The library manages admission state for one schedulable quantum resource per
admission context. A higher layer can create multiple contexts when it owns
multiple devices.

The core library owns:

- device admission configuration
- resource estimator configuration
- admission policy selection
- reservation accounting
- capacity accounting
- structured decision reporting
- optional task-level usage accounting

The caller owns:

- authentication
- resource-manager integration
- job launch
- scheduler integration
- provider submission
- result retrieval
- persistent storage

Admission decides whether a job or lease can enter the active set. Scheduling
decides which accepted quantum task occupies a QPU next.

## Design Goals

- Provide a small C ABI that can be called from C, Python, resource-manager
  plugins, runtime services, and simulators.
- Keep admission policy separate from provider submission and task scheduling.
- Make workload payloads opaque. Admission operates on declared metadata and
  estimator outputs, not on provider-native circuit objects.
- Keep cost estimation pluggable. Different devices, providers, and sites can
  supply different estimators while using the same admission policies.
- Support deterministic evaluate and reserve operations. `evaluate()` is a
  dry run. `reserve()` atomically creates an admitted reservation.
- Return structured decisions that can be translated by external systems.
- Use policy plugins so site-specific admission policies can be added without
  changing the core library.
- Use SWIG for Python bindings.

## Repository Layout

The repository skeleton should stay small. Files should be added when the
implementation needs a separate compilation unit, not because the final design
might eventually grow there.

```text
qhw-admission/
  CMakeLists.txt
  pyproject.toml
  README.md
  LICENSE

  include/
    qhw_admission/
      qhw_admission.h
      qhw_admission_types.h

  src/
    qhw_admission_internal.h
    qhw_admission.c
    qhw_reservation.c
    qhw_error.c
    qhw_thread.c

    policies/
      unlimited.c
      credit.c
      rate.c

    estimators/
      baseline.c

    util/
      qhw_hash_table.c
      qhw_hash_table.h

  swg/
    qhw_admission.i
    qhw_admission_typemaps.i

  python/
    qhw_admission/
      __init__.py
      admission.py

  tests/
    c/
      test_core.c
      test_unlimited.c
      test_credit.c
      test_rate.c
      test_estimator.c
      test_lifecycle.c
      test_threading.c

    python/
      test_unlimited.py
      test_credit.py
      test_rate.py
      test_estimator.py
      test_lifecycle.py

  docs/
    detailed-design.md
    design-notes.md
    qhw-admission-standard.md
    policies.md
```

`qhw_admission.h` should include the public API and
`qhw_admission_types.h` should define public data structures. Split
headers only when the public surface becomes large enough to justify it.

Estimators are loaded through the estimator plugin interface. The standard
baseline estimator lives in `src/estimators/baseline.c` and builds as a
plugin. Hardware-specific estimators use the same interface without changing
the core library.

The repository skeleton includes only a hash table utility. Reservation lookup
is keyed by reservation ID and should not be linear. Expiration uses a scan in
the lean design because it is not on the hot reserve path. A heap or RB tree is
appropriate when expiration volume justifies it.

The README, detailed design, policy notes, and tests are the primary
documentation targets. Man pages belong with the finalized public API.

## Build System

CMake is the primary build system. It should build the core library, policy
plugins, estimator plugins, C tests, and SWIG-generated Python extension.
Python packaging should use `pyproject.toml` and `scikit-build-core`, matching
the direction used by `qhw-scheduler`.

Build options should be explicit:

```text
QHW_ADM_BUILD_SHARED=ON|OFF
QHW_ADM_BUILD_STATIC=ON|OFF
QHW_ADM_BUILD_PLUGINS=ON|OFF
QHW_ADM_BUILD_ESTIMATORS=ON|OFF
QHW_ADM_BUILD_PYTHON=ON|OFF
QHW_ADM_BUILD_TESTS=ON|OFF
QHW_ADM_INSTALL_PLUGINS=ON|OFF
QHW_ADM_INSTALL_ESTIMATORS=ON|OFF
```

When `QHW_ADM_BUILD_PYTHON=ON`, CMake should require SWIG, a Python
interpreter, and matching Python development headers. A build without those
dependencies should fail with a direct diagnostic. C-only builds can set
`QHW_ADM_BUILD_PYTHON=OFF`.

The standard build should produce:

- `libqhw_admission.so`
- `libqhw_admission.a`, when static builds are enabled
- `qhw_adm_unlimited.so`
- `qhw_adm_credit.so`
- `qhw_adm_rate.so`
- `qhw_adm_estimator_baseline.so`
- SWIG-generated Python extension module
- C and Python tests

## Install Layout

The install target should use the selected CMake prefix.

```text
<prefix>/
  include/
    qhw_admission/
      qhw_admission.h
      qhw_admission_types.h

  lib/
    libqhw_admission.so
    libqhw_admission.a

    qhw_admission/
      policies/
        qhw_adm_unlimited.so
        qhw_adm_credit.so
        qhw_adm_rate.so

      estimators/
        qhw_adm_estimator_baseline.so

    cmake/
      qhw_admission/
        qhw_admissionConfig.cmake
        qhw_admissionTargets.cmake

  lib/pkgconfig/
    qhw_admission.pc
```

## Core Data Model

### Metadata Values

Several public structures need extensible metadata. The metadata representation
should follow the `qhw-scheduler` style so the two libraries can share usage
patterns.

| Structure | Visibility | Purpose |
|---|---|---|
| `qhw_adm_kv_t` | Public | Generic key-value option and metadata entry. |
| `qhw_adm_value_t` | Public | Tagged scalar value used by key-value entries. |

Supported scalar types are unsigned integers, signed integers, doubles,
booleans, and strings. Binary values are an extension point for compact opaque
metadata.

### Baseline Circuit Shape

The baseline circuit shape is the reference unit for credit and rate
accounting. A device rate is expressed in baseline units per time span.

| Field | Meaning |
|---|---|
| `qubit_count` | Number of qubits in the baseline circuit. |
| `depth` | Circuit depth used as a scaling reference. |
| `one_q_gate_count` | Number of one-qubit gates. |
| `two_q_gate_count` | Number of two-qubit gates. |
| `shots` | Number of shots. |
| `measurement_count` | Number of measurements or measured qubits. |

The baseline shape is configurable because each site needs a practical unit of
accounting. A device may choose a small benchmark circuit, a representative
production circuit, or a conservative default.

### Device Profile

The device profile describes the admission target.

| Field | Meaning |
|---|---|
| `device_id` | Local device identifier. |
| `time_span_ns` | Accounting window for rate calculations. |
| `baseline` | Baseline circuit shape. |
| `max_qubits` | Maximum supported qubit count. |
| `max_shots` | Maximum shots per task, if known. |
| `total_credits` | Total credit capacity for credit policy. |
| `device_rate` | Baseline units per time span for rate policy. |
| `concurrent_jobs` | Target concurrency for rate-slice defaults. |
| `metadata` | Device-specific values used by estimators or policies. |

If `device_rate` is zero, the rate policy derives it from the selected
estimator:

```text
device_rate = ceil(time_span_ns / estimated_baseline_ns)
```

### Task Class

An admission request can describe one or more task classes. Each task class
represents a repeated quantum task shape.

| Field | Meaning |
|---|---|
| `count` | Number of tasks with this shape. |
| `qubit_count` | Expected or maximum qubit count. |
| `depth` | Expected or maximum circuit depth. |
| `one_q_gate_count` | Expected one-qubit gate count. |
| `two_q_gate_count` | Expected two-qubit gate count. |
| `shots` | Expected shot count. |
| `measurement_count` | Expected measurement count. |
| `metadata` | Estimator-specific inputs. |

Multiple task classes allow a caller to represent a mixed workload without
charging every task at the maximum observed size.

### Admission Request

An admission request describes the expected quantum demand of a job, lease, or
session.

| Field | Meaning |
|---|---|
| `request_id` | Caller-generated request identifier. |
| `user_id` | User, account, or tenant identifier. |
| `job_id` | External job identifier, if available. |
| `reservation_id` | Optional caller-provided reservation identifier. |
| `walltime_ns` | Requested job walltime. |
| `classical_runtime_ns` | Expected non-QPU runtime. |
| `overhead_ns` | Runtime overhead subtracted from quantum budget. |
| `priority` | Optional admission priority. |
| `task_class_count` | Number of task classes. |
| `task_classes` | Array of task class descriptors. |
| `metadata` | Request-level metadata. |

The quantum budget is:

```text
quantum_budget_ns = walltime_ns - classical_runtime_ns - overhead_ns
```

### Estimate

The estimator returns timing and accounting values.

| Field | Meaning |
|---|---|
| `execution_ns` | Estimated QPU execution time. |
| `measurement_ns` | Estimated measurement contribution. |
| `compile_ns` | Estimated compilation or lowering time. |
| `transfer_ns` | Estimated transfer time to the control system. |
| `control_overhead_ns` | Estimated control-system setup overhead. |
| `total_ns` | Total estimated time used for admission. |
| `baseline_units` | Work expressed in baseline-circuit units. |
| `confidence_ppm` | Optional confidence in parts per million. |

When an estimator returns `baseline_units = 0`, the core derives baseline
units from total time:

```text
baseline_units = ceil(total_ns / estimated_baseline_ns)
```

### Decision

Admission returns a structured decision.

| Value | Meaning |
|---|---|
| `QHW_ADM_DECISION_ACCEPTED` | The request was admitted. |
| `QHW_ADM_DECISION_DELAYED` | The request fits the device but capacity is unavailable. |
| `QHW_ADM_DECISION_REJECTED` | The request cannot be supported. |

The decision object should include:

| Field | Meaning |
|---|---|
| `reservation_id` | Reservation created for accepted requests. |
| `reason_code` | Machine-readable reason. |
| `credits_required` | Credit demand for the request. |
| `rate_required` | Rate demand for the request. |
| `capacity_available` | Available capacity at decision time. |
| `estimated_total_ns` | Estimated total quantum demand. |
| `quantum_budget_ns` | Available quantum budget. |
| `retry_after_ns` | Optional retry hint. |
| `message` | Human-readable diagnostic text. |

### Reservation

A reservation records capacity held for an accepted request.

| Field | Meaning |
|---|---|
| `reservation_id` | Reservation identifier. |
| `request_id` | Request that created the reservation. |
| `device_id` | Device that accepted the reservation. |
| `state` | Pending, active, released, expired, or cancelled. |
| `credits_reserved` | Credits held by credit policy. |
| `credits_consumed` | Credits consumed by task-level accounting. |
| `rate_reserved` | Rate units held by rate policy. |
| `estimated_total_ns` | Estimated quantum demand. |
| `created_at_ns` | Creation timestamp. |
| `expires_at_ns` | Lease expiration time. |

## Resource Estimator Interface

The estimator converts task metadata into timing and baseline-unit estimates.
It is a plugin or callback surface because cost differs across hardware,
control systems, compilation paths, calibration state, and site policy.

The core callbacks are:

| Callback | Purpose |
|---|---|
| `estimate_task(device, task_class, out, user_data)` | Estimate one task class. |
| `estimate_request(device, request, out, user_data)` | Estimate a whole request. |
| `estimate_baseline(device, baseline, out, user_data)` | Estimate the configured baseline. |
| `validate_request(device, request, user_data)` | Validate request metadata. |

If `estimate_request()` is provided, the core uses it. Whole-request estimation
can account for batching, repeated compilation, shared transfer costs, and
provider-side execution behavior. If it is absent, the core sums
`count * estimate_task()` across the task classes.

The baseline estimator should use configurable timing fields:

```text
gate_ns =
  one_q_gate_count * one_q_gate_ns
  + two_q_gate_count * two_q_gate_ns

execution_ns = (gate_ns + measurement_ns) * shots

transfer_ns =
  one_q_gate_count * one_q_gate_transfer_ns
  + two_q_gate_count * two_q_gate_transfer_ns
  + measurement_transfer_ns

total_ns =
  execution_ns + transfer_ns + compile_ns + control_overhead_ns
```

Provider estimators can replace this with measured timing, backend target
properties, calibration-derived timing, control-system models, or external
resource-estimation tools.

## Policy Semantics

### Unlimited

Unlimited admission validates the request and returns an accepted decision. It
does not consume credit or rate capacity.

The policy still runs estimation. That keeps decision records useful for logs,
tests, and policy comparison.

### Credit

Credit admission uses a finite credit budget.

Configuration:

| Field | Meaning |
|---|---|
| `total_credits` | Total credits available on the device. |
| `reservation_ttl_ns` | Lease duration for accepted reservations. |
| `allow_overcommit` | Testing option that permits controlled overcommit. |

Credit demand is:

```text
credits_required = ceil(estimated_total_ns / estimated_baseline_ns)
```

Decision rules:

| Condition | Decision |
|---|---|
| `credits_required > total_credits` | `REJECTED` |
| `credits_required > available_credits` | `DELAYED` |
| Otherwise | `ACCEPTED` |

Accepted reservations subtract from `available_credits`. Releasing, expiring,
or cancelling a reservation returns the held credits.

### Rate

Rate admission uses a finite throughput budget expressed as baseline units per
time span.

Configuration:

| Field | Meaning |
|---|---|
| `time_span_ns` | Accounting window. |
| `device_rate` | Baseline units executable per window. |
| `concurrent_jobs` | Target concurrency for default slices. |
| `rate_slice` | Minimum allocatable rate unit. |
| `reservation_ttl_ns` | Lease duration for accepted reservations. |

`rate_slice` defaults to:

```text
rate_slice = max(1, floor(device_rate / concurrent_jobs))
```

Rate demand is:

```text
rate_required =
  ceil((baseline_units_required * time_span_ns) / quantum_budget_ns)
```

Decision rules:

| Condition | Decision |
|---|---|
| `quantum_budget_ns <= 0` | `REJECTED` |
| `rate_required > device_rate` | `REJECTED` |
| `rate_required > available_rate` | `DELAYED` |
| `available_rate < rate_slice` | `DELAYED` |
| Otherwise | `ACCEPTED` |

Accepted reservations allocate at least one `rate_slice`. Requests that need
more receive the smallest multiple of `rate_slice` that satisfies
`rate_required`, bounded by available capacity.

## Policy Plugin Interface

Admission policies should be loadable plugins. The core owns request parsing,
reservation storage, threading, and error reporting. A policy plugin owns the
admission algorithm.

A policy descriptor should include:

| Field | Meaning |
|---|---|
| `abi_version` | ABI version supported by the plugin. |
| `struct_size` | Descriptor size for compatibility checks. |
| `name` | Policy name, such as `credit` or `rate`. |
| `capabilities` | Supported operations and accounting modes. |
| `init` | Create policy-local state. |
| `destroy` | Destroy policy-local state. |
| `configure` | Apply policy options. |
| `evaluate` | Compute decision without mutating capacity. |
| `reserve` | Admit request and update policy-local capacity. |
| `release` | Return capacity held by a reservation. |
| `consume` | Optional task-level usage accounting. |
| `return_usage` | Optional task-level return path. |
| `capacity` | Report total and available policy capacity. |

The standard distribution should install `unlimited`, `credit`, and `rate`
policy plugins.

## Public C ABI

The public ABI should use opaque handles for library-managed objects.

| Handle | Purpose |
|---|---|
| `qhw_adm_t` | Admission context. |
| `qhw_adm_policy_t` | Loaded policy instance. |
| `qhw_adm_estimator_t` | Loaded estimator instance. |

### Context

| API | Purpose |
|---|---|
| `qhw_adm_create(attr, out)` | Create an admission context. |
| `qhw_adm_destroy(ctx)` | Destroy an admission context. |
| `qhw_adm_last_error(ctx)` | Return the last diagnostic string. |
| `qhw_adm_get_threading(ctx, out)` | Report threading mode. |

### Device And Policy Configuration

| API | Purpose |
|---|---|
| `qhw_adm_set_device(ctx, profile)` | Configure the admission target. |
| `qhw_adm_load_policy(ctx, path)` | Load a policy plugin. |
| `qhw_adm_set_policy(ctx, name, options, count)` | Select and configure a policy. |
| `qhw_adm_load_estimator(ctx, path)` | Load an estimator plugin. |
| `qhw_adm_set_estimator(ctx, name, options, count)` | Select and configure an estimator. |
| `qhw_adm_set_estimator_callbacks(ctx, callbacks)` | Install caller callbacks. |
| `qhw_adm_get_capacity(ctx, out)` | Report total and available capacity. |

### Reservation Lifecycle

| API | Purpose |
|---|---|
| `qhw_adm_evaluate(ctx, request, out_decision)` | Dry-run admission. |
| `qhw_adm_reserve(ctx, request, out_decision)` | Admit and hold capacity. |
| `qhw_adm_get_reservation(ctx, reservation_id, out)` | Return reservation state. |
| `qhw_adm_release(ctx, reservation_id, reason)` | Release held capacity. |
| `qhw_adm_cancel(ctx, reservation_id, reason)` | Cancel an unused reservation. |
| `qhw_adm_renew(ctx, reservation_id, ttl_ns)` | Extend reservation expiration. |
| `qhw_adm_expire(ctx, now_ns)` | Expire stale reservations. |

### Usage Accounting

| API | Purpose |
|---|---|
| `qhw_adm_validate_task(ctx, reservation_id, usage)` | Check task usage. |
| `qhw_adm_consume(ctx, reservation_id, usage)` | Charge task-level usage. |
| `qhw_adm_return_usage(ctx, reservation_id, usage)` | Return task-level usage. |
| `qhw_adm_get_usage(ctx, reservation_id, out)` | Report usage state. |

Task-level accounting can be a no-op for policies that do not need it.

## Python Binding

SWIG should generate the low-level Python extension. A hand-written Python
package should provide a small object model:

| Python class | Purpose |
|---|---|
| `AdmissionContext` | Owns a `qhw_adm_t` handle. |
| `DeviceProfile` | Python representation of a device profile. |
| `BaselineCircuit` | Python representation of the baseline shape. |
| `TaskClass` | Python representation of a task class. |
| `AdmissionRequest` | Python representation of a request. |
| `Decision` | Python view of a decision. |
| `Reservation` | Python view of reservation state. |

The Python wrapper should keep ownership clear. Python classes can build C
descriptors and call the SWIG module. Long-running C calls should release the
GIL. Callback paths that call into Python must acquire the GIL before invoking
Python code.

## Threading

The implementation should support two threading modes:

| Mode | Meaning |
|---|---|
| `QHW_ADM_THREAD_USER` | The caller serializes access to a context. |
| `QHW_ADM_THREAD_SAFE` | The library protects context state internally. |

Internal locking should cover reservation tables, capacity counters, policy
state, and estimator state. Policy plugins should receive enough context to
use the core lock discipline rather than inventing their own incompatible
state protection.

## Data Structures

The implementation should reuse the utility style from `qhw-scheduler`.
Large tables should avoid linear scans on hot paths.

Recommended private structures:

| Structure | Purpose |
|---|---|
| Hash table keyed by reservation ID | Fast reservation lookup. |
| RB tree or heap keyed by expiration time | Efficient reservation expiration. |
| Linked list per reservation state | Diagnostics and state iteration. |
| Plugin registry hash table | Fast policy and estimator lookup by name. |

`evaluate()` can perform temporary calculations without inserting into the
reservation table. `reserve()` should insert reservation state only after the
policy decision and capacity update succeed.

## Error Handling

Every public function returns a status code. The context stores the latest
diagnostic string. Functions that allocate output structures should provide a
matching free function when ownership crosses the ABI boundary.

Representative status codes:

| Status | Meaning |
|---|---|
| `QHW_ADM_OK` | Operation succeeded. |
| `QHW_ADM_ERR_INVAL` | Invalid input. |
| `QHW_ADM_ERR_NOMEM` | Allocation failed. |
| `QHW_ADM_ERR_NOT_FOUND` | Requested object was not found. |
| `QHW_ADM_ERR_STATE` | Object is in the wrong state. |
| `QHW_ADM_ERR_POLICY` | Policy plugin rejected the operation. |
| `QHW_ADM_ERR_ESTIMATOR` | Estimator failed. |
| `QHW_ADM_ERR_UNSUPPORTED` | Requested feature is unsupported. |

## Tests

The repository should include C and Python tests as part of the standard
project structure.

C tests should cover:

- context lifecycle
- device profile validation
- baseline estimator calculations
- unlimited policy decisions
- credit accepted, delayed, and rejected decisions
- rate accepted, delayed, and rejected decisions
- reservation release, cancellation, renewal, and expiration
- task-level usage accounting
- invalid input and overflow cases
- thread-safe mode under concurrent reserve/release calls

Python tests should cover:

- wrapper object construction
- reserve/evaluate flows for all policies
- estimator callback behavior
- GIL-safe callbacks
- decision and reservation conversion

## Initial Implementation Phases

### Phase 1: Skeleton

- Add repository build files.
- Add public headers.
- Add opaque handles and core data structures.
- Implement context lifecycle and error handling.
- Add C tests for creation and validation.

### Phase 2: Baseline Estimator

- Implement baseline estimator plugin.
- Support configurable baseline circuit shape.
- Support one-qubit and two-qubit gate counts.
- Add overflow-safe arithmetic helpers.
- Add C tests for timing and baseline-unit calculations.

### Phase 3: Unlimited Policy

- Implement `unlimited` policy plugin.
- Add evaluate and reserve paths.
- Add reservation table insertion and release.
- Add C and Python tests.

### Phase 4: Credit Policy

- Implement finite credit budget.
- Add release, cancellation, and expiration accounting.
- Add optional task-level consume and return paths.
- Add C and Python tests.

### Phase 5: Rate Policy

- Implement device-rate derivation.
- Implement rate slices.
- Add walltime feasibility checks.
- Add C and Python tests.

### Phase 6: Python Package

- Add SWIG interface files.
- Add Python wrapper classes.
- Add editable install support through `pyproject.toml`.
- Add Python tests for all public wrapper flows.

### Phase 7: Documentation

- Add `README.md` with build, install, and test commands.
- Add `docs/qhw-admission-standard.md`.
- Add policy documentation.
- Add man pages for public APIs.

## Open Design Questions

- Should task-level consumption be required for credit policy only, or should
  every policy expose a common usage-accounting path?
- Should reservations be strict leases with explicit renewal, or should callers
  choose that behavior through policy options?
- Should persistence be part of the core library or provided by a caller-owned
  snapshot/restore layer?
- Should estimator plugins be loaded with the same registry mechanism as
  policy plugins, or should estimators use only caller-provided callbacks?
