#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/automation.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace lis3dh {

/// LIS3DH chip ID returned by WHO_AM_I register
static const uint8_t LIS3DH_CHIP_ID = 0x33;

/// I2C auto-increment flag — must be OR'd into the register address for multi-byte reads
static const uint8_t I2C_AUTO_INCREMENT = 0x80;

// ---- Register Map ----

enum class RegisterMap : uint8_t {
  WHO_AM_I = 0x0F,

  CTRL_REG1 = 0x20,
  CTRL_REG2 = 0x21,
  CTRL_REG3 = 0x22,
  CTRL_REG4 = 0x23,
  CTRL_REG5 = 0x24,
  CTRL_REG6 = 0x25,
  REFERENCE = 0x26,
  STATUS_REG = 0x27,

  OUT_X_L = 0x28,
  OUT_X_H = 0x29,
  OUT_Y_L = 0x2A,
  OUT_Y_H = 0x2B,
  OUT_Z_L = 0x2C,
  OUT_Z_H = 0x2D,

  FIFO_CTRL = 0x2E,
  FIFO_SRC = 0x2F,

  INT1_CFG = 0x30,
  INT1_SRC = 0x31,
  INT1_THS = 0x32,
  INT1_DUR = 0x33,

  INT2_CFG = 0x34,
  INT2_SRC = 0x35,
  INT2_THS = 0x36,
  INT2_DUR = 0x37,

  CLICK_CFG = 0x38,
  CLICK_SRC = 0x39,
  CLICK_THS = 0x3A,
  TIME_LIMIT = 0x3B,
  TIME_LATENCY = 0x3C,
  TIME_WINDOW = 0x3D,
};

// ---- Configuration Enums ----

enum class Range : uint8_t {
  RANGE_2G = 0b00,
  RANGE_4G = 0b01,
  RANGE_8G = 0b10,
  RANGE_16G = 0b11,
};

enum class DataRate : uint8_t {
  ODR_POWER_DOWN = 0b0000,
  ODR_1HZ = 0b0001,
  ODR_10HZ = 0b0010,
  ODR_25HZ = 0b0011,
  ODR_50HZ = 0b0100,
  ODR_100HZ = 0b0101,
  ODR_200HZ = 0b0110,
  ODR_400HZ = 0b0111,
};

enum class Resolution : uint8_t {
  RES_LOW_POWER = 0,  // 8-bit  (LPen=1, HR=0)
  RES_NORMAL = 1,     // 10-bit (LPen=0, HR=0)
  RES_HIGH_RES = 2,   // 12-bit (LPen=0, HR=1)
};

// ---- Register Bitfield Structures ----

// CTRL_REG1 (0x20)
union RegCtrl1 {
  struct {
    bool x_enable : 1;   // bit 0  — X-axis enable
    bool y_enable : 1;   // bit 1  — Y-axis enable
    bool z_enable : 1;   // bit 2  — Z-axis enable
    bool low_power : 1;  // bit 3  — Low-power mode enable (LPen)
    DataRate odr : 4;     // bit 7:4 — Output data rate
  } __attribute__((packed));
  uint8_t raw{0x00};
};

// CTRL_REG3 (0x22) — Interrupt control on INT1 pin
union RegCtrl3 {
  struct {
    uint8_t unused : 1;   // bit 0
    bool i1_overrun : 1;  // bit 1 — FIFO overrun on INT1
    bool i1_wtm : 1;      // bit 2 — FIFO watermark on INT1
    bool i1_drdy2 : 1;    // bit 3 — DRDY2 on INT1
    bool i1_drdy1 : 1;    // bit 4 — DRDY1 on INT1
    bool i1_aoi2 : 1;     // bit 5 — AOI2 on INT1
    bool i1_aoi1 : 1;     // bit 6 — AOI1 on INT1
    bool i1_click : 1;    // bit 7 — Click on INT1
  } __attribute__((packed));
  uint8_t raw{0x00};
};

// CTRL_REG4 (0x23)
union RegCtrl4 {
  struct {
    bool spi_3wire : 1;     // bit 0   — SPI 3-wire mode (SIM)
    uint8_t self_test : 2;  // bit 2:1 — Self-test enable
    bool high_res : 1;      // bit 3   — High-resolution output (HR)
    Range fs : 2;            // bit 5:4 — Full-scale selection
    bool ble : 1;            // bit 6   — Big/little endian
    bool bdu : 1;            // bit 7   — Block data update
  } __attribute__((packed));
  uint8_t raw{0x00};
};

// CTRL_REG5 (0x24)
union RegCtrl5 {
  struct {
    bool d4d_int2 : 1;   // bit 0 — 4D on INT2
    bool lir_int2 : 1;   // bit 1 — Latch INT2
    bool d4d_int1 : 1;   // bit 2 — 4D on INT1
    bool lir_int1 : 1;   // bit 3 — Latch INT1
    uint8_t unused : 2;  // bit 5:4
    bool fifo_en : 1;    // bit 6 — FIFO enable
    bool boot : 1;        // bit 7 — Reboot memory
  } __attribute__((packed));
  uint8_t raw{0x00};
};

// INTx_CFG (0x30 / 0x34) — Interrupt generator configuration
union RegIntCfg {
  struct {
    bool x_low : 1;   // bit 0 — Enable X below threshold (XLIE)
    bool x_high : 1;  // bit 1 — Enable X above threshold (XHIE)
    bool y_low : 1;   // bit 2 — Enable Y below threshold (YLIE)
    bool y_high : 1;  // bit 3 — Enable Y above threshold (YHIE)
    bool z_low : 1;   // bit 4 — Enable Z below threshold (ZLIE)
    bool z_high : 1;  // bit 5 — Enable Z above threshold (ZHIE)
    bool sixd : 1;     // bit 6 — 6-direction detection (6D)
    bool aoi : 1;      // bit 7 — AND/OR combination (AOI)
  } __attribute__((packed));
  uint8_t raw{0x00};
};

// INTx_SRC (0x31 / 0x35) — Interrupt generator source
union RegIntSrc {
  struct {
    bool x_low : 1;      // bit 0
    bool x_high : 1;     // bit 1
    bool y_low : 1;      // bit 2
    bool y_high : 1;     // bit 3
    bool z_low : 1;      // bit 4
    bool z_high : 1;     // bit 5
    bool ia : 1;          // bit 6 — Interrupt active
    uint8_t unused : 1;  // bit 7
  } __attribute__((packed));
  uint8_t raw{0x00};
};

// CLICK_CFG (0x38)
union RegClickCfg {
  struct {
    bool x_single : 1;   // bit 0
    bool x_double : 1;   // bit 1
    bool y_single : 1;   // bit 2
    bool y_double : 1;   // bit 3
    bool z_single : 1;   // bit 4
    bool z_double : 1;   // bit 5
    uint8_t unused : 2;  // bit 7:6
  } __attribute__((packed));
  uint8_t raw{0x00};
};

// CLICK_SRC (0x39)
union RegClickSrc {
  struct {
    bool x : 1;             // bit 0 — X click
    bool y : 1;             // bit 1 — Y click
    bool z : 1;             // bit 2 — Z click
    bool sign : 1;          // bit 3 — Click sign (0=positive, 1=negative)
    bool single_click : 1;  // bit 4 — Single click detected
    bool double_click : 1;  // bit 5 — Double click detected
    bool ia : 1;             // bit 6 — Interrupt active
    uint8_t unused : 1;     // bit 7
  } __attribute__((packed));
  uint8_t raw{0x00};
};

// ---- Orientation (derived from acceleration data) ----

enum class OrientationXY : uint8_t {
  PORTRAIT_UPRIGHT = 0,
  PORTRAIT_UPSIDE_DOWN = 1,
  LANDSCAPE_LEFT = 2,
  LANDSCAPE_RIGHT = 3,
};

// ---- Component Class ----

class LIS3DHComponent : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override;
  void update() override;
  float get_setup_priority() const override;

  void set_range(Range range) { this->range_ = range; }
  void set_data_rate(DataRate data_rate) { this->data_rate_ = data_rate; }
  void set_resolution(Resolution resolution) { this->resolution_ = resolution; }

#ifdef USE_SENSOR
  SUB_SENSOR(acceleration_x)
  SUB_SENSOR(acceleration_y)
  SUB_SENSOR(acceleration_z)
#endif

#ifdef USE_TEXT_SENSOR
  SUB_TEXT_SENSOR(orientation_xy)
  SUB_TEXT_SENSOR(orientation_z)
#endif

  Trigger<> *get_tap_trigger() { return &this->tap_trigger_; }
  Trigger<> *get_double_tap_trigger() { return &this->double_tap_trigger_; }
  Trigger<> *get_freefall_trigger() { return &this->freefall_trigger_; }
  Trigger<> *get_orientation_trigger() { return &this->orientation_trigger_; }

 protected:
  Range range_{Range::RANGE_2G};
  DataRate data_rate_{DataRate::ODR_100HZ};
  Resolution resolution_{Resolution::RES_HIGH_RES};

  /// Sensitivity in g per digit (after right-shifting raw 16-bit value by 4)
  float sensitivity_{0.001f};

  struct {
    float x{0};
    float y{0};
    float z{0};
  } data_{};

  struct {
    uint32_t last_tap_ms{0};
    uint32_t last_double_tap_ms{0};
    uint32_t last_freefall_ms{0};
    uint32_t last_orientation_ms{0};
    OrientationXY orientation_xy{OrientationXY::PORTRAIT_UPRIGHT};
    bool orientation_z{false};
    bool never_published{true};
  } status_{};

  bool configure_ctrl_regs_();
  bool configure_click_detection_();
  bool configure_freefall_detection_();
  bool configure_orientation_detection_();

  bool read_data_();
  void poll_click_source_();
  void poll_int1_source_();
  void poll_int2_source_();

  Trigger<> tap_trigger_;
  Trigger<> double_tap_trigger_;
  Trigger<> freefall_trigger_;
  Trigger<> orientation_trigger_;
};

}  // namespace lis3dh
}  // namespace esphome
