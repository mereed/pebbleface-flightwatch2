Pebble.addEventListener("ready",
  function(e) {
    var time = Math.round((new Date()).getTime() / 1000);
    Pebble.sendAppMessage({"0": time});
  }
);


/*

function sendTimezoneToWatch() {
  // Get the number of seconds to add to convert localtime to utc
  var offsetMinutes = new Date().getTimezoneOffset() * 60;
  // Send it to the watch
  Pebble.sendAppMessage({ timezoneOffset: offsetMinutes })
}
*/