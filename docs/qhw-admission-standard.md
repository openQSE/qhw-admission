# qhw-admission Interface Standard

## Scope

This document describes the public qhw-admission interface. The interface
defines how a caller creates an admission context, registers managed quantum
devices, selects estimators and policies, evaluates reservation requests,
creates reservations, and accounts for qtask usage.

The C binding is normative for this implementation. Other language bindings
should preserve the same object lifetimes, state transitions, return codes,
and structured outputs.

## General Interface

### Handles

| Handle | Meaning |
|---|---|
| `qhw_adm_t` | Admission context. It owns devices, policies, estimators, reservations, diagnostics, and accounting state. |
| `qhw_adm_policy_t` | Loaded policy plugin handle owned by the admission context. |
| `qhw_adm_estimator_t` | Loaded estimator plugin handle owned by the admission context. |

### Return Codes

Every public operation that can fail returns `qhw_adm_rc_t`.

| Code | Meaning |
|---|---|
| `QHW_ADM_OK` | Operation completed successfully. |
| `QHW_ADM_ERR_INVAL` | Invalid argument or malformed public structure. |
| `QHW_ADM_ERR_NOMEM` | Allocation failed. |
| `QHW_ADM_ERR_NOT_FOUND` | Requested object was not found. |
| `QHW_ADM_ERR_STATE` | Object state does not allow the operation. |
| `QHW_ADM_ERR_POLICY` | Policy callback or policy validation failed. |
| `QHW_ADM_ERR_ESTIMATOR` | Estimator callback or estimator validation failed. |
| `QHW_ADM_ERR_UNSUPPORTED` | Requested feature is unsupported. |
| `QHW_ADM_ERR_EXISTS` | Object already exists. |

### Structure Size Rule

Every public structure with a `struct_size` field must set it to at least the
size known to the caller. The implementation validates that value before
reading or writing fields. Output structures are caller allocated.

### Output Lifetime

Pointer fields returned through public output structures are context-owned
views. They remain valid until the next mutating call on the same context or
until the context is destroyed. Python wrappers copy pointer-backed fields into
Python-owned objects before returning to the caller.

## Public Structures

| Structure | Purpose |
|---|---|
| `qhw_adm_attr_t` | Context creation attributes. |
| `qhw_adm_value_t` | Tagged scalar value used by metadata and options. |
| `qhw_adm_kv_t` | Numeric key-value entry used for metadata and options. |
| `qhw_adm_baseline_t` | Baseline circuit shape used by credit and rate accounting. |
| `qhw_adm_device_profile_t` | Device profile registered with an admission context. |
| `qhw_adm_capacity_snapshot_t` | Caller-supplied live capacity projection. |
| `qhw_adm_capacity_provider_t` | Callback table for capacity snapshots. |
| `qhw_adm_capacity_view_t` | Capacity view computed by the core and passed to policies. |
| `qhw_adm_qtask_class_t` | Resource-estimation shape for repeated qtasks. |
| `qhw_adm_request_t` | Admission request evaluated or reserved by the core. |
| `qhw_adm_estimate_t` | Estimator output for a qtask class, request, or baseline. |
| `qhw_adm_decision_t` | Structured accepted, delayed, or rejected decision. |
| `qhw_adm_reservation_t` | Published reservation state and counters. |
| `qhw_adm_usage_t` | Proposed or completed qtask usage event. |
| `qhw_adm_usage_state_t` | Reservation usage snapshot. |
| `qhw_adm_compliance_t` | Overuse, underuse, and policy-action snapshot. |
| `qhw_adm_actual_usage_t` | Measured usage record for estimator feedback. |
| `qhw_adm_policy_grant_t` | Capacity grant proposed by a policy plugin. |
| `qhw_adm_policy_desc_t` | Policy plugin descriptor. |
| `qhw_adm_estimator_desc_t` | Estimator plugin descriptor. |

## C Binding

### Context Lifecycle

```c
qhw_adm_rc_t qhw_adm_create(
	const qhw_adm_attr_t *attr,
	qhw_adm_t **out_ctx);

void qhw_adm_destroy(qhw_adm_t *ctx);

const char *qhw_adm_last_error(const qhw_adm_t *ctx);

qhw_adm_rc_t qhw_adm_get_threading(
	qhw_adm_t *ctx,
	qhw_adm_threading_t *out_threading);
```

| Parameter | Direction | Meaning |
|---|---|---|
| `attr` | IN | Optional context attributes. `NULL` selects `QHW_ADM_THREAD_USER`. |
| `out_ctx` | OUT | Receives the created admission context. |
| `ctx` | IN | Admission context handle. |
| `out_threading` | OUT | Receives the context threading mode. |

`qhw_adm_last_error()` returns a diagnostic string owned by the context.

### Configuration

```c
qhw_adm_rc_t qhw_adm_load_config(
	qhw_adm_t *ctx,
	const char *path,
	uint64_t flags);

qhw_adm_rc_t qhw_adm_load_config_string(
	qhw_adm_t *ctx,
	const char *yaml_text,
	size_t yaml_len,
	uint64_t flags);
```

| Parameter | Direction | Meaning |
|---|---|---|
| `path` | IN | YAML configuration file path. |
| `yaml_text` | IN | YAML configuration buffer. |
| `yaml_len` | IN | Buffer length. Zero means NUL-terminated text. |
| `flags` | IN | Exactly one of `QHW_ADM_CONFIG_MERGE` or `QHW_ADM_CONFIG_REPLACE`. |

### Device And Estimator Configuration

```c
qhw_adm_rc_t qhw_adm_register_device(
	qhw_adm_t *ctx,
	const qhw_adm_device_profile_t *profile);

qhw_adm_rc_t qhw_adm_unregister_device(
	qhw_adm_t *ctx,
	uint64_t device_id);

qhw_adm_rc_t qhw_adm_get_device(
	qhw_adm_t *ctx,
	uint64_t device_id,
	qhw_adm_device_profile_t *out_profile);

qhw_adm_rc_t qhw_adm_set_baseline(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const qhw_adm_baseline_t *baseline);

qhw_adm_rc_t qhw_adm_set_estimator(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const char *name,
	const qhw_adm_kv_t *options,
	size_t option_count);

qhw_adm_rc_t qhw_adm_load_estimator(
	qhw_adm_t *ctx,
	const char *path,
	qhw_adm_estimator_t **out_estimator);

qhw_adm_rc_t qhw_adm_add_estimator_path(
	qhw_adm_t *ctx,
	const char *path);
```

| Parameter | Direction | Meaning |
|---|---|---|
| `profile` | IN | Device profile copied into the context. |
| `device_id` | IN | Device key used for lookup and policy selection. |
| `out_profile` | OUT | Receives a device profile view. |
| `baseline` | IN | Replacement baseline shape for the device. |
| `name` | IN | Estimator name registered in the context. |
| `options` | IN | Optional estimator configuration entries. |
| `option_count` | IN | Number of estimator option entries. |
| `path` | IN | Estimator shared object path or search directory. |
| `out_estimator` | OUT | Receives a loaded estimator handle. |

### Policy Configuration

```c
qhw_adm_rc_t qhw_adm_load_policy(
	qhw_adm_t *ctx,
	const char *path,
	qhw_adm_policy_t **out_policy);

qhw_adm_rc_t qhw_adm_add_policy_path(
	qhw_adm_t *ctx,
	const char *path);

qhw_adm_rc_t qhw_adm_set_policy(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const char *name,
	const qhw_adm_kv_t *options,
	size_t option_count);
```

| Parameter | Direction | Meaning |
|---|---|---|
| `path` | IN | Policy shared object path or search directory. |
| `out_policy` | OUT | Receives a loaded policy handle. |
| `device_id` | IN | Target device. |
| `name` | IN | Policy name. |
| `options` | IN | Optional policy configuration entries. |
| `option_count` | IN | Number of policy option entries. |

### Capacity And Estimation

```c
qhw_adm_rc_t qhw_adm_set_capacity_provider(
	qhw_adm_t *ctx,
	const qhw_adm_capacity_provider_t *provider);

qhw_adm_rc_t qhw_adm_get_capacity(
	qhw_adm_t *ctx,
	uint64_t device_id,
	uint64_t scope_id,
	qhw_adm_capacity_view_t *out_capacity);

qhw_adm_rc_t qhw_adm_estimate_qtask_class(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const qhw_adm_qtask_class_t *task_class,
	qhw_adm_estimate_t *out_estimate);

qhw_adm_rc_t qhw_adm_estimate_baseline(
	qhw_adm_t *ctx,
	uint64_t device_id,
	qhw_adm_estimate_t *out_estimate);
```

| Parameter | Direction | Meaning |
|---|---|---|
| `provider` | IN | Capacity callback table. |
| `scope_id` | IN | Optional policy scope, account, or reservation domain. |
| `out_capacity` | OUT | Receives a computed capacity view. |
| `task_class` | IN | Qtask shape to estimate. |
| `out_estimate` | OUT | Receives timing and baseline-unit estimate. |

### Reservation Operations

```c
qhw_adm_rc_t qhw_adm_evaluate(
	qhw_adm_t *ctx,
	const qhw_adm_request_t *request,
	qhw_adm_decision_t *out_decision);

qhw_adm_rc_t qhw_adm_reserve(
	qhw_adm_t *ctx,
	const qhw_adm_request_t *request,
	qhw_adm_decision_t *out_decision);

qhw_adm_rc_t qhw_adm_get_reservation(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	qhw_adm_reservation_t *out_reservation);

qhw_adm_rc_t qhw_adm_release(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	uint64_t reason_code);

qhw_adm_rc_t qhw_adm_cancel(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	uint64_t reason_code);

qhw_adm_rc_t qhw_adm_renew(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	uint64_t now_ns,
	uint64_t ttl_ns);

qhw_adm_rc_t qhw_adm_expire(
	qhw_adm_t *ctx,
	uint64_t now_ns,
	size_t *out_expired_count);
```

| Parameter | Direction | Meaning |
|---|---|---|
| `request` | IN | Admission request. |
| `out_decision` | OUT | Receives structured decision. |
| `reservation_id` | IN | Reservation identifier. |
| `out_reservation` | OUT | Receives reservation state. |
| `reason_code` | IN | Site or API reason code for terminal transition. |
| `now_ns` | IN | Event time. Zero uses the context clock where documented. |
| `ttl_ns` | IN | Renewal duration. |
| `out_expired_count` | OUT | Receives number of expired reservations. |

`qhw_adm_evaluate()` is a dry run. `qhw_adm_reserve()` commits accepted
capacity and publishes a reservation.

### Usage Accounting

```c
qhw_adm_rc_t qhw_adm_authorize_usage(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	const qhw_adm_usage_t *usage,
	qhw_adm_decision_t *out_decision);

qhw_adm_rc_t qhw_adm_consume(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	const qhw_adm_usage_t *usage,
	qhw_adm_decision_t *out_decision);

qhw_adm_rc_t qhw_adm_return_usage(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	const qhw_adm_usage_t *usage);

qhw_adm_rc_t qhw_adm_get_usage(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	qhw_adm_usage_state_t *out_usage);

qhw_adm_rc_t qhw_adm_get_compliance(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	qhw_adm_compliance_t *out_compliance);

qhw_adm_rc_t qhw_adm_record_actual(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	const qhw_adm_actual_usage_t *actual);
```

| Parameter | Direction | Meaning |
|---|---|---|
| `usage` | IN | Estimated or returned usage event. |
| `out_usage` | OUT | Receives aggregate usage state. |
| `out_compliance` | OUT | Receives compliance state. |
| `actual` | IN | Measured execution, compile, transfer, and control timing. |

`qhw_adm_authorize_usage()` is a dry run. `qhw_adm_consume()` records
pre-submit consumption. `qhw_adm_record_actual()` records measured feedback
after completion.

## Python Binding

The Python package exposes a small facade over the C binding:

```python
from qhw_admission import AdmissionContext

with AdmissionContext() as ctx:
    ctx.load_config("admission.yaml")
    decision = ctx.reserve(request)
```

The Python facade copies string and metadata outputs into Python-owned objects.
It raises `AdmissionError` when the C API returns a nonzero status.
