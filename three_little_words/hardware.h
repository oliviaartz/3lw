#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <U8g2lib.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"

#include "fp.hpp"

using namespace fp;
using namespace fp::constants;

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#define NUM_WORDS 3

uint TOP_BTN_CCW[] = {0, 16, 21};
uint ENC_BTN_CW[]  = {1, 17, 22};
uint TRIG_IN[]     = {18, 19, 20};
uint CV_IN[]       = {26, 27, 28};
uint VOCT_OFFSET[] = {2,  6,  8};
uint CV_OFFSET[]   = {10, 12, 14};

class GateTrigger {
public:
  uint pin;
  bool state;
  bool fallingEdge;
  bool risingEdge;
  GateTrigger(uint pin) {
    this->pin = pin;
    this->state = false;
    this->fallingEdge = false;
    this->risingEdge = false;
    gpio_pull_up(pin);
  }
  void Update() {
    bool newState = !gpio_get(pin);
    if(newState > state) this->risingEdge = true;
    if(newState < state) this->fallingEdge = true;
    this->state = newState; 
  }
  bool State() { return state; }
  bool FallingEdge() {
    if(fallingEdge) {
      fallingEdge = false;
      return true;
    }
    return false;
  }
  bool RisingEdge() {
    if(risingEdge) {
      risingEdge = false;
      return true;
    }
    return false;
  }
};

class ButtonAndEncoder {
private:
  bool _topButtonPressed;
  bool _encButtonPressed;
public:
  enum State {
    NONE,
    TOP_CCW_TRIG,
    ENC_CW_TRIG
  };
  uint topButtonCCW;
  uint encButtonCW;
  int encValue;
  State state;
  uint64_t nextCanTrigger;
  uint64_t delayTime;
  bool topButtonHeld;
  bool encButtonHeld;
  int topButtonHeldFor;
  int encButtonHeldFor;
  bool topButtonPressed() {
    bool out = _topButtonPressed;
    _topButtonPressed = false;
    return out;
  }
  bool encButtonPressed() {
    bool out = _encButtonPressed;
    _encButtonPressed = false;
    return out;
  }
  ButtonAndEncoder(uint topButtonCcw, uint encButtonCw) {
    this->topButtonCCW = topButtonCcw;
    this->encButtonCW = encButtonCw;
    this->encValue = 0;
    this->state = NONE;
    this->nextCanTrigger = time_us_64();
    this->delayTime = 1000000/25;
    this->topButtonHeld = false;
    this->_topButtonPressed = false;
    this->encButtonHeld = false;
    this->_encButtonPressed = false;
    this->topButtonHeldFor = 0;
    this->encButtonHeldFor = 0;
    gpio_pull_up(topButtonCCW);
    gpio_pull_up(encButtonCW);
  }

  int GetDelta() {
    int delta = encValue;
    encValue = 0;
    return delta;
  }

  void Update() {
    if(time_us_64() > (nextCanTrigger + delayTime)) {
      bool topButtonState = !gpio_get(topButtonCCW);
      bool encButtonState = !gpio_get(encButtonCW);
      if(state != NONE) {
        _topButtonPressed |= !topButtonHeld && topButtonState;
        _encButtonPressed |= !encButtonHeld && encButtonState;
      }
      topButtonHeldFor = topButtonHeld && topButtonState ? topButtonHeldFor + 1 : 0;
      encButtonHeldFor = encButtonHeld && encButtonState ? encButtonHeldFor + 1 : 0;
      topButtonHeld = topButtonState;
      encButtonHeld = encButtonState;
      state = NONE;
    }
  }
  void Update(uint pin, uint32_t events) {
    switch(state) {
      case NONE:
        if(time_us_64() > nextCanTrigger) {
          if(pin == topButtonCCW && !gpio_get(pin)) {
            state = TOP_CCW_TRIG;
            nextCanTrigger = time_us_64() + delayTime;
          }
          if(pin == encButtonCW && !gpio_get(pin)) {
            state = ENC_CW_TRIG;
            nextCanTrigger = time_us_64() + delayTime;
          }
        }
        break;
      case TOP_CCW_TRIG:
        if(pin == encButtonCW && !gpio_get(pin)) {
          encValue++;
          state = NONE;
          nextCanTrigger = time_us_64() + delayTime;
        }
        break;
      case ENC_CW_TRIG:
        if(pin == topButtonCCW && !gpio_get(pin)) {
          encValue--;
          state = NONE;
          nextCanTrigger = time_us_64() + delayTime;
        }
        break;
    }
  }
};

class AnalogOut {
public:
  uint16_t res;
  uint offset;
  fp_t<int, 12> bipolarOffset;
  fp_t<int, 12> bipolarCoef;
  fp_t<int, 12> unipolarCoef;
  AnalogOut(int offset, int resolution = 255, double negMax = VOCT_NOUT_MAX, double posMax = VOCT_POUT_MAX) {
    this->offset = offset;
    this->res = resolution;
    // --- //
    this->bipolarOffset = fp_t<int, 12>(posMax * 0.5 / negMax);
    this->bipolarCoef = fp_t<int, 12>(1.0 / (posMax / 2.0));
    this->unipolarCoef = fp_t<int, 12>(1.0 / posMax);
    // --- //
    for(uint16_t i=0;i<2;i++) {
      uint slice_num = pwm_gpio_to_slice_num(i + offset);
      pwm_config cfg = pwm_get_default_config();
      pwm_config_set_clkdiv_int(&cfg, 1);
      pwm_config_set_wrap(&cfg, this->res);
      pwm_init(slice_num, &cfg, true);
      gpio_set_function(i + offset, GPIO_FUNC_PWM);
      pwm_set_gpio_level(i + offset, 0);
    }
  }
  void Set(double level) {
    pwm_set_gpio_level(offset, (uint16_t)(level*res));
  }
  void SetOffset(double level) {
    pwm_set_gpio_level(offset+1, (uint16_t)(level*res));
  }

  void SetCycles(int cycles) {
    pwm_set_gpio_level(offset, (uint16_t)cycles);
  }
  void SetCyclesOffset(int cycles) {
    pwm_set_gpio_level(offset+1, (uint16_t)cycles);
  }

  void SetBipolar(fp_t<int, 12> v) {
    pwm_set_gpio_level(offset, int(fp_t<int, 0>(res) * bipolarOffset));
    pwm_set_gpio_level(offset+1, (res>>1) + int(fp_t<int, 0>(res>>1) * fp_t<int, 12>(v * bipolarCoef)));
  }
  void SetUnipolar(fp_t<int, 12> v) {
    pwm_set_gpio_level(offset, 0);
    pwm_set_gpio_level(offset+1, int(fp_t<int, 0>(res) * fp_t<int, 12>(v * unipolarCoef)));
  }
};

class TLWHardware {
public:
  static TLWHardware* _tlwhw_;
  static void (*_audioCallback_)(void);
  struct repeating_timer _timer_;

  U8G2_SSD1306_128X64_NONAME_F_HW_I2C* display;
  ButtonAndEncoder* control[NUM_WORDS];
  GateTrigger* trigIn[NUM_WORDS];
  fp_t<int, 12> analogIn[NUM_WORDS];
  AnalogOut* voctOut[NUM_WORDS];
  AnalogOut* cvOut[NUM_WORDS];

  static void controlHandler(uint gpio, uint32_t events) {
    for(int i=0; i<NUM_WORDS; i++) {
      _tlwhw_->control[i]->Update(gpio, events);
    }
  }

  static fp_t<int, 12> mix(fp_t<int, 12> a, fp_t<int, 12> b, fp_t<int, 12> x) {
    if(x > fp_t<int, 12>(1)) x = fp_t<int, 12>(1);
    if(x < fp_t<int, 12>(0)) x = fp_t<int, 12>(0);
    return fp_t<int, 12>(a * (fp_t<int, 12>(1) - x)) + fp_t<int, 12>(b * x);
  }

  static fp_t<int, 12> pow4(fp_t<int, 12> x) {
    return fp_t<int, 12>(x * x) * fp_t<int, 12>(x * x);
  }

  static bool audioHandler(struct repeating_timer *t) {
    while(multicore_fifo_rvalid()) {
      uint32_t val = multicore_fifo_pop_blocking();

      // approximate nonlinear twiddle factor magic
      #define HW_OFFSET (fp_t<int, 12>(0.04))
      #define HW_COEF_A (fp_t<int, 12>(1.10988))
      #define HW_COEF_B (fp_t<int, 12>(0.96805))
      fp_t<int, 12> inVal = ((fp_t<int, 12>(val & 0x00FFFFFF)>>12) - HW_OFFSET) * HW_COEF_A;
      inVal = inVal * mix(fp_t<int, 12>(1), HW_COEF_B, fp_t<int, 12>(pow4(inVal)));
      inVal = inVal * fp_t<int, 0>(10) - fp_t<int, 0>(5);

      _tlwhw_->analogIn[val>>24] = inVal;
    }
    if(_audioCallback_ != NULL) _audioCallback_();
    return true;
  }

  static void core1Entry() {
    uint16_t activeAdcChannel = 0;
    uint32_t channelAccumulators[NUM_WORDS];
    adc_init();
    for(int i=0;i<NUM_WORDS;i++) {
      adc_gpio_init(CV_IN[i]);
      channelAccumulators[i] = 0;
    }
    while(1) {
      if(multicore_fifo_wready()) {
        const float smoothingFactor = 0.8;
        const int oversampling = 3;
        uint32_t temp = 0;
        for(int i=0;i<oversampling;i++) {
          temp += adc_read();
        }
        channelAccumulators[activeAdcChannel] = uint32_t(
          fp_t<int, 0>(channelAccumulators[activeAdcChannel]) * fp_t<int, 12>(1.0 - smoothingFactor) + \
          fp_t<int, 0>(temp) * fp_t<int, 12>(smoothingFactor / oversampling)
        );
        multicore_fifo_push_blocking(((channelAccumulators[activeAdcChannel])&0x00FFFFFF) | (activeAdcChannel<<24));
        if(++activeAdcChannel>=NUM_WORDS) activeAdcChannel = 0;
        adc_select_input(activeAdcChannel);
      }
    }
  }

  void Init(void (*audioCallback)(void)) {
    if(_tlwhw_ == NULL) {
      display = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE, 5, 4);
      display->setBusClock(400000);
      display->begin();
      for(int i=0; i<NUM_WORDS; i++) {
        control[i] = new ButtonAndEncoder(TOP_BTN_CCW[i], ENC_BTN_CW[i]);
        gpio_set_irq_enabled_with_callback(TOP_BTN_CCW[i], GPIO_IRQ_EDGE_FALL, true, &controlHandler);
        gpio_set_irq_enabled_with_callback(ENC_BTN_CW[i], GPIO_IRQ_EDGE_FALL, true, &controlHandler);
        trigIn[i]   = new GateTrigger(TRIG_IN[i]);
        analogIn[i] = 0;
        voctOut[i]  = new AnalogOut(VOCT_OFFSET[i], 240, VOCT_NOUT_MAX, VOCT_POUT_MAX);
        cvOut[i]    = new AnalogOut(CV_OFFSET[i], 240, CV_NOUT_MAX, CV_POUT_MAX);
      }

      multicore_launch_core1(core1Entry);

      this->_audioCallback_ = audioCallback;
      add_repeating_timer_us(-TIMER_INTERVAL, audioHandler, NULL, &_timer_);
      _tlwhw_ = this;
    }
  }

  void SetAudioCallback(void (*audioCallback)(void)) { _audioCallback_ = audioCallback; }

  void Update() {
    for(int i=0; i<NUM_WORDS; i++) {
      control[i]->Update();
    }
  }
};
TLWHardware* TLWHardware::_tlwhw_ = NULL;
void (*TLWHardware::_audioCallback_)(void) = NULL;

#endif