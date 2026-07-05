# qhw-admission

`qhw-admission` is a C admission-control library for managed quantum
resources. It evaluates job-level requests against device profiles, resource
estimators, admission policies, and active reservations. It returns structured
decisions that a resource manager, runtime service, or simulator can translate
into site behavior.

The repository builds:

- `libqhw_admission.so`
- standard policy plugins: `unlimited`, `credit`, and `rate`
- SWIG-based Python bindings
- C and Python tests

## Build

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Build without the standard policy plugins:

```bash
cmake -S . -B build-noplugins -DQHW_ADM_BUILD_PLUGINS=OFF
cmake --build build-noplugins -j
```

## Test

Run the full test suite:

```bash
ctest --test-dir build --output-on-failure
```

Run one test by name:

```bash
ctest --test-dir build -R test_credit --output-on-failure
ctest --test-dir build -R test_python_credit --output-on-failure
ctest --test-dir build -R test_rate --output-on-failure
ctest --test-dir build -R test_python_rate --output-on-failure
ctest --test-dir build -R test_python_installed_package --output-on-failure
```

Run the no-plugin configuration:

```bash
ctest --test-dir build-noplugins --output-on-failure
```

## Install

```bash
cmake --install build --prefix install
```

The install tree contains:

```text
install/include/qhw_admission/
install/lib/libqhw_admission.so
install/lib/qhw_admission/policies/qhw_adm_unlimited.so
install/lib/qhw_admission/policies/qhw_adm_credit.so
install/lib/qhw_admission/policies/qhw_adm_rate.so
install/share/man/man3/
```

Build a Python wheel when `scikit-build-core` is available:

```bash
python3 -m pip wheel . --no-deps --no-build-isolation -w dist
python3 -m pip install dist/qhw_admission-*.whl
```

For development, build the package in editable mode:

```bash
python3 -m pip install -e . --no-build-isolation
```

## Python Quick Start

```python
from qhw_admission import (
    AdmissionContext,
    AdmissionRequest,
    Baseline,
    DECISION_ACCEPTED,
    DeviceProfile,
    QtaskClass,
    WORKLOAD_HYBRID_JOB,
)

baseline = Baseline(
    qubit_count=4,
    depth=10,
    one_q_gate_count=10,
    two_q_gate_count=5,
    shots=100,
    measurement_count=2,
)

profile = DeviceProfile(
    device_id=7,
    time_span_ns=1_000_000_000,
    baseline=baseline,
    max_qubits=20,
    max_shots=10_000,
    one_q_gate_ns=20,
    two_q_gate_ns=100,
    measurement_ns=1000,
    total_credits=10,
)

task = QtaskClass(
    class_id=11,
    count=2,
    qubit_count=4,
    depth=12,
    one_q_gate_count=20,
    two_q_gate_count=10,
    shots=100,
    measurement_count=2,
)

request = AdmissionRequest(
    request_id=42,
    device_id=7,
    user_id=1000,
    job_id=2000,
    scope_id=3,
    workload_kind=WORKLOAD_HYBRID_JOB,
    walltime_ns=2_000_000_000,
    task_class=task,
)

with AdmissionContext() as ctx:
    ctx.register_device(profile)
    ctx.add_policy_path("build/policies")
    ctx.set_policy(7, "credit")

    decision = ctx.reserve(request)
    if decision.decision == DECISION_ACCEPTED:
        print("reservation", decision.reservation_id)
```

## C Quick Start

```c
#include <qhw_admission/qhw_admission.h>

qhw_adm_t *ctx = NULL;
qhw_adm_baseline_t baseline = {
	.struct_size = sizeof(baseline),
	.qubit_count = 4,
	.depth = 10,
	.one_q_gate_count = 10,
	.two_q_gate_count = 5,
	.shots = 100,
	.measurement_count = 2,
};
qhw_adm_device_profile_t profile = {
	.struct_size = sizeof(profile),
	.device_id = 7,
	.time_span_ns = 1000000000ULL,
	.baseline = baseline,
	.max_qubits = 20,
	.max_shots = 10000,
	.one_q_gate_ns = 20,
	.two_q_gate_ns = 100,
	.measurement_ns = 1000,
	.total_credits = 10,
};
qhw_adm_qtask_class_t task = {
	.struct_size = sizeof(task),
	.class_id = 11,
	.count = 2,
	.qubit_count = 4,
	.depth = 12,
	.one_q_gate_count = 20,
	.two_q_gate_count = 10,
	.shots = 100,
	.measurement_count = 2,
};
qhw_adm_request_t request = {
	.struct_size = sizeof(request),
	.request_id = 42,
	.device_id = 7,
	.user_id = 1000,
	.job_id = 2000,
	.scope_id = 3,
	.workload_kind = QHW_ADM_WORKLOAD_HYBRID_JOB,
	.walltime_ns = 2000000000ULL,
	.task_class_count = 1,
	.task_classes = &task,
};
qhw_adm_decision_t decision = {
	.struct_size = sizeof(decision),
};

if (qhw_adm_create(NULL, &ctx) != QHW_ADM_OK)
	return 1;
qhw_adm_register_device(ctx, &profile);
qhw_adm_add_policy_path(ctx, "build/policies");
qhw_adm_set_policy(ctx, profile.device_id, "credit", NULL, 0);
qhw_adm_reserve(ctx, &request, &decision);
qhw_adm_destroy(ctx);
```

## YAML Configuration

The same device and policy setup can be loaded from YAML:

```yaml
plugin_paths:
  policies: ["build/policies"]
devices:
  - device_id: 7
    max_qubits: 20
    max_shots: 10000
    time_span_ns: 1000000000
    baseline:
      qubit_count: 4
      depth: 10
      one_q_gate_count: 10
      two_q_gate_count: 5
      measurement_count: 2
      shots: 100
    timing:
      one_q_gate_ns: 20
      two_q_gate_ns: 100
      measurement_ns: 1000
      compile_ns: 1000
      control_overhead_ns: 200
      provider_overhead_ns: 300
    credit:
      total_credits: 10
    policy:
      name: credit
```

Load it from C:

```c
qhw_adm_load_config(ctx, "admission.yaml", QHW_ADM_CONFIG_MERGE);
```

Load it from Python:

```python
ctx.load_config("admission.yaml")
```

## Policies

| Policy | Capacity model | Typical use |
|---|---|---|
| `unlimited` | Creates a reservation for every valid request. | Functional testing and deployments where another layer owns QoS. |
| `credit` | Reserves finite baseline units. | Bounded admission for a shared QPU over an accounting window. |
| `rate` | Reserves finite throughput slices. | Hybrid workloads that need throughput during a walltime window. |

Credit capacity uses explicit `total_credits` when nonzero. Otherwise, it is
derived from the configured baseline circuit and `time_span_ns`.

Rate capacity uses explicit `device_rate` when nonzero. Otherwise, it is
derived from the configured baseline circuit and `time_span_ns`.

See [docs/policies.md](docs/policies.md) for policy details.

## Usage Accounting Flow

Admission accepts a job-level envelope. A controller then accounts for qtasks
as they move through scheduling and device submission.

```text
reserve job
authorize qtask usage
schedule qtask
consume qtask usage before device submission
record actual usage after completion
return unused usage on cancellation or partial execution
release, cancel, renew, or expire the reservation
```

The scheduler remains responsible for task ordering. Admission tracks the
reservation envelope and compliance state.

## More Documentation

- [docs/detailed-design.md](docs/detailed-design.md)
- [docs/qhw-admission-standard.md](docs/qhw-admission-standard.md)
- [docs/policies.md](docs/policies.md)
- `man qhw_admission`
