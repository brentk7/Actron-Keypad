#include "esphome.h"

#define NPULSE 40  // Define the number of pulses to be captured

class clsLedProto {
  public: // Add this line to change the access level to public for the following members
  enum ClassStatLeds {
    // Enumerations representing the indices in the pulse train for various status LEDs on the HVAC unit
    COOL = 0,
    AUTO = 1,  //RUN_BLINK
    UNK1 = 2,
    RUN = 3,
    ROOM = 4,
    UNK2 = 5,
    UNK3 = 6,
    UNK4 = 7,
    FAN_CONT = 8,  // COOL_AUTO
    FAN_HI = 9,
    FAN_MID = 10,
    FAN_LOW = 11,
    ROOM3 = 12,
    ROOM4 = 13,
    ROOM2 = 14,
    HEAT = 15,
    _3C = 16,
    _3F = 17,
    _3G = 18,
    _3B = 19,
    _3A = 20,
    ROOM1 = 21,
    _3E = 22,
    _3D = 23,
    _2B = 24,
    _2F = 25,
    _2G = 26,
    _2E = 27,
    DP = 28,
    _2C = 29,
    _2D = 30,
    _2A = 31,
    _1D = 32,
    UNK5 = 33,
    _1C = 34,
    _1B = 35,
    _1E = 36,
    _1G = 37,
    _1F = 38,
    _1A = 39,  
    UNK6 = 40
    };

  unsigned long last_intr_us;  // Timestamp of the last interrupt in microseconds
  unsigned long last_work;     // Timestamp of the last work in the main loop in microseconds
  char pulse_vec[NPULSE];      // Temporary storage for the pulse train read during the interrupt
  volatile unsigned char nlow; // Counter for the number of low pulses read
  volatile unsigned char nbits; // Counter for the number of bits read to be published to Home Assistant (volatile means can be changed externally)
  volatile unsigned char dbg_nerr; // Counter for the number of errors (volatile means can be changed externally)
  volatile bool do_work;       // Flag indicating whether there's work to be done in the main loop
  bool data_error;             // Flag indicating whether there's been a data error
  bool newdata;                // Flag indicating whether there's new data to be processed
  char p[NPULSE];              // Storage for the most recent stable pulse train read from the unit  

  //Interrupt Handler
  void handleIntr() {
    auto nowu = micros();           // Stores the current microsecond count at the time of the interrupt.
    unsigned long dtu = nowu - last_intr_us;  // Calculates the time difference in microseconds since the last interrupt.
    last_intr_us = nowu;           // Updates the last interrupt timestamp to the current timestamp.
    if (dtu > 3500) {
      // Do nothing, as this is assumed to be the start of the start bit
      data_error = false;  // Reset the data error flag
      return;
    }
    if (dtu >= 2700) {
    // Start bit detected, reset bit_count
      nlow = 0;
    } else {
    // Data bit detected
    if (nlow >= NPULSE) {
      //ESP_LOGD("custom","Too many pulses: %d", nlow);  //Adding this makes unstable - guess shoudln't log in ISR (maybe increment counter rather)
      data_error = true;  // Set the data error flag
      ++dbg_nerr;  // Increment the error counter
      nlow = NPULSE;  // Ensures that nlow does not exceed NPULSE, resetting it to NPULSE if it does.
    }  
    pulse_vec[nlow] = dtu < 1000;      // Records a '1' or '0' in the pulse vector based on the time difference being less than 800 microseconds.
    ++nlow;       // Increments the nlow counter, indicating a new pulse has been recorded.
    do_work = 1;  // Sets the do_work flag to true, indicating that there's work to be done in the main loop.
    }
  }
  

  char decode_digit(uint8_t hex_value) {
  //This function takes a hex value representing a digit on the display and returns the corresponding character
  //Using conventional segment display values, the hex values are as follows:  
    switch (hex_value) {
      case 0x3F: return '0';
      case 0x06: return '1';
      case 0x5B: return '2';
      case 0x4F: return '3';
      case 0x66: return '4';
      case 0x6D: return '5';
      case 0x7C: return '6';
      case 0x07: return '7';
      case 0x7F: return '8';
      case 0x67: return '9';
      case 0x73: return 'P';  // 'P' is a special case
      default: return '?';   // Return '?' for unrecognized hex values
    }
  }

float get_display_value() {
    uint8_t digit1_bits = (p[_1G] << 6) | (p[_1F] << 5) | (p[_1E] << 4) | (p[_1D] << 3) | (p[_1C] << 2) | (p[_1B] << 1) | p[_1A];
    uint8_t digit2_bits = (p[_2G] << 6) | (p[_2F] << 5) | (p[_2E] << 4) | (p[_2D] << 3) | (p[_2C] << 2) | (p[_2B] << 1) | p[_2A];
    uint8_t digit3_bits = (p[_3G] << 6) | (p[_3F] << 5) | (p[_3E] << 4) | (p[_3D] << 3) | (p[_3C] << 2) | (p[_3B] << 1) | p[_3A];

    std::string display_str;
    display_str += decode_digit(digit1_bits);
    display_str += decode_digit(digit2_bits);
    display_str += decode_digit(digit3_bits);

    for (char c : display_str) {
        if (!isdigit(c) ) return -1.0f;  // return -1 if any character is not a digit
    }
    float display_value = std::stof(display_str);  // Convert string to float
    if (p[DP]) display_value *= 0.1f;              // Apply decimal point if DP bit is set
    return display_value;
    }
  
  void mloop() {
    unsigned long now = micros();  // Get the current microsecond count
    if (do_work) {      // If there's work to do (set by handleIntr())
      do_work = 0;      // Reset the work flag
      last_work = now;  // Update the last work time to now
    } else {
      unsigned long dt = now - last_work;  // Calculate the time since last work
      if (dt > 40000 && nlow) {  // If more than 40000 microseconds have passed and there are pulses recorded
        nbits = nlow;            // Set the number of bits to the number of pulses recorded
        nlow = 0;                // Reset the pulse counter (BCK added this line)
        if (nbits ==  40 && !data_error ) {  // If exactly 40 pulses have been recorded (BCK changed from 42. Sometimes we get 41 bits and this has invalid data)
          if(memcmp(p, pulse_vec, sizeof p) != 0) {  // If the pulse data has changed
            newdata = true;           // Set the newdata flag for the publish_state() call
            //for (int n = 0; n < 45; ++n){       // Loop through each element of the pulse vector
            //  if (p[n] != pulse_vec[n]) ESP_LOGD("custom","%d: %d, ", n, pulse_vec[n]);  // Log the changed data
            //}
            memcpy(p, pulse_vec, sizeof p);  // Copy the new pulse data           
          }
        } else {
          ESP_LOGD("custom","Only %d bits received (Or data error)", nbits);  // Log the number of bits received
        }
        last_work = now;  // Update the last work time to now
      }
    }
  }
};

clsLedProto ledProto;  // Instantiate a clsLedProto object named ledProto

void handleInterrupt() {
    // Global function to handle interrupts and call the appropriate method on ledProto
    ledProto.handleIntr();  
}

class KeypadStatus : public Component{
private:
  TextSensor *bitString   ;
  Sensor *setpoint_temp    ;
  Sensor *bitcount        ;
  BinarySensor *cool      ;
  BinarySensor *auto_md      ;
  BinarySensor *unk1      ;
  BinarySensor *run       ;
  BinarySensor *room      ;
  BinarySensor *unk2      ;
  BinarySensor *unk3      ;
  BinarySensor *unk4      ;
  BinarySensor *fan_cont  ;
  BinarySensor *fan_hi    ;
  BinarySensor *fan_mid   ;
  BinarySensor *fan_low   ;
  BinarySensor *room3     ;
  BinarySensor *room4     ;
  BinarySensor *room2     ;
  BinarySensor *heat      ;
  BinarySensor *room1     ;
  BinarySensor *unk5      ;

public:
  int adc_pin;
  float get_setup_priority() const override { return esphome::setup_priority::IO; }
  KeypadStatus(int adc_pin             ,  //Pin for ADC connection
               TextSensor *bitString   ,
               Sensor *setpoint_temp    ,
               Sensor *bitcount        ,
               BinarySensor *cool      ,
               BinarySensor *auto_md ,
               BinarySensor *unk1      ,
               BinarySensor *run       ,
               BinarySensor *room      ,
               BinarySensor *unk2      ,
               BinarySensor *unk3      ,
               BinarySensor *unk4      ,
               BinarySensor *fan_cont ,
               BinarySensor *fan_hi    ,
               BinarySensor *fan_mid   ,
               BinarySensor *fan_low   ,
               BinarySensor *room3     ,
               BinarySensor *room4     ,
               BinarySensor *room2     ,
               BinarySensor *heat      ,
               BinarySensor *room1     ,
               BinarySensor *unk5      )  : adc_pin(adc_pin)
  {
    this->bitString    = bitString   ;
    this->setpoint_temp = setpoint_temp;
    this->bitcount     = bitcount    ;
    this->cool         = cool        ;
    this->auto_md    = auto_md   ;
    this->unk1         = unk1        ;
    this->run          = run         ;
    this->room         = room        ;
    this->unk2         = unk2        ;
    this->unk3         = unk3        ;
    this->unk4         = unk4        ;
    this->fan_cont    = fan_cont   ;
    this->fan_hi       = fan_hi      ;
    this->fan_mid      = fan_mid     ;
    this->fan_low      = fan_low     ;
    this->room3        = room3       ;
    this->room4        = room4       ;
    this->room2        = room2       ;
    this->heat         = heat        ;
    this->room1        = room1       ;
    this->unk5         = unk5        ;
  } 

  void setup() override
  {
  // Setup code to configure the pin and attach the interrupt
  //Send adc_pin to log
  ESP_LOGD("custom","adc_pin: %d", adc_pin); 
  //adc_pin = 33;
  pinMode(adc_pin, INPUT);
  attachInterrupt(digitalPinToInterrupt(adc_pin), handleInterrupt, FALLING);
    }

    void loop() override {
      // Main loop for the LedProto Component, processes the pulse train and publishes the state to Home Assistant
      ledProto.mloop();
      // Initialize an empty string
      std::string text;
      
      if (ledProto.newdata) {  // Publish the text to the TextSensor
      // Loop through each element of the char array
        text = "";
        for(int i = 0; i < NPULSE; ++i) {
          // Append '0' or '1' to the string based on the value of each element
          text += (ledProto.p[i] ? '1' : '0');
        }
        bitString->publish_state(text);
        ledProto.newdata = false;
      
      // Publish the display value as a number
      float display_value = ledProto.get_display_value();
      setpoint_temp->publish_state(display_value);
//      bitcount->publish_state(ledProto.nbits);
      bitcount->publish_state(ledProto.dbg_nerr); //Changed to publish the error count instead of the bit count

      // Publish the status of each LED as a binary sensor (convert to boolean with check for 0)
      cool->publish_state(ledProto.p[clsLedProto::COOL] != 0);
      auto_md->publish_state(ledProto.p[clsLedProto::AUTO] != 0);
      unk1->publish_state(ledProto.p[clsLedProto::UNK1] != 0);
      run->publish_state(ledProto.p[clsLedProto::RUN] != 0);
      room->publish_state(ledProto.p[clsLedProto::ROOM] != 0);
      unk2->publish_state(ledProto.p[clsLedProto::UNK2] != 0);
      unk3->publish_state(ledProto.p[clsLedProto::UNK3] != 0);
      unk4->publish_state(ledProto.p[clsLedProto::UNK4] != 0);
      fan_cont->publish_state(ledProto.p[clsLedProto::FAN_CONT] != 0);
      fan_hi->publish_state(ledProto.p[clsLedProto::FAN_HI] != 0);
      fan_mid->publish_state(ledProto.p[clsLedProto::FAN_MID] != 0);
      fan_low->publish_state(ledProto.p[clsLedProto::FAN_LOW] != 0);
      room3->publish_state(ledProto.p[clsLedProto::ROOM3] != 0);
      room4->publish_state(ledProto.p[clsLedProto::ROOM4] != 0);
      room2->publish_state(ledProto.p[clsLedProto::ROOM2] != 0);
      heat->publish_state(ledProto.p[clsLedProto::HEAT] != 0);
      room1->publish_state(ledProto.p[clsLedProto::ROOM1] != 0);
      unk5->publish_state(ledProto.p[clsLedProto::UNK5] != 0);

      }
    }
};