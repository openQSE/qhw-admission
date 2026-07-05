from . import _qhw_admission as _native


OK = _native.QHW_ADM_OK
ERR_INVAL = _native.QHW_ADM_ERR_INVAL
ERR_NOMEM = _native.QHW_ADM_ERR_NOMEM
ERR_NOT_FOUND = _native.QHW_ADM_ERR_NOT_FOUND
ERR_STATE = _native.QHW_ADM_ERR_STATE
ERR_POLICY = _native.QHW_ADM_ERR_POLICY
ERR_ESTIMATOR = _native.QHW_ADM_ERR_ESTIMATOR
ERR_UNSUPPORTED = _native.QHW_ADM_ERR_UNSUPPORTED
ERR_EXISTS = _native.QHW_ADM_ERR_EXISTS

THREAD_USER = _native.QHW_ADM_THREAD_USER
THREAD_SAFE = _native.QHW_ADM_THREAD_SAFE


class AdmissionError(RuntimeError):
    pass


class AdmissionContext:
    def __init__(self, threading=THREAD_USER):
        self._ctx = None
        attr = _native.qhw_adm_attr_t()
        attr.struct_size = _native.qhw_adm_attr_sizeof()
        attr.threading = threading
        attr.options = None
        attr.option_count = 0

        self._ctx = _native.qhw_adm_py_create(attr)
        if self._ctx is None:
            raise AdmissionError("qhw_adm_create failed")

    def close(self):
        if self._ctx is not None:
            _native.qhw_adm_destroy(self._ctx)
            self._ctx = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def __del__(self):
        self.close()

    @property
    def threading(self):
        self._require_open()
        threading = _native.qhw_adm_py_get_threading(self._ctx)
        if threading < 0:
            raise AdmissionError("qhw_adm_get_threading failed")
        return threading

    @property
    def last_error(self):
        self._require_open()
        return _native.qhw_adm_last_error(self._ctx)

    def _require_open(self):
        if self._ctx is None:
            raise AdmissionError("admission context is closed")


__all__ = [
    "AdmissionContext",
    "AdmissionError",
    "OK",
    "ERR_INVAL",
    "ERR_NOMEM",
    "ERR_NOT_FOUND",
    "ERR_STATE",
    "ERR_POLICY",
    "ERR_ESTIMATOR",
    "ERR_UNSUPPORTED",
    "ERR_EXISTS",
    "THREAD_USER",
    "THREAD_SAFE",
]
