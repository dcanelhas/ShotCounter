var reportIssue = function(err) {
  console.log('ShotCounter error: ' + JSON.stringify(err));
};

Pebble.addEventListener('ready', function(e) {
  console.log('ShotCounter JS Ready');
});

// Current values, populated from the watch before the config view opens.
var current = {
  THRESHOLD: 50,
  ROF_RPS: 3,
  THEME: 0,
  SHOW_MAG: 1,
  DEBUG_MODE: 0,
  MAG_CAP: 10
};

function buildHTML() {
  var html = '<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>ShotCounter Settings</title>' +
    '<style>body{font-family:sans-serif;padding:15px;background:#f4f4f4;} .card{background:#fff;padding:15px;border-radius:8px;margin-bottom:15px;box-shadow:0 1px 3px rgba(0,0,0,0.1);} label{font-weight:bold;display:block;margin-bottom:5px;margin-top:10px;} select,input{width:100%;padding:10px;margin-bottom:10px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box;} button{background:#ff4081;color:#fff;border:none;padding:12px;width:100%;border-radius:4px;font-size:16px;font-weight:bold;cursor:pointer;margin-top:15px;} .val{font-weight:normal;color:#666;float:right;} .hint{font-weight:normal;font-size:12px;color:#888;margin-top:-8px;margin-bottom:10px;}</style></head><body>' +
    '<h2>ShotCounter Settings</h2>' +
    '<div class="card">' +
    '<label>Recoil Threshold <span class="val" id="slider_val">' + current.THRESHOLD + '</span></label>' +
    '<input type="range" id="slider" min="0" max="100" step="5" value="' + current.THRESHOLD + '" oninput="document.getElementById(\'slider_val\').innerText = this.value;">' +
    '<label>Max Rate of Fire <span class="val" id="rof_val">' + current.ROF_RPS + ' RPS</span></label>' +
    '<input type="range" id="rof_slider" min="1" max="25" step="1" value="' + current.ROF_RPS + '" oninput="document.getElementById(\'rof_val\').innerText = this.value + \' RPS\';">' +
    '<label>Debug Energy Overlay</label>' +
    '<select id="debug_mode">' +
    '<option value="0"' + (current.DEBUG_MODE == 0 ? ' selected' : '') + '>Off</option>' +
    '<option value="1"' + (current.DEBUG_MODE == 1 ? ' selected' : '') + '>On (plot impulses and threshold)</option>' +
    '</select>' +
    '<label>Color Theme</label>' +
    '<select id="theme">' +
    '<option value="0"' + (current.THEME == 0 ? ' selected' : '') + '>Dark mono - white text, black bg</option>' +
    '<option value="1"' + (current.THEME == 1 ? ' selected' : '') + '>Fluorescent - cyan text, black bg</option>' +
    '<option value="2"' + (current.THEME == 2 ? ' selected' : '') + '>Terminal - green text, black bg</option>' +
    '<option value="3"' + (current.THEME == 3 ? ' selected' : '') + '>Light mono - black text, white bg</option>' +
    '<option value="4"' + (current.THEME == 4 ? ' selected' : '') + '>Dark red - red text, black bg</option>' +
    '</select>' +
    '<label>Display Magazine Info</label>' +
    '<select id="show_mag">' +
    '<option value="1"' + (current.SHOW_MAG == 1 ? ' selected' : '') + '>On</option>' +
    '<option value="0"' + (current.SHOW_MAG == 0 ? ' selected' : '') + '>Off</option>' +
    '</select>' +
    '<div class="hint">On shows your magazine count and capacity in the corner.</div>' +
    '<button onclick="save()">Save Settings</button>' +
    '</div>' +
    '<script>' +
    'function save(){' +
    '  var config = {' +
    '    THRESHOLD: parseInt(document.getElementById("slider").value, 10),' +
    '    ROF_RPS: parseInt(document.getElementById("rof_slider").value, 10),' +
    '    THEME: parseInt(document.getElementById("theme").value, 10),' +
    '    SHOW_MAG: parseInt(document.getElementById("show_mag").value, 10),' +
    '    DEBUG_MODE: parseInt(document.getElementById("debug_mode").value, 10)' +
    '  };' +
    '  location.href = "pebblejs://close#" + encodeURIComponent(JSON.stringify(config));' +
    '}' +
    '</script></body></html>';

  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
}

Pebble.addEventListener('showConfiguration', function(e) {
  // Ask the watch for its current settings, then populate the view once answered.
  // Open the config regardless after a short delay so it never stays blank
  // if the watch is slow to reply.
  pendingConfig = true;
  setTimeout(function() {
    if (pendingConfig) {
      pendingConfig = false;
      buildHTML();
    }
  }, 1200);
  Pebble.sendAppMessage({'GET_STATE': 1}, function() {
    console.log('Asked watch for current state');
  }, function(err) {
    reportIssue(err);
  });
});

// Watch replies with its current state.
var pendingConfig = false;
Pebble.addEventListener('appmessage', function(e) {
  var m = e.payload;
  if (m.THRESHOLD !== undefined) current.THRESHOLD = m.THRESHOLD;
  if (m.ROF_RPS !== undefined) current.ROF_RPS = m.ROF_RPS;
  if (m.THEME !== undefined) current.THEME = m.THEME;
  if (m.SHOW_MAG !== undefined) current.SHOW_MAG = m.SHOW_MAG;
  if (m.DEBUG_MODE !== undefined) current.DEBUG_MODE = m.DEBUG_MODE;
  if (m.MAG_CAP !== undefined) current.MAG_CAP = m.MAG_CAP;
  if (pendingConfig) {
    pendingConfig = false;
    buildHTML();
  }
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && e.response) {
    try {
      var dict = JSON.parse(decodeURIComponent(e.response));
      var payload = {};

      if (dict.THRESHOLD !== undefined) payload.THRESHOLD = parseInt(dict.THRESHOLD, 10);
      if (dict.THEME !== undefined) payload.THEME = parseInt(dict.THEME, 10);
      if (dict.SHOW_MAG !== undefined) payload.SHOW_MAG = parseInt(dict.SHOW_MAG, 10);
      if (dict.ROF_RPS !== undefined) payload.ROF_RPS = parseInt(dict.ROF_RPS, 10);
      if (dict.DEBUG_MODE !== undefined) payload.DEBUG_MODE = parseInt(dict.DEBUG_MODE, 10);

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
