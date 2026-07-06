import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROTOCOL_HEADER = ROOT / "include" / "grinder_protocol.h"
RUNTIME_HEADER = ROOT / "include" / "grinder_runtime.h"
LOW_LATENCY_HEADER = ROOT / "include" / "grinder_runtime_low_latency.h"
DISCOVERY_HEADER = ROOT / "include" / "grinder_discovery.h"
ADAPTIVE_HEADER = ROOT / "include" / "grinder_adaptive_safety.h"
RUNTIME_ADAPTIVE_HEADER = ROOT / "include" / "grinder_runtime_adaptive.h"
PERSISTENCE_HEADER = ROOT / "include" / "grinder_settings_persistence.h"
MENU_HEADER = ROOT / "include" / "menu.h"
HDS_SOURCE = ROOT / "src" / "hds.ino"
POWER_HEADER = ROOT / "include" / "power.h"
MAC_PATTERN = re.compile(r"^[0-9A-F]{2}(:[0-9A-F]{2}){5}$")


def assert_contains(path, text):
    content = path.read_text(encoding="utf-8")
    assert text in content, f"{path} missing {text!r}"


def assert_not_contains(path, text):
    content = path.read_text(encoding="utf-8")
    assert text not in content, f"{path} unexpectedly contains {text!r}"


def parse_response(line):
    parts = line.split()
    if len(parts) == 3 and parts[0] == "OK" and MAC_PATTERN.match(parts[1]):
      state = parts[2].removeprefix("state=")
      if state in {"ON", "OFF"}:
          return {"kind": "OK", "mac": parts[1], "relay_on": state == "ON"}
    if len(parts) == 2 and parts[0] == "BUSY" and MAC_PATTERN.match(parts[1]):
        return {"kind": "BUSY", "mac": parts[1], "relay_on": False}
    if len(parts) == 3 and parts[0] == "ERR" and MAC_PATTERN.match(parts[1]) and parts[2].startswith("reason="):
        return {"kind": "ERR", "mac": parts[1], "reason": parts[2][7:], "relay_on": False}
    return None


def cutoff(target, safety):
    return target - safety


def clamp(value, minimum, maximum):
    return max(minimum, min(maximum, value))


def adaptive_recommendation(current_safety, target, start_weight, final_weight, duration_ms, history, final_locked=True, valid=True):
    if not valid or not final_locked:
        return current_safety, history, False
    if duration_ms < 500:
        return current_safety, history, False
    span = final_weight - start_weight
    if span < 2.0:
        return current_safety, history, False
    average_rate = span / (duration_ms / 1000.0)
    if average_rate <= 0.0 or average_rate > 6.0:
        return current_safety, history, False
    error = final_weight - target
    if abs(error) > 5.0:
        return current_safety, history, False
    recommendation = clamp(current_safety + clamp(error, -2.0, 2.0), 0.0, 10.0)
    next_history = (history + [recommendation])[-3:]
    return sum(next_history) / len(next_history), next_history, True


def skip_reason(current_safety, target, start_weight, final_weight, duration_ms, final_locked=True, valid=True, reason="none"):
    if not valid:
        return reason
    if not final_locked:
        return "not_final"
    if duration_ms < 500:
        return "duration"
    span = final_weight - start_weight
    if span < 2.0:
        return "span"
    average_rate = span / (duration_ms / 1000.0)
    if average_rate <= 0.0 or average_rate > 6.0:
        return "rate"
    if abs(final_weight - target) > 5.0:
        return "error"
    return "none"


def removal_detected(weight, zero_min, zero_max, tolerance, stop_weight, final_weight):
    if weight <= zero_min - tolerance:
        return True
    if not (zero_min <= weight <= zero_max):
        return False
    observed = max(stop_weight, final_weight)
    return observed > zero_max + tolerance and observed - weight >= 2.0


class AdaptiveShotModel:
    def __init__(self):
        self.active = False
        self.valid = True
        self.final_locked = False
        self.start_weight = 0.0
        self.final_weight = 0.0
        self.stable_weight = 0.0
        self.start_at = 0
        self.last_increase_at = 0
        self.off_at = 0
        self.final_at = 0
        self.stable_since_at = 0
        self.reason = "none"

    def track(self, weight, zero_max, target, now):
        if not self.active:
            if weight > zero_max:
                self.active = True
                self.valid = True
                self.final_locked = False
                self.start_weight = zero_max
                self.final_weight = weight
                self.stable_weight = weight
                self.start_at = now
                self.last_increase_at = now
                self.off_at = 0
                self.final_at = 0
                self.stable_since_at = now
                self.reason = "none"
            return
        if self.final_locked or not self.valid:
            return
        if self.off_at:
            if weight + 0.5 < self.final_weight:
                self.invalidate("post_off_drop")
                return
            if weight > self.final_weight + 0.10:
                self.final_weight = weight
            if not self.stable_since_at or abs(weight - self.stable_weight) > 0.15:
                self.stable_weight = weight
                self.stable_since_at = now
                return
            if now - self.off_at >= 1000 and now - self.stable_since_at >= 1500:
                self.final_weight = weight
                self.final_at = now
                self.final_locked = True
            return
        if weight > self.final_weight + 0.10:
            self.final_weight = weight
            self.last_increase_at = now
            return
        if not self.off_at and self.final_weight < target and now - self.last_increase_at >= 2000:
            self.invalidate("stall")

    def mark_off(self, weight, zero_max, target, now):
        self.track(weight, zero_max, target, now)
        if self.active and self.valid and not self.off_at:
            self.off_at = now
            self.stable_weight = weight
            self.stable_since_at = now

    def learn(self, current_safety, target, history):
        duration_ms = self.final_at - self.start_at
        return adaptive_recommendation(
            current_safety,
            target,
            self.start_weight,
            self.final_weight,
            duration_ms,
            history,
            final_locked=self.final_locked,
            valid=self.valid,
        )

    def learn_reason(self, current_safety, target):
        duration_ms = self.final_at - self.start_at
        return skip_reason(
            current_safety,
            target,
            self.start_weight,
            self.final_weight,
            duration_ms,
            final_locked=self.final_locked,
            valid=self.valid,
            reason=self.reason,
        )

    def invalidate(self, reason="invalid"):
        self.valid = False
        self.reason = reason


class GrinderModel:
    def __init__(self):
        self.state = "connected"
        self.selected_mac = "A4:C1:38:12:34:56"
        self.zero_min = -1.0
        self.zero_max = 1.0
        self.zero_hold = 1000
        self.target = 20.0
        self.safety = 0.2
        self.grind_rate = 0.0
        self.rate_samples = 0
        self.zero_since = None
        self.zero_track = None
        self.cutoff_guard_active = False
        self.cutoff_guard_zero_exit_at = None
        self.tare_pending = False
        self.grind_confirmed = False
        self.grind_candidate_active = False
        self.setup_mass_blocked = False
        self.grind_candidate_start_at = None
        self.grind_candidate_start_weight = 0.0
        self.grind_candidate_last_weight = 0.0
        self.grind_candidate_positive_samples = 0
        self.now = 0
        self.last_command_at = 0
        self.sent = []
        self.pending = None

    def zero_stable(self, weight):
        if not (self.zero_min <= weight <= self.zero_max):
            self.zero_since = None
            self.zero_track = None
            return False
        if self.zero_track is None or abs(weight - self.zero_track) > 0.15:
            self.zero_track = weight
            self.zero_since = self.now
            return False
        return self.now - self.zero_since >= self.zero_hold

    def send(self, command):
        self.sent.append(command)
        self.pending = command
        self.last_command_at = self.now

    def send_off(self):
        self.sent.append("!\n")
        self.pending = "OFF"
        self.last_command_at = self.now

    def cutoff_protected(self, weight):
        if self.zero_min <= weight <= self.zero_max:
            self.cutoff_guard_active = False
            self.cutoff_guard_zero_exit_at = None
            return True
        if not self.cutoff_guard_active:
            self.cutoff_guard_active = True
            self.cutoff_guard_zero_exit_at = self.now
            return True
        return self.now - self.cutoff_guard_zero_exit_at < 1500

    def reset_grind_confirmation(self):
        self.grind_confirmed = False
        self.grind_candidate_active = False
        self.setup_mass_blocked = False
        self.grind_candidate_start_at = None
        self.grind_candidate_start_weight = 0.0
        self.grind_candidate_last_weight = 0.0
        self.grind_candidate_positive_samples = 0

    def notify_tare_requested(self):
        self.tare_pending = True

    def notify_tare_complete(self):
        self.tare_pending = False
        self.grind_rate = 0.0
        self.rate_samples = 0
        self.cutoff_guard_active = False
        self.cutoff_guard_zero_exit_at = None
        self.reset_grind_confirmation()

    def start_grind_candidate(self, weight):
        self.grind_candidate_active = True
        self.grind_candidate_start_at = self.now
        self.grind_candidate_start_weight = self.zero_max
        self.grind_candidate_last_weight = weight
        self.grind_candidate_positive_samples = 0

    def candidate_rate_valid(self, weight):
        if not self.grind_candidate_active or self.grind_candidate_start_at is None:
            return False
        duration_ms = self.now - self.grind_candidate_start_at
        if duration_ms <= 0:
            return False
        rise = weight - self.grind_candidate_start_weight
        if rise <= 0:
            return False
        return rise / (duration_ms / 1000.0) <= 6.0

    def track_grind_confirmation(self, weight):
        if self.grind_confirmed:
            return True
        if not self.grind_candidate_active:
            if weight > self.zero_max + 3.0:
                self.setup_mass_blocked = True
                return False
            self.start_grind_candidate(weight)
            return False
        if weight > self.grind_candidate_last_weight + 0.03:
            self.grind_candidate_positive_samples += 1
        self.grind_candidate_last_weight = weight
        duration_ms = self.now - self.grind_candidate_start_at
        rise = weight - self.grind_candidate_start_weight
        if (
            duration_ms < 1500
            or rise < 2.0
            or self.grind_candidate_positive_samples < 4
            or not self.candidate_rate_valid(weight)
        ):
            return False
        self.grind_confirmed = True
        self.setup_mass_blocked = False
        return True

    def cutoff_eligible(self, weight, protected):
        if self.tare_pending:
            return False
        if self.zero_min <= weight <= self.zero_max:
            self.reset_grind_confirmation()
            return False
        if self.setup_mass_blocked:
            return False
        if self.track_grind_confirmation(weight):
            return True
        if weight >= cutoff(self.target, self.safety) and not protected:
            self.setup_mass_blocked = True
        return False

    def handle_response(self, line):
        response = parse_response(line)
        if response is None:
            self.state = "error"
            return
        if response["mac"] != self.selected_mac:
            self.state = "error"
            return
        if response["kind"] != "OK":
            self.state = "error"
            return
        if self.pending == "ON" and response["relay_on"]:
            self.pending = None
            self.state = "grinding"
        if self.pending == "OFF" and not response["relay_on"]:
            self.pending = None
            self.state = "await_removal"

    def tick(self, weight, now, connected=True):
        self.now = now
        if not connected and self.state in {"grinding", "stopping"}:
            self.state = "error"
            return
        if self.state == "connected" and self.zero_stable(weight):
            self.state = "armed"
        if self.state == "armed" and self.pending is None:
            self.send("ON")
        if self.state == "grinding":
            protected = self.cutoff_protected(weight)
            eligible = self.cutoff_eligible(weight, protected)
            if weight >= cutoff(self.target, self.safety) and not protected and eligible:
                self.state = "stopping"
                self.send_off()
        if self.state == "stopping" and self.pending == "OFF" and self.now - self.last_command_at >= 150:
            self.send_off()


def test_response_parser_and_wrong_mac():
    assert parse_response("OK A4:C1:38:12:34:56 state=OFF") == {
        "kind": "OK",
        "mac": "A4:C1:38:12:34:56",
        "relay_on": False,
    }
    assert parse_response("BUSY A4:C1:38:12:34:56")["kind"] == "BUSY"
    assert parse_response("ERR A4:C1:38:12:34:56 reason=bad_mac")["reason"] == "bad_mac"
    model = GrinderModel()
    model.pending = "PING"
    model.handle_response("OK A4:C1:38:AA:BB:CC state=OFF")
    assert model.state == "error"


def test_cutoff_math():
    assert abs(cutoff(20.0, 0.2) - 19.8) < 0.0001
    assert abs(cutoff(15.0, 2.0) - 13.0) < 0.0001


def test_adaptive_safety_learns_from_valid_grinds():
    safety, history, learned = adaptive_recommendation(0.2, 15.0, 1.0, 15.2, 5000, [])
    assert learned
    assert abs(safety - 0.4) < 0.0001
    safety, history, learned = adaptive_recommendation(safety, 15.0, 1.0, 14.9, 5000, history)
    assert learned
    assert abs(safety - 0.35) < 0.0001
    safety, history, learned = adaptive_recommendation(safety, 15.0, 1.0, 15.1, 5000, history)
    assert learned
    assert abs(safety - 0.3833333) < 0.0001
    safety, history, learned = adaptive_recommendation(safety, 15.0, 1.0, 15.4, 5000, history)
    assert learned
    assert abs(safety - 0.5111111) < 0.0001


def test_adaptive_safety_rejects_fast_fake_grind():
    safety, history, learned = adaptive_recommendation(0.2, 15.0, 1.0, 15.2, 1000, [])
    assert not learned
    assert safety == 0.2
    assert history == []


def test_adaptive_safety_waits_for_stable_post_off_weight():
    shot = AdaptiveShotModel()
    shot.track(1.2, 1.0, 15.0, 1000)
    shot.track(12.0, 1.0, 15.0, 4500)
    shot.mark_off(14.8, 1.0, 15.0, 5000)
    shot.track(15.2, 1.0, 15.0, 6000)
    assert not shot.final_locked
    shot.track(15.24, 1.0, 15.0, 7000)
    assert not shot.final_locked
    shot.track(15.18, 1.0, 15.0, 7600)
    safety, history, learned = shot.learn(0.2, 15.0, [])
    assert learned
    assert shot.final_weight == 15.18
    assert abs(safety - 0.38) < 0.0001


def test_adaptive_safety_rejects_pickup_transient_before_final_lock():
    shot = AdaptiveShotModel()
    shot.track(1.2, 1.0, 15.0, 1000)
    shot.track(12.0, 1.0, 15.0, 4500)
    shot.mark_off(14.8, 1.0, 15.0, 5000)
    shot.track(15.2, 1.0, 15.0, 6000)
    shot.track(18.0, 1.0, 15.0, 6500)
    shot.track(15.2, 1.0, 15.0, 7000)
    safety, history, learned = shot.learn(0.2, 15.0, [])
    assert not learned
    assert shot.learn_reason(0.2, 15.0) == "post_off_drop"
    assert safety == 0.2
    assert history == []


def test_adaptive_safety_rejects_below_target_stall():
    shot = AdaptiveShotModel()
    shot.track(1.2, 1.0, 15.0, 1000)
    shot.track(1.25, 1.0, 15.0, 3000)
    shot.mark_off(1.25, 1.0, 15.0, 3200)
    shot.track(1.25, 1.0, 15.0, 4200)
    safety, history, learned = shot.learn(0.2, 15.0, [])
    assert not learned
    assert safety == 0.2
    assert history == []


def test_removal_to_zero_range_counts_and_skips_early_learning():
    shot = AdaptiveShotModel()
    shot.track(1.2, 1.0, 14.6, 60000)
    shot.track(21.21, 1.0, 14.6, 68000)
    shot.mark_off(21.21, 1.0, 14.6, 69000)
    removed = removal_detected(0.23, -1.0, 1.0, 0.5, 21.21, shot.final_weight)
    assert removed
    shot.invalidate("removed")
    safety, history, learned = shot.learn(0.3, 14.6, [])
    assert not learned
    assert shot.learn_reason(0.3, 14.6) == "removed"
    assert safety == 0.3
    assert history == []


def test_post_off_weight_drop_skips_learning():
    shot = AdaptiveShotModel()
    shot.track(6.31, 1.0, 14.6, 296507)
    shot.track(6.44, 1.0, 14.6, 297512)
    shot.track(11.90, 1.0, 14.6, 301507)
    shot.mark_off(13.41, 1.0, 14.6, 301900)
    shot.track(6.80, 1.0, 14.6, 302900)
    safety, history, learned = shot.learn(2.3, 14.6, [])
    assert not learned
    assert shot.learn_reason(2.3, 14.6) == "post_off_drop"
    assert safety == 2.3
    assert history == []


def test_on_blocked_until_stable_zero():
    model = GrinderModel()
    model.tick(0.0, 0)
    model.tick(0.1, 500)
    assert "ON" not in model.sent
    model.tick(0.1, 1001)
    assert model.state == "armed"
    assert model.sent == ["ON"]


def test_lost_connection_enters_error():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 10.0
    model.tick(12.0, 2500, connected=False)
    assert model.state == "error"
    assert model.sent == []


def test_fast_off_emits_emergency_line():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 10.0
    model.tick(1.2, 1000)
    model.tick(2.5, 1500)
    model.tick(4.0, 2000)
    model.tick(6.0, 2500)
    model.tick(8.2, 3000)
    model.tick(9.9, 3500)
    assert model.sent == ["!\n"]
    assert model.pending == "OFF"


def test_zero_exit_guard_blocks_fast_jump_before_1500_ms():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 15.0
    model.safety = 0.2
    model.tick(20.0, 1000)
    model.tick(20.0, 2499)
    assert model.sent == []
    assert model.state == "grinding"


def test_zero_exit_guard_allows_cutoff_after_1500_ms():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 15.0
    model.safety = 0.2
    model.tick(1.2, 1000)
    model.tick(3.0, 1600)
    model.tick(5.0, 2200)
    model.tick(8.0, 2800)
    model.tick(10.0, 3400)
    model.tick(12.0, 4000)
    model.tick(14.8, 4600)
    assert model.sent == ["!\n"]
    assert model.state == "stopping"


def test_zero_exit_guard_resets_inside_zero_range():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 15.0
    model.safety = 0.2
    model.tick(20.0, 1000)
    model.tick(0.2, 1800)
    model.tick(20.0, 1900)
    model.tick(20.0, 3399)
    assert model.sent == []
    assert model.state == "grinding"
    model.tick(20.0, 3400)
    assert model.sent == []
    assert model.setup_mass_blocked


def test_cup_placement_above_cutoff_blocks_without_off():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 15.0
    model.safety = 0.2
    model.tick(180.0, 1000)
    model.tick(180.0, 2500)
    assert model.sent == []
    assert model.state == "grinding"
    assert model.setup_mass_blocked
    assert not model.grind_confirmed


def test_tare_request_blocks_cutoff_immediately():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 15.0
    model.safety = 0.2
    model.grind_confirmed = True
    model.cutoff_guard_active = True
    model.cutoff_guard_zero_exit_at = 0
    model.notify_tare_requested()
    model.tick(14.8, 3000)
    assert model.sent == []
    assert model.state == "grinding"
    assert model.tare_pending


def test_tare_complete_resets_guard_and_grind_confirmation():
    model = GrinderModel()
    model.state = "grinding"
    model.tare_pending = True
    model.grind_confirmed = True
    model.setup_mass_blocked = True
    model.cutoff_guard_active = True
    model.cutoff_guard_zero_exit_at = 1000
    model.notify_tare_complete()
    assert not model.tare_pending
    assert not model.grind_confirmed
    assert not model.setup_mass_blocked
    assert not model.cutoff_guard_active


def test_after_tare_real_rising_grind_can_cutoff():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 15.0
    model.safety = 0.2
    model.notify_tare_requested()
    model.tick(180.0, 1000)
    model.notify_tare_complete()
    model.tick(0.0, 1100)
    model.tick(1.2, 1200)
    model.tick(3.0, 1800)
    model.tick(5.0, 2300)
    model.tick(8.0, 3000)
    model.tick(10.0, 3600)
    model.tick(14.9, 5000)
    assert model.sent == ["!\n"]
    assert model.state == "stopping"


def test_returning_to_zero_resets_setup_mass_block():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 15.0
    model.tick(30.0, 1000)
    model.tick(30.0, 2500)
    assert model.setup_mass_blocked
    model.tick(0.0, 2600)
    assert not model.setup_mass_blocked
    assert not model.grind_confirmed


def test_real_grind_below_four_gps_reaches_cutoff():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 15.0
    model.safety = 0.2
    model.tick(1.2, 1000)
    model.tick(3.0, 1600)
    model.tick(4.8, 2100)
    model.tick(9.0, 3300)
    model.tick(14.8, 5000)
    assert model.grind_confirmed
    assert model.sent == ["!\n"]


def test_too_fast_step_rise_is_setup_mass_not_grind():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 15.0
    model.safety = 0.2
    model.tick(1.2, 1000)
    model.tick(20.0, 2500)
    assert model.sent == []
    assert model.setup_mass_blocked
    assert not model.grind_confirmed


def test_fast_grind_near_six_gps_reaches_cutoff():
    model = GrinderModel()
    model.state = "grinding"
    model.target = 15.0
    model.safety = 0.2
    model.tick(1.2, 1000)
    model.tick(3.4, 1400)
    model.tick(5.8, 1800)
    model.tick(8.2, 2200)
    model.tick(10.6, 2600)
    model.tick(13.0, 3000)
    model.tick(14.8, 3300)
    assert model.grind_confirmed
    assert model.sent == ["!\n"]


def test_duplicate_off_retry_safe():
    model = GrinderModel()
    model.state = "stopping"
    model.pending = "OFF"
    model.last_command_at = 0
    model.tick(19.0, 149)
    assert model.sent == []
    model.tick(19.0, 150)
    assert model.sent == ["!\n"]
    model.tick(19.0, 300)
    assert model.sent == ["!\n", "!\n"]
    model.handle_response("OK A4:C1:38:12:34:56 state=OFF")
    assert model.state == "await_removal"


def test_low_latency_source_order_and_weight_sources():
    low_latency = LOW_LATENCY_HEADER.read_text(encoding="utf-8")
    cutoff_start = low_latency.index("static inline void grinderTickGrindingCutoff")
    assert low_latency.index("grinderSendOff()", cutoff_start) < low_latency.index("[grinder] cutoff", cutoff_start)
    assert "const uint8_t command[] = { '!', '\\n' };" in low_latency
    assert "grinderRuntime.client.write(command, sizeof(command))" in low_latency
    assert 'return grinderSendSimpleCommand("OFF", GRINDER_COMMAND_OFF);' in low_latency
    assert "GRINDER_CUTOFF_ZERO_EXIT_PROTECTION_MS 1500" in low_latency
    assert "grinderCutoffProtected" in low_latency
    assert "grinderCutoffEligible" in low_latency
    assert "grinderRuntimeNotifyTareRequested" in low_latency
    assert "grinderRuntimeNotifyTareComplete" in low_latency
    assert "GRINDER_CONFIRM_MIN_POSITIVE_SAMPLES 4" in low_latency
    assert "GRINDER_CONFIRM_MAX_INITIAL_GRAMS 3.0f" in low_latency
    assert "GRINDER_ADAPTIVE_MAX_AVERAGE_RATE_GPS" in low_latency
    assert "grinderCutoffGrams(grinderSettings.targetGrams, grinderSettings.safetyMarginGrams)" in low_latency
    fresh_start = low_latency.index("static inline void grinderRuntimeFreshWeightTick")
    assert low_latency.index("grinderPlugConnectionStale(millis())", fresh_start) < low_latency.index("grinderTickGrindingCutoff(weight);", fresh_start)

    hds = HDS_SOURCE.read_text(encoding="utf-8")
    assert hds.index("f_grinder_fast_weight = tracking_compensated;") < hds.index("float stable_output = applyStableOutput(tracking_compensated);")
    assert "grinderRuntimeFreshWeightTick(f_grinder_fast_weight, grinderFastWeightSequence);" in hds
    assert "grinderRuntimeTick(f_displayedValue);" in hds
    assert "grinderRuntimeNotifyTareRequested();" in hds
    assert "grinderRuntimeNotifyTareComplete();" in hds

    runtime = RUNTIME_HEADER.read_text(encoding="utf-8")
    assert "case GRINDER_STATE_GRINDING:" in runtime
    assert "grinderTrackAdaptiveShot(weight);" in runtime
    assert "grinderTickGrinding(weight);" not in runtime
    assert "tarePending" in runtime
    assert "grindConfirmed" in runtime
    assert "setupMassBlocked" in runtime
    assert "GRINDER_RUNTIME_PING_TIMEOUT_MS 1200" in runtime
    assert "grinderRuntime.pendingCommand == GRINDER_COMMAND_PING" in runtime


def test_sampling_changes_blocked_during_grinder_cutoff_states():
    low_latency = LOW_LATENCY_HEADER.read_text(encoding="utf-8")
    assert "grinderRuntimeLocksScaleSampling" in low_latency
    assert "GRINDER_STATE_GRINDING" in low_latency
    assert "GRINDER_STATE_STOPPING" in low_latency
    assert "GRINDER_STATE_FINDING_PLUG" in low_latency
    assert_contains(HDS_SOURCE, "grinderRuntimeLocksScaleSampling()")
    assert_contains(MENU_HEADER, "grinderRuntimeLocksScaleSampling()")


def test_firmware_contracts():
    assert_contains(PROTOCOL_HEADER, "GRINDER_TCP_PORT 31980")
    assert_contains(PROTOCOL_HEADER, "grinderParseResponse")
    assert_contains(PROTOCOL_HEADER, "grinderCopyMacPrefix")
    assert_contains(PROTOCOL_HEADER, "grinderCutoffGrams")
    assert_contains(PROTOCOL_HEADER, "return targetGrams - safetyMarginGrams;")
    assert_not_contains(PROTOCOL_HEADER, "effectiveLatencySeconds")
    assert_not_contains(RUNTIME_HEADER, "effectiveLatencySeconds")
    assert_not_contains(RUNTIME_HEADER, 'preferences.getFloat("latency"')
    assert_not_contains(RUNTIME_HEADER, 'preferences.putFloat("latency"')
    assert_contains(RUNTIME_HEADER, "targetGrams = 15.0f")
    assert_contains(RUNTIME_HEADER, "GRINDER_RUNTIME_RECONNECT_INTERVAL_MS 3000")
    assert_contains(RUNTIME_HEADER, "GRINDER_RUNTIME_STARTUP_DISCOVERY_DELAY_MS 15000")
    assert_contains(RUNTIME_HEADER, "GRINDER_RUNTIME_HOST_RESOLVE_TIMEOUT_MS 250")
    assert_contains(RUNTIME_HEADER, 'preferences.begin("grinder"')
    assert_contains(RUNTIME_HEADER, '#include "grinder_runtime_adaptive.h"')
    assert_contains(RUNTIME_ADAPTIVE_HEADER, "grinderResetAdaptiveSafety")
    assert_contains(RUNTIME_ADAPTIVE_HEADER, "grinderLearnAdaptiveSafety")
    assert_contains(RUNTIME_ADAPTIVE_HEADER, "grinderLearnAdaptiveSafetyIfReady")
    assert_contains(RUNTIME_ADAPTIVE_HEADER, "grinderInvalidateAdaptiveShot")
    assert_contains(RUNTIME_ADAPTIVE_HEADER, "reason=%s")
    assert_contains(RUNTIME_ADAPTIVE_HEADER, "grinderMarkSettingsDirty();")
    assert_not_contains(RUNTIME_ADAPTIVE_HEADER, "grinderSaveSettings();")
    assert_contains(RUNTIME_HEADER, "settingsDirty")
    assert_contains(RUNTIME_HEADER, '#include "grinder_settings_persistence.h"')
    assert_contains(PERSISTENCE_HEADER, "grinderFlushSettingsIfDirty")
    assert_contains(PERSISTENCE_HEADER, "grinderSaveLastIpIfChanged")
    assert_contains(PERSISTENCE_HEADER, "grinderSaveLookupHintsIfChanged")
    assert_contains(RUNTIME_HEADER, 'preferences.getUInt("safe_count"')
    assert_contains(RUNTIME_HEADER, 'preferences.isKey("safe0"')
    assert_contains(RUNTIME_HEADER, 'preferences.isKey("safe1"')
    assert_contains(RUNTIME_HEADER, 'preferences.isKey("safe2"')
    assert_contains(RUNTIME_HEADER, 'preferences.putFloat("safe2"')
    assert_contains(RUNTIME_HEADER, "esp_read_mac(mac, ESP_MAC_WIFI_STA)")
    assert_contains(RUNTIME_HEADER, "[grinder] bad response line=")
    assert_contains(RUNTIME_HEADER, '#include "grinder_discovery.h"')
    assert_contains(DISCOVERY_HEADER, 'MDNS.queryService("grinderplug", "tcp")')
    assert_contains(DISCOVERY_HEADER, "grinderDiscoverPlugs(bool debugRaw = true, uint8_t attempts = 3)")
    assert_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsByRawMdns")
    assert_contains(DISCOVERY_HEADER, "grinderAddDiscoveryFromRawMdnsResult")
    assert_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsByRawMdns(350, false)")
    assert_contains(DISCOVERY_HEADER, "grinderFindSelectedByMdns(GrinderDiscoveredPlug *plug, bool debugRaw = true)")
    assert_contains(RUNTIME_HEADER, "grinderFindSelectedByMdns(&plug, false)")
    assert_contains(RUNTIME_HEADER, "now - grinderRuntime.stateEnteredAt < GRINDER_RUNTIME_STARTUP_DISCOVERY_DELAY_MS")
    assert_contains(DISCOVERY_HEADER, "GRINDER_DISCOVERY_ENABLE_TCP_FALLBACK 0")
    assert_contains(DISCOVERY_HEADER, "wifiEnsureMdnsReadyForSta")
    assert_contains(DISCOVERY_HEADER, "WiFi.setSleep(false)")
    assert_contains(DISCOVERY_HEADER, 'mdns_query_ptr("_grinderplug", "_tcp"')
    assert_contains(DISCOVERY_HEADER, "grinderDebugRawMdnsQuery")
    assert_contains(DISCOVERY_HEADER, 'model != "NOUS_A6T"')
    assert_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsByTcpScan")
    assert_contains(DISCOVERY_HEADER, "GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS")
    assert_contains(DISCOVERY_HEADER, "GRINDER_TCP_RESPONSE_BUSY")
    assert_contains(ADAPTIVE_HEADER, "GRINDER_ADAPTIVE_MAX_AVERAGE_RATE_GPS 6.0f")
    assert_contains(ADAPTIVE_HEADER, "GRINDER_ADAPTIVE_FINAL_DELAY_MS 1000")
    assert_contains(ADAPTIVE_HEADER, "GRINDER_ADAPTIVE_FINAL_STABLE_MS 1500")
    assert_contains(ADAPTIVE_HEADER, "GRINDER_ADAPTIVE_FINAL_STABLE_TOLERANCE_GRAMS 0.15f")
    assert_contains(ADAPTIVE_HEADER, "GRINDER_ADAPTIVE_STALL_MS 2000")
    assert_contains(ADAPTIVE_HEADER, "GRINDER_ADAPTIVE_MAX_POST_OFF_DROP_GRAMS 0.50f")
    assert_contains(ADAPTIVE_HEADER, "grinderAdaptiveSafetyCalculate")
    assert_contains(ADAPTIVE_HEADER, "grinderAdaptiveSafetyRecord")
    assert_contains(ADAPTIVE_HEADER, "grinderAdaptiveShotInvalidate")
    assert_contains(ADAPTIVE_HEADER, "grinderAdaptiveShotMarkOff")
    assert_contains(ADAPTIVE_HEADER, "grinderAdaptiveSkipReasonText")
    assert_contains(ADAPTIVE_HEADER, "GRINDER_ADAPTIVE_SKIP_POST_OFF_DROP")
    assert_contains(ADAPTIVE_HEADER, "GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE 3")
    assert_contains(RUNTIME_HEADER, "grinderWeightIndicatesRemoval")
    assert_contains(RUNTIME_HEADER, "observedDoseWeight - weight >= GRINDER_ADAPTIVE_MIN_SPAN_GRAMS")
    assert_contains(RUNTIME_HEADER, "GRINDER_STATE_AWAIT_REMOVAL")
    assert_contains(RUNTIME_HEADER, "GRINDER_STATE_AWAIT_ZERO")
    assert_contains(RUNTIME_HEADER, '#include "grinder_runtime_low_latency.h"')
    assert_contains(LOW_LATENCY_HEADER, "grinderSendOff")
    assert_contains(LOW_LATENCY_HEADER, "grinderRuntimeFreshWeightTick")
    assert_contains(LOW_LATENCY_HEADER, "grinderMaintainWifiLatencyMode")
    assert_contains(LOW_LATENCY_HEADER, "esp_wifi_set_ps(WIFI_PS_NONE)")
    assert_contains(LOW_LATENCY_HEADER, "esp_wifi_set_ps(WIFI_PS_MIN_MODEM)")
    assert_contains(RUNTIME_HEADER, "client.connect(ip, GRINDER_TCP_PORT, GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS)")
    assert_contains(MENU_HEADER, "menuGrinder")
    assert_contains(MENU_HEADER, "Grinder Plug")
    assert_contains(MENU_HEADER, "grinderFindPlugsForSelection")
    assert_contains(MENU_HEADER, "grinderDrawPlugList")
    assert_contains(MENU_HEADER, "grinderEditNumber")
    assert_contains(MENU_HEADER, "grinderHandleDraftAdjust")
    assert_contains(MENU_HEADER, "bool grinderEnsureWifiReadyForDiscovery()")
    assert_contains(MENU_HEADER, "if (!b_wifiEnabled) {\n    b_wifiOnBoot = true;\n    wifi_init();")
    assert_contains(MENU_HEADER, "step * 10.0f")
    assert_contains(MENU_HEADER, "grinderResetAdaptiveSafety();")
    assert_contains(MENU_HEADER, "bool b_grinderMenuDirectEntry = false")
    assert_contains(MENU_HEADER, "currentSelection->parentMenu == &menuGrinder && b_grinderMenuDirectEntry")
    assert_not_contains(MENU_HEADER, "menuGrinderFind")
    assert_not_contains(RUNTIME_HEADER, "grinderCycleTarget")
    assert_contains(HDS_SOURCE, "grinderLoadSettings();")
    assert_contains(HDS_SOURCE, "grinderRuntimeTick(f_displayedValue);")
    assert_contains(HDS_SOURCE, "GRINDER_MENU_CHORD_HOLD_MS 500")
    assert_contains(HDS_SOURCE, "bool handleGrinderMenuChord()")
    assert_contains(HDS_SOURCE, "currentMenu = grinderMenu;")
    assert_contains(HDS_SOURCE, "b_buttonChordSuppressUntilRelease = true")
    assert_contains(HDS_SOURCE, "buttonChecksSuppressedUntilRelease()")
    assert_contains(HDS_SOURCE, "if (!buttonChecksSuppressedUntilRelease() && !handleGrinderMenuChord())")
    assert_contains(HDS_SOURCE, "void beforeDeepSleepFlush()")
    assert_contains(HDS_SOURCE, "grinderFlushSettingsIfDirty();")
    assert_contains(POWER_HEADER, "beforeDeepSleepFlush();")


if __name__ == "__main__":
    test_response_parser_and_wrong_mac()
    test_cutoff_math()
    test_adaptive_safety_learns_from_valid_grinds()
    test_adaptive_safety_rejects_fast_fake_grind()
    test_adaptive_safety_waits_for_stable_post_off_weight()
    test_adaptive_safety_rejects_pickup_transient_before_final_lock()
    test_adaptive_safety_rejects_below_target_stall()
    test_removal_to_zero_range_counts_and_skips_early_learning()
    test_post_off_weight_drop_skips_learning()
    test_on_blocked_until_stable_zero()
    test_lost_connection_enters_error()
    test_fast_off_emits_emergency_line()
    test_zero_exit_guard_blocks_fast_jump_before_1500_ms()
    test_zero_exit_guard_allows_cutoff_after_1500_ms()
    test_zero_exit_guard_resets_inside_zero_range()
    test_cup_placement_above_cutoff_blocks_without_off()
    test_tare_request_blocks_cutoff_immediately()
    test_tare_complete_resets_guard_and_grind_confirmation()
    test_after_tare_real_rising_grind_can_cutoff()
    test_returning_to_zero_resets_setup_mass_block()
    test_real_grind_below_four_gps_reaches_cutoff()
    test_too_fast_step_rise_is_setup_mass_not_grind()
    test_fast_grind_near_six_gps_reaches_cutoff()
    test_duplicate_off_retry_safe()
    test_low_latency_source_order_and_weight_sources()
    test_sampling_changes_blocked_during_grinder_cutoff_states()
    test_firmware_contracts()
    print("grinder TCP contract tests passed")
