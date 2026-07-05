#ifndef QHW_ADMISSION_TYPES_H
#define QHW_ADMISSION_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include <qhw_admission/qhw_admission_abi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qhw_adm qhw_adm_t;
typedef struct qhw_adm_policy qhw_adm_policy_t;
typedef struct qhw_adm_estimator qhw_adm_estimator_t;
typedef struct qhw_adm_request qhw_adm_request_t;
typedef struct qhw_adm_reservation qhw_adm_reservation_t;
typedef struct qhw_adm_actual_usage qhw_adm_actual_usage_t;

typedef enum qhw_adm_rc {
	QHW_ADM_OK = 0,
	QHW_ADM_ERR_INVAL = -1,
	QHW_ADM_ERR_NOMEM = -2,
	QHW_ADM_ERR_NOT_FOUND = -3,
	QHW_ADM_ERR_STATE = -4,
	QHW_ADM_ERR_POLICY = -5,
	QHW_ADM_ERR_ESTIMATOR = -6,
	QHW_ADM_ERR_UNSUPPORTED = -7,
	QHW_ADM_ERR_EXISTS = -8
} qhw_adm_rc_t;

typedef enum qhw_adm_threading {
	QHW_ADM_THREAD_USER = 0,
	QHW_ADM_THREAD_SAFE = 1
} qhw_adm_threading_t;

typedef enum qhw_adm_device_state {
	QHW_ADM_DEVICE_AVAILABLE = 1,
	QHW_ADM_DEVICE_UNAVAILABLE = 2,
	QHW_ADM_DEVICE_DRAINING = 3,
	QHW_ADM_DEVICE_MAINTENANCE = 4
} qhw_adm_device_state_t;

typedef enum qhw_adm_workload_kind {
	QHW_ADM_WORKLOAD_QUANTUM_JOB = 1,
	QHW_ADM_WORKLOAD_HYBRID_JOB = 2
} qhw_adm_workload_kind_t;

typedef enum qhw_adm_value_type {
	QHW_ADM_VALUE_U64 = 1,
	QHW_ADM_VALUE_I64 = 2,
	QHW_ADM_VALUE_F64 = 3,
	QHW_ADM_VALUE_BOOL = 4,
	QHW_ADM_VALUE_STRING = 5,
	QHW_ADM_VALUE_PTR = 6
} qhw_adm_value_type_t;

typedef enum qhw_adm_reason {
	QHW_ADM_REASON_NONE = 0,
	QHW_ADM_REASON_ACCEPTED = 1,
	QHW_ADM_REASON_DEVICE_NOT_FOUND = 2,
	QHW_ADM_REASON_DEVICE_UNAVAILABLE = 3,
	QHW_ADM_REASON_INVALID_REQUEST = 4,
	QHW_ADM_REASON_ESTIMATOR_FAILED = 5,
	QHW_ADM_REASON_POLICY_FAILED = 6,
	QHW_ADM_REASON_INSUFFICIENT_CREDITS = 7,
	QHW_ADM_REASON_INSUFFICIENT_RATE = 8,
	QHW_ADM_REASON_REQUEST_TOO_LARGE = 9,
	QHW_ADM_REASON_WALLTIME_INFEASIBLE = 10,
	QHW_ADM_REASON_RESERVATION_NOT_FOUND = 11,
	QHW_ADM_REASON_RESERVATION_TERMINAL = 12,
	QHW_ADM_REASON_OVER_LIMIT = 13,
	QHW_ADM_REASON_UNSUPPORTED = 14,
	QHW_ADM_REASON_OBJECT_EXISTS = 15,
	QHW_ADM_REASON_ACTIVE_RESERVATIONS = 16,
	QHW_ADM_REASON_SCOPE_LIMIT = 17,
	QHW_ADM_REASON_USAGE_NOT_AUTHORIZED = 18
} qhw_adm_reason_t;

typedef enum qhw_adm_meta_key {
	QHW_ADM_META_WORKLOAD_KIND = 1,
	QHW_ADM_META_SESSION_ID = 2,
	QHW_ADM_META_SCOPE_ID = 3,
	QHW_ADM_META_DEADLINE_NS = 4,
	QHW_ADM_META_LATEST_START_NS = 5,
	QHW_ADM_META_LATEST_FINISH_NS = 6,
	QHW_ADM_META_QOS_CLASS = 7,
	QHW_ADM_META_LAYER_COUNT = 8,
	QHW_ADM_META_BATCH_COUNT = 9,
	QHW_ADM_META_PROVIDER_BATCHING = 10,
	QHW_ADM_META_COMPILE_NS = 11,
	QHW_ADM_META_LOWERING_NS = 12,
	QHW_ADM_META_TRANSFER_NS = 13,
	QHW_ADM_META_CONTROL_OVERHEAD_NS = 14,
	QHW_ADM_META_PROVIDER_OVERHEAD_NS = 15,
	QHW_ADM_META_ONE_Q_GATE_NS = 16,
	QHW_ADM_META_TWO_Q_GATE_NS = 17,
	QHW_ADM_META_MEASUREMENT_NS = 18,
	QHW_ADM_META_ONE_Q_GATE_TRANSFER_NS = 19,
	QHW_ADM_META_TWO_Q_GATE_TRANSFER_NS = 20,
	QHW_ADM_META_MEASUREMENT_TRANSFER_NS = 21,
	QHW_ADM_META_LOGICAL_QUBITS = 22,
	QHW_ADM_META_LOGICAL_CYCLES = 23,
	QHW_ADM_META_T_COUNT = 24,
	QHW_ADM_META_T_DEPTH = 25,
	QHW_ADM_META_TARGET_LOGICAL_ERROR_PPM = 26,
	QHW_ADM_META_CODE_FAMILY = 27,
	QHW_ADM_META_CODE_DISTANCE = 28,
	QHW_ADM_META_MAGIC_STATE_COUNT = 29,
	QHW_ADM_META_DECODER_OVERHEAD_NS = 30,
	QHW_ADM_META_CLASSICAL_CONTROL_OVERHEAD_NS = 31,
	QHW_ADM_META_ESTIMATOR_VERSION = 32,
	QHW_ADM_META_OBSERVED_DEVICE_NS = 33,
	QHW_ADM_META_CONSUMED_CREDITS = 34,
	QHW_ADM_META_CONSUMED_RATE = 35,
	QHW_ADM_META_UNUSED_CAPACITY = 36,
	QHW_ADM_META_OVER_LIMIT_EVENTS = 37
} qhw_adm_meta_key_t;

typedef struct qhw_adm_value {
	uint32_t type;
	uint32_t flags;
	union {
		uint64_t u64;
		int64_t i64;
		double f64;
		uint32_t boolean;
		const char *string;
		void *ptr;
	} value;
} qhw_adm_value_t;

typedef struct qhw_adm_kv {
	uint64_t key;
	qhw_adm_value_t value;
} qhw_adm_kv_t;

typedef struct qhw_adm_attr {
	size_t struct_size;
	qhw_adm_threading_t threading;
	const qhw_adm_kv_t *options;
	size_t option_count;
} qhw_adm_attr_t;

typedef struct qhw_adm_baseline {
	size_t struct_size;
	uint32_t qubit_count;
	uint64_t depth;
	uint64_t one_q_gate_count;
	uint64_t two_q_gate_count;
	uint64_t shots;
	uint64_t measurement_count;
} qhw_adm_baseline_t;

typedef struct qhw_adm_device_profile {
	size_t struct_size;
	uint64_t device_id;
	uint64_t time_span_ns;
	qhw_adm_baseline_t baseline;
	uint32_t max_qubits;
	uint64_t max_shots;
	uint64_t one_q_gate_ns;
	uint64_t two_q_gate_ns;
	uint64_t measurement_ns;
	uint64_t one_q_gate_transfer_ns;
	uint64_t two_q_gate_transfer_ns;
	uint64_t measurement_transfer_ns;
	uint64_t compile_ns;
	uint64_t control_overhead_ns;
	uint64_t provider_overhead_ns;
	uint64_t total_credits;
	uint64_t device_rate;
	uint32_t concurrent_jobs;
	uint64_t default_ttl_ns;
	const qhw_adm_kv_t *metadata;
	size_t metadata_count;
} qhw_adm_device_profile_t;

typedef struct qhw_adm_qtask_class {
	size_t struct_size;
	uint64_t class_id;
	uint64_t count;
	uint32_t qubit_count;
	uint64_t depth;
	uint64_t one_q_gate_count;
	uint64_t two_q_gate_count;
	uint64_t shots;
	uint64_t measurement_count;
	const qhw_adm_kv_t *metadata;
	size_t metadata_count;
} qhw_adm_qtask_class_t;

typedef struct qhw_adm_estimate {
	size_t struct_size;
	uint64_t execution_ns;
	uint64_t measurement_ns;
	uint64_t compile_ns;
	uint64_t transfer_ns;
	uint64_t control_overhead_ns;
	uint64_t total_ns;
	uint64_t baseline_units;
	uint32_t confidence_ppm;
} qhw_adm_estimate_t;

typedef struct qhw_adm_estimator_desc qhw_adm_estimator_desc_t;

typedef const qhw_adm_estimator_desc_t *(
	*qhw_adm_estimator_plugin_fn)(void);

typedef qhw_adm_rc_t (*qhw_adm_estimator_init_fn)(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_kv_t *options,
	size_t option_count,
	void **out_state);

typedef void (*qhw_adm_estimator_destroy_fn)(void *state);

typedef qhw_adm_rc_t (*qhw_adm_estimator_configure_fn)(
	void *state,
	const qhw_adm_kv_t *options,
	size_t option_count);

typedef qhw_adm_rc_t (*qhw_adm_estimator_estimate_task_fn)(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_qtask_class_t *task_class,
	qhw_adm_estimate_t *out_estimate);

typedef qhw_adm_rc_t (*qhw_adm_estimator_estimate_request_fn)(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	qhw_adm_estimate_t *out_estimate);

typedef qhw_adm_rc_t (*qhw_adm_estimator_estimate_baseline_fn)(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_baseline_t *baseline,
	qhw_adm_estimate_t *out_estimate);

typedef qhw_adm_rc_t (*qhw_adm_estimator_validate_request_fn)(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request);

typedef qhw_adm_rc_t (*qhw_adm_estimator_record_actual_fn)(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_actual_usage_t *actual);

struct qhw_adm_estimator_desc {
	size_t struct_size;
	uint32_t abi_version;
	const char *name;
	uint64_t capabilities;
	qhw_adm_estimator_init_fn init;
	qhw_adm_estimator_destroy_fn destroy;
	qhw_adm_estimator_configure_fn configure;
	qhw_adm_estimator_estimate_task_fn estimate_task;
	qhw_adm_estimator_estimate_request_fn estimate_request;
	qhw_adm_estimator_estimate_baseline_fn estimate_baseline;
	qhw_adm_estimator_validate_request_fn validate_request;
	qhw_adm_estimator_record_actual_fn record_actual;
};

#ifdef __cplusplus
}
#endif

#endif
