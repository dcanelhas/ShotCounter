Pebble.addEventListener('ready', function(e) {
  console.log('ShotCounter JS Ready');
});

Pebble.addEventListener('showConfiguration', function(e) {
  var html = '<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>ShotCounter Settings</title>' +
    '<style>body{font-family:sans-serif;padding:15px;background:#f4f4f4;} .card{background:#fff;padding:15px;border-radius:8px;margin-bottom:15px;box-shadow:0 1px 3px rgba(0,0,0,0.1);} label{font-weight:bold;display:block;margin-bottom:5px;margin-top:10px;} select,input{width:100%;padding:10px;margin-bottom:10px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box;} button{background:#ff4081;color:#fff;border:none;padding:12px;width:100%;border-radius:4px;font-size:16px;font-weight:bold;cursor:pointer;margin-top:15px;} .val{font-weight:normal;color:#666;float:right;}</style></head><body>' +
    '<h2>ShotCounter Settings</h2>' +
    '<div class="card">' +
    '<label>Recoil Threshold <span class="val" id="slider_val">50</span></label>' +
    '<input type="range" id="slider" min="0" max="100" step="5" value="50" oninput="document.getElementById(\'slider_val\').innerText = this.value;">' +
    '<label>Max Rate of Fire <span class="val" id="rof_val">3 RPS</span></label>' +
    '<input type="range" id="rof_slider" min="1" max="25" step="1" value="3" oninput="document.getElementById(\'rof_val\').innerText = this.value + \' RPS\';">' +
    '<label>Debug Energy Overlay</label>' +
    '<select id="debug_mode">' +
    '<option value="0">Off</option>' +
    '<option value="1">On (Show color-coded per-axis energy)</option>' +
    '</select>' +
    '<label>Color Theme</label>' +
    '<select id="theme">' +
    '<option value="0">Dark mono - white text, black bg</option>' +
    '<option value="1">Fluorescent - cyan text, black bg</option>' +
    '<option value="2">Terminal - green text, black bg</option>' +
    '<option value="3">Light mono - black text, white bg</option>' +
    '<option value="4">Dark red - red text, black bg</option>' +
    '</select>' +
    '<label>Display Magazine Info</label>' +
    '<select id="show_mag">' +
    '<option value="1">Enabled (Show Mags & Mag Cap)</option>' +
    '<option value="0">Disabled (Clean / Large Counter)</option>' +
    '</select>' +
    '<button onclick="save()">Save Settings</button>' +
    '</div>' +
    '<script>' +
    'function save(){' +
    '  var thrVal = parseInt(document.getElementById("slider").value, 10);' +
    '  var rofVal = parseInt(document.getElementById("rof_slider").value, 10);' +
    '  var config = {' +
    '    THRESHOLD: thrVal,' +
    '    ROF_RPS: rofVal,' +
    '    THEME: parseInt(document.getElementById("theme").value, 10),' +
    '    SHOW_MAG: parseInt(document.getElementById("show_mag").value, 10),' +
    '    DEBUG_MODE: parseInt(document.getElementById("debug_mode").value, 10)' +
    '  };' +
    '  location.href = "pebblejs://close#" + encodeURIComponent(JSON.stringify(config));' +
    '}' +
    '</script></body></html>';

  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && e.response) {
    try {
      var dict = JSON.parse(decodeURIComponent(e.response));
      var payload = {};
      
      if (dict.THRESHOLD !== undefined) {
        payload[10000] = parseInt(dict.THRESHOLD, 10);
      }
      if (dict.THEME !== undefined) {
        payload[10001] = parseInt(dict.THEME, 10);
      }
      if (dict.SHOW_MAG !== undefined) {
        payload[10002] = parseInt(dict.SHOW_MAG, 10);
      }
      if (dict.ROF_RPS !== undefined) {
        payload[10003] = parseInt(dict.ROF_RPS, 10);
      }
      if (dict.DEBUG_MODE !== undefined) {
        payload[10004] = parseInt(dict.DEBUG_MODE, 10);
      }
      
      Pebble.sendAppMessage(payload, function() {
        console.log('Settings successfully sent to watch!');
      }, function(err) {
        console.log('Error sending settings to watch: ' + JSON.stringify(err));
      });
    } catch (err) {
      console.log('Error parsing settings response: ' + err);
    }
  }
});
