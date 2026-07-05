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

WORKLOAD_QUANTUM_JOB = _native.QHW_ADM_WORKLOAD_QUANTUM_JOB
WORKLOAD_HYBRID_JOB = _native.QHW_ADM_WORKLOAD_HYBRID_JOB

DECISION_ACCEPTED = _native.QHW_ADM_DECISION_ACCEPTED
DECISION_DELAYED = _native.QHW_ADM_DECISION_DELAYED
DECISION_REJECTED = _native.QHW_ADM_DECISION_REJECTED

RESERVATION_PENDING = _native.QHW_ADM_RESERVATION_PENDING
RESERVATION_ACTIVE = _native.QHW_ADM_RESERVATION_ACTIVE
RESERVATION_RELEASED = _native.QHW_ADM_RESERVATION_RELEASED
RESERVATION_EXPIRED = _native.QHW_ADM_RESERVATION_EXPIRED
RESERVATION_CANCELLED = _native.QHW_ADM_RESERVATION_CANCELLED

REASON_NONE = _native.QHW_ADM_REASON_NONE

COMPLIANCE_ALLOW = _native.QHW_ADM_COMPLIANCE_ALLOW
COMPLIANCE_DELAY = _native.QHW_ADM_COMPLIANCE_DELAY
COMPLIANCE_REJECT = _native.QHW_ADM_COMPLIANCE_REJECT
COMPLIANCE_THROTTLE = _native.QHW_ADM_COMPLIANCE_THROTTLE
COMPLIANCE_TERMINATE = _native.QHW_ADM_COMPLIANCE_TERMINATE

VALUE_U64 = _native.QHW_ADM_VALUE_U64
VALUE_I64 = _native.QHW_ADM_VALUE_I64
VALUE_F64 = _native.QHW_ADM_VALUE_F64
VALUE_BOOL = _native.QHW_ADM_VALUE_BOOL
VALUE_STRING = _native.QHW_ADM_VALUE_STRING
VALUE_PTR = _native.QHW_ADM_VALUE_PTR

OPT_CREDIT_RESERVATION_TTL_NS = (
    _native.QHW_ADM_OPT_CREDIT_RESERVATION_TTL_NS
)
OPT_CREDIT_ALLOW_OVERCOMMIT = _native.QHW_ADM_OPT_CREDIT_ALLOW_OVERCOMMIT
OPT_CREDIT_OVERCOMMIT_CREDITS = (
    _native.QHW_ADM_OPT_CREDIT_OVERCOMMIT_CREDITS
)
OPT_CREDIT_OVERCOMMIT_PPM = _native.QHW_ADM_OPT_CREDIT_OVERCOMMIT_PPM
OPT_RATE_RESERVATION_TTL_NS = _native.QHW_ADM_OPT_RATE_RESERVATION_TTL_NS
OPT_RATE_SLICE = _native.QHW_ADM_OPT_RATE_SLICE


class AdmissionError(RuntimeError):
    pass


def _copy_metadata(metadata, metadata_count):
    copied = []
    if metadata is None or metadata_count == 0:
        return tuple(copied)

    for index in range(metadata_count):
        key = _native.qhw_adm_py_metadata_key(
            metadata,
            metadata_count,
            index,
        )
        value_type = _native.qhw_adm_py_metadata_type(
            metadata,
            metadata_count,
            index,
        )
        if value_type == VALUE_U64:
            value = _native.qhw_adm_py_metadata_u64(
                metadata,
                metadata_count,
                index,
            )
        elif value_type == VALUE_I64:
            value = _native.qhw_adm_py_metadata_i64(
                metadata,
                metadata_count,
                index,
            )
        elif value_type == VALUE_F64:
            value = _native.qhw_adm_py_metadata_f64(
                metadata,
                metadata_count,
                index,
            )
        elif value_type == VALUE_BOOL:
            value = bool(
                _native.qhw_adm_py_metadata_bool(
                    metadata,
                    metadata_count,
                    index,
                )
            )
        elif value_type == VALUE_STRING:
            value = _native.qhw_adm_py_metadata_string(
                metadata,
                metadata_count,
                index,
            )
        elif value_type == VALUE_PTR:
            value = _native.qhw_adm_py_metadata_ptr(
                metadata,
                metadata_count,
                index,
            )
        else:
            value = None
        copied.append((key, value_type, value))

    return tuple(copied)


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


class AdmissionRequest:
    def __init__(
        self,
        request_id,
        device_id,
        user_id,
        job_id,
        scope_id,
        workload_kind,
        walltime_ns,
        task_class,
        reservation_id=0,
        ttl_ns=0,
        classical_runtime_ns=0,
        overhead_ns=0,
        priority=0,
    ):
        self._native = _native.qhw_adm_py_request_create_single(
            request_id,
            device_id,
            user_id,
            job_id,
            scope_id,
            reservation_id,
            workload_kind,
            walltime_ns,
            ttl_ns,
            classical_runtime_ns,
            overhead_ns,
            priority,
            task_class._native,
        )
        if self._native is None:
            raise AdmissionError("qhw_adm_request allocation failed")

    def close(self):
        if self._native is not None:
            _native.qhw_adm_py_request_destroy(self._native)
            self._native = None

    def __del__(self):
        self.close()


class Decision:
    def __init__(self, native):
        self.decision = native.decision
        self.request_id = native.request_id
        self.device_id = native.device_id
        self.scope_id = native.scope_id
        self.reservation_id = native.reservation_id
        self.reason_code = native.reason_code
        self.credits_required = native.credits_required
        self.rate_required = native.rate_required
        self.capacity_available = native.capacity_available
        self.estimated_total_ns = native.estimated_total_ns
        self.estimated_start_ns = native.estimated_start_ns
        self.estimated_finish_ns = native.estimated_finish_ns
        self.latest_finish_ns = native.latest_finish_ns
        self.quantum_budget_ns = native.quantum_budget_ns
        self.capacity_granted = native.capacity_granted
        self.compliance_action = native.compliance_action
        self.retry_after_ns = native.retry_after_ns
        self.confidence_ppm = native.confidence_ppm
        self.message = native.message
        self.metadata = _copy_metadata(native.metadata, native.metadata_count)


class Reservation:
    def __init__(self, native):
        self.reservation_id = native.reservation_id
        self.request_id = native.request_id
        self.device_id = native.device_id
        self.scope_id = native.scope_id
        self.user_id = native.user_id
        self.job_id = native.job_id
        self.workload_kind = native.workload_kind
        self.state = native.state
        self.credits_reserved = native.credits_reserved
        self.credits_consumed = native.credits_consumed
        self.rate_reserved = native.rate_reserved
        self.rate_consumed = native.rate_consumed
        self.quantum_budget_ns = native.quantum_budget_ns
        self.device_profile_version = native.device_profile_version
        self.policy_version = native.policy_version
        self.estimator_version = native.estimator_version
        self.estimated_total_ns = native.estimated_total_ns
        self.actual_total_ns = native.actual_total_ns
        self.unused_capacity = native.unused_capacity
        self.overuse_count = native.overuse_count
        self.underuse_score = native.underuse_score
        self.created_at_ns = native.created_at_ns
        self.expires_at_ns = native.expires_at_ns
        self.metadata = _copy_metadata(native.metadata, native.metadata_count)


class CapacityView:
    def __init__(self, native):
        self.device_id = native.device_id
        self.scope_id = native.scope_id
        self.device_state = native.device_state
        self.now_ns = native.now_ns
        self.next_available_ns = native.next_available_ns
        self.queued_baseline_units = native.queued_baseline_units
        self.queued_estimated_ns = native.queued_estimated_ns
        self.active_reservation_count = native.active_reservation_count
        self.total_credits = native.total_credits
        self.credits_reserved = native.credits_reserved
        self.credits_consumed = native.credits_consumed
        self.credits_returned = native.credits_returned
        self.core_available_credits = native.core_available_credits
        self.external_credit_limit = native.external_credit_limit
        self.scoped_reserved_credits = native.scoped_reserved_credits
        self.effective_available_credits = native.effective_available_credits
        self.total_rate = native.total_rate
        self.rate_reserved = native.rate_reserved
        self.rate_consumed = native.rate_consumed
        self.rate_returned = native.rate_returned
        self.core_available_rate = native.core_available_rate
        self.external_rate_limit = native.external_rate_limit
        self.scoped_reserved_rate = native.scoped_reserved_rate
        self.effective_available_rate = native.effective_available_rate
        self.scheduler_policy_id = native.scheduler_policy_id
        self.confidence_ppm = native.confidence_ppm
        self.metadata = _copy_metadata(native.metadata, native.metadata_count)


class Usage:
    def __init__(
        self,
        reservation_id,
        task_id=0,
        class_id=0,
        event_time_ns=0,
        estimated_ns=0,
        actual_ns=0,
        baseline_units=0,
        credits=0,
        rate_units=0,
    ):
        self._native = _native.qhw_adm_usage_t()
        self._native.struct_size = _native.qhw_adm_usage_sizeof()
        self._native.reservation_id = reservation_id
        self._native.task_id = task_id
        self._native.class_id = class_id
        self._native.event_time_ns = event_time_ns
        self._native.estimated_ns = estimated_ns
        self._native.actual_ns = actual_ns
        self._native.baseline_units = baseline_units
        self._native.credits = credits
        self._native.rate_units = rate_units
        self._native.metadata = None
        self._native.metadata_count = 0


class UsageState:
    def __init__(self, native):
        self.reservation_id = native.reservation_id
        self.credits_reserved = native.credits_reserved
        self.credits_consumed = native.credits_consumed
        self.rate_reserved = native.rate_reserved
        self.rate_consumed = native.rate_consumed
        self.estimated_total_ns = native.estimated_total_ns
        self.actual_total_ns = native.actual_total_ns
        self.remaining_credits = native.remaining_credits
        self.remaining_rate = native.remaining_rate


class Compliance:
    def __init__(self, native):
        self.reservation_id = native.reservation_id
        self.overuse_count = native.overuse_count
        self.underuse_score = native.underuse_score
        self.unused_capacity = native.unused_capacity
        self.action = native.action
        self.message = native.message


class ActualUsage:
    def __init__(
        self,
        reservation_id,
        task_id=0,
        observed_device_ns=0,
        observed_compile_ns=0,
        observed_transfer_ns=0,
        observed_control_overhead_ns=0,
    ):
        self._native = _native.qhw_adm_actual_usage_t()
        self._native.struct_size = _native.qhw_adm_actual_usage_sizeof()
        self._native.reservation_id = reservation_id
        self._native.task_id = task_id
        self._native.observed_device_ns = observed_device_ns
        self._native.observed_compile_ns = observed_compile_ns
        self._native.observed_transfer_ns = observed_transfer_ns
        self._native.observed_control_overhead_ns = (
            observed_control_overhead_ns
        )
        self._native.metadata = None
        self._native.metadata_count = 0


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

    def load_policy(self, path):
        self._require_open()
        policy = _native.qhw_adm_py_load_policy(self._ctx, path)
        if policy is None:
            detail = self.last_error
            if detail:
                raise AdmissionError(f"qhw_adm_load_policy failed: {detail}")
            raise AdmissionError("qhw_adm_load_policy failed")
        return policy

    def add_estimator_path(self, path):
        self._require_open()
        rc = _native.qhw_adm_add_estimator_path(self._ctx, path)
        self._check_rc(rc, "qhw_adm_add_estimator_path")

    def add_policy_path(self, path):
        self._require_open()
        rc = _native.qhw_adm_add_policy_path(self._ctx, path)
        self._check_rc(rc, "qhw_adm_add_policy_path")

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

    def set_policy(self, device_id, name):
        self._require_open()
        rc = _native.qhw_adm_set_policy(
            self._ctx,
            device_id,
            name,
            None,
            0,
        )
        self._check_rc(rc, "qhw_adm_set_policy")

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

    def evaluate(self, request):
        self._require_open()
        decision = _native.qhw_adm_decision_t()
        decision.struct_size = _native.qhw_adm_decision_sizeof()
        rc = _native.qhw_adm_evaluate(self._ctx, request._native, decision)
        self._check_rc(rc, "qhw_adm_evaluate")
        return Decision(decision)

    def reserve(self, request):
        self._require_open()
        decision = _native.qhw_adm_decision_t()
        decision.struct_size = _native.qhw_adm_decision_sizeof()
        rc = _native.qhw_adm_reserve(self._ctx, request._native, decision)
        self._check_rc(rc, "qhw_adm_reserve")
        return Decision(decision)

    def get_reservation(self, reservation_id):
        self._require_open()
        reservation = _native.qhw_adm_reservation_t()
        reservation.struct_size = _native.qhw_adm_reservation_sizeof()
        rc = _native.qhw_adm_get_reservation(
            self._ctx,
            reservation_id,
            reservation,
        )
        self._check_rc(rc, "qhw_adm_get_reservation")
        return Reservation(reservation)

    def get_capacity(self, device_id, scope_id=0):
        self._require_open()
        capacity = _native.qhw_adm_capacity_view_t()
        capacity.struct_size = _native.qhw_adm_capacity_view_sizeof()
        rc = _native.qhw_adm_get_capacity(
            self._ctx,
            device_id,
            scope_id,
            capacity,
        )
        self._check_rc(rc, "qhw_adm_get_capacity")
        return CapacityView(capacity)

    def release(self, reservation_id, reason_code=REASON_NONE):
        self._require_open()
        rc = _native.qhw_adm_release(
            self._ctx,
            reservation_id,
            reason_code,
        )
        self._check_rc(rc, "qhw_adm_release")

    def cancel(self, reservation_id, reason_code=REASON_NONE):
        self._require_open()
        rc = _native.qhw_adm_cancel(
            self._ctx,
            reservation_id,
            reason_code,
        )
        self._check_rc(rc, "qhw_adm_cancel")

    def renew(self, reservation_id, now_ns, ttl_ns):
        self._require_open()
        rc = _native.qhw_adm_renew(
            self._ctx,
            reservation_id,
            now_ns,
            ttl_ns,
        )
        self._check_rc(rc, "qhw_adm_renew")

    def expire(self, now_ns=0):
        self._require_open()
        expired_count = _native.qhw_adm_py_expire(self._ctx, now_ns)
        if expired_count < 0:
            self._check_rc(ERR_STATE, "qhw_adm_expire")
        return expired_count

    def authorize_usage(self, reservation_id, usage):
        self._require_open()
        decision = _native.qhw_adm_decision_t()
        decision.struct_size = _native.qhw_adm_decision_sizeof()
        rc = _native.qhw_adm_authorize_usage(
            self._ctx,
            reservation_id,
            usage._native,
            decision,
        )
        self._check_rc(rc, "qhw_adm_authorize_usage")
        return Decision(decision)

    def consume(self, reservation_id, usage):
        self._require_open()
        decision = _native.qhw_adm_decision_t()
        decision.struct_size = _native.qhw_adm_decision_sizeof()
        rc = _native.qhw_adm_consume(
            self._ctx,
            reservation_id,
            usage._native,
            decision,
        )
        self._check_rc(rc, "qhw_adm_consume")
        return Decision(decision)

    def return_usage(self, reservation_id, usage):
        self._require_open()
        rc = _native.qhw_adm_return_usage(
            self._ctx,
            reservation_id,
            usage._native,
        )
        self._check_rc(rc, "qhw_adm_return_usage")

    def get_usage(self, reservation_id):
        self._require_open()
        usage = _native.qhw_adm_usage_state_t()
        usage.struct_size = _native.qhw_adm_usage_state_sizeof()
        rc = _native.qhw_adm_get_usage(self._ctx, reservation_id, usage)
        self._check_rc(rc, "qhw_adm_get_usage")
        return UsageState(usage)

    def get_compliance(self, reservation_id):
        self._require_open()
        compliance = _native.qhw_adm_compliance_t()
        compliance.struct_size = _native.qhw_adm_compliance_sizeof()
        rc = _native.qhw_adm_get_compliance(
            self._ctx,
            reservation_id,
            compliance,
        )
        self._check_rc(rc, "qhw_adm_get_compliance")
        return Compliance(compliance)

    def record_actual(self, reservation_id, actual):
        self._require_open()
        rc = _native.qhw_adm_record_actual(
            self._ctx,
            reservation_id,
            actual._native,
        )
        self._check_rc(rc, "qhw_adm_record_actual")

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
    "AdmissionRequest",
    "ActualUsage",
    "Baseline",
    "CapacityView",
    "Compliance",
    "COMPLIANCE_ALLOW",
    "COMPLIANCE_DELAY",
    "COMPLIANCE_REJECT",
    "COMPLIANCE_TERMINATE",
    "COMPLIANCE_THROTTLE",
    "CONFIG_MERGE",
    "CONFIG_REPLACE",
    "DECISION_ACCEPTED",
    "DECISION_DELAYED",
    "DECISION_REJECTED",
    "Decision",
    "DeviceProfile",
    "Estimate",
    "QtaskClass",
    "REASON_NONE",
    "RESERVATION_ACTIVE",
    "RESERVATION_CANCELLED",
    "RESERVATION_EXPIRED",
    "RESERVATION_PENDING",
    "RESERVATION_RELEASED",
    "Reservation",
    "Usage",
    "UsageState",
    "OK",
    "OPT_CREDIT_ALLOW_OVERCOMMIT",
    "OPT_CREDIT_OVERCOMMIT_CREDITS",
    "OPT_CREDIT_OVERCOMMIT_PPM",
    "OPT_CREDIT_RESERVATION_TTL_NS",
    "OPT_RATE_RESERVATION_TTL_NS",
    "OPT_RATE_SLICE",
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
    "VALUE_BOOL",
    "VALUE_F64",
    "VALUE_I64",
    "VALUE_PTR",
    "VALUE_STRING",
    "VALUE_U64",
    "WORKLOAD_HYBRID_JOB",
    "WORKLOAD_QUANTUM_JOB",
]
