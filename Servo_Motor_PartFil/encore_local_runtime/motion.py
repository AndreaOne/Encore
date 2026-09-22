from __future__ import annotations

from dataclasses import dataclass

from .runtime import FlipEvent


@dataclass(frozen=True)
class VirtualMotionConfig:
    motor_pwm: int = 150
    motor_run_ms: int = 1500
    servo_rest_angle: int = 170
    servo_lift_angle: int = 20
    servo_rise_delay_ms: int = 8
    servo_return_delay_ms: int = 10
    servo_hold_ms: int = 500


@dataclass(frozen=True)
class VirtualMotionEvent:
    time_ms: int
    label: str
    detail: str = ""


def build_virtual_motion_events(
    flip_events: tuple[FlipEvent, ...] | list[FlipEvent],
    config: VirtualMotionConfig | None = None,
) -> tuple[VirtualMotionEvent, ...]:
    motion_config = config or VirtualMotionConfig()
    events: list[VirtualMotionEvent] = []

    servo_up_duration_ms = _servo_duration_ms(
        motion_config.servo_rest_angle,
        motion_config.servo_lift_angle,
        motion_config.servo_rise_delay_ms,
    )
    servo_down_duration_ms = _servo_duration_ms(
        motion_config.servo_lift_angle,
        motion_config.servo_rest_angle,
        motion_config.servo_return_delay_ms,
    )

    for flip_event in flip_events:
        start_ms = int(flip_event.observation_time_ms)
        motor_stop_ms = start_ms + motion_config.motor_run_ms
        servo_up_done_ms = motor_stop_ms + servo_up_duration_ms
        servo_down_start_ms = servo_up_done_ms + motion_config.servo_hold_ms
        flip_end_ms = servo_down_start_ms + servo_down_duration_ms

        events.extend(
            [
                VirtualMotionEvent(
                    time_ms=start_ms,
                    label="FLIP START",
                    detail=(
                        f"page={flip_event.marker.target_page} "
                        f"marker_ms={flip_event.marker.time_ms} "
                        f"estimate_ms={flip_event.estimated_ms:.2f}"
                    ),
                ),
                VirtualMotionEvent(
                    time_ms=start_ms,
                    label="MOTOR START",
                    detail=f"pwm={motion_config.motor_pwm} run_ms={motion_config.motor_run_ms}",
                ),
                VirtualMotionEvent(
                    time_ms=motor_stop_ms,
                    label="MOTOR STOP",
                ),
                VirtualMotionEvent(
                    time_ms=motor_stop_ms,
                    label="SERVO UP",
                    detail=(
                        f"{motion_config.servo_rest_angle}->{motion_config.servo_lift_angle} "
                        f"delay={motion_config.servo_rise_delay_ms}ms "
                        f"duration={servo_up_duration_ms}ms"
                    ),
                ),
                VirtualMotionEvent(
                    time_ms=servo_up_done_ms,
                    label="SERVO HOLD",
                    detail=f"hold_ms={motion_config.servo_hold_ms}",
                ),
                VirtualMotionEvent(
                    time_ms=servo_down_start_ms,
                    label="SERVO DOWN",
                    detail=(
                        f"{motion_config.servo_lift_angle}->{motion_config.servo_rest_angle} "
                        f"delay={motion_config.servo_return_delay_ms}ms "
                        f"duration={servo_down_duration_ms}ms"
                    ),
                ),
                VirtualMotionEvent(
                    time_ms=flip_end_ms,
                    label="FLIP END",
                    detail=f"page={flip_event.marker.target_page}",
                ),
            ]
        )

    return tuple(events)


def _servo_duration_ms(start_angle: int, end_angle: int, step_delay_ms: int) -> int:
    return (abs(end_angle - start_angle) + 1) * step_delay_ms
