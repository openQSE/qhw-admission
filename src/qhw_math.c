#include "qhw_admission_internal.h"

#include <stdint.h>

qhw_adm_rc_t qhw_adm_add_u64(uint64_t a, uint64_t b, uint64_t *out)
{
	if (out == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (UINT64_MAX - a < b) {
		return QHW_ADM_ERR_INVAL;
	}

	*out = a + b;
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_mul_u64(uint64_t a, uint64_t b, uint64_t *out)
{
	if (out == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (a != 0 && b > UINT64_MAX / a) {
		return QHW_ADM_ERR_INVAL;
	}

	*out = a * b;
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_ceil_div_u64(uint64_t a, uint64_t b, uint64_t *out)
{
	uint64_t quotient;

	if (out == NULL || b == 0) {
		return QHW_ADM_ERR_INVAL;
	}

	quotient = a / b;
	if (a % b != 0) {
		if (quotient == UINT64_MAX) {
			return QHW_ADM_ERR_INVAL;
		}
		quotient++;
	}

	*out = quotient;
	return QHW_ADM_OK;
}
