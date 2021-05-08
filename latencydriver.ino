// The output can be fed into benchstat to summarize/compare:
// https://pkg.go.dev/golang.org/x/perf/cmd/benchstat

volatile uint32_t t0 = 0; // keypress started
elapsedMillis emt0;
elapsedMicros eut0;
volatile bool simulate_press = false;

static uint32_t last_scan = 0;
static uint32_t scan_deltas[3];
static uint32_t scan_delta_idx;

void onScan() {
  if (simulate_press) {
    // connect row scan signal with column read
    digitalWrite(11, digitalRead(10));
  } else {
    // always read not pressed otherwise
    digitalWrite(11, HIGH);
  }

  // Store the delay since the last onScan interrupt,
  // which either represents the time between
  // turning the row on and off (e.g. 607 cycles),
  // or the time between two matrix scans (row off til row on,
  // e.g. 125395 cycles).
  const uint32_t now = ARM_DWT_CYCCNT;
  const uint32_t scan_delta = now - last_scan;
  last_scan = now;

  // Keep 3 values to always have one matrix-to-matrix delay:
  scan_deltas[scan_delta_idx] = scan_delta;
  scan_delta_idx++;
  if (scan_delta_idx == 3) {
    scan_delta_idx = 0;
  }
}

bool capsLockOn() {
  return !digitalRead(12);
}

// Figure out the ARM_DWT_CYCCNT (32-bit register) value range,
// to verify the cycle counter reports valid values for our
// measurements.
const double nanosecond = 1;
const double microsecond = 1000 * nanosecond;
const double millisecond = 1000 * microsecond;
const double second = 1000 * millisecond;
const double cycles_per_ns = (double)F_CPU / second;
const double ns_per_cycle = second / (double)F_CPU;
const uint32_t largest_cycle_counter_value = UINT32_MAX;
const unsigned long cycle_counter_range_millis =
  (double)largest_cycle_counter_value / (cycles_per_ns * millisecond);

static bool lastCapsLock;

void onCapsLockLED() {
  const bool nowCapsLock = capsLockOn();
  if (lastCapsLock == nowCapsLock) {
    return; // duplicate
  }
  lastCapsLock = nowCapsLock;
  const uint32_t t1 = ARM_DWT_CYCCNT;
  const uint32_t elapsed_millis = emt0;
  const uint32_t elapsed_micros = eut0;
  uint32_t elapsed_nanos = (t1 - t0) / cycles_per_ns;

  if (elapsed_millis > cycle_counter_range_millis) {
    elapsed_nanos = 0;
    Serial.printf("measurement exceeds cycle counter value range\r\n");
  }

  Serial.printf("# Caps Lock LED (pin 12) is now %s\r\n", nowCapsLock ? "on" : "off");
  Serial.printf("# %u ms == %u us\r\n", elapsed_millis, elapsed_micros);
  Serial.printf("BenchmarkKeypressToLEDReport 1 %u ns/op\r\n", elapsed_nanos);
  Serial.printf("\r\n");
}

void setup() {
  Serial.begin(9600);

  // Connected to kinT pin 15, COL_2
  pinMode(11, OUTPUT);
  digitalWrite(11, HIGH);

  // Connected to kinT pin 8, ROW_0.
  // Pin 11 will be high/low in accordance with pin 10
  // to simulate a key-press, and always high (unpressed)
  // otherwise.
  pinMode(10, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(10), onScan, CHANGE);

  // Connected to the kinT LED_CAPS_LOCK output:
  pinMode(12, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(12), onCapsLockLED, CHANGE);
}

void setT0(void) {
  t0 = ARM_DWT_CYCCNT;
  emt0 = 0;
  eut0 = 0;
}

bool caps_lock_on_to_off = false;

bool greet = true;

elapsedMillis since_last_print = 0;

void loop() {
  if (!Serial) {
    greet = true;
    return;
  }
  if (greet) {
    Serial.printf("# kinT latency measurement driver\r\n");
    Serial.printf("#   t  - trigger measurement\r\n");
    greet = false;
  }

  if (simulate_press && (unsigned long)emt0 > 500) {
    simulate_press = false;
    if (caps_lock_on_to_off) {
      // When turning Caps Lock off (but not when turning it on,
      // only when turning it off!), Linux sends the LED HID
      // report on key release, not on key press.
      setT0();
    }
  }
  if (Serial.available()) {
    byte incomingByte = Serial.read();
    switch (incomingByte) {
      case 't':
      caps_lock_on_to_off = capsLockOn();
      Serial.printf("# Caps Lock key pressed (transition: %s)\r\n",
        caps_lock_on_to_off ? "on to off" : "off to on");
      simulate_press = true;
      setT0();
      break;
    }
  }
  if ((unsigned long)since_last_print > 5000) {
    uint32_t highest_delta = 0;
    for (int i = 0; i < 3; i++) {
      if (scan_deltas[i] > highest_delta) {
        highest_delta = scan_deltas[i];
      }
    }
    Serial.printf("# scan-to-scan delay: %u ns\r\n", (uint32_t)((double)highest_delta * ns_per_cycle));
    since_last_print = 0;
  }
}
