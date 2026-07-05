#include "qhw_admission_internal.h"

#include <stdlib.h>
#include <string.h>

static qhw_adm_threading_t qhw_adm_default_threading(
	const qhw_adm_attr_t *attr)
{
	if (attr == NULL) {
		return QHW_ADM_THREAD_USER;
	}

	return attr->threading;
}

static void qhw_adm_cleanup(qhw_adm_t *ctx)
{
	if (ctx == NULL) {
		return;
	}

	if (ctx->lock_initialized) {
		(void)pthread_mutex_destroy(&ctx->lock);
	}

	qhw_adm_fini_registries(ctx);
	qhw_adm_free_estimator_paths(ctx);
	qhw_adm_free_policy_paths(ctx);
	qhw_adm_free_metadata_count(ctx->options, ctx->option_count);
	free(ctx);
}

qhw_adm_rc_t qhw_adm_create(
	const qhw_adm_attr_t *attr,
	qhw_adm_t **out_ctx)
{
	qhw_adm_t *ctx;
	qhw_adm_rc_t rc;

	if (out_ctx == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_ctx = NULL;

	rc = qhw_adm_validate_attr(attr);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	ctx = (qhw_adm_t *)calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	ctx->threading = qhw_adm_default_threading(attr);
	ctx->next_reservation_id = 1;

	if (attr != NULL && attr->option_count > 0) {
		rc = qhw_adm_copy_metadata(
			attr->options,
			attr->option_count,
			&ctx->options);
		if (rc != QHW_ADM_OK) {
			qhw_adm_cleanup(ctx);
			return rc;
		}
		ctx->option_count = attr->option_count;
	}

	rc = qhw_adm_init_registries(ctx);
	if (rc != QHW_ADM_OK) {
		qhw_adm_cleanup(ctx);
		return rc;
	}

	rc = qhw_adm_register_builtin_estimators(ctx);
	if (rc != QHW_ADM_OK) {
		qhw_adm_cleanup(ctx);
		return rc;
	}

	if (ctx->threading == QHW_ADM_THREAD_SAFE) {
		if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
			qhw_adm_cleanup(ctx);
			return QHW_ADM_ERR_STATE;
		}
		ctx->lock_initialized = 1;
	}

	qhw_adm_clear_error(ctx);
	*out_ctx = ctx;
	return QHW_ADM_OK;
}

void qhw_adm_destroy(qhw_adm_t *ctx)
{
	if (ctx == NULL) {
		return;
	}

	qhw_adm_cleanup(ctx);
}

const char *qhw_adm_last_error(const qhw_adm_t *ctx)
{
	if (ctx == NULL) {
		return "qhw-admission context is null";
	}

	return ctx->last_error;
}

qhw_adm_rc_t qhw_adm_get_threading(
	qhw_adm_t *ctx,
	qhw_adm_threading_t *out_threading)
{
	qhw_adm_rc_t rc;

	if (ctx == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	if (out_threading == NULL) {
		qhw_adm_set_error(ctx, "out_threading is null");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_INVAL;
	}

	*out_threading = ctx->threading;
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}
