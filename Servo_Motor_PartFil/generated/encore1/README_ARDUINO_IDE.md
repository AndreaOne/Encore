# Encore Arduino IDE Notes

Open this folder itself in Arduino IDE:

- `/Users/andrea/Documents/Arduino/Servo_Motor_PartFil/generated/encore`

The main sketch file is:

- `/Users/andrea/Documents/Arduino/Servo_Motor_PartFil/generated/encore/encore.ino`

## What Changed

This runtime now does two important things:

- uses `observation.snappedPitchHz` before score snapping, so hardware tracking matches the local runtime more closely
- keeps the newer tracking / particle-filter logic, while using the proven `final4`-style blocking motion sequence for motor and servo

## Before Upload

Make sure Arduino IDE is set to your ESP32 board and serial port.

If you want to use SPIFFS score files, upload the contents of:

- `/Users/andrea/Documents/Arduino/Servo_Motor_PartFil/generated/encore/data`

The sketch also has built-in score fallback enabled, so it can still boot without SPIFFS.

## Default Motion Pins

The runtime now matches the wiring from the working `final4` sketch:

- `servo = GPIO 32`
- `motor IN1 = GPIO 25`
- `motor IN2 = GPIO 26`
- `motor ENA = GPIO 27`

On boot it prints the motion pins so you can verify the uploaded sketch is the expected one.
It also runs a short motion self-test by default so you can verify motor and servo before any audio tracking starts.

## Serial Output To Expect

You should now see:

- `RawPitch`
- `SnapPitch`
- `Pitch`

in the debug log, plus motion logs like:

- `MOTOR START`
- `MOTOR STOP`
- `SERVO UP`
- `SERVO HOLD`
- `SERVO DOWN`
- `FLIP END`
