#pragma once
#include "graphics/sprite.h"

namespace icons {
  // 32x32 weather icons (used by both Now and Forecast views)
  extern const sprite::Sprite SUN_BIG;
  extern const sprite::Sprite PARTLY_CLOUDY_BIG;
  extern const sprite::Sprite CLOUDY_BIG;
  extern const sprite::Sprite RAIN_BIG;
  extern const sprite::Sprite HEAVY_RAIN_BIG;
  extern const sprite::Sprite SNOW_BIG;
  extern const sprite::Sprite THUNDERSTORM_BIG;
  extern const sprite::Sprite FOG_BIG;

  // Pick the right 32x32 icon for a WMO weather code
  const sprite::Sprite& forWeatherCodeBig(int code);

  // Small 8x8 utility icons
  extern const sprite::Sprite SUN_RISING;   // sun above horizon (for sunrise time)
  extern const sprite::Sprite SUN_SETTING;  // sun below horizon (for sunset time)
}