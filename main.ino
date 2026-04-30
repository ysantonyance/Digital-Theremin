// DIGITAL THEREMIN — Arduino Uno
// Controls pitch and volume using two HC-SR04 ultrasonic distance sensors.
// Includes 4 sound modes, an LED pitch visualizer, and a Simon Says mini-game.
// -----------------------------------------------------------------------------

// --- Pin definitions ---
int trig1 = 8;         // Trigger pin for sensor 1 (controls pitch)
int echo1 = 7;         // Echo pin for sensor 1
int trig2 = 5;         // Trigger pin for sensor 2 (controls volume / octave shift)
int echo2 = 4;         // Echo pin for sensor 2
int buzzerPin = 6;     // Passive buzzer output (PWM-capable pin)
int modeButton = 2;    // Button to cycle through sound modes
int simonButton = 3;   // Button to start / stop the Simon Says game

// --- LED arrays ---
int vizLeds[]  = {9, 10, 11, 12, 13};    // 5 LEDs that visualize the current pitch frequency
int simonLeds[] = {A1, A2, A3, A4, A5}; // 5 LEDs used as Simon Says indicators (analog pins used as digital)

// --- Global state ---
int mode = 0;             // Currently active sound mode (0-3)
int totalModes = 4;       // Total number of sound modes available
bool lastModeBtn  = HIGH; // Previous state of mode button (for edge detection)
bool lastSimonBtn = HIGH; // Previous state of Simon button (for edge detection)
bool simonActive  = false;// Whether Simon Says game is currently running
long lastDist = 0;        // Last measured pitch distance -- used in Mode 3 (squeak) for delta detection

const int MAX_DIST = 40;  // Maximum usable sensing distance in cm (fixed constant)

// --- Simon Says state variables ---
int simonSequence[20];       // Stores the randomly generated sequence of LED indices (max 20 rounds)
int simonLength = 1;         // Current round length (grows by 1 each successful round)
int playerStep = 0;          // Which step in the sequence the player is currently on
bool showingSequence = false; // True while Arduino is playing back the LED sequence
bool playerTurn = false;     // True when it is the player's turn to mirror the sequence
int simonNotes[] = {262, 330, 392, 494, 523}; // Musical notes (Hz) mapped to each of the 5 Simon zones
int lastZone = -1;           // Last zone the player hovered over (prevents re-triggering)
unsigned long zoneTimer = 0; // Timestamp of when the player entered the current zone (for 800ms dwell)

// -----------------------------------------------------------------------------
// getDistance()
// Measures distance (cm) from an HC-SR04 sensor by averaging 5 readings.
// Averaging reduces jitter caused by sensor noise or environmental interference.
// A 30ms gap between sensor 1 and sensor 2 reads (in loop()) prevents
// acoustic crosstalk between the two sensors.
// -----------------------------------------------------------------------------
long getDistance(int trigPin, int echoPin) {
  long total = 0;
  int samples = 5;
  for (int i = 0; i < samples; i++) {
    // Send a 10-microsecond HIGH pulse to trigger an ultrasonic burst
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    // pulseIn() measures the time (us) the echo pin stays HIGH
    // Distance (cm) = time * 0.034 / 2
    long d = pulseIn(echoPin, HIGH);
    total += d * 0.034 / 2;
    delay(10); // Small pause between samples to let the ultrasonic burst dissipate
  }
  return total / samples; // Return the arithmetic mean
}

// -----------------------------------------------------------------------------
// squeakUp() / squeakDown()
// Mode 3 - "Squeaky Toy" effect.
// Plays a rapid frequency sweep upward or downward depending on hand movement direction.
// -----------------------------------------------------------------------------
void squeakUp() {
  for (int f = 400; f <= 2000; f += 80) {
    tone(buzzerPin, f); delay(15);
  }
  noTone(buzzerPin);
}

void squeakDown() {
  for (int f = 2000; f >= 400; f -= 80) {
    tone(buzzerPin, f); delay(15);
  }
  noTone(buzzerPin);
}

// -----------------------------------------------------------------------------
// updateVizLeds()
// Lights up 0-5 LEDs proportionally to the current frequency.
// Low frequency (200 Hz) -> 0 LEDs lit;  high frequency (2000 Hz) -> 5 LEDs lit.
// Gives the player a real-time visual "pitch meter".
// -----------------------------------------------------------------------------
void updateVizLeds(int freq) {
  int level = map(constrain(freq, 200, 2000), 200, 2000, 0, 5);
  for (int i = 0; i < 5; i++) {
    digitalWrite(vizLeds[i], i < level ? HIGH : LOW);
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
// Lights up one Simon LED and plays the corresponding note for 'dur' milliseconds.
// Used both when displaying the sequence AND confirming a correct player input.
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
// Initialises a new Simon Says game:
// - Resets length to 1 and player step to 0
// - Generates a full 20-step random sequence upfront
// - Sets showingSequence = true so the next loop() iteration plays it back
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
// After playback, flips to playerTurn = true and starts the 800ms dwell timer.
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
// Runs once at power-on. Configures all pin modes and starts serial output.
// INPUT_PULLUP means the button reads HIGH when not pressed, LOW when pressed.
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
  randomSeed(millis()); // Seed the RNG from elapsed time
}

// -----------------------------------------------------------------------------
// loop()
// Main execution loop - runs continuously after setup().
// Order of operations each iteration:
//   1. Check mode button -> cycle sound mode
//   2. Check Simon button -> toggle game on/off
//   3. Read both sensors (30ms gap between them)
//   4. If Simon is active -> handle game logic, then return
//   5. Otherwise -> apply the selected sound mode and update LEDs
// -----------------------------------------------------------------------------
void loop() {

  // --- 1. Mode button (edge detection: only fires on falling edge HIGH->LOW) ---
  bool currentModeBtn = digitalRead(modeButton);
  if (currentModeBtn == LOW && lastModeBtn == HIGH) {
    if (!simonActive) {       // Mode switching is disabled during Simon Says
      mode = (mode + 1) % totalModes;
      flashVizLeds();         // Visual confirmation of the mode change
      Serial.print("Mode: "); Serial.println(mode);
    }
    delay(400); // Debounce: ignore bouncing contacts for 400ms
  }
  lastModeBtn = currentModeBtn;

  // --- 2. Simon button ---
  bool currentSimonBtn = digitalRead(simonButton);
  if (currentSimonBtn == LOW && lastSimonBtn == HIGH) {
    simonActive = !simonActive; // Toggle Simon Says on/off
    if (simonActive) {
      startSimon();
    } else {
      // Clean up: turn off all Simon LEDs and buzzer
      for (int i = 0; i < 5; i++) digitalWrite(simonLeds[i], LOW);
      noTone(buzzerPin);
      Serial.println("Simon says: OFF");
    }
    delay(400);
  }
  lastSimonBtn = currentSimonBtn;

  // --- 3. Read sensors (30ms gap prevents acoustic crosstalk) ---
  long pitchDist  = getDistance(trig1, echo1); // Hand above sensor 1 -> pitch
  delay(30);
  long volumeDist = getDistance(trig2, echo2); // Hand above sensor 2 -> volume/octave

  // --- 4. Simon Says game logic ---
  if (simonActive) {
    if (showingSequence) {
      playSimonSequence(); // Plays the sequence and sets playerTurn = true
      return;
    }

    if (playerTurn && pitchDist > 0 && pitchDist < 200) {
      // Map the measured distance to one of 5 zones (0-4), matching the 5 Simon LEDs
      int zone = map(constrain(pitchDist, 2, MAX_DIST), 2, MAX_DIST, 0, 4);

      // Highlight the zone the player is currently hovering over
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

      // Confirm the zone selection after holding still for 800ms
      if (millis() - zoneTimer > 800) {
        if (zone == simonSequence[playerStep]) {
          // Correct -- confirm with light+sound, advance to next step
          simonLight(zone, 300);
          playerStep++;
          lastZone = -1;
          zoneTimer = millis();

          if (playerStep >= simonLength) {
            // Completed the full round - play win jingle, increase difficulty
            simonFlashAll(true);
            simonLength++;
            showingSequence = true;
            playerTurn = false;
            Serial.print("Correct! Round: "); Serial.println(simonLength);
          }
        } else {
          // Wrong zone - play fail jingle and end the game
          simonFlashAll(false);
          Serial.println("Wrong! Game over.");
          simonActive = false;
          for (int i = 0; i < 5; i++) digitalWrite(simonLeds[i], LOW);
        }
      }
    }
    return; // Skip theremin logic while Simon is running
  }

  // --- Guard: if sensor reading is out of valid range, silence everything ---
  if (pitchDist <= 0 || pitchDist >= 200) {
    noTone(buzzerPin);
    updateVizLeds(0);
    return;
  }

  // Map pitch distance to a frequency: close hand = high pitch, far hand = low pitch
  int freq = map(constrain(pitchDist, 2, MAX_DIST), 2, MAX_DIST, 200, 2000);

  updateVizLeds(freq); // Update the 5-LED pitch visualizer

  // --- 5. Sound modes ---

  if (mode == 0) {
    // MODE 0: Free pitch -- direct continuous frequency control from sensor 1
    tone(buzzerPin, freq);
    Serial.print("Mode 0 | Freq: "); Serial.println(freq);

  } else if (mode == 1) {
    // MODE 1: Octave shift -- sensor 2 shifts the base frequency up or down one octave
    if (volumeDist < 10)      freq = freq * 2; // Close hand -> octave up
    else if (volumeDist > 30) freq = freq / 2; // Far hand -> octave down
    freq = constrain(freq, 50, 4000);
    tone(buzzerPin, freq);
    Serial.print("Mode 1 | Freq: "); Serial.println(freq);

  } else if (mode == 2) {
    // MODE 2: C-major scale snap - quantises the free frequency to the nearest
    // note in the C major scale {C4, D4, E4, F4, G4, A4, B4, C5}
    int cMajor[] = {262, 294, 330, 349, 392, 440, 494, 523};
    int nearest = cMajor[0];
    for (int i = 1; i < 8; i++) {
      if (abs(freq - cMajor[i]) < abs(freq - nearest))
        nearest = cMajor[i];
    }
    tone(buzzerPin, nearest);
    Serial.print("Mode 2 | Note: "); Serial.println(nearest);

  } else if (mode == 3) {
    // MODE 3: Squeaky toy -- plays a rising or falling sweep based on hand movement direction.
    // Compares current distance to the previous reading (lastDist) to detect motion direction:
    //   diff < -3 means hand moved closer -> squeak upward
    //   diff > +3 means hand moved away   -> squeak downward
    //   small diff -> silence (hand is stationary)
    if (lastDist == 0) lastDist = pitchDist;
    long diff = pitchDist - lastDist;
    if (diff < -3) {
      squeakUp();
      Serial.println("Mode 3: SQUEAK UP!");
    } else if (diff > 3) {
      squeakDown();
      Serial.println("Mode 3: squeak down");
    } else {
      noTone(buzzerPin);
    }
    lastDist = pitchDist; // Store reading for next iteration's delta calculation
  }

  delay(100); // Loop rate ~10 Hz -- balances responsiveness with stable sensor readings
}
