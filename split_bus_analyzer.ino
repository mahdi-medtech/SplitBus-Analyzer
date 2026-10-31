#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// =====================================================
// SPLITBUS ANALYZER v2.0
// ESP32 + SH1106 OLED + Push Button
// Supports R8, R16, W8, W16
// =====================================================

// OLED configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SH1106G display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// ESP32 pins
const int SDA_PIN = 21;
const int SCL_PIN = 22;
const int BUTTON_PIN = 18;

// Timing model
const float BUS_CLOCK_HZ = 10000000.0;  // 10 MHz
const unsigned long TURNAROUND_CYCLES = 1;

// Input limits
const int MAX_OPS = 100;

// Operation representation
struct Operation {
  char direction;  // R or W
  int bits;        // 8 or 16
};

Operation operations[MAX_OPS];
int operationCount = 0;

// Operation counts
int readCount = 0;
int writeCount = 0;

// Data volume
unsigned long readBits = 0;
unsigned long writeBits = 0;

// Shared bus results
unsigned long sharedCycles = 0;
unsigned long sharedReadCycles = 0;
unsigned long sharedWriteCycles = 0;
unsigned long directionChanges = 0;

// Split bus results
unsigned long splitCycles = 0;
unsigned long splitReadCycles = 0;
unsigned long splitWriteCycles = 0;

// Latency
float sharedLatencyUs = 0;
float splitLatencyUs = 0;
float improvementPercent = 0;

// OLED screen state
int screenState = 0;

// Button debounce
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;

unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_MS = 40;

// =====================================================
// OLED MESSAGE
// =====================================================

void showMessage(const char *line1, const char *line2) {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 10);
  display.println(line1);

  display.setCursor(0, 28);
  display.println(line2);

  display.display();
}

// =====================================================
// INPUT PARSER
// Accepts R8, R16, W8, W16
// Example: R8 R16 W8 W16
// =====================================================

bool parseOperations(String input) {
  operationCount = 0;

  readCount = 0;
  writeCount = 0;

  readBits = 0;
  writeBits = 0;

  input.trim();
  input.toUpperCase();
  input.replace(',', ' ');

  int start = 0;

  while (start < input.length()) {
    // Skip whitespace
    while (
      start < input.length() &&
      (input.charAt(start) == ' ' ||
       input.charAt(start) == '\t')
    ) {
      start++;
    }

    if (start >= input.length()) {
      break;
    }

    int end = start;

    while (
      end < input.length() &&
      input.charAt(end) != ' ' &&
      input.charAt(end) != '\t'
    ) {
      end++;
    }

    String token = input.substring(start, end);

    if (
      token != "R8" &&
      token != "R16" &&
      token != "W8" &&
      token != "W16"
    ) {
      Serial.print("Invalid operation: ");
      Serial.println(token);

      Serial.println(
        "Use R8, R16, W8, or W16."
      );

      return false;
    }

    if (operationCount >= MAX_OPS) {
      Serial.println("ERROR: Maximum 100 operations.");
      return false;
    }

    Operation op;

    op.direction = token.charAt(0);
    op.bits = token.substring(1).toInt();

    operations[operationCount++] = op;

    if (op.direction == 'R') {
      readCount++;
      readBits += op.bits;
    } else {
      writeCount++;
      writeBits += op.bits;
    }

    start = end;
  }

  if (operationCount == 0) {
    Serial.println("ERROR: No operations entered.");
    return false;
  }

  return true;
}

// =====================================================
// SHARED 16-BIT BIDIRECTIONAL BUS
//
// Assumptions:
// - Each R8, R16, W8, or W16 takes one cycle.
// - Every direction change adds TURNAROUND_CYCLES.
// - Memory alignment and wait states are ignored.
// =====================================================

void calculateSharedBus() {
  sharedReadCycles = readCount;
  sharedWriteCycles = writeCount;
  directionChanges = 0;

  for (int i = 1; i < operationCount; i++) {
    if (
      operations[i].direction !=
      operations[i - 1].direction
    ) {
      directionChanges++;
    }
  }

  sharedCycles =
    sharedReadCycles +
    sharedWriteCycles +
    directionChanges * TURNAROUND_CYCLES;

  sharedLatencyUs =
    ((float)sharedCycles / BUS_CLOCK_HZ)
    * 1000000.0;
}

// =====================================================
// SPLIT 8-BIT READ/WRITE BUSES
//
// Each bus transfers 8 bits per cycle.
// A 16-bit operation therefore takes two cycles.
// Read and write workloads can overlap ideally.
// =====================================================

void calculateSplitBus() {
  splitReadCycles = 0;
  splitWriteCycles = 0;

  for (int i = 0; i < operationCount; i++) {
    unsigned long cycles =
      operations[i].bits / 8;

    if (operations[i].direction == 'R') {
      splitReadCycles += cycles;
    } else {
      splitWriteCycles += cycles;
    }
  }

  // Ideal independent scheduling.
  // This does not model dependencies or ordering constraints.

  splitCycles = max(
    splitReadCycles,
    splitWriteCycles
  );

  splitLatencyUs =
    ((float)splitCycles / BUS_CLOCK_HZ)
    * 1000000.0;
}

// =====================================================
// PERFORMANCE COMPARISON
// Positive: split bus is faster.
// Negative: split bus is slower.
// =====================================================

void calculateComparison() {
  if (sharedCycles > 0) {
    improvementPercent =
      (
        (float)sharedCycles -
        (float)splitCycles
      ) / (float)sharedCycles * 100.0;
  } else {
    improvementPercent = 0;
  }
}

// =====================================================
// SCREEN 1: SHARED BUS
// =====================================================

void displaySharedBus() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("SHARED BUS 16-BIT");

  display.drawLine(0, 11, 127, 11, SH110X_WHITE);

  display.setCursor(0, 15);
  display.print("Cycles: ");
  display.println(sharedCycles);

  display.setCursor(0, 27);
  display.print("Latency: ");
  display.print(sharedLatencyUs, 2);
  display.println(" us");

  display.setCursor(0, 39);
  display.print("Read: ");
  display.print(sharedReadCycles);

  display.print(" Write: ");
  display.println(sharedWriteCycles);

  display.setCursor(0, 51);
  display.print("Turnarounds: ");
  display.println(directionChanges);

  display.display();
}

// =====================================================
// SCREEN 2: SPLIT BUS
// =====================================================

void displaySplitBus() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("SPLIT BUS 8+8");

  display.drawLine(0, 11, 127, 11, SH110X_WHITE);

  display.setCursor(0, 15);
  display.print("Cycles: ");
  display.println(splitCycles);

  display.setCursor(0, 27);
  display.print("Latency: ");
  display.print(splitLatencyUs, 2);
  display.println(" us");

  display.setCursor(0, 39);
  display.print("Read: ");
  display.println(splitReadCycles);

  display.setCursor(0, 51);
  display.print("Write: ");
  display.println(splitWriteCycles);

  display.display();
}

// =====================================================
// SCREEN 3: COMPARISON
// =====================================================

void displayComparison() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("BUS COMPARISON");

  display.drawLine(0, 11, 127, 11, SH110X_WHITE);

  display.setCursor(0, 15);
  display.print("Shared: ");
  display.print(sharedCycles);
  display.println(" cyc");

  display.setCursor(0, 27);
  display.print("Split: ");
  display.print(splitCycles);
  display.println(" cyc");

  display.setCursor(0, 39);

  if (improvementPercent > 0.001 ||
    improvementPercent < -0.001) {
    display.print("Gain: ");
    display.print(improvementPercent, 1);
    display.println("%");
  }
  else {
    display.println("Equal cycle count");
  }

  display.setCursor(0, 51);
  display.println("Press button: NEXT");

  display.display();
}

// =====================================================
// DISPLAY STATE
// =====================================================

void updateDisplay() {
  switch (screenState) {
    case 0:
      displaySharedBus();
      break;

    case 1:
      displaySplitBus();
      break;

    case 2:
      displayComparison();
      break;
  }
}

// =====================================================
// SERIAL RESULTS
// =====================================================

void runAnalysis() {
  calculateSharedBus();
  calculateSplitBus();
  calculateComparison();

  Serial.println();
  Serial.println("========== RESULTS ==========");

  Serial.print("Operations: ");

  for (int i = 0; i < operationCount; i++) {
    Serial.print(operations[i].direction);
    Serial.print(operations[i].bits);
    Serial.print(' ');
  }

  Serial.println();

  Serial.println("--- Shared 16-bit bus ---");

  Serial.print("Read: ");
  Serial.println(sharedReadCycles);

  Serial.print("Write: ");
  Serial.println(sharedWriteCycles);

  Serial.print("Read data (bits): ");
  Serial.println(readBits);

  Serial.print("Write data (bits): ");
  Serial.println(writeBits);

  Serial.print("Direction changes: ");
  Serial.println(directionChanges);

  Serial.print("Total cycles: ");
  Serial.println(sharedCycles);

  Serial.print("Latency (us): ");
  Serial.println(sharedLatencyUs, 4);

  Serial.println("--- Split 8+8-bit buses ---");

  Serial.print("Read: ");
  Serial.println(splitReadCycles);

  Serial.print("Write: ");
  Serial.println(splitWriteCycles);

  Serial.print("Total cycles: ");
  Serial.println(splitCycles);

  Serial.print("Latency (us): ");
  Serial.println(splitLatencyUs, 4);

  Serial.println("--- Comparison ---");

  Serial.print("Split improvement (%): ");
  Serial.println(improvementPercent, 2);

  Serial.println("==============================");
  Serial.println();

  screenState = 0;
  updateDisplay();
}

// =====================================================
// BUTTON HANDLER
// GPIO 18 to GND; INPUT_PULLUP enabled.
// =====================================================

void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  if (
    millis() - lastDebounceTime >
    DEBOUNCE_MS
  ) {
    if (reading != stableButtonState) {
      stableButtonState = reading;

      if (stableButtonState == LOW) {
        screenState++;

        if (screenState > 2) {
          screenState = 0;
        }

        updateDisplay();
      }
    }
  }

  lastButtonReading = reading;
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  Serial.setTimeout(100);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(OLED_ADDRESS, true)) {
    Serial.println("OLED initialization failed.");

    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.display();

  showMessage(
    "SPLITBUS ANALYZER",
    "Waiting for input..."
  );

  Serial.println();
  Serial.println("SPLITBUS ANALYZER v2.0");
  Serial.println("----------------------");
  Serial.println("Enter operations:");
  Serial.println("R8  = Read 8 bits");
  Serial.println("R16 = Read 16 bits");
  Serial.println("W8  = Write 8 bits");
  Serial.println("W16 = Write 16 bits");
  Serial.println();
  Serial.println("Example:");
  Serial.println("R8 R16 W8 W16 R16");
  Serial.println();
  Serial.println("Baud rate: 115200");
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  handleButton();

  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');

    if (parseOperations(input)) {
      runAnalysis();
    } else {
      Serial.println("Try: R8 R16 W8 W16 R16");
    }
  }
}
