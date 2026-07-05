#include "qhw_admission_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum qhw_adm_config_section {
	QHW_ADM_CFG_NONE = 0,
	QHW_ADM_CFG_PLUGIN_PATHS = 1,
	QHW_ADM_CFG_PLUGIN_ESTIMATORS = 2,
	QHW_ADM_CFG_PLUGIN_POLICIES = 3,
	QHW_ADM_CFG_DEVICES = 4,
	QHW_ADM_CFG_BASELINE = 5,
	QHW_ADM_CFG_TIMING = 6,
	QHW_ADM_CFG_CREDIT = 7,
	QHW_ADM_CFG_RATE = 8,
	QHW_ADM_CFG_ESTIMATOR = 9,
	QHW_ADM_CFG_POLICY = 10,
	QHW_ADM_CFG_ESTIMATOR_OPTIONS = 11,
	QHW_ADM_CFG_POLICY_OPTIONS = 12,
};

struct qhw_adm_config_device {
	qhw_adm_device_profile_t profile;
	char *estimator_name;
	qhw_adm_kv_t *estimator_options;
	size_t estimator_option_count;
	char *policy_name;
	qhw_adm_kv_t *policy_options;
	size_t policy_option_count;
	int active;
};

struct qhw_adm_pending_config {
	struct qhw_adm_config_device *devices;
	size_t device_count;
	char **estimator_paths;
	size_t estimator_path_count;
	char **policy_paths;
	size_t policy_path_count;
	struct qhw_list_node loaded_estimators;
	struct qhw_list_node loaded_policies;
};

struct qhw_adm_loaded_estimator {
	struct qhw_list_node node;
	qhw_adm_estimator_t *estimator;
};

struct qhw_adm_loaded_policy {
	struct qhw_list_node node;
	qhw_adm_policy_t *policy;
};

static void *config_alloc(size_t size, void *user_data)
{
	(void)user_data;
	return malloc(size);
}

static void config_free(void *ptr, void *user_data)
{
	(void)user_data;
	free(ptr);
}

static void qhw_adm_pending_config_fini(
	struct qhw_adm_pending_config *config)
{
	struct qhw_list_node *node;
	size_t i;

	if (config == NULL) {
		return;
	}

	for (i = 0; i < config->device_count; i++) {
		free(config->devices[i].estimator_name);
		qhw_adm_free_metadata_count(
			config->devices[i].estimator_options,
			config->devices[i].estimator_option_count);
		free(config->devices[i].policy_name);
		qhw_adm_free_metadata_count(
			config->devices[i].policy_options,
			config->devices[i].policy_option_count);
	}
	free(config->devices);

	for (i = 0; i < config->estimator_path_count; i++) {
		free(config->estimator_paths[i]);
	}
	free(config->estimator_paths);

	while ((node = qhw_list_pop_front(&config->loaded_estimators)) !=
	       NULL) {
		struct qhw_adm_loaded_estimator *loaded;

		loaded = qhw_container_of(
			node,
			struct qhw_adm_loaded_estimator,
			node);
		free(loaded);
	}

	for (i = 0; i < config->policy_path_count; i++) {
		free(config->policy_paths[i]);
	}
	free(config->policy_paths);

	while ((node = qhw_list_pop_front(&config->loaded_policies)) !=
	       NULL) {
		struct qhw_adm_loaded_policy *loaded;

		loaded = qhw_container_of(
			node,
			struct qhw_adm_loaded_policy,
			node);
		free(loaded);
	}
	memset(config, 0, sizeof(*config));
}

static void qhw_adm_pending_config_init(
	struct qhw_adm_pending_config *config)
{
	memset(config, 0, sizeof(*config));
	qhw_list_init(&config->loaded_estimators);
	qhw_list_init(&config->loaded_policies);
}

static char *trim(char *line)
{
	char *end;

	while (*line != '\0' && isspace((unsigned char)*line)) {
		line++;
	}

	end = line + strlen(line);
	while (end > line && isspace((unsigned char)end[-1])) {
		end--;
	}
	*end = '\0';
	return line;
}

static char *strip_quotes(char *value)
{
	size_t len;

	value = trim(value);
	len = strlen(value);
	if (len >= 2 &&
	    ((value[0] == '"' && value[len - 1] == '"') ||
	     (value[0] == '\'' && value[len - 1] == '\''))) {
		value[len - 1] = '\0';
		return value + 1;
	}

	return value;
}

static int split_key_value(char *line, char **out_key, char **out_value)
{
	char *colon;

	colon = strchr(line, ':');
	if (colon == NULL) {
		return -1;
	}

	*colon = '\0';
	*out_key = trim(line);
	*out_value = strip_quotes(colon + 1);
	return 0;
}

static qhw_adm_rc_t parse_u64(const char *value, uint64_t *out)
{
	char *end;
	unsigned long long parsed;

	if (value == NULL || *value == '\0' || out == NULL) {
		return QHW_ADM_ERR_INVAL;
	}
	if (value[0] == '-' || value[0] == '+') {
		return QHW_ADM_ERR_INVAL;
	}

	errno = 0;
	parsed = strtoull(value, &end, 0);
	if (errno == ERANGE || *end != '\0' || parsed > UINT64_MAX) {
		return QHW_ADM_ERR_INVAL;
	}

	*out = (uint64_t)parsed;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t parse_u32(const char *value, uint32_t *out)
{
	uint64_t parsed;
	qhw_adm_rc_t rc;

	rc = parse_u64(value, &parsed);
	if (rc != QHW_ADM_OK || parsed > UINT32_MAX) {
		return QHW_ADM_ERR_INVAL;
	}

	*out = (uint32_t)parsed;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t add_path(
	char ***path_array,
	size_t *path_count,
	const char *path)
{
	char **paths;
	char *copy;

	if (path_array == NULL || path_count == NULL || path == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	copy = qhw_adm_strdup(path);
	if (copy == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	if (*path_count >= SIZE_MAX / sizeof(*paths)) {
		free(copy);
		return QHW_ADM_ERR_NOMEM;
	}

	paths = realloc(
		*path_array,
		(*path_count + 1) * sizeof(*paths));
	if (paths == NULL) {
		free(copy);
		return QHW_ADM_ERR_NOMEM;
	}

	*path_array = paths;
	(*path_array)[*path_count] = copy;
	(*path_count)++;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t add_estimator_path(
	struct qhw_adm_pending_config *config,
	const char *path)
{
	return add_path(
		&config->estimator_paths,
		&config->estimator_path_count,
		path);
}

static qhw_adm_rc_t add_policy_path(
	struct qhw_adm_pending_config *config,
	const char *path)
{
	return add_path(
		&config->policy_paths,
		&config->policy_path_count,
		path);
}

static qhw_adm_rc_t add_device(
	struct qhw_adm_pending_config *config,
	struct qhw_adm_config_device **out_device)
{
	struct qhw_adm_config_device *devices;
	struct qhw_adm_config_device *device;

	if (config->device_count >= SIZE_MAX / sizeof(*devices)) {
		return QHW_ADM_ERR_NOMEM;
	}

	devices = realloc(
		config->devices,
		(config->device_count + 1) * sizeof(*devices));
	if (devices == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	config->devices = devices;
	device = &config->devices[config->device_count];
	memset(device, 0, sizeof(*device));
	device->active = 1;
	device->profile.struct_size = sizeof(device->profile);
	device->profile.baseline.struct_size = sizeof(device->profile.baseline);
	config->device_count++;
	*out_device = device;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t set_estimator_name(
	struct qhw_adm_config_device *device,
	const char *name)
{
	char *copy;

	copy = qhw_adm_strdup(name);
	if (copy == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	free(device->estimator_name);
	device->estimator_name = copy;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t set_policy_name(
	struct qhw_adm_config_device *device,
	const char *name)
{
	char *copy;

	copy = qhw_adm_strdup(name);
	if (copy == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	free(device->policy_name);
	device->policy_name = copy;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t parse_baseline_field(
	qhw_adm_baseline_t *baseline,
	const char *key,
	const char *value)
{
	if (strcmp(key, "qubit_count") == 0) {
		return parse_u32(value, &baseline->qubit_count);
	}
	if (strcmp(key, "depth") == 0) {
		return parse_u64(value, &baseline->depth);
	}
	if (strcmp(key, "one_q_gate_count") == 0) {
		return parse_u64(value, &baseline->one_q_gate_count);
	}
	if (strcmp(key, "two_q_gate_count") == 0) {
		return parse_u64(value, &baseline->two_q_gate_count);
	}
	if (strcmp(key, "shots") == 0) {
		return parse_u64(value, &baseline->shots);
	}
	if (strcmp(key, "measurement_count") == 0) {
		return parse_u64(value, &baseline->measurement_count);
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t parse_device_top_field(
	qhw_adm_device_profile_t *profile,
	const char *key,
	const char *value)
{
	if (strcmp(key, "device_id") == 0) {
		return parse_u64(value, &profile->device_id);
	}
	if (strcmp(key, "time_span_ns") == 0) {
		return parse_u64(value, &profile->time_span_ns);
	}
	if (strcmp(key, "max_qubits") == 0) {
		return parse_u32(value, &profile->max_qubits);
	}
	if (strcmp(key, "max_shots") == 0) {
		return parse_u64(value, &profile->max_shots);
	}
	if (strcmp(key, "default_ttl_ns") == 0) {
		return parse_u64(value, &profile->default_ttl_ns);
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t parse_timing_field(
	qhw_adm_device_profile_t *profile,
	const char *key,
	const char *value)
{
	if (strcmp(key, "one_q_gate_ns") == 0) {
		return parse_u64(value, &profile->one_q_gate_ns);
	}
	if (strcmp(key, "two_q_gate_ns") == 0) {
		return parse_u64(value, &profile->two_q_gate_ns);
	}
	if (strcmp(key, "measurement_ns") == 0) {
		return parse_u64(value, &profile->measurement_ns);
	}
	if (strcmp(key, "one_q_gate_transfer_ns") == 0) {
		return parse_u64(value, &profile->one_q_gate_transfer_ns);
	}
	if (strcmp(key, "two_q_gate_transfer_ns") == 0) {
		return parse_u64(value, &profile->two_q_gate_transfer_ns);
	}
	if (strcmp(key, "measurement_transfer_ns") == 0) {
		return parse_u64(value, &profile->measurement_transfer_ns);
	}
	if (strcmp(key, "compile_ns") == 0) {
		return parse_u64(value, &profile->compile_ns);
	}
	if (strcmp(key, "control_overhead_ns") == 0) {
		return parse_u64(value, &profile->control_overhead_ns);
	}
	if (strcmp(key, "provider_overhead_ns") == 0) {
		return parse_u64(value, &profile->provider_overhead_ns);
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t parse_credit_field(
	qhw_adm_device_profile_t *profile,
	const char *key,
	const char *value)
{
	if (strcmp(key, "total_credits") == 0) {
		return parse_u64(value, &profile->total_credits);
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t parse_rate_field(
	qhw_adm_device_profile_t *profile,
	const char *key,
	const char *value)
{
	if (strcmp(key, "device_rate") == 0) {
		return parse_u64(value, &profile->device_rate);
	}
	if (strcmp(key, "concurrent_jobs") == 0) {
		return parse_u32(value, &profile->concurrent_jobs);
	}

	return QHW_ADM_OK;
}

static uint64_t metadata_key_from_name(const char *key)
{
	static const struct {
		const char *name;
		uint64_t key;
	} keys[] = {
		{ "workload_kind", QHW_ADM_META_WORKLOAD_KIND },
		{ "session_id", QHW_ADM_META_SESSION_ID },
		{ "scope_id", QHW_ADM_META_SCOPE_ID },
		{ "deadline_ns", QHW_ADM_META_DEADLINE_NS },
		{ "latest_start_ns", QHW_ADM_META_LATEST_START_NS },
		{ "latest_finish_ns", QHW_ADM_META_LATEST_FINISH_NS },
		{ "qos_class", QHW_ADM_META_QOS_CLASS },
		{ "layer_count", QHW_ADM_META_LAYER_COUNT },
		{ "batch_count", QHW_ADM_META_BATCH_COUNT },
		{ "provider_batching", QHW_ADM_META_PROVIDER_BATCHING },
		{ "compile_ns", QHW_ADM_META_COMPILE_NS },
		{ "lowering_ns", QHW_ADM_META_LOWERING_NS },
		{ "transfer_ns", QHW_ADM_META_TRANSFER_NS },
		{ "control_overhead_ns", QHW_ADM_META_CONTROL_OVERHEAD_NS },
		{ "provider_overhead_ns", QHW_ADM_META_PROVIDER_OVERHEAD_NS },
		{ "one_q_gate_ns", QHW_ADM_META_ONE_Q_GATE_NS },
		{ "two_q_gate_ns", QHW_ADM_META_TWO_Q_GATE_NS },
		{ "measurement_ns", QHW_ADM_META_MEASUREMENT_NS },
		{
			"one_q_gate_transfer_ns",
			QHW_ADM_META_ONE_Q_GATE_TRANSFER_NS
		},
		{
			"two_q_gate_transfer_ns",
			QHW_ADM_META_TWO_Q_GATE_TRANSFER_NS
		},
		{
			"measurement_transfer_ns",
			QHW_ADM_META_MEASUREMENT_TRANSFER_NS
		},
		{ "logical_qubits", QHW_ADM_META_LOGICAL_QUBITS },
		{ "logical_cycles", QHW_ADM_META_LOGICAL_CYCLES },
		{ "t_count", QHW_ADM_META_T_COUNT },
		{ "t_depth", QHW_ADM_META_T_DEPTH },
		{
			"target_logical_error_ppm",
			QHW_ADM_META_TARGET_LOGICAL_ERROR_PPM
		},
		{ "code_family", QHW_ADM_META_CODE_FAMILY },
		{ "code_distance", QHW_ADM_META_CODE_DISTANCE },
		{ "magic_state_count", QHW_ADM_META_MAGIC_STATE_COUNT },
		{ "decoder_overhead_ns", QHW_ADM_META_DECODER_OVERHEAD_NS },
		{
			"classical_control_overhead_ns",
			QHW_ADM_META_CLASSICAL_CONTROL_OVERHEAD_NS
		},
		{ "estimator_version", QHW_ADM_META_ESTIMATOR_VERSION },
		{ "observed_device_ns", QHW_ADM_META_OBSERVED_DEVICE_NS },
		{ "consumed_credits", QHW_ADM_META_CONSUMED_CREDITS },
		{ "consumed_rate", QHW_ADM_META_CONSUMED_RATE },
		{ "unused_capacity", QHW_ADM_META_UNUSED_CAPACITY },
		{ "over_limit_events", QHW_ADM_META_OVER_LIMIT_EVENTS },
	};
	size_t i;

	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (strcmp(key, keys[i].name) == 0) {
			return keys[i].key;
		}
	}

	return qhw_adm_hash_string(key);
}

static qhw_adm_rc_t parse_option_value(
	const char *value,
	qhw_adm_value_t *out_value)
{
	uint64_t u64;

	if (value == NULL || out_value == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	memset(out_value, 0, sizeof(*out_value));
	if (strcmp(value, "true") == 0) {
		out_value->type = QHW_ADM_VALUE_BOOL;
		out_value->value.boolean = 1;
		return QHW_ADM_OK;
	}
	if (strcmp(value, "false") == 0) {
		out_value->type = QHW_ADM_VALUE_BOOL;
		out_value->value.boolean = 0;
		return QHW_ADM_OK;
	}
	if (parse_u64(value, &u64) == QHW_ADM_OK) {
		out_value->type = QHW_ADM_VALUE_U64;
		out_value->value.u64 = u64;
		return QHW_ADM_OK;
	}

	out_value->type = QHW_ADM_VALUE_STRING;
	out_value->value.string = qhw_adm_strdup(value);
	if (out_value->value.string == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}
	return QHW_ADM_OK;
}

static qhw_adm_rc_t add_option(
	qhw_adm_kv_t **option_array,
	size_t *option_count,
	const char *key,
	const char *value)
{
	qhw_adm_kv_t *options;
	qhw_adm_kv_t option;
	qhw_adm_rc_t rc;

	if (option_array == NULL || option_count == NULL) {
		return QHW_ADM_ERR_INVAL;
	}
	if (*option_count >= SIZE_MAX / sizeof(*options)) {
		return QHW_ADM_ERR_NOMEM;
	}

	memset(&option, 0, sizeof(option));
	option.key = metadata_key_from_name(key);
	rc = parse_option_value(value, &option.value);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	options = realloc(
		*option_array,
		(*option_count + 1) * sizeof(*options));
	if (options == NULL) {
		if (option.value.type == QHW_ADM_VALUE_STRING) {
			free((void *)option.value.value.string);
		}
		return QHW_ADM_ERR_NOMEM;
	}

	*option_array = options;
	(*option_array)[*option_count] = option;
	(*option_count)++;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t add_estimator_option(
	struct qhw_adm_config_device *device,
	const char *key,
	const char *value)
{
	return add_option(
		&device->estimator_options,
		&device->estimator_option_count,
		key,
		value);
}

static qhw_adm_rc_t add_policy_option(
	struct qhw_adm_config_device *device,
	const char *key,
	const char *value)
{
	return add_option(
		&device->policy_options,
		&device->policy_option_count,
		key,
		value);
}

static qhw_adm_rc_t parse_device_field(
	struct qhw_adm_config_device *device,
	enum qhw_adm_config_section section,
	const char *key,
	const char *value)
{
	if (section == QHW_ADM_CFG_BASELINE) {
		return parse_baseline_field(&device->profile.baseline, key, value);
	}
	if (section == QHW_ADM_CFG_TIMING) {
		return parse_timing_field(&device->profile, key, value);
	}
	if (section == QHW_ADM_CFG_CREDIT) {
		return parse_credit_field(&device->profile, key, value);
	}
	if (section == QHW_ADM_CFG_RATE) {
		return parse_rate_field(&device->profile, key, value);
	}
	if (section == QHW_ADM_CFG_ESTIMATOR &&
	    strcmp(key, "name") == 0) {
		return set_estimator_name(device, value);
	}
	if (section == QHW_ADM_CFG_ESTIMATOR_OPTIONS) {
		return add_estimator_option(device, key, value);
	}
	if (section == QHW_ADM_CFG_POLICY &&
	    strcmp(key, "name") == 0) {
		return set_policy_name(device, value);
	}
	if (section == QHW_ADM_CFG_POLICY_OPTIONS) {
		return add_policy_option(device, key, value);
	}

	return parse_device_top_field(&device->profile, key, value);
}

static qhw_adm_rc_t parse_inline_list(
	struct qhw_adm_pending_config *config,
	char *value,
	enum qhw_adm_config_section section)
{
	char *cursor;
	char *end;

	value = trim(value);
	if (value[0] != '[') {
		return QHW_ADM_OK;
	}

	cursor = value + 1;
	end = strrchr(cursor, ']');
	if (end == NULL) {
		return QHW_ADM_ERR_INVAL;
	}
	*end = '\0';

	while (*cursor != '\0') {
		char *comma = strchr(cursor, ',');
		char *path;

		if (comma != NULL) {
			*comma = '\0';
		}
		path = strip_quotes(cursor);
		if (*path != '\0') {
			qhw_adm_rc_t rc;

			if (section == QHW_ADM_CFG_PLUGIN_POLICIES) {
				rc = add_policy_path(config, path);
			} else {
				rc = add_estimator_path(config, path);
			}
			if (rc != QHW_ADM_OK) {
				return rc;
			}
		}
		if (comma == NULL) {
			break;
		}
		cursor = comma + 1;
	}

	return QHW_ADM_OK;
}

static int in_plugin_path_section(enum qhw_adm_config_section section)
{
	return section == QHW_ADM_CFG_PLUGIN_PATHS ||
		section == QHW_ADM_CFG_PLUGIN_ESTIMATORS ||
		section == QHW_ADM_CFG_PLUGIN_POLICIES;
}

static qhw_adm_rc_t parse_yaml_text(
	const char *yaml_text,
	size_t yaml_len,
	struct qhw_adm_pending_config *config)
{
	struct qhw_adm_config_device *current_device = NULL;
	enum qhw_adm_config_section section = QHW_ADM_CFG_NONE;
	char *text;
	char *line;
	qhw_adm_rc_t rc = QHW_ADM_OK;

	if (yaml_text == NULL || config == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (yaml_len == 0) {
		yaml_len = strlen(yaml_text);
	}
	text = malloc(yaml_len + 1);
	if (text == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}
	memcpy(text, yaml_text, yaml_len);
	text[yaml_len] = '\0';

	line = strtok(text, "\n");
	while (line != NULL && rc == QHW_ADM_OK) {
		char *comment;
		char *content;
		char *key;
		char *value;
		size_t indent = 0;

		while (line[indent] == ' ') {
			indent++;
		}

		comment = strchr(line, '#');
		if (comment != NULL) {
			*comment = '\0';
		}
		content = trim(line);
		if (*content == '\0') {
			line = strtok(NULL, "\n");
			continue;
		}

		if (strncmp(content, "- ", 2) == 0) {
			content = trim(content + 2);
			if (section == QHW_ADM_CFG_PLUGIN_ESTIMATORS) {
				rc = add_estimator_path(config, strip_quotes(content));
			} else if (section == QHW_ADM_CFG_PLUGIN_POLICIES) {
				rc = add_policy_path(config, strip_quotes(content));
			} else if (split_key_value(content, &key, &value) == 0 &&
				   (section == QHW_ADM_CFG_DEVICES ||
				    strcmp(key, "device_id") == 0)) {
				rc = add_device(config, &current_device);
				if (rc != QHW_ADM_OK) {
					break;
				}
				section = QHW_ADM_CFG_DEVICES;
				rc = parse_device_field(
					current_device,
					QHW_ADM_CFG_DEVICES,
					key,
					value);
			}
			line = strtok(NULL, "\n");
			continue;
		}

		if (split_key_value(content, &key, &value) != 0) {
			line = strtok(NULL, "\n");
			continue;
		}

		if (indent == 0 && strcmp(key, "plugin_paths") == 0) {
			section = QHW_ADM_CFG_PLUGIN_PATHS;
		} else if (indent == 0 && strcmp(key, "devices") == 0) {
			section = QHW_ADM_CFG_DEVICES;
		} else if (in_plugin_path_section(section) &&
			   strcmp(key, "estimators") == 0) {
			section = QHW_ADM_CFG_PLUGIN_ESTIMATORS;
			rc = parse_inline_list(config, value, section);
		} else if (in_plugin_path_section(section) &&
			   strcmp(key, "policies") == 0) {
			section = QHW_ADM_CFG_PLUGIN_POLICIES;
			rc = parse_inline_list(config, value, section);
		} else if (current_device != NULL && strcmp(key, "baseline") == 0) {
			section = QHW_ADM_CFG_BASELINE;
		} else if (current_device != NULL && strcmp(key, "timing") == 0) {
			section = QHW_ADM_CFG_TIMING;
		} else if (current_device != NULL && strcmp(key, "credit") == 0) {
			section = QHW_ADM_CFG_CREDIT;
		} else if (current_device != NULL && strcmp(key, "rate") == 0) {
			section = QHW_ADM_CFG_RATE;
		} else if (current_device != NULL &&
			   strcmp(key, "estimator") == 0) {
			section = QHW_ADM_CFG_ESTIMATOR;
		} else if (section == QHW_ADM_CFG_ESTIMATOR &&
			   strcmp(key, "options") == 0) {
			section = QHW_ADM_CFG_ESTIMATOR_OPTIONS;
		} else if (current_device != NULL &&
			   strcmp(key, "policy") == 0) {
			section = QHW_ADM_CFG_POLICY;
		} else if (section == QHW_ADM_CFG_POLICY &&
			   strcmp(key, "options") == 0) {
			section = QHW_ADM_CFG_POLICY_OPTIONS;
		} else if (current_device != NULL) {
			rc = parse_device_field(current_device, section, key, value);
		}

		line = strtok(NULL, "\n");
	}

	free(text);
	return rc;
}

static qhw_adm_rc_t validate_config_flags(uint64_t flags)
{
	uint64_t known = QHW_ADM_CONFIG_MERGE | QHW_ADM_CONFIG_REPLACE;

	if ((flags & ~known) != 0) {
		return QHW_ADM_ERR_INVAL;
	}
	if ((flags & QHW_ADM_CONFIG_MERGE) != 0 &&
	    (flags & QHW_ADM_CONFIG_REPLACE) != 0) {
		return QHW_ADM_ERR_INVAL;
	}
	if ((flags & known) == 0) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t validate_pending_config(
	struct qhw_adm_pending_config *config)
{
	size_t i;

	for (i = 0; i < config->device_count; i++) {
		qhw_adm_rc_t rc;

		rc = qhw_adm_validate_device_profile(&config->devices[i].profile);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
	}

	return QHW_ADM_OK;
}

static void free_hash_table_values(
	struct qhw_hash_table *table,
	void (*free_value)(void *value, void *user_data))
{
	qhw_hash_table_fini(table, free_value, NULL);
}

static void free_staged_device_value(void *value, void *user_data)
{
	struct qhw_adm_device_entry *entry = value;
	qhw_adm_t *ctx = user_data;
	void *current;

	if (ctx == NULL || entry == NULL) {
		return;
	}

	current = qhw_hash_table_find(&ctx->devices, entry->profile.device_id);
	if (current == entry) {
		return;
	}

	qhw_adm_free_device_entry(entry, NULL);
}

static void free_staged_device_table(
	qhw_adm_t *ctx,
	struct qhw_hash_table *table)
{
	qhw_hash_table_fini(table, free_staged_device_value, ctx);
}

static int config_has_device(
	struct qhw_adm_pending_config *config,
	uint64_t device_id)
{
	size_t i;

	for (i = 0; i < config->device_count; i++) {
		if (config->devices[i].profile.device_id == device_id) {
			return 1;
		}
	}

	return 0;
}

static qhw_adm_rc_t track_loaded_estimator(
	struct qhw_adm_pending_config *config,
	qhw_adm_estimator_t *estimator)
{
	struct qhw_adm_loaded_estimator *loaded;

	loaded = calloc(1, sizeof(*loaded));
	if (loaded == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	loaded->estimator = estimator;
	qhw_list_push_back(&config->loaded_estimators, &loaded->node);
	return QHW_ADM_OK;
}

static qhw_adm_rc_t track_loaded_policy(
	struct qhw_adm_pending_config *config,
	qhw_adm_policy_t *policy)
{
	struct qhw_adm_loaded_policy *loaded;

	loaded = calloc(1, sizeof(*loaded));
	if (loaded == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	loaded->policy = policy;
	qhw_list_push_back(&config->loaded_policies, &loaded->node);
	return QHW_ADM_OK;
}

static void rollback_loaded_estimators(
	qhw_adm_t *ctx,
	struct qhw_adm_pending_config *config)
{
	struct qhw_list_node *node;

	while ((node = qhw_list_pop_front(&config->loaded_estimators)) !=
	       NULL) {
		struct qhw_adm_loaded_estimator *loaded;

		loaded = qhw_container_of(
			node,
			struct qhw_adm_loaded_estimator,
			node);
		qhw_adm_remove_estimator_entry(ctx, loaded->estimator);
		free(loaded);
	}
}

static void rollback_loaded_policies(
	qhw_adm_t *ctx,
	struct qhw_adm_pending_config *config)
{
	struct qhw_list_node *node;

	while ((node = qhw_list_pop_front(&config->loaded_policies)) !=
	       NULL) {
		struct qhw_adm_loaded_policy *loaded;

		loaded = qhw_container_of(
			node,
			struct qhw_adm_loaded_policy,
			node);
		qhw_adm_remove_policy_entry(ctx, loaded->policy);
		free(loaded);
	}
}

static qhw_adm_rc_t add_device_entry_to_table(
	struct qhw_hash_table *table,
	struct qhw_adm_device_entry *entry)
{
	if (qhw_hash_table_insert(
		table,
		entry->profile.device_id,
		entry) != 0) {
		return QHW_ADM_ERR_NOMEM;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t select_staged_estimator(
	qhw_adm_t *ctx,
	struct qhw_adm_pending_config *config,
	struct qhw_adm_config_device *device,
	struct qhw_adm_device_entry *entry)
{
	qhw_adm_estimator_t *estimator;
	qhw_adm_rc_t rc;
	int loaded = 0;

	if (device->estimator_name == NULL) {
		return QHW_ADM_OK;
	}

	rc = qhw_adm_find_or_load_estimator(
		ctx,
		device->estimator_name,
		config->estimator_paths,
		config->estimator_path_count,
		&estimator,
		&loaded);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "estimator was not found");
		return rc;
	}
	if (loaded) {
		rc = track_loaded_estimator(config, estimator);
		if (rc != QHW_ADM_OK) {
			qhw_adm_remove_estimator_entry(ctx, estimator);
			return rc;
		}
	}

	return qhw_adm_set_device_estimator_entry(
		entry,
		estimator,
		device->estimator_options,
		device->estimator_option_count);
}

static qhw_adm_rc_t select_staged_policy(
	qhw_adm_t *ctx,
	struct qhw_adm_pending_config *config,
	struct qhw_adm_config_device *device,
	struct qhw_adm_device_entry *entry)
{
	qhw_adm_policy_t *policy;
	qhw_adm_rc_t rc;
	int loaded = 0;

	if (device->policy_name == NULL) {
		return QHW_ADM_OK;
	}

	rc = qhw_adm_find_or_load_policy(
		ctx,
		device->policy_name,
		config->policy_paths,
		config->policy_path_count,
		&policy,
		&loaded);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "policy was not found");
		return rc;
	}
	if (loaded) {
		rc = track_loaded_policy(config, policy);
		if (rc != QHW_ADM_OK) {
			qhw_adm_remove_policy_entry(ctx, policy);
			return rc;
		}
	}

	return qhw_adm_set_device_policy_entry(
		entry,
		policy,
		device->policy_options,
		device->policy_option_count);
}

static qhw_adm_rc_t add_current_devices(
	qhw_adm_t *ctx,
	struct qhw_adm_pending_config *config,
	struct qhw_hash_table *table)
{
	size_t i;

	for (i = 0; i < ctx->devices.bucket_count; i++) {
		struct qhw_hash_entry *hash_entry = ctx->devices.buckets[i];

		while (hash_entry != NULL) {
			struct qhw_adm_device_entry *src = hash_entry->value;
			qhw_adm_rc_t rc;

			hash_entry = hash_entry->next;
			if (config_has_device(config, src->profile.device_id)) {
				continue;
			}

			rc = add_device_entry_to_table(table, src);
			if (rc != QHW_ADM_OK) {
				return rc;
			}
		}
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t add_config_devices(
	qhw_adm_t *ctx,
	struct qhw_adm_pending_config *config,
	struct qhw_hash_table *table)
{
	size_t i;

	for (i = 0; i < config->device_count; i++) {
		struct qhw_adm_device_entry *entry;
		uint64_t device_id;
		qhw_adm_rc_t rc;

		device_id = config->devices[i].profile.device_id;
		if (qhw_hash_table_find(&ctx->devices, device_id) != NULL &&
		    qhw_adm_device_has_active_reservation(ctx, device_id)) {
			qhw_adm_set_error(ctx, "device has active reservations");
			return QHW_ADM_ERR_STATE;
		}

		rc = qhw_adm_create_device_entry(
			&config->devices[i].profile,
			&entry);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
		rc = select_staged_estimator(
			ctx,
			config,
			&config->devices[i],
			entry);
		if (rc != QHW_ADM_OK) {
			qhw_adm_free_device_entry(entry, NULL);
			return rc;
		}
		rc = select_staged_policy(
			ctx,
			config,
			&config->devices[i],
			entry);
		if (rc != QHW_ADM_OK) {
			qhw_adm_free_device_entry(entry, NULL);
			return rc;
		}
		rc = add_device_entry_to_table(table, entry);
		if (rc != QHW_ADM_OK) {
			qhw_adm_free_device_entry(entry, NULL);
			return rc;
		}
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t reject_active_omitted_devices(
	qhw_adm_t *ctx,
	struct qhw_adm_pending_config *config,
	uint64_t flags)
{
	size_t i;

	if ((flags & QHW_ADM_CONFIG_REPLACE) == 0) {
		return QHW_ADM_OK;
	}

	for (i = 0; i < ctx->devices.bucket_count; i++) {
		struct qhw_hash_entry *hash_entry = ctx->devices.buckets[i];

		while (hash_entry != NULL) {
			struct qhw_adm_device_entry *entry = hash_entry->value;
			uint64_t device_id = entry->profile.device_id;

			hash_entry = hash_entry->next;
			if (config_has_device(config, device_id)) {
				continue;
			}
			if (qhw_adm_device_has_active_reservation(ctx, device_id)) {
				qhw_adm_set_error(
					ctx,
					"device has active reservations");
				return QHW_ADM_ERR_STATE;
			}
		}
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t build_new_path_array(
	uint64_t flags,
	char **old_paths,
	size_t old_path_count,
	char **config_paths,
	size_t config_path_count,
	char ***out_paths,
	size_t *out_count)
{
	char **paths = NULL;
	size_t old_count = 0;
	size_t count;
	size_t i;
	size_t out_i = 0;

	if ((flags & QHW_ADM_CONFIG_MERGE) != 0) {
		old_count = old_path_count;
	}
	count = old_count + config_path_count;
	if (count > 0) {
		paths = calloc(count, sizeof(*paths));
		if (paths == NULL) {
			return QHW_ADM_ERR_NOMEM;
		}
	}

	for (i = 0; i < old_count; i++) {
		paths[out_i] = qhw_adm_strdup(old_paths[i]);
		if (paths[out_i] == NULL) {
			goto nomem;
		}
		out_i++;
	}
	for (i = 0; i < config_path_count; i++) {
		paths[out_i] = qhw_adm_strdup(config_paths[i]);
		if (paths[out_i] == NULL) {
			goto nomem;
		}
		out_i++;
	}

	*out_paths = paths;
	*out_count = count;
	return QHW_ADM_OK;

nomem:
	for (i = 0; i < out_i; i++) {
		free(paths[i]);
	}
	free(paths);
	return QHW_ADM_ERR_NOMEM;
}

static void free_path_array(char **paths, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		free(paths[i]);
	}
	free(paths);
}

static qhw_adm_rc_t stage_config(
	qhw_adm_t *ctx,
	struct qhw_adm_pending_config *config,
	uint64_t flags,
	struct qhw_hash_table *new_devices,
	char ***new_estimator_paths,
	size_t *new_estimator_path_count,
	char ***new_policy_paths,
	size_t *new_policy_path_count)
{
	qhw_adm_rc_t rc;

	if (qhw_hash_table_init(
		new_devices,
		8,
		config_alloc,
		config_free,
		NULL) != 0) {
		return QHW_ADM_ERR_NOMEM;
	}

	rc = reject_active_omitted_devices(ctx, config, flags);
	if (rc != QHW_ADM_OK) {
		goto fail;
	}

	if ((flags & QHW_ADM_CONFIG_MERGE) != 0) {
		rc = add_current_devices(ctx, config, new_devices);
		if (rc != QHW_ADM_OK) {
			goto fail;
		}
	}

	rc = add_config_devices(ctx, config, new_devices);
	if (rc != QHW_ADM_OK) {
		goto fail;
	}

	rc = build_new_path_array(
		flags,
		ctx->estimator_paths,
		ctx->estimator_path_count,
		config->estimator_paths,
		config->estimator_path_count,
		new_estimator_paths,
		new_estimator_path_count);
	if (rc != QHW_ADM_OK) {
		goto fail;
	}

	rc = build_new_path_array(
		flags,
		ctx->policy_paths,
		ctx->policy_path_count,
		config->policy_paths,
		config->policy_path_count,
		new_policy_paths,
		new_policy_path_count);
	if (rc != QHW_ADM_OK) {
		goto fail;
	}

	return QHW_ADM_OK;

fail:
	free_staged_device_table(ctx, new_devices);
	return rc;
}

static void detach_moved_devices(
	struct qhw_hash_table *old_devices,
	struct qhw_hash_table *new_devices)
{
	size_t i;

	for (i = 0; i < new_devices->bucket_count; i++) {
		struct qhw_hash_entry *hash_entry = new_devices->buckets[i];

		while (hash_entry != NULL) {
			struct qhw_adm_device_entry *entry = hash_entry->value;
			void *old_entry;

			hash_entry = hash_entry->next;
			old_entry = qhw_hash_table_find(
				old_devices,
				entry->profile.device_id);
			if (old_entry == entry) {
				(void)qhw_hash_table_remove(
					old_devices,
					entry->profile.device_id);
			}
		}
	}
}

static void commit_config(
	qhw_adm_t *ctx,
	struct qhw_hash_table *new_devices,
	char **new_estimator_paths,
	size_t new_estimator_path_count,
	char **new_policy_paths,
	size_t new_policy_path_count)
{
	struct qhw_hash_table old_devices = ctx->devices;
	char **old_estimator_paths = ctx->estimator_paths;
	size_t old_estimator_path_count = ctx->estimator_path_count;
	char **old_policy_paths = ctx->policy_paths;
	size_t old_policy_path_count = ctx->policy_path_count;

	ctx->devices = *new_devices;
	memset(new_devices, 0, sizeof(*new_devices));
	ctx->estimator_paths = new_estimator_paths;
	ctx->estimator_path_count = new_estimator_path_count;
	ctx->policy_paths = new_policy_paths;
	ctx->policy_path_count = new_policy_path_count;
	qhw_adm_clear_error(ctx);

	detach_moved_devices(&old_devices, &ctx->devices);
	free_hash_table_values(&old_devices, qhw_adm_free_device_entry);
	free_path_array(old_estimator_paths, old_estimator_path_count);
	free_path_array(old_policy_paths, old_policy_path_count);
}

qhw_adm_rc_t qhw_adm_load_config_string(
	qhw_adm_t *ctx,
	const char *yaml_text,
	size_t yaml_len,
	uint64_t flags)
{
	struct qhw_adm_pending_config config;
	struct qhw_hash_table new_devices;
	char **new_estimator_paths = NULL;
	size_t new_estimator_path_count = 0;
	char **new_policy_paths = NULL;
	size_t new_policy_path_count = 0;
	qhw_adm_rc_t rc;

	qhw_adm_pending_config_init(&config);
	memset(&new_devices, 0, sizeof(new_devices));
	if (ctx == NULL || yaml_text == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = validate_config_flags(flags);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = parse_yaml_text(yaml_text, yaml_len, &config);
	if (rc == QHW_ADM_OK) {
		rc = validate_pending_config(&config);
	}
	if (rc != QHW_ADM_OK) {
		qhw_adm_pending_config_fini(&config);
		return rc;
	}

	rc = qhw_adm_lock(ctx);
	if (rc == QHW_ADM_OK) {
		qhw_adm_clear_output(ctx);
		rc = stage_config(
			ctx,
			&config,
			flags,
			&new_devices,
			&new_estimator_paths,
			&new_estimator_path_count,
			&new_policy_paths,
			&new_policy_path_count);
		if (rc == QHW_ADM_OK) {
			commit_config(
				ctx,
				&new_devices,
				new_estimator_paths,
				new_estimator_path_count,
				new_policy_paths,
				new_policy_path_count);
			new_estimator_paths = NULL;
			new_estimator_path_count = 0;
			new_policy_paths = NULL;
			new_policy_path_count = 0;
		} else {
			free_staged_device_table(ctx, &new_devices);
			rollback_loaded_estimators(ctx, &config);
			rollback_loaded_policies(ctx, &config);
		}
		qhw_adm_unlock(ctx);
	}

	free_path_array(new_estimator_paths, new_estimator_path_count);
	free_path_array(new_policy_paths, new_policy_path_count);
	free_staged_device_table(ctx, &new_devices);
	qhw_adm_pending_config_fini(&config);
	return rc;
}

qhw_adm_rc_t qhw_adm_load_config(
	qhw_adm_t *ctx,
	const char *path,
	uint64_t flags)
{
	FILE *file;
	char *buffer;
	long size;
	size_t read_size;
	qhw_adm_rc_t rc;

	if (ctx == NULL || path == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	file = fopen(path, "rb");
	if (file == NULL) {
		return QHW_ADM_ERR_NOT_FOUND;
	}
	if (fseek(file, 0, SEEK_END) != 0) {
		(void)fclose(file);
		return QHW_ADM_ERR_INVAL;
	}
	size = ftell(file);
	if (size < 0) {
		(void)fclose(file);
		return QHW_ADM_ERR_INVAL;
	}
	if (fseek(file, 0, SEEK_SET) != 0) {
		(void)fclose(file);
		return QHW_ADM_ERR_INVAL;
	}

	buffer = malloc((size_t)size + 1);
	if (buffer == NULL) {
		(void)fclose(file);
		return QHW_ADM_ERR_NOMEM;
	}
	read_size = fread(buffer, 1, (size_t)size, file);
	(void)fclose(file);
	if (read_size != (size_t)size) {
		free(buffer);
		return QHW_ADM_ERR_INVAL;
	}
	buffer[size] = '\0';

	rc = qhw_adm_load_config_string(ctx, buffer, (size_t)size, flags);
	free(buffer);
	return rc;
}
