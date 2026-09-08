
#include <Arduino.h>

//
// =======================================================================================================
// SERVO MODES
// =======================================================================================================
//

// Servo frequencies and corresponding microseconds ranges -------
  // Std = 50Hz   1000 - 1500 - 2000µs = gemäss ursprünglichem Standard
  // NOR = 100Hz  1000 - 1500 - 2000µs = normal = für die meisten analogen Servos
  // SHR = 333Hz  1000 - 1500 - 2000µs = Sanwa High Response = für alle Digitalservos
  // SSR = 400Hz   130 -  300 - 470µs  = Sanwa Super Response = nur für Sanwa Servos der SRG-Linie
  // SUR = 800Hz   130 -  300 - 470µs  = Sanwa Ultra Response
  // SXR = 1600Hz  130 -  300 - 470µs  = Sanwa Xtreme Response

// Default µs values see eepromInit()

// Frequency for a given mode - shared by servoModes() (display/duty) and setupMcpwm() (hardware timers)
int servoHzForMode(int mode)
{
  switch (mode)
  {
  case NOR:
    return 100;
  case SHR:
    return 333;
  case SSR:
    return 400;
  case SUR:
    return 800;
  case SXR:
    return 1600;
  default:
    return 50; // STD
  }
}

// Display name for a given mode
String servoModeName(int mode)
{
  switch (mode)
  {
  case NOR:
    return "NOR";
  case SHR:
    return "SHR";
  case SSR:
    return "SSR";
  case SUR:
    return "SUR";
  case SXR:
    return "SXR";
  default:
    return "Std.";
  }
}

// Resolve the calibrated center position of a given servo channel, independent of which channel is
// currently selected. Used to initialize/re-center all outputs at once (e.g. when entering a mode).
int servoCenterForChannel(uint8_t ch)
{
  int mode = SERVO_MODE_PER_GROUP[servoTimerGroup(ch)];
  return SERVO_CENTER_BY_MODE[ch][mode];
}

void servoModes()
{
  // Mode is stored per timer group now, not globally - refresh the legacy SERVO_MODE scalar to
  // whichever group the currently selected channel belongs to before using it below.
  SERVO_MODE = SERVO_MODE_PER_GROUP[servoTimerGroup(selectedServo)];
  SERVO_MODE = constrain(SERVO_MODE, (int)STD, (int)SXR);

  SERVO_Hz = servoHzForMode(SERVO_MODE);
  servoMode = servoModeName(SERVO_MODE);

  // Min/Max/Center are stored per channel AND per mode now - each of the 6 modes remembers its own
  // calibration for a given channel, instead of the 2 previously shared families (Std/NOR/SHR and
  // SSR/SUR/SXR).
  SERVO_MAX = SERVO_MAX_BY_MODE[selectedServo][SERVO_MODE];
  SERVO_MIN = SERVO_MIN_BY_MODE[selectedServo][SERVO_MODE];
  SERVO_CENTER = SERVO_CENTER_BY_MODE[selectedServo][SERVO_MODE];

  // Persist any clamping back to the group this channel belongs to
  SERVO_MODE_PER_GROUP[servoTimerGroup(selectedServo)] = SERVO_MODE;

  // Auto calculate a useful servo step size
  SERVO_STEPS = (SERVO_MAX - SERVO_MIN) / 100;
}
