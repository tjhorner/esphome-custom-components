#pragma once

#include "esphome/core/component.h"
#include "esphome/core/version.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace project_version {

class ProjectVersionTextSensor : public text_sensor::TextSensor, public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;

#if ESPHOME_VERSION_CODE < VERSION_CODE(2025, 8, 0)
  std::string unique_id() override;
#endif

};

}  // namespace version
}  // namespace esphome
