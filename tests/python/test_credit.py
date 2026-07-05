import os
import unittest

from qhw_admission import (
    AdmissionContext,
    AdmissionError,
    AdmissionRequest,
    Baseline,
    DECISION_ACCEPTED,
    DECISION_DELAYED,
    DECISION_REJECTED,
    DeviceProfile,
    QtaskClass,
    RESERVATION_RELEASED,
    WORKLOAD_HYBRID_JOB,
)


def make_profile(total_credits, time_span_ns=1_000_000_000):
    baseline = Baseline(
        qubit_count=4,
        depth=10,
        one_q_gate_count=10,
        two_q_gate_count=5,
        shots=100,
        measurement_count=2,
    )
    return DeviceProfile(
        device_id=7,
        time_span_ns=time_span_ns,
        baseline=baseline,
        max_qubits=20,
        max_shots=10_000,
        one_q_gate_ns=20,
        two_q_gate_ns=100,
        measurement_ns=1000,
        one_q_gate_transfer_ns=1,
        two_q_gate_transfer_ns=4,
        measurement_transfer_ns=10,
        compile_ns=1000,
        control_overhead_ns=200,
        provider_overhead_ns=300,
        total_credits=total_credits,
        default_ttl_ns=60_000_000_000,
    )


def make_task(count=2):
    return QtaskClass(
        class_id=11,
        count=count,
        qubit_count=4,
        depth=12,
        one_q_gate_count=20,
        two_q_gate_count=10,
        shots=100,
        measurement_count=2,
    )


def make_request(task, request_id=42):
    return AdmissionRequest(
        request_id=request_id,
        device_id=7,
        user_id=1000,
        job_id=2000,
        scope_id=3,
        workload_kind=WORKLOAD_HYBRID_JOB,
        walltime_ns=2_000_000_000,
        ttl_ns=1_000_000,
        task_class=task,
    )


class CreditPolicyTests(unittest.TestCase):
    def setup_context(self, total_credits):
        ctx = AdmissionContext()
        ctx.register_device(make_profile(total_credits))
        ctx.add_policy_path(os.environ["QHW_ADM_TEST_CREDIT_DIR"])
        ctx.set_policy(7, "credit")
        return ctx

    def test_accept_delay_and_release(self):
        task = make_task()

        with self.setup_context(5) as ctx:
            decision = ctx.reserve(make_request(task, 42))
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            self.assertEqual(decision.credits_required, 3)
            self.assertEqual(decision.capacity_granted, 3)
            self.assertEqual(decision.message, "accepted by credit policy")

            delayed = ctx.evaluate(make_request(task, 43))
            self.assertEqual(delayed.decision, DECISION_DELAYED)
            self.assertEqual(delayed.credits_required, 3)
            self.assertEqual(delayed.capacity_available, 2)
            self.assertEqual(delayed.message, "insufficient credits")

            ctx.release(decision.reservation_id)
            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_RELEASED)
            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.credits_reserved, 0)
            self.assertEqual(capacity.effective_available_credits, 5)

    def test_derived_credit_capacity(self):
        task = make_task()

        with AdmissionContext() as ctx:
            ctx.register_device(make_profile(0, time_span_ns=814_650))
            ctx.add_policy_path(os.environ["QHW_ADM_TEST_CREDIT_DIR"])
            ctx.set_policy(7, "credit")
            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.total_credits, 3)
            decision = ctx.reserve(make_request(task, 42))
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            self.assertEqual(decision.credits_required, 3)

    def test_zero_derived_credit_capacity_rejects(self):
        task = make_task(count=1)

        with AdmissionContext() as ctx:
            ctx.register_device(make_profile(0, time_span_ns=1))
            ctx.add_policy_path(os.environ["QHW_ADM_TEST_CREDIT_DIR"])
            ctx.set_policy(7, "credit")
            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.total_credits, 0)
            self.assertEqual(capacity.effective_available_credits, 0)

            decision = ctx.evaluate(make_request(task, 42))
            self.assertEqual(decision.decision, DECISION_REJECTED)
            self.assertEqual(decision.capacity_available, 0)
            self.assertEqual(decision.message, "credit capacity is unavailable")

            decision = ctx.reserve(make_request(task, 42))
            self.assertEqual(decision.decision, DECISION_REJECTED)
            self.assertEqual(decision.reservation_id, 0)

    def test_explicit_credits_override_derivation(self):
        with AdmissionContext() as ctx:
            ctx.register_device(make_profile(5, time_span_ns=814_650))
            ctx.add_policy_path(os.environ["QHW_ADM_TEST_CREDIT_DIR"])
            ctx.set_policy(7, "credit")
            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.total_credits, 5)
            self.assertEqual(capacity.effective_available_credits, 5)

    def test_rejects_oversized_request(self):
        task = make_task()

        with self.setup_context(2) as ctx:
            decision = ctx.evaluate(make_request(task, 42))
            self.assertEqual(decision.decision, DECISION_REJECTED)
            self.assertEqual(decision.credits_required, 3)
            self.assertEqual(
                decision.message,
                "credit request exceeds policy limit",
            )

    def test_yaml_overcommit_options(self):
        policy_dir = os.environ["QHW_ADM_TEST_CREDIT_DIR"]
        config = f"""
plugin_paths:
  policies: ["{policy_dir}"]
devices:
  - device_id: 7
    max_qubits: 20
    max_shots: 10000
    time_span_ns: 1000000000
    baseline:
      qubit_count: 4
      depth: 10
      one_q_gate_count: 10
      two_q_gate_count: 5
      measurement_count: 2
      shots: 100
    timing:
      one_q_gate_ns: 20
      two_q_gate_ns: 100
      measurement_ns: 1000
      one_q_gate_transfer_ns: 1
      two_q_gate_transfer_ns: 4
      measurement_transfer_ns: 10
      compile_ns: 1000
      control_overhead_ns: 200
      provider_overhead_ns: 300
    credit:
      total_credits: 2
    policy:
      name: credit
      options:
        allow_overcommit: true
        overcommit_credits: 1
    default_ttl_ns: 60000000000
"""
        task = make_task()

        with AdmissionContext() as ctx:
            ctx.load_config_string(config)
            decision = ctx.reserve(make_request(task, 42))
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            self.assertEqual(decision.credits_required, 3)
            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.total_credits, 2)
            self.assertEqual(capacity.credits_reserved, 3)

    def test_yaml_invalid_overcommit_ppm_rejected(self):
        policy_dir = os.environ["QHW_ADM_TEST_CREDIT_DIR"]
        config = f"""
plugin_paths:
  policies: ["{policy_dir}"]
devices:
  - device_id: 7
    max_qubits: 20
    max_shots: 10000
    time_span_ns: 1000000000
    baseline:
      qubit_count: 4
      depth: 10
      one_q_gate_count: 10
      two_q_gate_count: 5
      measurement_count: 2
      shots: 100
    timing:
      one_q_gate_ns: 20
      two_q_gate_ns: 100
      measurement_ns: 1000
    credit:
      total_credits: 2
    policy:
      name: credit
      options:
        allow_overcommit: true
        overcommit_ppm: 18446744073709551615
"""

        with AdmissionContext() as ctx:
            with self.assertRaises(AdmissionError):
                ctx.load_config_string(config)


if __name__ == "__main__":
    unittest.main()
