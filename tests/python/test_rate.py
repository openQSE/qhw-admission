import os
import unittest

from qhw_admission import (
    AdmissionContext,
    AdmissionRequest,
    Baseline,
    DECISION_ACCEPTED,
    DECISION_DELAYED,
    DECISION_REJECTED,
    DeviceProfile,
    QtaskClass,
    RESERVATION_EXPIRED,
    RESERVATION_RELEASED,
    WORKLOAD_HYBRID_JOB,
)


def make_profile(device_rate=4, concurrent_jobs=2, time_span_ns=1_000_000_000):
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
        device_rate=device_rate,
        concurrent_jobs=concurrent_jobs,
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


def make_request(task, request_id=42, walltime_ns=2_000_000_000):
    return AdmissionRequest(
        request_id=request_id,
        device_id=7,
        user_id=1000,
        job_id=2000,
        scope_id=3,
        workload_kind=WORKLOAD_HYBRID_JOB,
        walltime_ns=walltime_ns,
        ttl_ns=1_000_000,
        task_class=task,
    )


class RatePolicyTests(unittest.TestCase):
    def setup_context(self, profile=None):
        ctx = AdmissionContext()
        ctx.register_device(profile or make_profile())
        ctx.add_policy_path(os.environ["QHW_ADM_TEST_RATE_DIR"])
        ctx.set_policy(7, "rate")
        return ctx

    def test_exact_slice_and_release(self):
        task = make_task()
        request = make_request(task)

        with self.setup_context() as ctx:
            decision = ctx.evaluate(request)
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            self.assertEqual(decision.rate_required, 2)
            self.assertEqual(decision.capacity_available, 4)
            self.assertEqual(decision.capacity_granted, 2)
            self.assertEqual(decision.message, "accepted by rate policy")

            decision = ctx.reserve(request)
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.rate_reserved, 2)
            self.assertEqual(reservation.quantum_budget_ns, 2_000_000_000)

            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.total_rate, 4)
            self.assertEqual(capacity.rate_reserved, 2)
            self.assertEqual(capacity.effective_available_rate, 2)

            ctx.release(decision.reservation_id)
            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_RELEASED)
            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.rate_reserved, 0)
            self.assertEqual(capacity.effective_available_rate, 4)

    def test_multi_slice_and_delayed(self):
        task = make_task()

        with self.setup_context() as ctx:
            first = ctx.reserve(make_request(task, 42, 1_000_000_000))
            self.assertEqual(first.decision, DECISION_ACCEPTED)
            self.assertEqual(first.rate_required, 3)
            self.assertEqual(first.capacity_granted, 4)

            second = ctx.evaluate(make_request(task, 43, 1_000_000_000))
            self.assertEqual(second.decision, DECISION_DELAYED)
            self.assertEqual(second.rate_required, 3)
            self.assertEqual(second.capacity_available, 0)
            self.assertEqual(second.message, "insufficient rate")

    def test_derived_rate_capacity(self):
        profile = make_profile(
            device_rate=0,
            concurrent_jobs=3,
            time_span_ns=814_650,
        )

        with self.setup_context(profile) as ctx:
            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.total_rate, 3)
            self.assertEqual(capacity.effective_available_rate, 3)

    def test_oversized_request_rejected(self):
        profile = make_profile(device_rate=2)
        task = make_task()

        with self.setup_context(profile) as ctx:
            decision = ctx.evaluate(make_request(task, 42, 1_000_000_000))
            self.assertEqual(decision.decision, DECISION_REJECTED)
            self.assertEqual(decision.rate_required, 3)
            self.assertEqual(
                decision.message,
                "rate request exceeds policy limit",
            )

    def test_expire_returns_capacity(self):
        task = make_task()

        with self.setup_context() as ctx:
            decision = ctx.reserve(make_request(task))
            ctx.renew(decision.reservation_id, 1000, 1000)
            self.assertEqual(ctx.expire(2000), 1)
            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_EXPIRED)
            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.rate_reserved, 0)

    def test_yaml_configuration(self):
        policy_dir = os.environ["QHW_ADM_TEST_RATE_DIR"]
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
    rate:
      device_rate: 4
      concurrent_jobs: 2
    policy:
      name: rate
      options:
        rate_slice: 2
"""
        task = make_task()

        with AdmissionContext() as ctx:
            ctx.load_config_string(config)
            decision = ctx.reserve(make_request(task))
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            self.assertEqual(decision.rate_required, 2)
            self.assertEqual(decision.capacity_granted, 2)


if __name__ == "__main__":
    unittest.main()
