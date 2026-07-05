# qhw-admission Policies

## Overview

Policies decide how an estimated request consumes device capacity. The core
validates requests, computes estimates, builds a capacity view, and invokes the
selected policy. Policies return structured decisions and reservation grants.
The core commits accepted grants to the reservation ledger.

Standard policies are loaded from shared objects:

```text
qhw_adm_unlimited.so
qhw_adm_credit.so
qhw_adm_rate.so
```

Load a policy directory from C:

```c
qhw_adm_add_policy_path(ctx, "build/policies");
qhw_adm_set_policy(ctx, device_id, "credit", NULL, 0);
```

Load a policy directory from Python:

```python
ctx.add_policy_path("build/policies")
ctx.set_policy(device_id, "credit")
```

## Unlimited

The `unlimited` policy accepts every valid request when the device is
available. It creates reservations and reports reservation state, but it does
not reserve finite credits or rate units.

Use it for functional tests, interface bring-up, and deployments where another
layer already enforces admission policy.

## Credit

The `credit` policy admits requests against a finite credit budget. One credit
represents one configured baseline circuit unit. The selected estimator converts
each request into `baseline_units`. The policy reserves that many credits when
capacity is available.

Credit capacity is configured in two ways.

| Source | Behavior |
|---|---|
| `qhw_adm_device_profile_t.total_credits` | Nonzero values define the credit capacity directly. |
| `time_span_ns` and baseline estimate | Used when `total_credits` is zero. Capacity is `floor(time_span_ns / estimated_baseline_ns)`. |

Policy options:

| Option | Meaning |
|---|---|
| `QHW_ADM_OPT_CREDIT_RESERVATION_TTL_NS` | Policy-selected reservation lease duration. |
| `QHW_ADM_OPT_CREDIT_ALLOW_OVERCOMMIT` | Enables bounded overcommit. |
| `QHW_ADM_OPT_CREDIT_OVERCOMMIT_CREDITS` | Absolute extra credits available above the device budget. |
| `QHW_ADM_OPT_CREDIT_OVERCOMMIT_PPM` | Proportional extra credits from `0` to `1000000` PPM. |

YAML example:

```yaml
devices:
  - device_id: 7
    time_span_ns: 1000000000
    credit:
      total_credits: 10
    policy:
      name: credit
      options:
        allow_overcommit: false
```

## Rate

The `rate` policy admits requests against finite device throughput. A rate unit
represents one baseline circuit unit per accounting window. The policy grants
one or more slices of device throughput to a reservation.

Rate capacity is configured in two ways.

| Source | Behavior |
|---|---|
| `qhw_adm_device_profile_t.device_rate` | Nonzero values define rate capacity directly. |
| `time_span_ns` and baseline estimate | Used when `device_rate` is zero. Capacity is `ceil(time_span_ns / estimated_baseline_ns)`. |

The policy computes the request's quantum budget from walltime:

```text
quantum_budget_ns =
  walltime_ns - classical_runtime_ns - overhead_ns
```

Then it computes the requested rate:

```text
rate_required =
  ceil((baseline_units_required * time_span_ns) / quantum_budget_ns)
```

Policy options:

| Option | Meaning |
|---|---|
| `QHW_ADM_OPT_RATE_RESERVATION_TTL_NS` | Policy-selected reservation lease duration. |
| `QHW_ADM_OPT_RATE_SLICE` | Minimum allocatable rate grant. |

YAML example:

```yaml
devices:
  - device_id: 7
    time_span_ns: 1000000000
    rate:
      device_rate: 4
      concurrent_jobs: 2
    policy:
      name: rate
      options:
        rate_slice: 2
```

## Usage Accounting

Credit and rate policies participate in usage accounting. A controller can ask
for authorization before a qtask reaches the scheduler, consume capacity before
device submission, return unused capacity, and record measured usage after
completion.

```text
authorize_usage()
consume()
return_usage()
record_actual()
get_usage()
get_compliance()
```

Rate usage uses `qhw_adm_usage_t.event_time_ns` to select the accounting
window. If it is zero, the context clock supplies the event time.

## Plugin Contract

Site policies implement `qhw_adm_policy_desc_t` and export
`qhw_adm_policy_plugin`. The core validates the descriptor before the
policy is registered. Policy callbacks receive copied public structures and
return structured decisions or grants. The core owns reservation publication,
ledger commits, rollback, and public output lifetimes.
