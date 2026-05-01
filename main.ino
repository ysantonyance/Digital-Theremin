// DIGITAL THEREMIN — Arduino Uno
// Controls pitch and volume using two HC-SR04 ultrasonic distance sensors.
// Includes 4 sound modes, an LED pitch visualizer, and a Simon Says mini-game.
// Sensor 2 controls vibrato depth in modes 0, 1, 2 and trill speed in mode 3.
// -----------------------------------------------------------------------------

// --- Pin definitions ---
int trig1 = 8;         // Trigger pin for sensor 1 (controls pitch)
int echo1 = 7;         // Echo pin for sensor 1
int trig2 = 5;         // Trigger pin for sensor 2 (controls vibrato / trill speed)
int echo2 = 4;         // Echo pin for sensor 2
int buzzerPin = 6;     // Passive buzzer output (PWM-capable pin)
int modeButton = 2;    // Button to cycle through sound modes
int simonButton = 3;   // Button to start / stop the Simon Says game

// --- LED arrays ---
int vizLeds[]  = {9, 10, 11, 12, 13};    // 5 LEDs that visualize the current pitch
int simonLeds[] = {A1, A2, A3, A4, A5}; // 5 LEDs used as Simon Says indicators

// --- Global state ---
int mode = 0;
int totalModes = 4;
bool lastModeBtn  = HIGH;
bool lastSimonBtn = HIGH;
bool simonActive  = false;

const int MAX_DIST = 35;
const int MIN_DIST = 5;

// --- Smoothing ---
// Instead of using raw sensor readings directly, we maintain smoothed values
// that gradually move toward the real reading each loop iteration.
// This prevents sudden jumps from sensor noise causing unwanted note changes.
float smoothDist = 0; // exponentially smoothed pitch distance (sensor 1)
float smoothVol  = 0; // exponentially smoothed vibrato/trill distance (sensor 2)

// --- Note-per-LED mapping ---
// Each of the 5 LEDs corresponds to one fixed note.
// When a LED lights up, only its note plays — no more jitter between frequencies.
// Scale: C4, E4, G4, B4, C5 (a C major pentatonic-ish spread across the range)
int ledNotes[] = {262, 330, 392, 494, 523};

// --- Simon Says state variables ---
int simonSequence[20];
int simonLength = 1;
int playerStep = 0;
bool showingSequence = false;
bool playerTurn = false;
int simonNotes[] = {262, 330, 392, 494, 523}; // Notes mapped to each Simon zone
int lastZone = -1;
unsigned long zoneTimer = 0;

// -----------------------------------------------------------------------------
// getZone()
// Converts a smoothed distance into one of 5 equal zones (0-4).
// Zone 4 = closest to sensor, zone 0 = furthest away.
// Each zone spans roughly 4 cm — all zones are equal width,
// so the furthest note is just as easy to reach as any other.
// Adjust the boundary values if a particular zone feels too narrow or wide.
// -----------------------------------------------------------------------------
int getZone(float dist) {
  if (dist < 9)        return 4; // 5–9 cm
  else if (dist < 13)  return 3; // 9–13 cm
  else if (dist < 17)  return 2; // 13–17 cm
  else if (dist < 21)  return 1; // 17–21 cm
  else                 return 0; // 21–35 cm
}

// -----------------------------------------------------------------------------
// getDistance()
// Measures distance (cm) from an HC-SR04 sensor by averaging 3 readings.
// pulseIn timeout of 23200us prevents the function hanging when no echo returns.
// -----------------------------------------------------------------------------
long getDistance(int trigPin, int echoPin) {
  long total = 0;
  int samples = 3;
  for (int i = 0; i < samples; i++) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    long d = pulseIn(echoPin, HIGH, 23200);
    total += d * 0.034 / 2;
  }
  return total / samples;
}

// -----------------------------------------------------------------------------
// playWithVibrato()
// Plays a note with vibrato effect controlled by sensor 2 (smoothVol).
// Vibrato is created by rapidly alternating between freq+depth and freq-depth.
//
// smoothVol = 0 (no hand)       -> pure tone, no vibrato
// smoothVol close to MIN_DIST   -> strong vibrato (~40 Hz swing)
// smoothVol close to MAX_DIST   -> subtle vibrato (~5 Hz swing)
// -----------------------------------------------------------------------------
void playWithVibrato(int freq, float vol) {
  if (vol <= 0) {
    // No hand over sensor 2 — play a clean unmodified tone
    tone(buzzerPin, freq);
    return;
  }
  // Map distance to vibrato depth: close hand = strong, far hand = subtle
  int depth = map(constrain((int)vol, MIN_DIST, MAX_DIST), MIN_DIST, MAX_DIST, 40, 5);
  int speed = 30; // ms per half-cycle — lower = faster wobble

  tone(buzzerPin, freq + depth); delay(speed);
  tone(buzzerPin, freq - depth); delay(speed);
}

// -----------------------------------------------------------------------------
// playTrill()
// Mode 3 - "Trill" effect.
// Rapidly alternates between the current note and the next note up in the scale.
// This mimics a classical trill ornament used in flute and piano music.
//
// Sensor 1 selects the base note via getZone() as usual.
// Sensor 2 controls trill speed:
//   no hand      -> slow trill (100ms per note)
//   close hand   -> very fast trill (20ms per note)
//   far hand     -> medium trill (70ms per note)
//
// The upper note is always the next entry in ledNotes[] — if the hand is at
// the highest note (index 4), the trill wraps down to index 3 instead.
// -----------------------------------------------------------------------------
void playTrill(int baseFreq, int baseLevel, float vol) {
  // Pick the neighbour note to trill with
  int neighbourLevel = (baseLevel < 4) ? baseLevel + 1 : baseLevel - 1;
  int neighbourFreq  = ledNotes[neighbourLevel];

  // Map sensor 2 distance to trill speed: close = fast, far = slow
  int speed = 100; // default speed if no hand over sensor 2
  if (vol > 0) {
    speed = map(constrain((int)vol, MIN_DIST, MAX_DIST), MIN_DIST, MAX_DIST, 20, 100);
  }

  // Play one cycle of the trill (base -> neighbour)
  // The loop() will call this repeatedly creating a continuous trill effect
  tone(buzzerPin, baseFreq);     delay(speed);
  tone(buzzerPin, neighbourFreq); delay(speed);
}

// -----------------------------------------------------------------------------
// updateVizLeds()
// Lights up LEDs 0 through 'level' based on the current note index (0-4).
// All LEDs up to and including the active note are lit, the rest are off.
// This gives a "bar meter" effect that matches the note being played.
// -----------------------------------------------------------------------------
void updateVizLeds(int level) {
  for (int i = 0; i < 5; i++) {
    digitalWrite(vizLeds[i], i <= level ? HIGH : LOW);
  }
}

// flashVizLeds() -- briefly flashes all 5 pitch LEDs to confirm a mode change
void flashVizLeds() {
  for (int i = 0; i < 5; i++) digitalWrite(vizLeds[i], HIGH);
  delay(200);
  for (int i = 0; i < 5; i++) digitalWrite(vizLeds[i], LOW);
  delay(200);
}

// -----------------------------------------------------------------------------
// simonLight()
// Lights up one Simon LED and plays its note for 'dur' milliseconds.
// -----------------------------------------------------------------------------
void simonLight(int index, int dur) {
  digitalWrite(simonLeds[index], HIGH);
  tone(buzzerPin, simonNotes[index], dur);
  delay(dur);
  digitalWrite(simonLeds[index], LOW);
  noTone(buzzerPin);
  delay(100); // Brief pause so consecutive lights are visually distinct
}

// -----------------------------------------------------------------------------
// simonFlashAll()
// Flashes all 5 Simon LEDs 3 times with a win (1000 Hz) or lose (150 Hz) tone.
// -----------------------------------------------------------------------------
void simonFlashAll(bool win) {
  for (int r = 0; r < 3; r++) {
    for (int i = 0; i < 5; i++) digitalWrite(simonLeds[i], HIGH);
    tone(buzzerPin, win ? 1000 : 150, 200);
    delay(250);
    for (int i = 0; i < 5; i++) digitalWrite(simonLeds[i], LOW);
    noTone(buzzerPin);
    delay(150);
  }
}

// -----------------------------------------------------------------------------
// startSimon()
// Initialises a new Simon Says game.
// -----------------------------------------------------------------------------
void startSimon() {
  simonLength = 1;
  playerStep = 0;
  playerTurn = false;
  showingSequence = true;
  lastZone = -1;
  for (int i = 0; i < 20; i++) simonSequence[i] = random(0, 5);
  Serial.println("Simon says: GAME START!");
}

// -----------------------------------------------------------------------------
// playSimonSequence()
// Plays back simonLength steps of the stored sequence using simonLight().
// After playback, flips to playerTurn = true.
// -----------------------------------------------------------------------------
void playSimonSequence() {
  delay(600); // Pause before sequence starts so the player is ready
  for (int i = 0; i < simonLength; i++) {
    simonLight(simonSequence[i], 400);
    delay(100);
  }
  showingSequence = false;
  playerTurn = true;
  playerStep = 0;
  lastZone = -1;
  zoneTimer = millis();
  Serial.println("Your turn!");
}

// -----------------------------------------------------------------------------
// setup()
// -----------------------------------------------------------------------------
void setup() {
  pinMode(trig1, OUTPUT); pinMode(echo1, INPUT);
  pinMode(trig2, OUTPUT); pinMode(echo2, INPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(modeButton, INPUT_PULLUP);
  pinMode(simonButton, INPUT_PULLUP);

  for (int i = 0; i < 5; i++) {
    pinMode(vizLeds[i], OUTPUT);
    pinMode(simonLeds[i], OUTPUT);
  }

  Serial.begin(9600);
  randomSeed(millis());
}

// -----------------------------------------------------------------------------
// loop()
// Main execution loop. Order of operations each iteration:
//   1. Check mode button -> cycle sound mode
//   2. Check Simon button -> toggle game on/off
//   3. Read both sensors (15ms gap between them)
//   4. Apply exponential smoothing to both sensor readings
//   5. If Simon is active -> handle game logic, then return
//   6. Otherwise -> determine active LED/note via getZone() and apply sound mode
// -----------------------------------------------------------------------------
void loop() {

  // --- 1. Mode button (edge detection: fires only on falling edge HIGH->LOW) ---
  bool currentModeBtn = digitalRead(modeButton);
  if (currentModeBtn == LOW && lastModeBtn == HIGH) {
    if (!simonActive) {
      mode = (mode + 1) % totalModes;
      flashVizLeds();
      Serial.print("Mode: "); Serial.println(mode);
    }
    delay(400); // Debounce
  }
  lastModeBtn = currentModeBtn;

  // --- 2. Simon button ---
  bool currentSimonBtn = digitalRead(simonButton);
  if (currentSimonBtn == LOW && lastSimonBtn == HIGH) {
    simonActive = !simonActive;
    if (simonActive) {
      startSimon();
    } else {
      for (int i = 0; i < 5; i++) digitalWrite(simonLeds[i], LOW);
      noTone(buzzerPin);
      Serial.println("Simon says: OFF");
    }
    delay(400);
  }
  lastSimonBtn = currentSimonBtn;

  // --- 3. Read sensors (15ms gap prevents acoustic crosstalk) ---
  long pitchDist  = getDistance(trig1, echo1);
  delay(15);
  long volumeDist = getDistance(trig2, echo2);

  // --- 4. Exponential smoothing of both sensors ---
  // Each smoothed value moves 25% toward the new reading each iteration.
  // Small jitter gets averaged out; large hand movements still register quickly.
  if (pitchDist > 0 && pitchDist <= MAX_DIST) {
    if (smoothDist == 0) smoothDist = pitchDist;
    smoothDist = smoothDist * 0.75 + pitchDist * 0.25;
  } else {
    smoothDist = 0;
  }

  if (volumeDist > 0 && volumeDist <= MAX_DIST) {
    if (smoothVol == 0) smoothVol = volumeDist;
    smoothVol = smoothVol * 0.75 + volumeDist * 0.25;
  } else {
    smoothVol = 0; // no hand over sensor 2 — vibrato/trill uses default speed
  }

  // --- 5. Simon Says game logic ---
  if (simonActive) {
    if (showingSequence) {
      playSimonSequence();
      return;
    }

    if (playerTurn && smoothDist > 0 && smoothDist <= MAX_DIST) {
      // zone is inverted once here so that the same physical hand position
      // means the same zone in both Simon and the theremin.
      // getZone() returns 4=close, 0=far — we flip it so 0=close, 4=far,
      // which matches the order simonLeds[] are wired on the board.
      int zone = 4 - getZone(smoothDist);

      // Light up only the LED matching the current zone
      for (int i = 0; i < 5; i++) {
        digitalWrite(simonLeds[i], i == zone ? HIGH : LOW);
      }

      Serial.print("Zone: "); Serial.print(zone);
      Serial.print(" | Need: "); Serial.println(simonSequence[playerStep]);

      // Reset the dwell timer every time the player moves to a new zone
      if (zone != lastZone) {
        lastZone = zone;
        zoneTimer = millis();
      }

      // Confirm zone selection after holding still for 1200ms
      if (millis() - zoneTimer > 1200) {
        if (zone == simonSequence[playerStep]) {
          // Correct — confirm with light+sound, advance to next step
          simonLight(zone, 300);
          playerStep++;
          lastZone = -1;
          zoneTimer = millis();

          if (playerStep >= simonLength) {
            simonFlashAll(true);
            simonLength++;
            showingSequence = true;
            playerTurn = false;
            Serial.print("Correct! Round: "); Serial.println(simonLength);
          }
        } else {
          // Wrong zone — play fail jingle and end the game
          simonFlashAll(false);
          Serial.println("Wrong! Game over.");
          simonActive = false;
          for (int i = 0; i < 5; i++) digitalWrite(simonLeds[i], LOW);
        }
      }
    }
    return; // Skip theremin logic while Simon is running
  }

  // --- Guard: hand out of range — silence and clear LEDs ---
  if (smoothDist <= 0) {
    noTone(buzzerPin);
    for (int i = 0; i < 5; i++) digitalWrite(vizLeds[i], LOW);
    return;
  }

  // --- 6. Determine active note from smoothed distance ---
  // getZone() divides the range into 5 equal-width zones so every note
  // is equally reachable — including the furthest one.
  int level = getZone(smoothDist);
  int freq  = ledNotes[level]; // fixed note for this zone

  updateVizLeds(level); // light up LEDs 0..level as a bar meter

  // --- 7. Sound modes ---

  if (mode == 0) {
    // MODE 0: Note-per-LED + vibrato
    // Sensor 1 selects the note, sensor 2 adds vibrato depth.
    // No hand over sensor 2 = clean tone; close hand = strong wobble.
    playWithVibrato(freq, smoothVol);
    Serial.print("Mode 0 | Level: "); Serial.print(level);
    Serial.print(" | Note: "); Serial.print(freq);
    Serial.print(" | Vibrato vol: "); Serial.println(smoothVol);

  } else if (mode == 1) {
    // MODE 1: Octave shift + vibrato
    // Sensor 2 shifts the note one octave up (close) or down (far),
    // and simultaneously adds vibrato based on exact distance.
    if (smoothVol > 0 && smoothVol < 8)        freq = freq * 2; // close -> octave up
    else if (smoothVol <= 0 || smoothVol > 22) freq = freq / 2; // far or absent -> octave down
    freq = constrain(freq, 50, 4000);
    playWithVibrato(freq, smoothVol);
    Serial.print("Mode 1 | Note: "); Serial.println(freq);

  } else if (mode == 2) {
    // MODE 2: C-major scale snap + vibrato
    // Snaps to nearest note in C major, then applies vibrato from sensor 2.
    int cMajor[] = {262, 294, 330, 349, 392, 440, 494, 523};
    int nearest = cMajor[0];
    for (int i = 1; i < 8; i++) {
      if (abs(freq - cMajor[i]) < abs(freq - nearest))
        nearest = cMajor[i];
    }
    playWithVibrato(nearest, smoothVol);
    Serial.print("Mode 2 | Note: "); Serial.println(nearest);

  } else if (mode == 3) {
    // MODE 3: Trill -- rapidly alternates between the current note and its
    // neighbour in ledNotes[]. Sensor 2 controls the trill speed:
    //   no hand    -> slow trill (100ms per note)
    //   close hand -> very fast trill (20ms per note)
    //   far hand   -> medium trill (70ms per note)
    playTrill(freq, level, smoothVol);
    Serial.print("Mode 3 | Trill | Note: "); Serial.print(freq);
    Serial.print(" | Speed vol: "); Serial.println(smoothVol);
  }

  delay(30);
}
