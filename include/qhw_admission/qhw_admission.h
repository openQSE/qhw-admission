#ifndef QHW_ADMISSION_H
#define QHW_ADMISSION_H

#include <qhw_admission/qhw_admission_types.h>

#ifdef __cplusplus
extern "C" {
#endif

qhw_adm_rc_t qhw_adm_create(
	const qhw_adm_attr_t *attr,
	qhw_adm_t **out_ctx);

void qhw_adm_destroy(qhw_adm_t *ctx);

const char *qhw_adm_last_error(const qhw_adm_t *ctx);

qhw_adm_rc_t qhw_adm_get_threading(
	qhw_adm_t *ctx,
	qhw_adm_threading_t *out_threading);

#ifdef __cplusplus
}
#endif

#endif
