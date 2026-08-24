#pragma once
#include "dashboard.h"
#include "config.h"

struct ForecastHour {
  int   offset_h;     // hours from "now" per publisher (typical: 1, 2, 3, 5, 8)
  float temp_f;
  float feels_like_f;
  int   precip_prob;
  int   weather_code;
  float wind_mph;
  int   wind_dir_deg;
  int   cloud_cover_pct;
  float uv_index;
  bool  is_day;
};

class WeatherDashboard : public Dashboard {
public:
  enum Mode { MODE_NOW, MODE_FORECAST };

  explicit WeatherDashboard(Mode m) : mode(m) {}

  const char* topic() const override { return "dashboard/forecast"; }
  const char* name()  const override {
    return mode == MODE_NOW ? "Weather (Now)" : "Weather (Forecast)";
  }
  void handlePayload(JsonDocument& doc) override;
  void render() override;

  enum Profile {
    PROF_WINDY, PROF_STORM, PROF_SNOW, PROF_RAIN, PROF_FOG,
    PROF_CLEAR_DAY, PROF_CLEAR_NIGHT, PROF_PARTLY_CLOUDY, PROF_OVERCAST
  };

private:
  Mode mode;

  String city      = "—";
  String model     = "";
  String updatedAt = "";

  // Headline (hours[0], typically +1H per the publisher's offset_h)
  String currentTime        = "";   // hours[0].time, ISO local "YYYY-MM-DDTHH:MM"
  float  currentTemp        = 0;
  float  feelsLike          = 0;
  int    currentCode        = -1;
  float  currentWindMph     = 0;
  int    currentPrecipPb    = 0;
  float  currentPrecipIn    = 0;
  int    currentHumidityPct = 0;
  float  currentDewPointF   = 0;
  int    currentCloudPct    = 0;
  float  currentVisibilityM = 0;
  float  currentUvIndex     = 0;
  float  currentWindGustMph = 0;
  int    currentWindDirDeg  = 0;
  float  currentPressureHpa = 0;
  float  currentSnowIn      = 0;
  bool   currentIsDay       = true;

  // Today (days[0]). hi/lo are shown on the Now view; the rest power the
  // daily-stats panel at the bottom of the Forecast view.
  bool   hasDay           = false;
  float  dayHighF         = 0;
  float  dayLowF          = 0;
  String daySunrise       = "";
  String daySunset        = "";
  float  dayWindMax       = 0;
  int    dayWindDirDom    = 0;
  float  dayUvMax         = 0;
  int    dayPrecipProbMax = 0;

  ForecastHour forecast[config::FORECAST_HOURS_SHOWN];
  int forecastCount = 0;

  // Mode renderers
  void renderNow();
  void renderForecast();

  // Profile + helpers
  Profile detectProfile() const;
  void drawContextStats(int x);
  void drawSunriseSunsetFooter();
  void drawWaiting();
};
