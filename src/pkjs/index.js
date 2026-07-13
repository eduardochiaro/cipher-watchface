var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);
var getWeather = require('./weather');

Pebble.addEventListener('ready', getWeather);
// watch pings an empty message every 30 min to request a refresh
Pebble.addEventListener('appmessage', getWeather);
// refetch right away when units change on the config page (Clay's own
// webviewclosed handler runs first, so settings are saved by now)
Pebble.addEventListener('webviewclosed', getWeather);
