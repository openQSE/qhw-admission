# qhw-admission Design Notes

This document captures temporary background notes used while shaping
`qhw-admission`. These notes are intentionally separate from
`detailed-design.md`, which describes the generic module.

## qSchedSim Analysis

qSchedSim already models the main admission-control concepts needed for the
first qhw-admission policies. The implementation is simulator-bound, but the
policy behavior and formulas are useful reference material.

### Current Credit-System Structure

`qschedsim/credits/base.py` defines a `CreditSystem` with three operations.

| Operation | qSchedSim role | qhw-admission interpretation |
|---|---|---|
| `submit_job(hjob)` | Admit or delay a hybrid job before it starts executing. | `reserve(request)` creates a reservation or returns a structured denial. |
| `reserve_resources(job_info, time_span, max_rate)` | Check whether a qtask can consume admitted budget while a job is active. | `consume(reservation_id, usage)` or scheduler-facing budget validation. |
| `return_credits(credits)` | Return capacity when a hybrid job completes. | `release(reservation_id)` or `expire(reservation_id)` returns held capacity. |

The base class derives two admission parameters:

- `rate_available` from `qdev.rate`.
- `rate_slice` from `qdev.rate / concurrent_jobs`, clamped to at least one.

That model captures the idea that the device has finite throughput and that
the admission layer may subdivide that throughput into allocatable slices.

### Device Rate And Baseline Circuit

`qschedsim/devices/base.py` computes the device rate from a baseline circuit:

```text
rate = ceil(time_span / get_qtask_exec_time(baseline_circ))
```

The rate unit is baseline circuits per configured time span. The generic
device model uses a baseline circuit with configurable depth and shots. It
estimates runtime as:

```text
execution_time =
  ((circuit_depth * two_q_gate_time) + measurement_time) * shots
  + (circuit_depth * two_q_gate_transfer_time)
  + measurement_transfer_time
```

The simulator model uses two-qubit gate time as the dominant gate cost and
leaves a note to revisit one-qubit and two-qubit counts separately. The
qhw-admission baseline estimator should generalize that idea with one-qubit
gate count, two-qubit gate count, depth, shots, measurement, transfer,
compilation, and control overhead.

### Unlimited Policy

`UnlimitedCreditSystem` is a baseline policy. It calculates basic job metadata
and checks that the quantum budget is positive:

```text
quantum_budget = walltime - classical_runtime - overhead
```

The job is oversubscribed if the quantum budget is less than or equal to zero.
Otherwise, the job is accepted without consuming rate or credit capacity.

This policy is useful for testing and comparison. It preserves the admission
API while removing admission pressure.

### Credit Policy

qSchedSim calls this `TimeBased`. It admits a job against a finite shared
credit budget. The simulator computes:

```text
scale_factor = ceil((depth / baseline_depth) * (shots / baseline_shots))
credits = quantum_task_count * scale_factor
```

The job is rejected if its credit request exceeds total device credits. It is
delayed if the request fits the device but exceeds currently available credits.
Accepted jobs hold credits until completion. During execution,
`reserve_resources()` can consume remaining job credits per qtask.

The qhw-admission credit policy should preserve this policy shape while using
an estimator for credit demand:

```text
credits = ceil(estimated_total_ns / estimated_baseline_ns)
```

### Rate Policy

`RateBased` admits jobs against a finite throughput rate. qSchedSim first
calculates the same baseline-scaled work:

```text
scale_factor = ceil((depth / baseline_depth) * (shots / baseline_shots))
credits = quantum_task_count * scale_factor
```

It then computes the minimum device rate needed for the job to fit within the
job's quantum budget:

```text
min_rate = ceil((credits * time_span) / quantum_budget)
```

The job is rejected if `min_rate` is greater than total device rate. It is
delayed if the job fits the device but the required rate is not currently
available. When capacity is available, the policy allocates at least one
`rate_slice` and may allocate multiple slices if the job needs more.

The qhw-admission rate policy should preserve this behavior with estimator
derived baseline units.

### Observed Policy Behavior

The qSchedSim analysis shows a stable pattern:

- Unlimited admission has near-zero activation delay, but it can create deep
  downstream queues and high hybrid-job completion time.
- Rate-based admission keeps the active set small. Jobs may wait longer before
  activation, but admitted jobs see shallow QPU queues and lower completion
  time.
- Credit-based admission sits between those extremes. It limits the active set
  without enforcing the same rate-slice behavior as the rate policy.

That behavior is the operational motivation for a reusable admission-control
library. Admission does not make the QPU faster. It controls how much work can
contend for the device at one time.

## QFw Integration Notes

These notes describe one possible consumer. They are not part of the generic
qhw-admission module contract.

The first QFw integration should expose admission as a service API consumed by
resource-manager-side code.

Development path:

1. Build and test `qhw-admission` independently.
2. Add a QFw `api_admission` surface.
3. Back the API with a QPM service or a small admission service.
4. Add a development test client that calls `reserve()` directly.
5. Add a SLURM integration layer that translates qhw-admission decisions into
   SLURM accept, delay, or reject behavior.
6. Pass accepted reservation IDs to the QFw runtime.
7. Make QPM task submission validate the reservation ID before queueing work.

The resource manager call must provide enough metadata for the estimator. If
the request lacks workload metadata, admission can only use conservative
defaults.

The expected QFw flow is:

```text
RMS / SLURM / Flux
  -> qhw-admission reserve()
  -> application starts with reservation
  -> application submits qtasks
  -> QPM validates reservation
  -> qhw-scheduler queues accepted qtasks
  -> provider submission
```

The scheduler should not decide whether a hybrid job is allowed to start. The
admission layer should not order runnable qtasks. The shared data should be
limited to identifiers and metadata such as reservation ID, job ID, owner,
priority, and estimated cost.

## Resource-Estimator Notes

Existing tools provide useful pieces, but no directly reusable
resource-manager-facing admission estimator was identified in the first pass.

Qiskit exposes backend timing inputs through objects such as
`InstructionDurations` and `Target`. These objects can help a provider or site
estimator derive gate timing and backend constraints. They do not define an
admission-control model for walltime, transfer overhead, site policy, or
control-system overhead.

Microsoft's Azure Quantum Resource Estimator is a useful architectural analog.
It estimates resources for fault-tolerant quantum programs. Its goals differ
from near-term device admission because it models physical qubits, error
correction, and fault-tolerant runtime.

The qhw-admission estimator should remain pluggable. A provider-specific
estimator can use Qiskit targets, qhw-data timing records, vendor calibration
data, measured timing models, or external estimators.

References:

- Qiskit `InstructionDurations`: https://qiskit.qotlabs.org/docs/api/qiskit/qiskit.transpiler.InstructionDurations
- Qiskit `Target`: https://qiskit.qotlabs.org/docs/api/qiskit/qiskit.transpiler.Target
- Microsoft Quantum Resource Estimator: https://learn.microsoft.com/en-us/azure/quantum/intro-to-resource-estimation
