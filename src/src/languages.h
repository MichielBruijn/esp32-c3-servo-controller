#include <Arduino.h>
// Language ressources strings for OLED menus

int noOfLanguages = 2; // 0 = English, 1 = Deutsch, 2 = Francais - LANGUAGE is fixed at 0 (English), kept as a bounds check only

// Settings
String settingsString[]{"Settings", "Einstellung", "Paramètres"};
String onString[]{"On", "Ein", "activé"};
String offString[]{"Off", "Aus", "désactivé"};
String noString[] {"No", "Nein", "Non"};
String yesString[] {"Yes", "Ja", "Oui"};
String factoryResetString[] {"! Factory Reset !", "! Werksreset !", "! Réinitialiser !"};
String servoStepsString[] {"Servo Steps µs", "Servo Schritte µs", "étape servo µs"};
// Short forms - the Settings screen's header already names the group ("Servo"/"Encoder"/
// "Speed"/etc., see settingsGroupName() in src.ino), so these no longer repeat it.
String channelString[] {"Channel", "Kanal", "Canal"};
String servoMaxString[] {"Max. µs", "Max. µs", "Max. µs"};
String servoMinString[] {"Min. µs", "Min. µs", "Min. µs"};
String servoCenterString[] {"Center µs", "Mitte µs", "Centre µs"};
String servoAngleString[] {"Angle", "Winkel", "Angle"};
String servoHzString[] {"Mode", "Mode", "Mode"};
String inversedString[] {"Inversed", "Invertiert", "Inversé"};
String standardString[] {"Standard", "Standart", "Défaut"};
String encoderDirectionString[] {"Direction", "Richtung", "Direction"};
String speedCurveString[] {"Curve", "Kurve", "Courbe"};

// Impuls lesen
String impulseString[] {"Impulse", "Impuls", "Impulsion"};
String impulseSignalString[] {"No signal", "Kein Signal", "Pas de signal"};

// Automatic mode
String delayString[] {"Delay", "Verz.", "Ret."};

// Selection screens
String servotesterString[] {"Servotester", "Servotester", "Testeur de servos"};
String readIbusString[] {"Read IBUS", "IBUS lesen", "Lire IBUS"};
String readSbusString[] {"Read SBUS", "SBUS lesen", "Lire SBUS"};
String readCh5String[] {"read CH5", "lesen CH5", "lire CH5"};
String readCh1Ch5String[] {"read CH1 - 5", "lesen CH1 - 5", "lire CH1 - 5"};
String PwmImpulseString[] {"PWM Impulse", "PWM Impuls", "PWM Impulsion"};
String automaticModeString[] {"Automatic Mode", "Automatik Modus","Mode automat."};
String oscillateServoString[] {"(Oscillate Servo)", "(Servo pendeln)", "(Osciller Servo)"};
String readOscilloscopeString[] {"Oscilloscope", "Oszilloskop", "Oscilloscope"};
String readOscilloscopeString2[] {"0-3.3V, GPIO 39", "0-3.3V, GPIO 39", "0-3.3V, GPIO 39"};
String signalGeneratorString[] {"Signal Generator", "Signal Generator", "Générat. de signal"};
String signalGeneratorString2[] {"0-3.3V, GPIO 25", "0-3.3V, GPIO 25", "0-3.3V, GPIO 25"};

// Setup
String passwordString[] {"Password:", "Passwort:", "Mot de passe:"};
String ipAddressString[] {"IP address:", "IP Adresse:", "IP Adresse:"};
String WiFiOnString[] {"WiFi On", "WiFi Ein", "WiFi activé"};
String WiFiOffString[] {"WiFi Off", "WiFi Aus", "WiFi desactivé"};
String apIpAddressString[] {"AP-IP-Adress: ", "AP-IP-Adresse: ", "AP-IP-Adresse: "};
String connectingAccessPointString[] {"AP (Accesspoint) connecting…", "AP (Zugangspunkt) einstellen…", "connecter le point d'accès…"};

// Instructions
String operationString[] {"******** Operation: ********", "******** Bedienung: ********", "****** Mode d'emploi : ******"};
String shortPressString[] {"Short Press = Select", "Kurz Drücken = Auswahl", "Appui brièvem. = Sélection"};
String longPressString[] {"Long Press = Back", "Lang Drücken = Zurück", "Appui long   = Retour"};
String doubleclickString[] {"Doubleclick = CH Change", "Doppelklick = CH Wechsel", "Double-clique = CH Changem."};
String RotateKnobString[] {"Rotate = Scroll / Adjust", "Drehen = Blättern / Einstell.", "Tourner     = Défiler"};
String bootButtonString[] {"BOOT btn = CH Change", "BOOT Taste = CH Wechsel", "Bouton BOOT = CH Changem."};

// EEPROM
String eepromReadString[] {"EEPROM read.", "EEPROM gelesen.", "EEPROM lire."};
String eepromWrittenString[] {"EEPROM written.", "EEPROM geschrieben.", "EEPROM écrit."};
String eepromInitString[] {"EEPROM initialized.", "EEPROM initialisiert.", "EEPROM initialisé."};

// Battery
String eepromVoltageString[] {"Battery voltage: ", "Akkuspannung: ", "Voltage de batterie: "};
String noBatteryString[] {"No Battery connected", "Kein Akku angeschlossen", "Pas de batterie connectée"};
