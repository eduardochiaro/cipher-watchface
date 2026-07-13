// Imperial when the user picked it on the config page (UNITS = 1), else metric.
// Clay persists the saved settings to localStorage under 'clay-settings'.
function isImperial() {
  try {
    var s = JSON.parse(localStorage.getItem('clay-settings')) || {};
    var u = s.UNITS;
    if (u && typeof u === 'object') { u = u.value; }
    return parseInt(u, 10) === 1;
  } catch (e) {
    return false;
  }
}

function fetchWeather(lat, lon, successCallback, errorCallback) {
  // Open-Meteo returns the values already in the requested unit, so the module
  // does the metric<->imperial translation here (C just renders the numbers).
  var units = isImperial()
    ? '&temperature_unit=fahrenheit&precipitation_unit=inch'
    : '';
  var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat + '&longitude=' + lon + '&current=temperature_2m,weather_code&forecast_days=1&timezone=auto' + units;

  var xhr = new XMLHttpRequest();
  
  xhr.onreadystatechange = function() {
    if (xhr.readyState === 4) {
      if (xhr.status === 200) {
        try {
          var data = JSON.parse(xhr.responseText);
          if (data.current) {
            var tempCelsius = Math.round(data.current.temperature_2m);
            // AppMessage carries int32s; round so floats don't get mangled.
            successCallback({
              temperature: tempCelsius, // in the user's unit (see isImperial)
              weatherCode: data.current.weather_code
            });
          } else {
            errorCallback('Invalid weather data received');
          }
        } catch (parseError) {
          errorCallback('JSON parse error: ' + parseError.message);
        }
      } else {
        errorCallback('HTTP error! status: ' + xhr.status);
      }
    }
  };
  
  xhr.onerror = function() {
    errorCallback('Network error occurred');
  };
  
  xhr.ontimeout = function() {
    errorCallback('Request timed out');
  };
  
  xhr.timeout = 10000; // 10 second timeout
  xhr.open('GET', url, true);
  xhr.send();
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(function(position) {
    fetchWeather(position.coords.latitude, position.coords.longitude, function(weatherData) {
      // Send weather data to C code
      Pebble.sendAppMessage({
        WEATHER_TEMPERATURE: weatherData.temperature, // already in the user's unit (see isImperial)
        WEATHER_CODE: weatherData.weatherCode, // Weather code for icon selections
      }, function() {
        console.log('Weather data sent to Pebble successfully');
      }, function(error) {
        console.log('Failed to send weather data to Pebble: ' + JSON.stringify(error));
      });
    }, function(error) {
      console.log('Failed to fetch weather data: ' + error);
    });   
  }, function(error) {
    console.log('Failed to get location: ' + error.message);
  }, { timeout: 15000, maximumAge: 1800000 }); // 30 minute cache
}

module.exports = getWeather;