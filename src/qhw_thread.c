#include "qhw_admission_internal.h"

qhw_adm_rc_t qhw_adm_lock(qhw_adm_t *ctx)
{
	if (ctx == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (ctx->threading != QHW_ADM_THREAD_SAFE) {
		return QHW_ADM_OK;
	}

	if (pthread_mutex_lock(&ctx->lock) != 0) {
		return QHW_ADM_ERR_STATE;
	}

	return QHW_ADM_OK;
}

void qhw_adm_unlock(qhw_adm_t *ctx)
{
	if (ctx == NULL || ctx->threading != QHW_ADM_THREAD_SAFE) {
		return;
	}

	(void)pthread_mutex_unlock(&ctx->lock);
}
