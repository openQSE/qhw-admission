%module _qhw_admission

%{
#include "qhw_admission/qhw_admission.h"
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
%}
