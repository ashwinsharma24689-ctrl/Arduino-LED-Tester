/*
  Arduino LED Tester + Resistor Calculator
  =========================================
  DPIN--39R--+--10R---TESTLED---GND
             |      |         |
            470u    ATOP     ABOT
             |
            GND

  Measures LED characteristics by charging up the cap to deliver target current and find forward voltage.
  From target current, we can calculate R to be used with a design supply voltage and a matching part number.*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---- Timing intervals ----
#define LCDINT  500   // LCD update interval in ms
#define KEYINT  200   // Key check interval in ms

// ---- Button pin and constants ----
#define KEYPIN    A0
#define btnRIGHT  6
#define btnUP     5
#define btnDOWN   4
#define btnLEFT   3
#define btnSELECT 2
#define btnNONE   (-1)

// ---- Test interface pins ----
#define ATOP  A2    // Analog pin at top of sense resistor
#define ABOT  A3    // Analog pin at bottom of sense resistor (across LED)
#define DPIN  3     // PWM output pin for current driver
#define OSAMP 16    // Oversampling count for ADC averaging

// ---- LCD setup ----
LiquidCrystal_I2C lcd(0x27, 16, 2);  // I2C address 0x27, 16 cols, 2 rows

// ---- Global variables ----
long itest = 10;    // Target test current in mA
long vset  = 14000; // Target supply voltage in mV
long vled, vrr, irr, pset; // LED voltage, sense resistor voltage, current, power dissipation
long irf   = 0;     // Raw current reading before PWM correction

long lastlcd = 0;   // Timestamp of last LCD update
long lastkey = 0;   // Timestamp of last key check
int  lcdflash = 0;  // LCD flashing phase toggle

long pdes   = 0;    // Power desired (unused, kept for compatibility)
long rval   = 0;    // Calculated resistor value in ohms
long rindex = 0;    // Index of nearest E24 resistor in lookup table
int  pwmout = 0;    // PWM duty cycle output (0-255)
int  rvalid = 0;    // 1 = valid resistor found, 0 = not valid

// ---- E24 resistor lookup table (Jaycar 1/2W range, starts at RR0524 for 10R) ----
#define RCOUNT 121
long rvals[] = {
  10,11,12,13,15,16,18,20,22,24,27,30,33,36,39,43,47,51,56,62,68,75,82,91,
  100,110,120,130,150,160,180,200,220,240,270,300,330,360,390,430,470,510,560,620,680,750,820,910,
  1000,1100,1200,1300,1500,1600,1800,2000,2200,2400,2700,3000,3300,3600,3900,4300,4700,
  5100,5600,6200,6800,7500,8200,9100,
  10000,11000,12000,13000,15000,16000,18000,20000,22000,24000,27000,30000,33000,36000,39000,43000,47000,
  51000,56000,62000,68000,75000,82000,91000,
  100000,110000,120000,130000,150000,160000,180000,200000,220000,240000,270000,300000,330000,360000,
  390000,430000,470000,510000,560000,620000,680000,750000,820000,910000,1000000
};

// ============================================================
void setup() {
  Serial.begin(9600);

  Wire.begin();
  lcd.init();

  // ---- Serial startup diagnostics ----
  Serial.println("=== Arduino LED Tester + Resistor Calculator ===");
  Serial.println("Initializing...");

  Wire.beginTransmission(0x27);
  byte error = Wire.endTransmission();
  if(error == 0){
    Serial.println("[OK]   I2C LCD found at address 0x27");
  } else {
    Serial.println("[FAIL] I2C LCD not found at 0x27 - check wiring");
  }

  pinMode(DPIN, OUTPUT);
  Serial.println("[OK]   PWM output pin D3 ready");
  Serial.println("[OK]   Analog sense pins A2 (ATOP) and A3 (ABOT) ready");
  Serial.println("================================================");
  Serial.println("RAW=analog counts | MEAS=measured values | SET=user settings | CALC=calculated results");
  Serial.println("================================================");

  // ---- LCD welcome screen ----
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(3, 0);        // Center on first line
  lcd.print("RES CALC");
  lcd.setCursor(2, 1);        // Center on second line
  lcd.print("LED  TESTER");

  // Blink backlight 3 times
  for(int i = 0; i < 3; i++){
    lcd.noBacklight();
    delay(200);
    lcd.backlight();
    delay(400);
  }
  delay(1000);  // Hold steady before clearing
  lcd.clear();
}

// ============================================================
void loop() {
  long atop, abot, arr;   // Raw analog sample values

  irf    = irr;           // Capture current before PWM correction
  rvalid = 0;             // Reset valid flag

  // ---- ADC oversampled readings ----
  atop = analogoversample(ATOP, OSAMP) / OSAMP;
  abot = analogoversample(ABOT, OSAMP) / OSAMP;
  arr  = atop - abot;     // Analog value across 10R sense resistor
  if(arr < 0){ arr = 0; } // Sanity check

  // ---- Measurements ----
  vled = abot * 5000 / 1023;  // Voltage across LED in mV
  vrr  = arr  * 5000 / 1023;  // Voltage across sense resistor in mV
  irr  = vrr  / 10;           // Current in mA (10R sense resistor)

  // ---- PWM control loop ----
  if(irr < itest){ pwmout++; if(pwmout > 255){ pwmout = 255; }}   // Ramp up if too low
  if(irr > itest){ pwmout--; if(pwmout < 0)  { pwmout = 0;   }}   // Ramp down if too high
  if(irr > 24)          { pwmout -= 5; if(pwmout < 0){ pwmout = 0; }} // Fast ramp down if way too high
  if(irr > itest * 3)   { pwmout -= 5; if(pwmout < 0){ pwmout = 0; }} // Fast ramp down if 3x target
  analogWrite(DPIN, pwmout);

  // ---- Resistor calculation ----
  rval = (vset - vled) / itest;  // Required resistance in ohms
  for(int i = 0; i < RCOUNT; i++){
    if(rvals[i] >= rval){
      rindex = i;
      rval   = rvals[rindex];
      i      = RCOUNT + 1;
      rvalid = 1;
    }
  }
  if(abs(irr - itest) > (itest / 5) + 1){ rvalid = 0; } // Not settled within 20%?
  if(vled > vset){ rvalid = 0; }                         // vled > vset means no valid resistor

  // ---- Power dissipation ----
  pset = 0;
  if(rvalid){ pset = itest * itest * rval; } // Result in microwatts (mA^2 * ohms)

  // ---- LCD + Serial update (every 500ms) ----
  if(millis() - lastlcd > LCDINT){
    lastlcd = millis();
    dolcd();
    lcdflash = 1 - lcdflash;

    // --- Serial Monitor Output ---
    Serial.print("PWM:"); Serial.print(pwmout);                              // PWM duty cycle (0-255)
    Serial.print(" | RAW: ATOP="); Serial.print(atop);                       // Raw ADC at top of sense resistor
    Serial.print(" ABOT="); Serial.print(abot);                              // Raw ADC at bottom of sense resistor
    Serial.print(" | MEAS: vled="); Serial.print(vled); Serial.print("mV");  // LED forward voltage in mV
    Serial.print(" vrr="); Serial.print(vrr); Serial.print("mV");            // Voltage across sense resistor in mV
    Serial.print(" irr="); Serial.print(irr); Serial.print("mA");            // Measured LED current in mA
    Serial.print(" irf="); Serial.print(irf); Serial.print("mA");            // Current before PWM correction
    Serial.print(" | SET: itest="); Serial.print(itest); Serial.print("mA"); // Target test current
    Serial.print(" vset="); Serial.print(vset); Serial.print("mV");          // Target supply voltage
    Serial.print(" | CALC: rval="); Serial.print(rval); Serial.print("R");   // Calculated resistor value
    Serial.print(" rindex="); Serial.print(rindex);                          // E24 lookup table index
    Serial.print(" rvalid="); Serial.println(rvalid);                        // 1=valid, 0=invalid
    // --- End Serial Output ---
  }

  // ---- Button check (every 200ms) ----
  if(millis() - lastkey > KEYINT){
    lastkey = millis();
    dobuttons();
  }

  delay(1);
} // end of loop

// ============================================================
void dolcd(){
  lcd.setCursor(0, 0); // First line

  // Target current (flashes when not valid)
  if(lcdflash || rvalid){
    if(itest > 9){ lcd.write(((itest / 10) % 10) + '0'); } else { lcd.write(' '); }
    lcd.write((itest % 10) + '0');
  } else {
    lcd.write(' '); lcd.write(' ');
  }
  lcd.write('m'); lcd.write('A'); lcd.write(' ');

  // LED forward voltage (0.0V to 9.9V)
  if(vled >= 1000){
    lcd.write(((vled / 1000) % 10) + '0');  // Units digit (e.g. '2' for 2100mV)
  } else {
    lcd.write('0');                          // Show 0 if less than 1V
  }
  lcd.write('.');
  lcd.write(((vled / 100) % 10) + '0');     // First decimal place (e.g. '1' for 2100mV)
  lcd.write('V'); lcd.write(' ');

  // Measured current
  if(irr > 9){ lcd.write(((irr / 10) % 10) + '0'); } else { lcd.write(' '); }
  lcd.write((irr % 10) + '0');
  lcd.write('m'); lcd.write('A'); lcd.write(' ');

  // Flash P if power > 0.5W
  if((pset > 499999) && (lcdflash)){ lcd.write('P'); } else { lcd.write(' '); }

  lcd.setCursor(0, 1); // Second line

  // Supply voltage
  if(vset > 9999){ lcd.write(((vset / 10000) % 10) + '0'); } else { lcd.write(' '); }
  lcd.write(((vset / 1000) % 10) + '0');
  lcd.write('V'); lcd.write(' ');

  // Resistor value and part number
  if(rvalid){
    lcdprintrval(rval);       // Resistor value (4 characters)
    lcd.write(' ');
    lcdprintpartno(rindex);   // Jaycar part number (6 characters)
    if(pset > 499999){ lcd.write('!'); } else { lcd.write(' '); } // ! if power > 0.5W
  } else {
    lcd.write(' ');
    lcd.write('-'); lcd.write('-'); lcd.write('-');
    lcd.write(' ');
    lcd.write('-'); lcd.write('-'); lcd.write('-');
    lcd.write('-'); lcd.write('-'); lcd.write('-');
    lcd.write(' ');
  }
}

// ============================================================
void lcdprintpartno(int index){
  // Jaycar part numbers start at RR0524 for 10R
  lcd.write('R'); lcd.write('R'); lcd.write('0');
  lcd.write((((index + 524) / 100) % 10) + '0');
  lcd.write((((index + 524) / 10)  % 10) + '0');
  lcd.write((((index + 524))       % 10) + '0');
}

// ============================================================
void lcdprintrval(long rval){
  // Prints resistor value in 10k0 format, always 4 characters
  long mult   = 1;
  long modval;
  if(rval > 999)   { mult = 1000;    }
  if(rval > 999999){ mult = 1000000; }
  modval = (10 * rval) / mult;

  if(modval > 999){        // nnnM format
    lcd.write(((modval / 1000) % 10) + '0');
    lcd.write(((modval / 100)  % 10) + '0');
    lcd.write(((modval / 10)   % 10) + '0');
    lcdprintmult(mult);
  } else if(modval > 99){  // nnMn format
    lcd.write(((modval / 100) % 10) + '0');
    lcd.write(((modval / 10)  % 10) + '0');
    lcdprintmult(mult);
    lcd.write((modval % 10) + '0');
  } else {                 // _nMn format
    lcd.write(' ');
    lcd.write(((modval / 10) % 10) + '0');
    lcdprintmult(mult);
    lcd.write((modval % 10) + '0');
  }
}

// ============================================================
void lcdprintmult(long mult){
  // Helper function to print resistor multiplier character
  switch(mult){
    case 1:       lcd.print('R'); break;
    case 1000:    lcd.print('k'); break;
    case 1000000: lcd.print('M'); break;
    default:      lcd.print('?'); break;
  }
}

// ============================================================
int read_LCD_buttons(){
  int adc_key_in = analogRead(KEYPIN);  // Read button value
  delay(5);                             // Debounce delay
  int k = (analogRead(KEYPIN) - adc_key_in);
  if(5 < abs(k))        return btnNONE; // Noise detected, ignore
  if(adc_key_in > 1000) return btnNONE;
  if(adc_key_in < 50)   return btnRIGHT;
  if(adc_key_in < 195)  return btnUP;
  if(adc_key_in < 380)  return btnDOWN;
  if(adc_key_in < 555)  return btnLEFT;
  if(adc_key_in < 790)  return btnSELECT;
  return btnNONE;
}

// ============================================================
void dobuttons(){
  int key = read_LCD_buttons();

  if(key == btnLEFT){
    itest--;
    if(itest < 1){ itest = 1; }
    Serial.print("[BTN] LEFT pressed  -> itest="); Serial.print(itest); Serial.println("mA");
  }
  if(key == btnRIGHT){
    itest++;
    if(itest > 20){ itest = 20; }
    Serial.print("[BTN] RIGHT pressed -> itest="); Serial.print(itest); Serial.println("mA");
  }
  if(key == btnUP){
    vset += 1000;
    if(vset > 99000){ vset = 99000; }
    Serial.print("[BTN] UP pressed    -> vset="); Serial.print(vset / 1000); Serial.println("V");
  }
  if(key == btnDOWN){
    vset -= 1000;
    if(vset < 0){ vset = 0; }
    Serial.print("[BTN] DOWN pressed  -> vset="); Serial.print(vset / 1000); Serial.println("V");
  }
}

// ============================================================
long analogoversample(int pin, int samples){
  // Read pin 'samples' times and return sum for averaging
  long n = 0;
  for(int i = 0; i < samples; i++){
    n = n + analogRead(pin);
  }
  return n;
}
