var Clay = require('@rebble/clay');
var clayConfig = require('./config');
// show the flat-color picker only while "Flat" is the selected mode
var clay = new Clay(clayConfig, function() {
  var cfg = this;
  cfg.on(cfg.EVENTS.AFTER_BUILD, function() {
    var mode = cfg.getItemByMessageKey('TIME_COLOR');
    var picker = cfg.getItemByMessageKey('FLAT_COLOR');
    if (!mode || !picker) {
      return;  // b/w watches: both items filtered out by capabilities
    }
    function toggle() {
      if (mode.get() === 'white') {
        picker.show();
      } else {
        picker.hide();
      }
    }
    mode.on('change', toggle);
    toggle();
  });
});
var getWeather = require('./weather');

Pebble.addEventListener('ready', getWeather);
// watch pings an empty message every 30 min to request a refresh
Pebble.addEventListener('appmessage', getWeather);
// refetch right away when units change on the config page (Clay's own
// webviewclosed handler runs first, so settings are saved by now)
Pebble.addEventListener('webviewclosed', getWeather);
