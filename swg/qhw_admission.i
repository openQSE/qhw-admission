%module _qhw_admission

%{
#include "qhw_admission/qhw_admission.h"
#include <stdlib.h>
%}

%include <stdint.i>
%include <typemaps.i>

%include "qhw_admission/qhw_admission_abi.h"
%include "qhw_admission/qhw_admission_types.h"
%include "qhw_admission/qhw_admission.h"

%constant uint64_t QHW_ADM_CONFIG_MERGE_VALUE = QHW_ADM_CONFIG_MERGE;
%constant uint64_t QHW_ADM_CONFIG_REPLACE_VALUE = QHW_ADM_CONFIG_REPLACE;

%inline %{
static size_t qhw_adm_attr_sizeof(void)
{
	return sizeof(qhw_adm_attr_t);
}

static size_t qhw_adm_baseline_sizeof(void)
{
	return sizeof(qhw_adm_baseline_t);
}

static size_t qhw_adm_device_profile_sizeof(void)
{
	return sizeof(qhw_adm_device_profile_t);
}

static size_t qhw_adm_qtask_class_sizeof(void)
{
	return sizeof(qhw_adm_qtask_class_t);
}

static size_t qhw_adm_estimate_sizeof(void)
{
	return sizeof(qhw_adm_estimate_t);
}

static size_t qhw_adm_capacity_view_sizeof(void)
{
	return sizeof(qhw_adm_capacity_view_t);
}

static size_t qhw_adm_request_sizeof(void)
{
	return sizeof(qhw_adm_request_t);
}

static size_t qhw_adm_decision_sizeof(void)
{
	return sizeof(qhw_adm_decision_t);
}

static size_t qhw_adm_reservation_sizeof(void)
{
	return sizeof(qhw_adm_reservation_t);
}

static qhw_adm_request_t *qhw_adm_py_request_create_single(
	uint64_t request_id,
	uint64_t device_id,
	uint64_t user_id,
	uint64_t job_id,
	uint64_t scope_id,
	uint64_t reservation_id,
	qhw_adm_workload_kind_t workload_kind,
	uint64_t walltime_ns,
	uint64_t ttl_ns,
	uint64_t classical_runtime_ns,
	uint64_t overhead_ns,
	int64_t priority,
	const qhw_adm_qtask_class_t *task_class)
{
	qhw_adm_request_t *request;
	qhw_adm_qtask_class_t *task_copy;

	if (task_class == NULL ||
	    task_class->struct_size < sizeof(*task_class)) {
		return NULL;
	}

	request = calloc(1, sizeof(*request));
	if (request == NULL) {
		return NULL;
	}

	task_copy = malloc(sizeof(*task_copy));
	if (task_copy == NULL) {
		free(request);
		return NULL;
	}
	*task_copy = *task_class;

	request->struct_size = sizeof(*request);
	request->request_id = request_id;
	request->device_id = device_id;
	request->user_id = user_id;
	request->job_id = job_id;
	request->scope_id = scope_id;
	request->reservation_id = reservation_id;
	request->workload_kind = workload_kind;
	request->walltime_ns = walltime_ns;
	request->ttl_ns = ttl_ns;
	request->classical_runtime_ns = classical_runtime_ns;
	request->overhead_ns = overhead_ns;
	request->priority = priority;
	request->task_class_count = 1;
	request->task_classes = task_copy;
	return request;
}

static void qhw_adm_py_request_destroy(qhw_adm_request_t *request)
{
	if (request == NULL) {
		return;
	}

	free((void *)request->task_classes);
	free(request);
}

static qhw_adm_t *qhw_adm_py_create(const qhw_adm_attr_t *attr)
{
	qhw_adm_t *ctx = NULL;

	if (qhw_adm_create(attr, &ctx) != QHW_ADM_OK) {
		return NULL;
	}

	return ctx;
}

static qhw_adm_threading_t qhw_adm_py_get_threading(qhw_adm_t *ctx)
{
	qhw_adm_threading_t threading = QHW_ADM_THREAD_USER;

	if (qhw_adm_get_threading(ctx, &threading) != QHW_ADM_OK) {
		return (qhw_adm_threading_t)-1;
	}

	return threading;
}

static qhw_adm_estimator_t *qhw_adm_py_load_estimator(
	qhw_adm_t *ctx,
	const char *path)
{
	qhw_adm_estimator_t *estimator = NULL;

	if (qhw_adm_load_estimator(ctx, path, &estimator) != QHW_ADM_OK) {
		return NULL;
	}

	return estimator;
}

static qhw_adm_policy_t *qhw_adm_py_load_policy(
	qhw_adm_t *ctx,
	const char *path)
{
	qhw_adm_policy_t *policy = NULL;

	if (qhw_adm_load_policy(ctx, path, &policy) != QHW_ADM_OK) {
		return NULL;
	}

	return policy;
}

static int64_t qhw_adm_py_expire(qhw_adm_t *ctx, uint64_t now_ns)
{
	size_t expired_count = 0;

	if (qhw_adm_expire(ctx, now_ns, &expired_count) != QHW_ADM_OK ||
	    expired_count > INT64_MAX) {
		return -1;
	}

	return (int64_t)expired_count;
}
%}
