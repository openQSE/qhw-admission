#include "qhw_admission_internal.h"

#include <stdio.h>
#include <string.h>

void qhw_adm_set_error(qhw_adm_t *ctx, const char *message)
{
	if (ctx == NULL || message == NULL) {
		return;
	}

	(void)snprintf(ctx->last_error, QHW_ADM_ERROR_LEN, "%s", message);
}

void qhw_adm_clear_error(qhw_adm_t *ctx)
{
	if (ctx == NULL) {
		return;
	}

	ctx->last_error[0] = '\0';
}
