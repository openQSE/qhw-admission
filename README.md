# qhw-admission

`qhw-admission` is a C admission-control library for quantum resources. It
builds a shared core library, loadable policy plugins, and SWIG-based Python
bindings.

## Build

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

## Test

Run the full C and Python test suite:

```bash
ctest --test-dir build --output-on-failure
```

Run one policy test directly:

```bash
ctest --test-dir build -R test_credit --output-on-failure
ctest --test-dir build -R test_python_credit --output-on-failure
```

Build without the standard policy plugins:

```bash
cmake -S . -B build-noplugins -DQHW_ADM_BUILD_PLUGINS=OFF
cmake --build build-noplugins -j
ctest --test-dir build-noplugins --output-on-failure
```

## Install

```bash
cmake --install build --prefix install
```

The core library installs under `install/lib`. Standard policy plugins install
under `install/lib/qhw_admission/policies`.

## Credit Policy Configuration

The credit policy admits reservations against a finite credit budget. One
credit represents one baseline circuit unit. Request demand is computed by the
selected estimator and reported as baseline units.

The credit budget can be configured explicitly or derived automatically.
Explicit credits take precedence.

```text
if total_credits != 0:
  credit_capacity = total_credits
else:
  estimated_baseline_ns = estimator(device_profile, baseline_circuit)
  credit_capacity = floor(time_span_ns / estimated_baseline_ns)
```

### Configurable Inputs

| Input | C field or API | YAML key | Purpose |
|---|---|---|---|
| Explicit credits | `qhw_adm_device_profile_t.total_credits` | `credit.total_credits` | Overrides derived credit capacity when nonzero. |
| Estimator | `qhw_adm_set_estimator()` | `estimator.name` | Computes baseline and request cost. |
| Timing window | `qhw_adm_device_profile_t.time_span_ns` | `time_span_ns` | Accounting window used when deriving credits. |
| Device profile | `qhw_adm_register_device()` | `devices[]` | Supplies timing, limits, and baseline shape. |
| Baseline circuit | `qhw_adm_device_profile_t.baseline` | `baseline` | Defines one credit unit. |
| Policy | `qhw_adm_set_policy(..., "credit", ...)` | `policy.name: credit` | Selects credit admission for the device. |
| Reservation TTL | `QHW_ADM_OPT_CREDIT_RESERVATION_TTL_NS` | `policy.options.reservation_ttl_ns` | Optional policy-selected reservation lease duration. |
| Overcommit enable | `QHW_ADM_OPT_CREDIT_ALLOW_OVERCOMMIT` | `policy.options.allow_overcommit` | Enables bounded overcommit. |
| Overcommit credits | `QHW_ADM_OPT_CREDIT_OVERCOMMIT_CREDITS` | `policy.options.overcommit_credits` | Adds an absolute overcommit limit. |
| Overcommit PPM | `QHW_ADM_OPT_CREDIT_OVERCOMMIT_PPM` | `policy.options.overcommit_ppm` | Adds a proportional overcommit limit from `0` to `1000000` PPM. |

### YAML Example

This example derives credits from `time_span_ns` because `total_credits` is
zero.

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
      one_q_gate_transfer_ns: 1
      two_q_gate_transfer_ns: 4
      measurement_transfer_ns: 10
      compile_ns: 1000
      control_overhead_ns: 200
      provider_overhead_ns: 300
    credit:
      total_credits: 0
    policy:
      name: credit
      options:
        allow_overcommit: false
```

This example overrides derived capacity:

```yaml
credit:
  total_credits: 100
```

### C Example

```c
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
	.total_credits = 0,
};

qhw_adm_register_device(ctx, &profile);
qhw_adm_add_policy_path(ctx, "build/policies");
qhw_adm_set_policy(ctx, profile.device_id, "credit", NULL, 0);
```

Set `profile.total_credits` to a nonzero value when the site wants an explicit
credit budget.
