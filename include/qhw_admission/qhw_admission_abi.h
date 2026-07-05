#ifndef QHW_ADMISSION_ABI_H
#define QHW_ADMISSION_ABI_H

#include <stdint.h>

#define QHW_ADM_ABI_VERSION 1U

#define QHW_ADM_POLICY_PLUGIN_SYMBOL "qhw_adm_policy_plugin"
#define QHW_ADM_ESTIMATOR_PLUGIN_SYMBOL "qhw_adm_estimator_plugin"

#define QHW_ADM_POLICY_CAP_USAGE_ACCOUNTING (UINT64_C(1) << 0)
#define QHW_ADM_POLICY_CAP_CAPACITY_REPORT  (UINT64_C(1) << 1)
#define QHW_ADM_POLICY_CAP_SCOPED_CAPACITY  (UINT64_C(1) << 2)

#define QHW_ADM_EST_CAP_TASK       (UINT64_C(1) << 0)
#define QHW_ADM_EST_CAP_REQUEST    (UINT64_C(1) << 1)
#define QHW_ADM_EST_CAP_BASELINE   (UINT64_C(1) << 2)
#define QHW_ADM_EST_CAP_FEEDBACK   (UINT64_C(1) << 3)

#define QHW_ADM_CONFIG_MERGE       (UINT64_C(1) << 0)
#define QHW_ADM_CONFIG_REPLACE     (UINT64_C(1) << 1)

#endif
