#include <qhw_admission/qhw_admission.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "check failed at %s:%d: %s\n", \
				__FILE__, __LINE__, #cond); \
			return 1; \
		} \
	} while (0)

static qhw_adm_baseline_t make_baseline(void)
{
	qhw_adm_baseline_t baseline = {
		.struct_size = sizeof(baseline),
		.qubit_count = 4,
		.depth = 10,
		.one_q_gate_count = 10,
		.two_q_gate_count = 5,
		.shots = 100,
		.measurement_count = 2,
	};

	return baseline;
}

static qhw_adm_device_profile_t make_profile(void)
{
	qhw_adm_device_profile_t profile = {
		.struct_size = sizeof(profile),
		.device_id = 7,
		.time_span_ns = 1000000000ULL,
		.baseline = make_baseline(),
		.max_qubits = 20,
		.max_shots = 10000,
		.one_q_gate_ns = 20,
		.two_q_gate_ns = 100,
		.measurement_ns = 1000,
		.one_q_gate_transfer_ns = 1,
		.two_q_gate_transfer_ns = 4,
		.measurement_transfer_ns = 10,
		.compile_ns = 1000,
		.control_overhead_ns = 200,
		.provider_overhead_ns = 300,
		.total_credits = 100000,
		.device_rate = 0,
		.concurrent_jobs = 4,
		.default_ttl_ns = 60000000000ULL,
	};

	return profile;
}

static qhw_adm_qtask_class_t make_task(void)
{
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

	return task;
}

static qhw_adm_estimate_t make_estimate_output(void)
{
	qhw_adm_estimate_t estimate;

	memset(&estimate, 0, sizeof(estimate));
	estimate.struct_size = sizeof(estimate);
	return estimate;
}

static qhw_adm_device_profile_t make_profile_output(void)
{
	qhw_adm_device_profile_t profile;

	memset(&profile, 0, sizeof(profile));
	profile.struct_size = sizeof(profile);
	return profile;
}

static int test_register_and_get_device(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_device_profile_t stored = make_profile_output();
	qhw_adm_kv_t metadata[1];
	char label[16] = "iqm-dev";

	memset(metadata, 0, sizeof(metadata));
	metadata[0].key = QHW_ADM_META_SESSION_ID;
	metadata[0].value.type = QHW_ADM_VALUE_STRING;
	metadata[0].value.value.string = label;
	profile.metadata = metadata;
	profile.metadata_count = 1;

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	(void)snprintf(label, sizeof(label), "changed");
	CHECK(qhw_adm_get_device(ctx, profile.device_id, &stored) ==
		QHW_ADM_OK);
	CHECK(stored.device_id == profile.device_id);
	CHECK(stored.metadata_count == 1);
	CHECK(stored.metadata[0].value.value.string != label);
	CHECK(strcmp(stored.metadata[0].value.value.string, "iqm-dev") == 0);
	CHECK(strcmp(qhw_adm_last_error(ctx), "") == 0);
	CHECK(qhw_adm_unregister_device(ctx, profile.device_id) == QHW_ADM_OK);
	stored = make_profile_output();
	CHECK(qhw_adm_get_device(ctx, profile.device_id, &stored) ==
		QHW_ADM_ERR_NOT_FOUND);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_baseline_estimate(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_estimate_t estimate = make_estimate_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_estimate_baseline(ctx, profile.device_id, &estimate) ==
		QHW_ADM_OK);
	CHECK(estimate.execution_ns == 270000);
	CHECK(estimate.measurement_ns == 200000);
	CHECK(estimate.transfer_ns == 50);
	CHECK(estimate.compile_ns == 1000);
	CHECK(estimate.control_overhead_ns == 200);
	CHECK(estimate.total_ns == 271550);
	CHECK(estimate.baseline_units == 1);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_task_estimate(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_estimate_t estimate = make_estimate_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_set_estimator(ctx, profile.device_id, "baseline",
		NULL, 0) == QHW_ADM_OK);
	CHECK(qhw_adm_estimate_qtask_class(ctx, profile.device_id, &task,
		&estimate) == QHW_ADM_OK);
	CHECK(estimate.execution_ns == 680000);
	CHECK(estimate.measurement_ns == 400000);
	CHECK(estimate.transfer_ns == 160);
	CHECK(estimate.compile_ns == 2000);
	CHECK(estimate.control_overhead_ns == 400);
	CHECK(estimate.total_ns == 683160);
	CHECK(estimate.baseline_units == 3);

	profile.baseline.one_q_gate_count = task.one_q_gate_count;
	profile.baseline.two_q_gate_count = task.two_q_gate_count;
	CHECK(qhw_adm_set_baseline(ctx, profile.device_id,
		&profile.baseline) == QHW_ADM_OK);
	CHECK(qhw_adm_estimate_qtask_class(ctx, profile.device_id, &task,
		&estimate) == QHW_ADM_OK);
	CHECK(estimate.baseline_units == 2);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_metadata_override(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_estimate_t estimate = make_estimate_output();
	qhw_adm_kv_t metadata[1];

	memset(metadata, 0, sizeof(metadata));
	metadata[0].key = QHW_ADM_META_ONE_Q_GATE_NS;
	metadata[0].value.type = QHW_ADM_VALUE_U64;
	metadata[0].value.value.u64 = 40;
	task.metadata = metadata;
	task.metadata_count = 1;

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_estimate_qtask_class(ctx, profile.device_id, &task,
		&estimate) == QHW_ADM_OK);
	CHECK(estimate.total_ns == 763160);
	CHECK(estimate.baseline_units == 3);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_invalid_estimate(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_estimate_t estimate = make_estimate_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	task.shots = profile.max_shots + 1;
	CHECK(qhw_adm_estimate_qtask_class(ctx, profile.device_id, &task,
		&estimate) == QHW_ADM_ERR_INVAL);
	task = make_task();
	task.one_q_gate_count = UINT64_MAX;
	CHECK(qhw_adm_estimate_qtask_class(ctx, profile.device_id, &task,
		&estimate) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_output_size_guards(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_device_profile_t stored;
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_estimate_t estimate;

	memset(&stored, 0, sizeof(stored));
	memset(&estimate, 0, sizeof(estimate));

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_get_device(ctx, profile.device_id, &stored) ==
		QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_estimate_baseline(ctx, profile.device_id, &estimate) ==
		QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_estimate_qtask_class(ctx, profile.device_id, &task,
		&estimate) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);
	return 0;
}

static const char *valid_yaml =
	"devices:\n"
	"  - device_id: 8\n"
	"    max_qubits: 20\n"
	"    max_shots: 10000\n"
	"    time_span_ns: 1000000000\n"
	"    baseline:\n"
	"      qubit_count: 4\n"
	"      depth: 10\n"
	"      one_q_gate_count: 10\n"
	"      two_q_gate_count: 5\n"
	"      measurement_count: 2\n"
	"      shots: 100\n"
	"    timing:\n"
	"      one_q_gate_ns: 20\n"
	"      two_q_gate_ns: 100\n"
	"      measurement_ns: 1000\n"
	"      one_q_gate_transfer_ns: 1\n"
	"      two_q_gate_transfer_ns: 4\n"
	"      measurement_transfer_ns: 10\n"
	"      compile_ns: 1000\n"
	"      control_overhead_ns: 200\n"
	"      provider_overhead_ns: 300\n"
	"    credit:\n"
	"      total_credits: 100000\n"
	"    rate:\n"
	"      device_rate: 0\n"
	"      concurrent_jobs: 4\n"
	"    estimator:\n"
	"      name: baseline\n";

static const char *multi_device_yaml =
	"devices:\n"
	"  - device_id: 8\n"
	"    max_qubits: 20\n"
	"    max_shots: 10000\n"
	"    baseline:\n"
	"      qubit_count: 4\n"
	"      depth: 10\n"
	"      one_q_gate_count: 10\n"
	"      two_q_gate_count: 5\n"
	"      measurement_count: 2\n"
	"      shots: 100\n"
	"    timing:\n"
	"      one_q_gate_ns: 20\n"
	"      two_q_gate_ns: 100\n"
	"      measurement_ns: 1000\n"
	"    estimator:\n"
	"      name: baseline\n"
	"  - device_id: 9\n"
	"    max_qubits: 20\n"
	"    max_shots: 10000\n"
	"    baseline:\n"
	"      qubit_count: 4\n"
	"      depth: 10\n"
	"      one_q_gate_count: 10\n"
	"      two_q_gate_count: 5\n"
	"      measurement_count: 2\n"
	"      shots: 100\n"
	"    timing:\n"
	"      one_q_gate_ns: 30\n"
	"      two_q_gate_ns: 100\n"
	"      measurement_ns: 1000\n"
	"    estimator:\n"
	"      name: baseline\n";

static const char *invalid_yaml =
	"devices:\n"
	"  - device_id: 9\n"
	"    max_qubits: 20\n"
	"    baseline:\n"
	"      qubit_count: 4\n"
	"      measurement_count: 2\n"
	"      shots: 100\n"
	"    timing:\n"
	"      one_q_gate_ns: 20\n"
	"      two_q_gate_ns: 100\n";

static const char *negative_yaml =
	"devices:\n"
	"  - device_id: 9\n"
	"    max_qubits: 20\n"
	"    max_shots: 10000\n"
	"    baseline:\n"
	"      qubit_count: 4\n"
	"      depth: 10\n"
	"      one_q_gate_count: 10\n"
	"      two_q_gate_count: 5\n"
	"      measurement_count: 2\n"
	"      shots: 100\n"
	"    timing:\n"
	"      one_q_gate_ns: -1\n"
	"      two_q_gate_ns: 100\n"
	"      measurement_ns: 1000\n";

static const char *overflow_yaml =
	"devices:\n"
	"  - device_id: 9\n"
	"    max_qubits: 20\n"
	"    max_shots: 10000\n"
	"    baseline:\n"
	"      qubit_count: 4\n"
	"      depth: 10\n"
	"      one_q_gate_count: 18446744073709551616\n"
	"      two_q_gate_count: 5\n"
	"      measurement_count: 2\n"
	"      shots: 100\n"
	"    timing:\n"
	"      one_q_gate_ns: 20\n"
	"      two_q_gate_ns: 100\n"
	"      measurement_ns: 1000\n";

static const char *unknown_estimator_yaml =
	"devices:\n"
	"  - device_id: 9\n"
	"    max_qubits: 20\n"
	"    max_shots: 10000\n"
	"    baseline:\n"
	"      qubit_count: 4\n"
	"      depth: 10\n"
	"      one_q_gate_count: 10\n"
	"      two_q_gate_count: 5\n"
	"      measurement_count: 2\n"
	"      shots: 100\n"
	"    timing:\n"
	"      one_q_gate_ns: 20\n"
	"      two_q_gate_ns: 100\n"
	"      measurement_ns: 1000\n"
	"    estimator:\n"
	"      name: missing\n";

static int test_config_string_and_flags(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t stored = make_profile_output();
	qhw_adm_estimate_t estimate = make_estimate_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, valid_yaml, 0,
		QHW_ADM_CONFIG_MERGE) == QHW_ADM_OK);
	CHECK(qhw_adm_get_device(ctx, 8, &stored) == QHW_ADM_OK);
	CHECK(stored.max_qubits == 20);
	CHECK(qhw_adm_estimate_baseline(ctx, 8, &estimate) == QHW_ADM_OK);
	CHECK(estimate.measurement_ns == 200000);
	CHECK(qhw_adm_load_config_string(ctx, valid_yaml, 0, 0) ==
		QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_load_config_string(ctx, valid_yaml, 0,
		QHW_ADM_CONFIG_MERGE | QHW_ADM_CONFIG_REPLACE) ==
		QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_load_config_string(ctx, valid_yaml, 0,
		QHW_ADM_CONFIG_MERGE | (UINT64_C(1) << 8)) ==
		QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_config_plugin_path_estimator(void)
{
	char yaml[4096];
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_estimate_t estimate = make_estimate_output();
	int written;

	written = snprintf(
		yaml,
		sizeof(yaml),
		"plugin_paths:\n"
		"  estimators: [\"%s\"]\n"
		"devices:\n"
		"  - device_id: 10\n"
		"    max_qubits: 20\n"
		"    max_shots: 10000\n"
		"    baseline:\n"
		"      qubit_count: 4\n"
		"      depth: 10\n"
		"      one_q_gate_count: 10\n"
		"      two_q_gate_count: 5\n"
		"      measurement_count: 2\n"
		"      shots: 100\n"
		"    timing:\n"
		"      one_q_gate_ns: 20\n"
		"      two_q_gate_ns: 100\n"
		"      measurement_ns: 1000\n"
		"    estimator:\n"
		"      name: mock\n"
		"      options:\n"
		"        observed_device_ns: 444\n"
		"        consumed_credits: 5\n",
		QHW_ADM_TEST_PLUGIN_DIR);
	CHECK(written > 0 && (size_t)written < sizeof(yaml));

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, yaml, 0,
		QHW_ADM_CONFIG_REPLACE) == QHW_ADM_OK);
	CHECK(qhw_adm_estimate_qtask_class(ctx, 10, &task, &estimate) ==
		QHW_ADM_OK);
	CHECK(estimate.total_ns == 444);
	CHECK(estimate.baseline_units == 5);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_config_multi_device(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t stored = make_profile_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, multi_device_yaml, 0,
		QHW_ADM_CONFIG_REPLACE) == QHW_ADM_OK);
	CHECK(qhw_adm_get_device(ctx, 8, &stored) == QHW_ADM_OK);
	CHECK(stored.device_id == 8);
	stored = make_profile_output();
	CHECK(qhw_adm_get_device(ctx, 9, &stored) == QHW_ADM_OK);
	CHECK(stored.device_id == 9);
	CHECK(stored.one_q_gate_ns == 30);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_config_rollback(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t stored = make_profile_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, valid_yaml, 0,
		QHW_ADM_CONFIG_REPLACE) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, invalid_yaml, 0,
		QHW_ADM_CONFIG_REPLACE) == QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_get_device(ctx, 8, &stored) == QHW_ADM_OK);
	CHECK(stored.device_id == 8);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_config_estimator_rollback(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t stored = make_profile_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, valid_yaml, 0,
		QHW_ADM_CONFIG_REPLACE) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, unknown_estimator_yaml, 0,
		QHW_ADM_CONFIG_REPLACE) == QHW_ADM_ERR_NOT_FOUND);
	CHECK(qhw_adm_get_device(ctx, 8, &stored) == QHW_ADM_OK);
	stored = make_profile_output();
	CHECK(qhw_adm_get_device(ctx, 9, &stored) == QHW_ADM_ERR_NOT_FOUND);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_config_rejects_negative_unsigned(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t stored = make_profile_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, negative_yaml, 0,
		QHW_ADM_CONFIG_MERGE) == QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_get_device(ctx, 9, &stored) == QHW_ADM_ERR_NOT_FOUND);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_config_rejects_overflow_unsigned(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t stored = make_profile_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, overflow_yaml, 0,
		QHW_ADM_CONFIG_MERGE) == QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_get_device(ctx, 9, &stored) == QHW_ADM_ERR_NOT_FOUND);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_config_file(void)
{
	const char *path = "qhw_adm_test_config.yaml";
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t stored = make_profile_output();
	FILE *file;

	file = fopen(path, "wb");
	CHECK(file != NULL);
	CHECK(fwrite(valid_yaml, 1, strlen(valid_yaml), file) ==
		strlen(valid_yaml));
	CHECK(fclose(file) == 0);

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config(ctx, path, QHW_ADM_CONFIG_MERGE) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_get_device(ctx, 8, &stored) == QHW_ADM_OK);
	qhw_adm_destroy(ctx);
	(void)remove(path);
	return 0;
}

static int test_external_estimator_load(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_estimate_t estimate = make_estimate_output();
	qhw_adm_estimator_t *estimator = NULL;

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_load_estimator(ctx, QHW_ADM_TEST_PLUGIN_PATH,
		&estimator) == QHW_ADM_OK);
	CHECK(estimator != NULL);
	CHECK(qhw_adm_set_estimator(ctx, profile.device_id, "mock", NULL, 0) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_estimate_qtask_class(ctx, profile.device_id, &task,
		&estimate) == QHW_ADM_OK);
	CHECK(estimate.total_ns == 333);
	CHECK(estimate.baseline_units == 7);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_estimator_search_path(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_estimate_t estimate = make_estimate_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_estimator_path(ctx, QHW_ADM_TEST_PLUGIN_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_estimator(ctx, profile.device_id, "mock", NULL, 0) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_estimate_qtask_class(ctx, profile.device_id, &task,
		&estimate) == QHW_ADM_OK);
	CHECK(estimate.total_ns == 333);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_merge_preserves_existing_estimator_state(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_estimate_t estimate = make_estimate_output();
	qhw_adm_kv_t options[2];

	memset(options, 0, sizeof(options));
	options[0].key = QHW_ADM_META_OBSERVED_DEVICE_NS;
	options[0].value.type = QHW_ADM_VALUE_U64;
	options[0].value.value.u64 = 555;
	options[1].key = QHW_ADM_META_CONSUMED_CREDITS;
	options[1].value.type = QHW_ADM_VALUE_U64;
	options[1].value.value.u64 = 9;

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_estimator_path(ctx, QHW_ADM_TEST_PLUGIN_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_estimator(ctx, profile.device_id, "mock",
		options, 2) == QHW_ADM_OK);
	CHECK(qhw_adm_estimate_qtask_class(ctx, profile.device_id, &task,
		&estimate) == QHW_ADM_OK);
	CHECK(estimate.total_ns == 555);
	CHECK(estimate.baseline_units == 9);

	CHECK(qhw_adm_load_config_string(ctx, valid_yaml, 0,
		QHW_ADM_CONFIG_MERGE) == QHW_ADM_OK);
	estimate = make_estimate_output();
	CHECK(qhw_adm_estimate_qtask_class(ctx, profile.device_id, &task,
		&estimate) == QHW_ADM_OK);
	CHECK(estimate.total_ns == 555);
	CHECK(estimate.baseline_units == 9);
	qhw_adm_destroy(ctx);
	return 0;
}

int main(void)
{
	CHECK(test_register_and_get_device() == 0);
	CHECK(test_baseline_estimate() == 0);
	CHECK(test_task_estimate() == 0);
	CHECK(test_metadata_override() == 0);
	CHECK(test_invalid_estimate() == 0);
	CHECK(test_output_size_guards() == 0);
	CHECK(test_config_string_and_flags() == 0);
	CHECK(test_config_plugin_path_estimator() == 0);
	CHECK(test_config_multi_device() == 0);
	CHECK(test_config_rollback() == 0);
	CHECK(test_config_estimator_rollback() == 0);
	CHECK(test_config_rejects_negative_unsigned() == 0);
	CHECK(test_config_rejects_overflow_unsigned() == 0);
	CHECK(test_config_file() == 0);
	CHECK(test_external_estimator_load() == 0);
	CHECK(test_estimator_search_path() == 0);
	CHECK(test_merge_preserves_existing_estimator_state() == 0);
	return 0;
}
