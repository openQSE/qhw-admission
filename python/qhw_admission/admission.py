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

CONFIG_MERGE = _native.QHW_ADM_CONFIG_MERGE_VALUE
CONFIG_REPLACE = _native.QHW_ADM_CONFIG_REPLACE_VALUE


class AdmissionError(RuntimeError):
    pass


class Baseline:
    def __init__(
        self,
        qubit_count,
        depth,
        one_q_gate_count,
        two_q_gate_count,
        shots,
        measurement_count,
    ):
        self._native = _native.qhw_adm_baseline_t()
        self._native.struct_size = _native.qhw_adm_baseline_sizeof()
        self._native.qubit_count = qubit_count
        self._native.depth = depth
        self._native.one_q_gate_count = one_q_gate_count
        self._native.two_q_gate_count = two_q_gate_count
        self._native.shots = shots
        self._native.measurement_count = measurement_count


class DeviceProfile:
    def __init__(
        self,
        device_id,
        baseline,
        max_qubits,
        one_q_gate_ns,
        two_q_gate_ns,
        measurement_ns,
        time_span_ns=0,
        max_shots=0,
        one_q_gate_transfer_ns=0,
        two_q_gate_transfer_ns=0,
        measurement_transfer_ns=0,
        compile_ns=0,
        control_overhead_ns=0,
        provider_overhead_ns=0,
        total_credits=0,
        device_rate=0,
        concurrent_jobs=0,
        default_ttl_ns=0,
    ):
        self._native = _native.qhw_adm_device_profile_t()
        self._native.struct_size = _native.qhw_adm_device_profile_sizeof()
        self._native.device_id = device_id
        self._native.time_span_ns = time_span_ns
        self._native.baseline = baseline._native
        self._native.max_qubits = max_qubits
        self._native.max_shots = max_shots
        self._native.one_q_gate_ns = one_q_gate_ns
        self._native.two_q_gate_ns = two_q_gate_ns
        self._native.measurement_ns = measurement_ns
        self._native.one_q_gate_transfer_ns = one_q_gate_transfer_ns
        self._native.two_q_gate_transfer_ns = two_q_gate_transfer_ns
        self._native.measurement_transfer_ns = measurement_transfer_ns
        self._native.compile_ns = compile_ns
        self._native.control_overhead_ns = control_overhead_ns
        self._native.provider_overhead_ns = provider_overhead_ns
        self._native.total_credits = total_credits
        self._native.device_rate = device_rate
        self._native.concurrent_jobs = concurrent_jobs
        self._native.default_ttl_ns = default_ttl_ns
        self._native.metadata = None
        self._native.metadata_count = 0


class QtaskClass:
    def __init__(
        self,
        class_id,
        count,
        qubit_count,
        depth,
        one_q_gate_count,
        two_q_gate_count,
        shots,
        measurement_count,
    ):
        self._native = _native.qhw_adm_qtask_class_t()
        self._native.struct_size = _native.qhw_adm_qtask_class_sizeof()
        self._native.class_id = class_id
        self._native.count = count
        self._native.qubit_count = qubit_count
        self._native.depth = depth
        self._native.one_q_gate_count = one_q_gate_count
        self._native.two_q_gate_count = two_q_gate_count
        self._native.shots = shots
        self._native.measurement_count = measurement_count
        self._native.metadata = None
        self._native.metadata_count = 0


class Estimate:
    def __init__(self, native):
        self.execution_ns = native.execution_ns
        self.measurement_ns = native.measurement_ns
        self.compile_ns = native.compile_ns
        self.transfer_ns = native.transfer_ns
        self.control_overhead_ns = native.control_overhead_ns
        self.total_ns = native.total_ns
        self.baseline_units = native.baseline_units
        self.confidence_ppm = native.confidence_ppm


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

    def register_device(self, profile):
        self._require_open()
        rc = _native.qhw_adm_register_device(self._ctx, profile._native)
        self._check_rc(rc, "qhw_adm_register_device")

    def load_config(self, path, flags=CONFIG_MERGE):
        self._require_open()
        rc = _native.qhw_adm_load_config(self._ctx, path, flags)
        self._check_rc(rc, "qhw_adm_load_config")

    def load_config_string(self, yaml_text, flags=CONFIG_MERGE):
        self._require_open()
        rc = _native.qhw_adm_load_config_string(
            self._ctx,
            yaml_text,
            len(yaml_text),
            flags,
        )
        self._check_rc(rc, "qhw_adm_load_config_string")

    def load_estimator(self, path):
        self._require_open()
        estimator = _native.qhw_adm_py_load_estimator(self._ctx, path)
        if estimator is None:
            detail = self.last_error
            if detail:
                raise AdmissionError(f"qhw_adm_load_estimator failed: {detail}")
            raise AdmissionError("qhw_adm_load_estimator failed")
        return estimator

    def add_estimator_path(self, path):
        self._require_open()
        rc = _native.qhw_adm_add_estimator_path(self._ctx, path)
        self._check_rc(rc, "qhw_adm_add_estimator_path")

    def unregister_device(self, device_id):
        self._require_open()
        rc = _native.qhw_adm_unregister_device(self._ctx, device_id)
        self._check_rc(rc, "qhw_adm_unregister_device")

    def set_baseline(self, device_id, baseline):
        self._require_open()
        rc = _native.qhw_adm_set_baseline(
            self._ctx,
            device_id,
            baseline._native,
        )
        self._check_rc(rc, "qhw_adm_set_baseline")

    def set_estimator(self, device_id, name="baseline"):
        self._require_open()
        rc = _native.qhw_adm_set_estimator(
            self._ctx,
            device_id,
            name,
            None,
            0,
        )
        self._check_rc(rc, "qhw_adm_set_estimator")

    def estimate_baseline(self, device_id):
        self._require_open()
        estimate = _native.qhw_adm_estimate_t()
        estimate.struct_size = _native.qhw_adm_estimate_sizeof()
        rc = _native.qhw_adm_estimate_baseline(
            self._ctx,
            device_id,
            estimate,
        )
        self._check_rc(rc, "qhw_adm_estimate_baseline")
        return Estimate(estimate)

    def estimate_qtask_class(self, device_id, task_class):
        self._require_open()
        estimate = _native.qhw_adm_estimate_t()
        estimate.struct_size = _native.qhw_adm_estimate_sizeof()
        rc = _native.qhw_adm_estimate_qtask_class(
            self._ctx,
            device_id,
            task_class._native,
            estimate,
        )
        self._check_rc(rc, "qhw_adm_estimate_qtask_class")
        return Estimate(estimate)

    def _require_open(self):
        if self._ctx is None:
            raise AdmissionError("admission context is closed")

    def _check_rc(self, rc, api):
        if rc != OK:
            detail = self.last_error
            if detail:
                raise AdmissionError(f"{api} failed: {detail}")
            raise AdmissionError(f"{api} failed with rc={rc}")


__all__ = [
    "AdmissionContext",
    "AdmissionError",
    "Baseline",
    "CONFIG_MERGE",
    "CONFIG_REPLACE",
    "DeviceProfile",
    "Estimate",
    "QtaskClass",
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
