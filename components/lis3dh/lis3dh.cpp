#include "lis3dh.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lis3dh {

static const char *const TAG = "lis3dh";

static const float GRAVITY_EARTH = 9.80665f;

/// Cooldown between repeated trigger events (ms)
static const uint32_t EVENT_COOLDOWN_MS = 500;

/// Sensitivity in g per digit (after raw >> 4) indexed by Range enum value.
/// From the LIS3DH datasheet (high-resolution 12-bit mode):
///   ±2g  →  1 mg/digit
///   ±4g  →  2 mg/digit
///   ±8g  →  4 mg/digit
///   ±16g → 12 mg/digit
/// These values are also correct for 10-bit and 8-bit modes when using raw >> 4,
/// because the lower bits are simply zero in those modes.
static const float SENSITIVITY[] = {0.001f, 0.002f, 0.004f, 0.012f};

// ---- String helpers for dump_config ----

static const char *range_to_string(Range range) {
  switch (range) {
    case Range::RANGE_2G:
      return "±2g";
    case Range::RANGE_4G:
      return "±4g";
    case Range::RANGE_8G:
      return "±8g";
    case Range::RANGE_16G:
      return "±16g";
    default:
      return "Unknown";
  }
}

static const char *data_rate_to_string(DataRate dr) {
  switch (dr) {
    case DataRate::ODR_POWER_DOWN:
      return "Power Down";
    case DataRate::ODR_1HZ:
      return "1 Hz";
    case DataRate::ODR_10HZ:
      return "10 Hz";
    case DataRate::ODR_25HZ:
      return "25 Hz";
    case DataRate::ODR_50HZ:
      return "50 Hz";
    case DataRate::ODR_100HZ:
      return "100 Hz";
    case DataRate::ODR_200HZ:
      return "200 Hz";
    case DataRate::ODR_400HZ:
      return "400 Hz";
    default:
      return "Unknown";
  }
}

static const char *resolution_to_string(Resolution res) {
  switch (res) {
    case Resolution::RES_LOW_POWER:
      return "Low Power (8-bit)";
    case Resolution::RES_NORMAL:
      return "Normal (10-bit)";
    case Resolution::RES_HIGH_RES:
      return "High Resolution (12-bit)";
    default:
      return "Unknown";
  }
}

static const char *orientation_xy_to_string(OrientationXY o) {
  switch (o) {
    case OrientationXY::PORTRAIT_UPRIGHT:
      return "Portrait Upright";
    case OrientationXY::PORTRAIT_UPSIDE_DOWN:
      return "Portrait Upside Down";
    case OrientationXY::LANDSCAPE_LEFT:
      return "Landscape Left";
    case OrientationXY::LANDSCAPE_RIGHT:
      return "Landscape Right";
    default:
      return "Unknown";
  }
}

static const char *orientation_z_to_string(bool z) { return z ? "Downwards looking" : "Upwards looking"; }

// ---- Setup ----

void LIS3DHComponent::setup() {
  // Verify chip ID
  uint8_t chip_id{0};
  if (!this->read_byte(static_cast<uint8_t>(RegisterMap::WHO_AM_I), &chip_id) || chip_id != LIS3DH_CHIP_ID) {
    ESP_LOGE(TAG, "WHO_AM_I register returned 0x%02X, expected 0x%02X", chip_id, LIS3DH_CHIP_ID);
    this->mark_failed();
    return;
  }

  // Calculate sensitivity from range
  this->sensitivity_ = SENSITIVITY[static_cast<uint8_t>(this->range_)];

  if (!this->configure_ctrl_regs_()) {
    ESP_LOGE(TAG, "Failed to configure control registers");
    this->mark_failed();
    return;
  }

  if (!this->configure_click_detection_()) {
    ESP_LOGW(TAG, "Failed to configure click detection");
  }

  if (!this->configure_freefall_detection_()) {
    ESP_LOGW(TAG, "Failed to configure freefall detection");
  }

  if (!this->configure_orientation_detection_()) {
    ESP_LOGW(TAG, "Failed to configure orientation detection");
  }
}

bool LIS3DHComponent::configure_ctrl_regs_() {
  // CTRL_REG1: data rate, low-power mode, enable all axes
  RegCtrl1 ctrl1;
  ctrl1.odr = this->data_rate_;
  ctrl1.low_power = (this->resolution_ == Resolution::RES_LOW_POWER);
  ctrl1.x_enable = true;
  ctrl1.y_enable = true;
  ctrl1.z_enable = true;
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::CTRL_REG1), ctrl1.raw)) {
    return false;
  }

  // CTRL_REG4: full-scale range, high-resolution bit, block data update
  RegCtrl4 ctrl4;
  ctrl4.bdu = true;
  ctrl4.fs = this->range_;
  ctrl4.high_res = (this->resolution_ == Resolution::RES_HIGH_RES);
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::CTRL_REG4), ctrl4.raw)) {
    return false;
  }

  // CTRL_REG5: latch interrupt requests on INT1 and INT2 source registers
  RegCtrl5 ctrl5;
  ctrl5.lir_int1 = true;
  ctrl5.lir_int2 = true;
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::CTRL_REG5), ctrl5.raw)) {
    return false;
  }

  return true;
}

bool LIS3DHComponent::configure_click_detection_() {
  // Enable single and double click detection on all three axes
  RegClickCfg click_cfg;
  click_cfg.x_single = true;
  click_cfg.x_double = true;
  click_cfg.y_single = true;
  click_cfg.y_double = true;
  click_cfg.z_single = true;
  click_cfg.z_double = true;
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::CLICK_CFG), click_cfg.raw)) {
    return false;
  }

  // Click threshold — aim for ~0.625g across all ranges.
  // Threshold LSB = full_scale_mg / 128.
  //   ±2g  → 16 mg/LSB → 40 LSB ≈ 0.64g
  //   ±4g  → 32 mg/LSB → 20 LSB ≈ 0.64g
  //   ±8g  → 62 mg/LSB → 10 LSB ≈ 0.62g
  //   ±16g → 125 mg/LSB → 5 LSB ≈ 0.63g
  uint8_t click_ths;
  switch (this->range_) {
    case Range::RANGE_2G:
      click_ths = 40;
      break;
    case Range::RANGE_4G:
      click_ths = 20;
      break;
    case Range::RANGE_8G:
      click_ths = 10;
      break;
    case Range::RANGE_16G:
      click_ths = 5;
      break;
    default:
      click_ths = 40;
      break;
  }
  // Bit 7 = LIR_Click (latch the click interrupt until CLICK_SRC is read)
  uint8_t click_ths_reg = (click_ths & 0x7F) | 0x80;
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::CLICK_THS), click_ths_reg)) {
    return false;
  }

  // TIME_LIMIT: max interval between click start and end (in 1/ODR)
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::TIME_LIMIT), 15)) {
    return false;
  }

  // TIME_LATENCY: dead zone after single click before double-click window (in 1/ODR)
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::TIME_LATENCY), 20)) {
    return false;
  }

  // TIME_WINDOW: window in which second click must arrive for double-click (in 1/ODR)
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::TIME_WINDOW), 50)) {
    return false;
  }

  return true;
}

bool LIS3DHComponent::configure_freefall_detection_() {
  // INT1 generator: freefall = AND combination, all axes below threshold
  RegIntCfg int1_cfg;
  int1_cfg.aoi = true;
  int1_cfg.sixd = false;
  int1_cfg.x_low = true;
  int1_cfg.y_low = true;
  int1_cfg.z_low = true;
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::INT1_CFG), int1_cfg.raw)) {
    return false;
  }

  // Freefall threshold — aim for ~350 mg.
  // Threshold LSB = full_scale_mg / 128.
  uint8_t ff_ths;
  switch (this->range_) {
    case Range::RANGE_2G:
      ff_ths = 22;
      break;
    case Range::RANGE_4G:
      ff_ths = 11;
      break;
    case Range::RANGE_8G:
      ff_ths = 6;
      break;
    case Range::RANGE_16G:
      ff_ths = 3;
      break;
    default:
      ff_ths = 22;
      break;
  }
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::INT1_THS), ff_ths & 0x7F)) {
    return false;
  }

  // Duration: minimum time the condition must hold (in 1/ODR)
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::INT1_DUR), 3)) {
    return false;
  }

  return true;
}

bool LIS3DHComponent::configure_orientation_detection_() {
  // INT2 generator: 6D movement detection (OR combination with 6D flag)
  RegIntCfg int2_cfg;
  int2_cfg.aoi = false;
  int2_cfg.sixd = true;
  int2_cfg.x_low = true;
  int2_cfg.x_high = true;
  int2_cfg.y_low = true;
  int2_cfg.y_high = true;
  int2_cfg.z_low = true;
  int2_cfg.z_high = true;
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::INT2_CFG), int2_cfg.raw)) {
    return false;
  }

  // Orientation threshold — aim for ~400 mg
  uint8_t orient_ths;
  switch (this->range_) {
    case Range::RANGE_2G:
      orient_ths = 26;
      break;
    case Range::RANGE_4G:
      orient_ths = 13;
      break;
    case Range::RANGE_8G:
      orient_ths = 6;
      break;
    case Range::RANGE_16G:
      orient_ths = 3;
      break;
    default:
      orient_ths = 26;
      break;
  }
  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::INT2_THS), orient_ths & 0x7F)) {
    return false;
  }

  if (!this->write_byte(static_cast<uint8_t>(RegisterMap::INT2_DUR), 0)) {
    return false;
  }

  return true;
}

// ---- dump_config ----

void LIS3DHComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LIS3DH:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, ESP_LOG_MSG_COMM_FAIL);
  }
  ESP_LOGCONFIG(TAG,
                "  Range: %s\n"
                "  Data Rate: %s\n"
                "  Resolution: %s",
                range_to_string(this->range_), data_rate_to_string(this->data_rate_),
                resolution_to_string(this->resolution_));
  LOG_UPDATE_INTERVAL(this);

#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Acceleration X", this->acceleration_x_sensor_);
  LOG_SENSOR("  ", "Acceleration Y", this->acceleration_y_sensor_);
  LOG_SENSOR("  ", "Acceleration Z", this->acceleration_z_sensor_);
#endif

#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "Orientation XY", this->orientation_xy_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Orientation Z", this->orientation_z_text_sensor_);
#endif
}

// ---- Data reading ----

bool LIS3DHComponent::read_data_() {
  uint8_t accel_data[6];

  // Multi-byte I2C read requires the auto-increment bit (0x80) set on the sub-address
  if (!this->read_bytes(static_cast<uint8_t>(RegisterMap::OUT_X_L) | I2C_AUTO_INCREMENT, accel_data, 6)) {
    return false;
  }

  // Raw data is left-justified in 16 bits. Shift right by 4 to obtain the
  // 12-bit-equivalent value (lower bits are zero in 10-bit and 8-bit modes).
  int16_t raw_x = static_cast<int16_t>((accel_data[1] << 8) | accel_data[0]) >> 4;
  int16_t raw_y = static_cast<int16_t>((accel_data[3] << 8) | accel_data[2]) >> 4;
  int16_t raw_z = static_cast<int16_t>((accel_data[5] << 8) | accel_data[4]) >> 4;

  // Convert to m/s² with simple single-pole low-pass filter (α = 0.5)
  auto lpf = [](float new_val, float old_val) -> float { return 0.5f * new_val + 0.5f * old_val; };

  this->data_.x = lpf(raw_x * this->sensitivity_ * GRAVITY_EARTH, this->data_.x);
  this->data_.y = lpf(raw_y * this->sensitivity_ * GRAVITY_EARTH, this->data_.y);
  this->data_.z = lpf(raw_z * this->sensitivity_ * GRAVITY_EARTH, this->data_.z);

  return true;
}

// ---- Event polling ----

void LIS3DHComponent::poll_click_source_() {
  RegClickSrc click_src;
  // Reading CLICK_SRC clears the latched interrupt
  if (!this->read_byte(static_cast<uint8_t>(RegisterMap::CLICK_SRC), &click_src.raw)) {
    return;
  }

  if (!click_src.ia) {
    return;
  }

  uint32_t now = millis();

  if (click_src.single_click && (now - this->status_.last_tap_ms > EVENT_COOLDOWN_MS)) {
    ESP_LOGV(TAG, "Single tap detected");
    this->tap_trigger_.trigger();
    this->status_.last_tap_ms = now;
  }

  if (click_src.double_click && (now - this->status_.last_double_tap_ms > EVENT_COOLDOWN_MS)) {
    ESP_LOGV(TAG, "Double tap detected");
    this->double_tap_trigger_.trigger();
    this->status_.last_double_tap_ms = now;
  }
}

void LIS3DHComponent::poll_int1_source_() {
  RegIntSrc int1_src;
  // Reading INT1_SRC clears the latched interrupt
  if (!this->read_byte(static_cast<uint8_t>(RegisterMap::INT1_SRC), &int1_src.raw)) {
    return;
  }

  if (!int1_src.ia) {
    return;
  }

  uint32_t now = millis();
  if (now - this->status_.last_freefall_ms > EVENT_COOLDOWN_MS) {
    ESP_LOGV(TAG, "Freefall detected");
    this->freefall_trigger_.trigger();
    this->status_.last_freefall_ms = now;
  }
}

void LIS3DHComponent::poll_int2_source_() {
  RegIntSrc int2_src;
  // Reading INT2_SRC clears the latched interrupt
  if (!this->read_byte(static_cast<uint8_t>(RegisterMap::INT2_SRC), &int2_src.raw)) {
    return;
  }

  if (!int2_src.ia) {
    return;
  }

  uint32_t now = millis();
  if (now - this->status_.last_orientation_ms > EVENT_COOLDOWN_MS) {
    ESP_LOGV(TAG, "Orientation change detected");
    this->orientation_trigger_.trigger();
    this->status_.last_orientation_ms = now;
  }
}

// ---- Main loop & update ----

void LIS3DHComponent::loop() {
  if (!this->is_ready()) {
    return;
  }

  if (!this->read_data_()) {
    this->status_set_warning();
    return;
  }

  this->poll_click_source_();
  this->poll_int1_source_();
  this->poll_int2_source_();

  this->status_clear_warning();
}

void LIS3DHComponent::update() {
  if (!this->is_ready()) {
    return;
  }

  ESP_LOGV(TAG, "Acceleration: {x = %+1.3f m/s², y = %+1.3f m/s², z = %+1.3f m/s²}", this->data_.x, this->data_.y,
           this->data_.z);

#ifdef USE_SENSOR
  if (this->acceleration_x_sensor_ != nullptr)
    this->acceleration_x_sensor_->publish_state(this->data_.x);
  if (this->acceleration_y_sensor_ != nullptr)
    this->acceleration_y_sensor_->publish_state(this->data_.y);
  if (this->acceleration_z_sensor_ != nullptr)
    this->acceleration_z_sensor_->publish_state(this->data_.z);
#endif

#ifdef USE_TEXT_SENSOR
  // Derive orientation from current acceleration data
  float abs_x = fabsf(this->data_.x);
  float abs_y = fabsf(this->data_.y);
  float abs_z = fabsf(this->data_.z);

  OrientationXY new_xy;
  if (abs_x > abs_y) {
    new_xy = (this->data_.x > 0) ? OrientationXY::LANDSCAPE_RIGHT : OrientationXY::LANDSCAPE_LEFT;
  } else {
    new_xy = (this->data_.y > 0) ? OrientationXY::PORTRAIT_UPRIGHT : OrientationXY::PORTRAIT_UPSIDE_DOWN;
  }
  bool new_z = (this->data_.z < 0);  // true = downwards looking

  if (this->orientation_xy_text_sensor_ != nullptr &&
      (new_xy != this->status_.orientation_xy || this->status_.never_published)) {
    this->orientation_xy_text_sensor_->publish_state(orientation_xy_to_string(new_xy));
  }
  if (this->orientation_z_text_sensor_ != nullptr &&
      (new_z != this->status_.orientation_z || this->status_.never_published)) {
    this->orientation_z_text_sensor_->publish_state(orientation_z_to_string(new_z));
  }

  this->status_.orientation_xy = new_xy;
  this->status_.orientation_z = new_z;
  this->status_.never_published = false;
#endif
}

float LIS3DHComponent::get_setup_priority() const { return setup_priority::DATA; }

}  // namespace lis3dh
}  // namespace esphome
