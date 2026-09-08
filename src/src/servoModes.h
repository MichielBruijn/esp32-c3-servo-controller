
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

// Resolve the calibrated center position of a given servo channel, independent of which channel is
// currently selected. Used to initialize/re-center all outputs at once (e.g. when entering a mode).
int servoCenterForChannel(uint8_t ch)
{
  int mode = SERVO_MODE_PER_GROUP[servoTimerGroup(ch)];
  if (mode == STD || mode == NOR || mode == SHR)
    return SERVO_CENTER_STD[ch];
  else
    return SERVO_CENTER_SANWA[ch];
}

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

void servoModes()
{
  // Mode is stored per timer group now, not globally - refresh the legacy SERVO_MODE scalar to
  // whichever group the currently selected channel belongs to before using it below.
  SERVO_MODE = SERVO_MODE_PER_GROUP[servoTimerGroup(selectedServo)];

  if (SERVO_MODE <= STD)
    SERVO_MODE = STD; // Min. limit

  // Modes with normal pulse lengths ---------------
  if (SERVO_MODE == STD)
  {
    SERVO_Hz = 50;
    SERVO_MAX = SERVO_MAX_STD[selectedServo];
    SERVO_CENTER = SERVO_CENTER_STD[selectedServo];
    SERVO_MIN = SERVO_MIN_STD[selectedServo];

    servoMode = "Std.";
  }

  if (SERVO_MODE == NOR)
  {
    SERVO_Hz = 100;
    SERVO_MAX = SERVO_MAX_STD[selectedServo];
    SERVO_CENTER = SERVO_CENTER_STD[selectedServo];
    SERVO_MIN = SERVO_MIN_STD[selectedServo];

    servoMode = "NOR";
  }

  if (SERVO_MODE == SHR)
  {
    SERVO_Hz = 333;
    SERVO_MAX = SERVO_MAX_STD[selectedServo];
    SERVO_CENTER = SERVO_CENTER_STD[selectedServo];
    SERVO_MIN = SERVO_MIN_STD[selectedServo];

    servoMode = "SHR";
  }

  // Modes with Sanwa pulse lengths ---------------
  if (SERVO_MODE == SSR)
  {
    SERVO_Hz = 400;
    SERVO_MAX = SERVO_MAX_SANWA[selectedServo];
    SERVO_CENTER = SERVO_CENTER_SANWA[selectedServo];
    SERVO_MIN = SERVO_MIN_SANWA[selectedServo];

    servoMode = "SSR";
  }

  if (SERVO_MODE == SUR)
  {
    SERVO_Hz = 800;
    SERVO_MAX = SERVO_MAX_SANWA[selectedServo];
    SERVO_CENTER = SERVO_CENTER_SANWA[selectedServo];
    SERVO_MIN = SERVO_MIN_SANWA[selectedServo];

    servoMode = "SUR";
  }

  if (SERVO_MODE == SXR)
  {
    SERVO_Hz = 1600;
    SERVO_MAX = SERVO_MAX_SANWA[selectedServo];
    SERVO_CENTER = SERVO_CENTER_SANWA[selectedServo];
    SERVO_MIN = SERVO_MIN_SANWA[selectedServo];

    servoMode = "SXR";
  }

  if (SERVO_MODE >= SXR)
    SERVO_MODE = SXR; // Max. limit

  // Persist any clamping back to the group this channel belongs to
  SERVO_MODE_PER_GROUP[servoTimerGroup(selectedServo)] = SERVO_MODE;

  // Auto calculate a useful servo step size
  SERVO_STEPS = (SERVO_MAX - SERVO_MIN) / 100;
}
