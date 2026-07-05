import os
import unittest

from qhw_admission import (
    AdmissionContext,
    AdmissionRequest,
    Baseline,
    DECISION_ACCEPTED,
    DeviceProfile,
    QtaskClass,
    RESERVATION_RELEASED,
    Usage,
    WORKLOAD_HYBRID_JOB,
)


def make_baseline():
    return Baseline(
        qubit_count=4,
        depth=10,
        one_q_gate_count=10,
        two_q_gate_count=5,
        shots=100,
        measurement_count=2,
    )


def make_profile(policy_name):
    return DeviceProfile(
        device_id=7,
        time_span_ns=1_000_000_000,
        baseline=make_baseline(),
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
        total_credits=10 if policy_name == "credit" else 0,
        device_rate=4 if policy_name == "rate" else 0,
        concurrent_jobs=2,
        default_ttl_ns=60_000_000_000,
    )


def make_task():
    return QtaskClass(
        class_id=11,
        count=2,
        qubit_count=4,
        depth=12,
        one_q_gate_count=20,
        two_q_gate_count=10,
        shots=100,
        measurement_count=2,
    )


def make_request(task):
    return AdmissionRequest(
        request_id=42,
        device_id=7,
        user_id=1000,
        job_id=2000,
        scope_id=3,
        workload_kind=WORKLOAD_HYBRID_JOB,
        walltime_ns=2_000_000_000,
        ttl_ns=1_000_000,
        task_class=task,
    )


class InstalledPackageIntegrationTests(unittest.TestCase):
    def policy_dir(self, policy_name):
        key = f"QHW_ADM_TEST_{policy_name.upper()}_DIR"
        return os.environ[key]

    def run_policy_flow(self, policy_name):
        with AdmissionContext() as ctx:
            ctx.register_device(make_profile(policy_name))
            ctx.add_policy_path(self.policy_dir(policy_name))
            ctx.set_policy(7, policy_name)

            device = ctx.get_device(7)
            self.assertEqual(device.device_id, 7)
            self.assertEqual(device.baseline.shots, 100)

            task = make_task()
            decision = ctx.reserve(make_request(task))
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            self.assertNotEqual(decision.reservation_id, 0)

            usage = Usage(
                reservation_id=decision.reservation_id,
                task_id=1001,
                class_id=11,
                baseline_units=1,
                credits=1 if policy_name == "credit" else 0,
                rate_units=1 if policy_name == "rate" else 0,
            )
            consumed = ctx.consume(decision.reservation_id, usage)
            self.assertEqual(consumed.decision, DECISION_ACCEPTED)

            ctx.release(decision.reservation_id)
            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_RELEASED)

    def test_standard_policy_flows(self):
        for policy_name in ("unlimited", "credit", "rate"):
            with self.subTest(policy=policy_name):
                self.run_policy_flow(policy_name)


if __name__ == "__main__":
    unittest.main()
