// Global state
let logAutoScroll = true;
let currentSection = 'bridge';
let otaApiKey = null; // Store OTA API key

// Navigation functions
function switchSection(sectionId) {
  // Hide all sections
  document.querySelectorAll('.section-content').forEach(section => {
    section.classList.remove('active');
  });
  
  // Show selected section
  const targetSection = document.getElementById(sectionId + '-section');
  if (targetSection) {
    targetSection.classList.add('active');
  }
  
  // Update desktop menu items
  document.querySelectorAll('.menu-item').forEach(item => {
    item.classList.remove('active');
  });
  
  // Update mobile menu items
  document.querySelectorAll('.mobile-menu-item').forEach(item => {
    item.classList.remove('active');
  });
  
  // Set active menu item (both desktop and mobile)
  const targetMenuItem = document.querySelector(`.menu-item[data-section="${sectionId}"]`);
  const targetMobileMenuItem = document.querySelector(`.mobile-menu-item[data-section="${sectionId}"]`);
  
  if (targetMenuItem) {
    targetMenuItem.classList.add('active');
  }
  if (targetMobileMenuItem) {
    targetMobileMenuItem.classList.add('active');
  }
  
  currentSection = sectionId;
  
  // Close mobile menu
  closeMobileMenu();
}

function toggleMobileMenu() {
  const mobileMenu = document.getElementById('mobile-menu');
  const menuToggle = document.getElementById('menu-toggle');
  
  if (mobileMenu && menuToggle) {
    mobileMenu.classList.toggle('show');
    menuToggle.classList.toggle('active');
    
    // Prevent body scroll when menu is open
    if (mobileMenu.classList.contains('show')) {
      document.body.style.overflow = 'hidden';
    } else {
      document.body.style.overflow = '';
    }
  }
}

function closeMobileMenu() {
  const mobileMenu = document.getElementById('mobile-menu');
  const menuToggle = document.getElementById('menu-toggle');
  
  if (mobileMenu && menuToggle) {
    mobileMenu.classList.remove('show');
    menuToggle.classList.remove('active');
    document.body.style.overflow = '';
  }
}

function initNavigation() {
  // Add click handlers for desktop menu items
  document.querySelectorAll('.menu-item').forEach(item => {
    item.addEventListener('click', () => {
      const sectionId = item.dataset.section;
      switchSection(sectionId);
    });
  });
  
  // Add click handlers for mobile menu items
  document.querySelectorAll('.mobile-menu-item').forEach(item => {
    item.addEventListener('click', () => {
      const sectionId = item.dataset.section;
      switchSection(sectionId);
    });
  });
  
  // Add click handler for mobile menu toggle
  const menuToggle = document.getElementById('menu-toggle');
  if (menuToggle) {
    menuToggle.addEventListener('click', toggleMobileMenu);
  }
  
  // Close mobile menu when clicking on overlay
  document.addEventListener('click', (e) => {
    const mobileMenu = document.getElementById('mobile-menu');
    const mobileMenuContent = mobileMenu ? mobileMenu.querySelector('.mobile-menu-content') : null;
    const menuToggle = document.getElementById('menu-toggle');
    
    if (mobileMenu && menuToggle && 
        mobileMenu.classList.contains('show') &&
        !menuToggle.contains(e.target)) {
      
      // If clicking on the overlay (not the content), close the menu
      if (e.target === mobileMenu || (!mobileMenuContent || !mobileMenuContent.contains(e.target))) {
        closeMobileMenu();
      }
    }
  });
  
  // Initialize with the Bridge section active
  switchSection('bridge');
}

// Node name editing functions
function startEditingNodeName(nameElement) {
  const container = nameElement.closest('.node-name-container');
  const input = container.querySelector('.node-name-input');
  const editButtons = container.querySelector('.edit-buttons');
  
  // Hide the name span and show input + buttons
  nameElement.style.display = 'none';
  input.style.display = 'inline-block';
  editButtons.style.display = 'flex';
  
  // Focus and select the input text
  input.focus();
  input.select();
  
  // Add keydown handler for Enter/Escape
  input.addEventListener('keydown', handleEditKeydown);
}

function handleEditKeydown(e) {
  if (e.key === 'Enter') {
    e.preventDefault();
    acceptNameEdit(e.target);
  } else if (e.key === 'Escape') {
    e.preventDefault();
    discardNameEdit(e.target);
  }
}

function acceptNameEdit(input) {
  const container = input.closest('.node-name-container');
  const nameElement = container.querySelector('.node-name');
  const editButtons = container.querySelector('.edit-buttons');
  const nodeEl = container.closest('.node');
  const uuid = nodeEl.dataset.uuid;
  const newName = input.value.trim();
  
  if (!newName) {
    showToast('Please enter a name', 'warning');
    return;
  }
  
  // Disable buttons during API call
  const acceptBtn = container.querySelector('.accept-btn');
  const discardBtn = container.querySelector('.discard-btn');
  acceptBtn.disabled = true;
  discardBtn.disabled = true;
  acceptBtn.textContent = '⏳';
  
  // Make API call to rename node
  fetch("/node/rename", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ uuid, name: newName })
  }).then(res => {
    if (res.ok) {
      // Update the display name and original name
      nameElement.textContent = newName;
      nameElement.dataset.originalName = newName;
      finishEditing(container, false);
      showToast('Node renamed successfully', 'success');
    } else {
      showToast('Failed to rename node', 'error');
      finishEditing(container, true);
    }
  }).catch(err => {
    showToast('Network error renaming node', 'error');
    finishEditing(container, true);
  });
}

function discardNameEdit(input) {
  const container = input.closest('.node-name-container');
  finishEditing(container, true);
}

function finishEditing(container, restore) {
  const nameElement = container.querySelector('.node-name');
  const input = container.querySelector('.node-name-input');
  const editButtons = container.querySelector('.edit-buttons');
  const acceptBtn = container.querySelector('.accept-btn');
  const discardBtn = container.querySelector('.discard-btn');
  
  if (restore) {
    // Restore original value
    const originalName = nameElement.dataset.originalName;
    input.value = originalName;
  }
  
  // Show name span, hide input and buttons
  nameElement.style.display = 'inline';
  input.style.display = 'none';
  editButtons.style.display = 'none';
  
  // Reset buttons
  acceptBtn.disabled = false;
  discardBtn.disabled = false;
  acceptBtn.textContent = '✓';
  
  // Remove keydown handler
  input.removeEventListener('keydown', handleEditKeydown);
}

// Utility functions
function showToast(message, type = 'info') {
  console.log(`[${type.toUpperCase()}] ${message}`);

  const container = document.getElementById('toast-container');
  if (!container) return;

  const icons = { success: '✓', error: '✕', warning: '⚠', info: 'ℹ' };
  const toast = document.createElement('div');
  toast.className = `toast toast-${type}`;
  toast.innerHTML = `<span class="toast-icon">${icons[type] || icons.info}</span><span class="toast-msg"></span>`;
  toast.querySelector('.toast-msg').textContent = message;
  container.appendChild(toast);

  // enter
  requestAnimationFrame(() => toast.classList.add('show'));

  const remove = () => {
    toast.classList.remove('show');
    toast.addEventListener('transitionend', () => toast.remove(), { once: true });
    setTimeout(() => toast.remove(), 400);
  };
  toast.addEventListener('click', remove);
  setTimeout(remove, type === 'error' ? 6000 : 3500);
}

// Collapsible log dock
function toggleLogDock(forceOpen) {
  const dock = document.getElementById('log-dock');
  if (!dock) return;
  const collapsed = forceOpen === true ? false
    : forceOpen === false ? true
    : !dock.classList.contains('collapsed');
  dock.classList.toggle('collapsed', collapsed);
  try { localStorage.setItem('logDockCollapsed', collapsed ? '1' : '0'); } catch (e) {}

  if (!collapsed) {
    const out = document.getElementById('log-output');
    const auto = document.getElementById('auto-scroll');
    if (out && (!auto || auto.checked)) out.scrollTop = out.scrollHeight;
  }
}

function initLogDock() {
  const dock = document.getElementById('log-dock');
  if (!dock) return;
  let collapsed = true;
  try { collapsed = localStorage.getItem('logDockCollapsed') !== '0'; } catch (e) {}
  dock.classList.toggle('collapsed', collapsed);

  // Restore a previously dragged height, then wire up the resize handle.
  const out = document.getElementById('log-output');
  const resizer = document.getElementById('log-dock-resizer');
  if (!out || !resizer) return;

  const clampH = h => Math.max(140, Math.min(Math.round(window.innerHeight * 0.85), h));

  try {
    const saved = parseInt(localStorage.getItem('logDockHeight'), 10);
    if (saved) out.style.height = clampH(saved) + 'px';
  } catch (e) {}

  let startY = 0, startH = 0, dragging = false;

  const onMove = e => {
    if (!dragging) return;
    // drag up => taller
    out.style.height = clampH(startH + (startY - e.clientY)) + 'px';
  };
  const onUp = () => {
    if (!dragging) return;
    dragging = false;
    document.body.style.userSelect = '';
    window.removeEventListener('pointermove', onMove);
    window.removeEventListener('pointerup', onUp);
    try { localStorage.setItem('logDockHeight', parseInt(out.style.height, 10)); } catch (e) {}
  };

  resizer.addEventListener('pointerdown', e => {
    if (dock.classList.contains('collapsed')) return;
    dragging = true;
    startY = e.clientY;
    startH = out.getBoundingClientRect().height;
    document.body.style.userSelect = 'none';
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
    e.preventDefault();
  });
}

function formatUptime(uptimeSeconds) {
  const days = Math.floor(uptimeSeconds / (24 * 3600));
  const hours = Math.floor((uptimeSeconds % (24 * 3600)) / 3600);
  const minutes = Math.floor((uptimeSeconds % 3600) / 60);
  
  return `${days.toString().padStart(2, '0')}d ${hours.toString().padStart(2, '0')}h ${minutes.toString().padStart(2, '0')}m`;
}

function updateBridgeStatus(systemData) {
  // Update connection status to green when we successfully get data
  const connectionStatus = document.getElementById('connection-status');
  if (connectionStatus) {
    connectionStatus.style.color = 'var(--success)';
  }
  
  // Update uptime
  if (systemData.uptime !== undefined) {
    const uptimeElement = document.getElementById('bridge-uptime');
    if (uptimeElement) {
      const uptimeSeconds = Math.floor(systemData.uptime / 1000000); // Convert microseconds to seconds
      uptimeElement.textContent = formatUptime(uptimeSeconds);
    }
  } else {
    // Fallback if uptime is not available
    const uptimeElement = document.getElementById('bridge-uptime');
    if (uptimeElement && uptimeElement.textContent === '00d 00h 00m') {
      uptimeElement.textContent = '--d --h --m';
    }
  }
  
  // Update memory
  if (systemData.memory) {
    const headerMemoryElement = document.getElementById('header-memory');
    if (headerMemoryElement) {
      const freeMemoryKB = Math.round(systemData.memory.free / 1024);
      headerMemoryElement.textContent = `${freeMemoryKB} KB`;
    }
  }
}

function updateNodeCount(count) {
  const counter = document.getElementById("node-count");
  if (counter) counter.textContent = `${count} provisioned`;
}

function updateDeviceCount(count) {
  const counter = document.getElementById("device-count");
  if (counter) counter.textContent = `${count} unprovisioned`;
}

function toggleEmptyState(containerId, emptyStateId, hasItems) {
  const container = document.getElementById(containerId);
  const emptyState = document.getElementById(emptyStateId);
  
  if (container && emptyState) {
    if (hasItems) {
      container.style.display = 'grid';
      emptyState.classList.remove('show');
    } else {
      container.style.display = 'none';
      emptyState.classList.add('show');
    }
  }
}

// --- Node control helpers -------------------------------------------------

// One shared 200 ms throttle, keyed per control so dragging one slider does not
// starve another. Only the most recent value per key is sent.
const _throttle = {};
function throttlePost(key, fn) {
  const st = _throttle[key] || (_throttle[key] = { timer: null, last: 0, pending: null });
  st.pending = fn;
  if (st.timer) return;
  const wait = Math.max(0, 200 - (Date.now() - st.last));
  st.timer = setTimeout(() => {
    const p = st.pending;
    st.pending = null;
    st.last = Date.now();
    st.timer = null;
    if (p) p();
  }, wait);
}

function postNode(path, body) {
  return fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body
  });
}

// Map a value from one range to another (integer result).
function rangeMap(v, lo, hi, olo, ohi) {
  return hi > lo ? Math.round((v - lo) * (ohi - olo) / (hi - lo) + olo) : olo;
}

// Lightness / brightness
function onSliderInput(uuid, el) {
  const output = el.parentElement.querySelector('output');
  if (output) output.value = el.value;
  throttlePost('lightness-' + uuid, () => sendLightness(uuid, el.value));
}

function sendLightness(uuid, value) {
  postNode("/node/set_lightness", `uuid=${encodeURIComponent(uuid)}&lightness=${encodeURIComponent(value)}`)
    .then(response => {
      if (!response.ok) showToast('Failed to set lightness', 'error');
    })
    .catch(() => showToast('Network error setting lightness', 'error'));
}

// Collapsible per-node control panel
function toggleNodeControls(btn) {
  const panel = btn.closest('.node').querySelector('.node-controls');
  if (!panel) return;
  panel.hidden = !panel.hidden;
  btn.classList.toggle('active', !panel.hidden);
}

// Power (Generic OnOff)
function setNodeOnoff(uuid, on) {
  postNode('/node/set_onoff', `uuid=${encodeURIComponent(uuid)}&onoff=${on ? 1 : 0}`)
    .then(r => {
      if (!r.ok) showToast('Failed to set power', 'error');
    })
    .catch(() => showToast('Network error setting power', 'error'));
}

// Colour (Light HSL) — the card carries H (0-360) and S (0-100) sliders; the
// lightness sent is whatever the card's brightness slider currently shows.
function onHslInput(uuid, el) {
  const nodeEl = el.closest('.node');
  const panel = nodeEl.querySelector('.node-controls');
  const h = +panel.querySelector('.hsl-h').value;
  const s = +panel.querySelector('.hsl-s').value;
  const brightEl = nodeEl.querySelector('.brightness-slider');
  const l = brightEl ? +brightEl.value : (+nodeEl.dataset.maxLightness || 65535);

  const swatch = panel.querySelector('.hsl-swatch');
  if (swatch) swatch.style.background = `hsl(${h}, ${s}%, 50%)`;
  const out = el.parentElement.querySelector('output');
  if (out) out.value = el.classList.contains('hsl-h') ? `${h}°` : `${s}%`;

  throttlePost('hsl-' + uuid, () =>
    postNode('/node/set_hsl', `uuid=${encodeURIComponent(uuid)}&h=${h}&s=${s}&l=${l}`)
      .then(r => { if (!r.ok) showToast('Failed to set colour', 'error'); })
      .catch(() => showToast('Network error setting colour', 'error')));
}

// Colour temperature (Light CTL)
function onTempInput(uuid, el) {
  const out = el.parentElement.querySelector('output');
  if (out) out.value = el.value + ' K';
  throttlePost('temp-' + uuid, () =>
    postNode('/node/set_temperature', `uuid=${encodeURIComponent(uuid)}&kelvin=${el.value}`)
      .then(r => { if (!r.ok) showToast('Failed to set temperature', 'error'); })
      .catch(() => showToast('Network error setting temperature', 'error')));
}

// Node management functions
function unprovision(uuid) {
  if (!confirm("Are you sure you want to unprovision this device? This will remove it from the mesh network.")) {
    return;
  }

  const button = event.target;
  button.disabled = true;
  button.textContent = 'Unprovisioning...';

  fetch("/node/unprovision", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}`
  }).then(response => {
    if (response.ok) {
      showToast('Device unprovisioned successfully', 'success');
      setTimeout(() => location.reload(), 1000);
    } else {
      showToast('Failed to unprovision device', 'error');
      button.disabled = false;
      button.textContent = 'Unprovision';
    }
  }).catch(err => {
    showToast('Network error during unprovisioning', 'error');
    button.disabled = false;
    button.textContent = 'Unprovision';
  });
}

function provision(uuid) {
  const button = event.target;
  button.disabled = true;
  button.textContent = 'Provisioning...';

  fetch("/node/provision", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}`
  }).then(response => {
    if (response.ok) {
      showToast('Device provisioned successfully', 'success');
      setTimeout(() => location.reload(), 1000);
    } else {
      showToast('Failed to provision device', 'error');
      button.disabled = false;
      button.textContent = 'Provision';
    }
  }).catch(err => {
    showToast('Network error during provisioning', 'error');
    button.disabled = false;
    button.textContent = 'Provision';
  });
}

// MQTT functions
function sendMqttStatus(uuid) {
  const button = event.target;
  const originalText = button.textContent;
  button.disabled = true;
  button.textContent = 'Sending...';

  fetch("/node/send_mqtt_status", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}`
  }).then(response => {
    if (response.ok) {
      showToast('MQTT status sent', 'success');
    } else {
      showToast('Failed to send MQTT status', 'error');
    }
  }).catch(err => {
    showToast('Network error sending MQTT status', 'error');
  }).finally(() => {
    button.disabled = false;
    button.textContent = originalText;
  });
}

function sendMqttDiscovery(uuid) {
  const button = event.target;
  const originalText = button.textContent;
  button.disabled = true;
  button.textContent = 'Sending...';

  fetch("/node/send_mqtt_discovery", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}`
  }).then(response => {
    if (response.ok) {
      showToast('MQTT discovery sent', 'success');
    } else {
      showToast('Failed to send MQTT discovery', 'error');
    }
  }).catch(err => {
    showToast('Network error sending MQTT discovery', 'error');
  }).finally(() => {
    button.disabled = false;
    button.textContent = originalText;
  });
}

// Bridge control functions
function sendBridgeMqttDiscovery() {
  const button = event.target;
  const originalText = button.innerHTML;
  button.disabled = true;
  button.innerHTML = '<span class="icon">⏳</span> Sending...';

  fetch("/mqtt/bridge_discovery", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: ""
  }).then(response => {
    if (response.ok) {
      showToast('Bridge MQTT discovery sent', 'success');
    } else {
      showToast('Failed to send bridge MQTT discovery', 'error');
    }
  }).catch(err => {
    showToast('Network error sending bridge discovery', 'error');
  }).finally(() => {
    button.disabled = false;
    button.innerHTML = originalText;
  });
}

function sendBridgeMqttStatus() {
  const button = event.target;
  const originalText = button.innerHTML;
  button.disabled = true;
  button.innerHTML = '<span class="icon">⏳</span> Sending...';

  fetch("/mqtt/bridge_status", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: ""
  }).then(response => {
    if (response.ok) {
      showToast('Bridge MQTT status sent', 'success');
    } else {
      showToast('Failed to send bridge MQTT status', 'error');
    }
  }).catch(err => {
    showToast('Network error sending bridge status', 'error');
  }).finally(() => {
    button.disabled = false;
    button.innerHTML = originalText;
  });
}

function restartBridge() {
  const message = "🔄 RESTART BRIDGE 🔄\n\n" +
                  "This action will:\n" +
                  "• Restart the ESP32 device\n" +
                  "• Disconnect all current connections\n" +
                  "• Reload all configurations\n\n" +
                  "Are you sure you want to proceed?";

  if (confirm(message)) {
    const button = event.target;
    button.disabled = true;
    button.innerHTML = '<span class="icon">⏳</span> Restarting...';
    
    // Close WebSocket connection cleanly before restart
    if (currentWebSocket) {
      currentWebSocket.close();
      currentWebSocket = null;
    }
    
    // Clear any existing reconnection timers
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    isReconnecting = false;
    
    fetch("/bridge/restart", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: ""
    }).then(() => {
      showToast('Bridge restart initiated', 'info');
      // Wait longer for ESP32 to fully restart (10 seconds instead of 5)
      setTimeout(() => {
        window.location.reload();
      }, 10000);
    }).catch(err => {
      showToast('Bridge restarting...', 'info');
      // Wait longer for ESP32 to fully restart (10 seconds instead of 5)
      setTimeout(() => {
        window.location.reload();
      }, 10000);
    });
  }
}

function resetWiFi() {
  const message = "⚠️ RESET WiFi SETTINGS ⚠️\n\n" +
                  "This action will:\n" +
                  "• Clear all stored WiFi credentials\n" +
                  "• Restart the ESP32 device\n" +
                  "• Enter captive portal setup mode\n" +
                  "• Require you to reconfigure WiFi access\n\n" +
                  "Are you sure you want to proceed?";
                  
  if (confirm(message)) {
    const button = event.target;
    button.disabled = true;
    button.innerHTML = '<span class="icon">⏳</span> Resetting...';
    
    fetch("/bridge/reset_wifi?" + Date.now(), {
      method: "POST",
      headers: { 
        "Content-Type": "application/x-www-form-urlencoded",
        "Cache-Control": "no-cache"
      },
      body: ""
    }).then(() => {
      showToast('WiFi reset initiated', 'info');
    }).catch(err => {
      showToast('WiFi reset initiated, device restarting...', 'info');
    });
  }
}

function loadNodes() {
  return fetch("/nodes.json")
    .then(res => res.json())
    .then(data => {
      const nodesContainer = document.getElementById("nodes");
      const unprovisionedContainer = document.getElementById("unprovisioned");
      if (!nodesContainer || !unprovisionedContainer) return;

      nodesContainer.innerHTML = '';
      unprovisionedContainer.innerHTML = '';

      const provisioned = data.provisioned || [];
      const unprovisioned = data.unprovisioned || [];

      provisioned.forEach(node => nodesContainer.appendChild(createNodeElement(node)));
      toggleEmptyState('nodes', 'no-nodes', provisioned.length > 0);
      updateNodeCount(provisioned.length);

      unprovisioned.forEach(device => unprovisionedContainer.appendChild(createDeviceElement(device)));
      toggleEmptyState('unprovisioned', 'no-devices', unprovisioned.length > 0);
      updateDeviceCount(unprovisioned.length);
    })
    .catch(err => {
      console.error('Failed to load nodes:', err);
      showToast('Failed to load device data', 'error');
    });
}

function refreshDevices() {
  const button = event.target.closest('button');
  const originalHtml = button.innerHTML;
  button.disabled = true;
  button.innerHTML = '<span class="icon">⏳</span> Refreshing...';

  loadNodes().finally(() => {
    button.disabled = false;
    button.innerHTML = originalHtml;
    showToast('Device list refreshed', 'info');
  });
}

// Log management functions
function clearLogs() {
  const logOutput = document.getElementById("log-output");
  if (logOutput) {
    logOutput.innerHTML = '';
    showToast('Logs cleared', 'info');
  }
}

// Node rendering functions
function createNodeElement(node) {
  const el = document.createElement("div");
  el.className = "node";
  el.dataset.uuid = node.uuid;

  // Feature bitmask (mirrors node_supported_features_t in ble_mesh_control.h).
  const F_ONOFF = 1, F_LIGHTNESS = 2, F_HSL = 4, F_CTL = 8;
  const f = node.features || 0;
  const hasLightness = f === 0 || (f & (F_LIGHTNESS | F_CTL));
  const hasOnoff = f === 0 || (f & F_ONOFF);
  const hasHsl = !!(f & F_HSL);
  const hasCtl = !!(f & F_CTL);
  const hasControls = hasOnoff || hasHsl || hasCtl;

  const maxL = node.max_lightness || 65535;
  el.dataset.maxLightness = maxL;

  const hue360 = rangeMap(node.hsl_h || 0, node.min_hue || 0, node.max_hue || 65535, 0, 360);
  const sat100 = rangeMap(node.hsl_s || 0, node.min_saturation || 0, node.max_saturation || 65535, 0, 100);
  const tMin = (node.min_temp && node.min_temp < node.max_temp) ? node.min_temp : 2000;
  const tMax = (node.max_temp && node.max_temp > tMin && node.max_temp < 20000) ? node.max_temp : 6500;
  const tCur = node.curr_temp || tMin;

  el.innerHTML = `
    <div class="node-header">
      <div class="node-name-container">
        <span class="node-name editable" data-original-name="${node.name}">${node.name}</span>
        <input type="text" class="node-name-input" value="${node.name}" style="display: none;">
        <div class="edit-buttons" style="display: none;">
          <button class="btn btn-primary btn-small accept-btn" title="Accept">✓</button>
          <button class="btn btn-secondary btn-small discard-btn" title="Discard">✕</button>
        </div>
      </div>
      <span class="node-status ${node.unicast ? 'online' : 'offline'}">
        ${node.unicast ? 'Online' : 'Offline'}
      </span>
    </div>

    <div class="node-info-grid">
      <div class="info-row">
        <span class="info-label">UUID:</span>
        <span class="info-value">${node.uuid}</span>
      </div>
      <div class="info-row">
        <span class="info-label">Address:</span>
        <span class="info-value">${node.unicast || 'Not assigned'}</span>
      </div>
      ${node.company ? `<div class="info-row">
        <span class="info-label">Manufacturer:</span>
        <span class="info-value">${node.company}</span>
      </div>` : ''}
    </div>

    <div class="lightness-control slider-row"${hasLightness ? '' : ' hidden'}>
      <span class="slider-tag">💡</span>
      <input type="range" class="brightness-slider" min="0" max="${maxL}" step="500" value="${node.hsl_l || 0}"
        oninput="onSliderInput('${node.uuid}', this)">
      <output>${node.hsl_l || 0}</output>
    </div>

    <div class="controls">
      ${hasControls ? `<button class="btn btn-secondary btn-small node-controls-toggle" onclick="toggleNodeControls(this)">
        <span class="icon">🎛️</span>
        Controls
      </button>` : ''}
      <button class="btn btn-secondary btn-small" onclick="sendMqttStatus('${node.uuid}')">
        <span class="icon">📊</span>
        MQTT Status
      </button>
      <button class="btn btn-secondary btn-small" onclick="sendMqttDiscovery('${node.uuid}')">
        <span class="icon">📡</span>
        MQTT Discovery
      </button>
      <button class="btn btn-danger btn-small" onclick="unprovision('${node.uuid}')">
        <span class="icon">🗑️</span>
        Unprovision
      </button>
    </div>

    ${hasControls ? `
    <div class="node-controls" hidden>
      ${hasOnoff ? `
      <div class="control-row">
        <span class="control-label">Power</span>
        <label class="toggle-switch">
          <input type="checkbox" ${node.onoff ? 'checked' : ''} onchange="setNodeOnoff('${node.uuid}', this.checked)">
          <span class="toggle-track"></span>
        </label>
      </div>` : ''}
      ${hasHsl ? `
      <div class="control-row">
        <span class="control-label">Color</span>
        <span class="hsl-swatch" style="background: hsl(${hue360}, ${sat100}%, 50%)"></span>
      </div>
      <div class="slider-row">
        <span class="slider-tag">H</span>
        <input type="range" class="hsl-h hue-slider" min="0" max="360" value="${hue360}" oninput="onHslInput('${node.uuid}', this)">
        <output>${hue360}°</output>
      </div>
      <div class="slider-row">
        <span class="slider-tag">S</span>
        <input type="range" class="hsl-s" min="0" max="100" value="${sat100}" oninput="onHslInput('${node.uuid}', this)">
        <output>${sat100}%</output>
      </div>` : ''}
      ${hasCtl ? `
      <div class="slider-row">
        <span class="slider-tag">🌡</span>
        <input type="range" class="temp-slider" min="${tMin}" max="${tMax}" step="50" value="${tCur}" oninput="onTempInput('${node.uuid}', this)">
        <output>${tCur} K</output>
      </div>` : ''}
    </div>` : ''}
  `;

  return el;
}

function createDeviceElement(device) {
  const el = document.createElement("div");
  el.className = "device";
  
  el.innerHTML = `
    <div class="node-info">
      <div class="info-item">
        <span class="info-label">UUID</span>
        <span class="info-value">${device.uuid}</span>
      </div>
      <div class="info-item">
        <span class="info-label">RSSI</span>
        <span class="info-value">${device.rssi} dBm</span>
      </div>
    </div>
    
    <div class="controls">
      <button class="btn btn-primary" onclick="provision('${device.uuid}')">
        <span class="icon">➕</span>
        Provision
      </button>
    </div>
  `;
  
  return el;
}

function createCommandElement(command) {
  const el = document.createElement("div");
  el.className = "command-item";
  
  el.innerHTML = `
    <div class="command-name">${command.name}</div>
    <div class="command-help">${command.help}</div>
  `;
  
  return el;
}

// WebSocket log handling
let reconnectTimer = null;
let isReconnecting = false;
let currentWebSocket = null;

function startLogSocket() {
  if (isReconnecting || currentWebSocket) return;
  
  // Use secure WebSocket if page is served over HTTPS
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const wsUrl = protocol + "//" + location.host + "/ws/logs";
  
  const ws = new WebSocket(wsUrl);
  currentWebSocket = ws;
  const logOutput = document.getElementById("log-output");
  const autoScrollCheckbox = document.getElementById("auto-scroll");

  ws.onopen = () => {
    isReconnecting = false;
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
  };

  ws.onmessage = event => {
    if (!logOutput) return;
    
    const line = event.data;
    let cls = "log-default";
    
    if (line.startsWith('E')) cls = "log-error";
    else if (line.startsWith('W')) cls = "log-warning";
    else if (line.startsWith('I')) cls = "log-info";

    const div = document.createElement("div");
    div.className = cls;
    div.textContent = line;
    logOutput.appendChild(div);
    
    // Auto-scroll if enabled
    if (autoScrollCheckbox && autoScrollCheckbox.checked) {
      logOutput.scrollTop = logOutput.scrollHeight;
    }
  };

  ws.onclose = (event) => {
    console.log('WebSocket closed:', event.code, event.reason);
    currentWebSocket = null;
    if (!isReconnecting) {
      isReconnecting = true;
      console.log('Scheduling WebSocket reconnection in 3 seconds...');
      reconnectTimer = setTimeout(() => {
        console.log('Attempting WebSocket reconnection...');
        isReconnecting = false; // Reset flag before attempting
        startLogSocket();
      }, 3000); // Wait 3 seconds before reconnecting
    }
  };

  ws.onerror = (error) => {
    currentWebSocket = null;
    ws.close();
  };
  
  // Handle auto-scroll checkbox
  if (autoScrollCheckbox && !autoScrollCheckbox.hasEventListener) {
    autoScrollCheckbox.addEventListener('change', (e) => {
      logAutoScroll = e.target.checked;
    });
    autoScrollCheckbox.hasEventListener = true;
  }
}

// Cleanup WebSocket on page unload
window.addEventListener('beforeunload', function() {
  if (currentWebSocket) {
    console.log("Closing WebSocket before page unload");
    currentWebSocket.close();
    currentWebSocket = null;
  }
});

// WiFi info functions
function loadWifiInfo() {
  fetch("/api/wifi_info")
    .then(res => res.json())
    .then(data => {
      updateWifiStatus(data);
    })
    .catch(err => {
      console.error('Failed to load WiFi info:', err);
      // Show offline status if WiFi info fails
      const wifiState = document.getElementById('wifi-state');
      if (wifiState) {
        wifiState.textContent = 'Error';
        wifiState.className = 'status-value network_error';
      }
    });
}

function updateWifiStatus(wifiData) {
  // Update status display
  const wifiState = document.getElementById('wifi-state');
  if (wifiState) {
    wifiState.textContent = capitalizeFirst(wifiData.status || 'Unknown');
    wifiState.className = `status-value ${wifiData.status || 'unknown'}`;
  }
  
  // Update SSID
  const sssidElement = document.getElementById('wifi-ssid');
  if (sssidElement) {
    sssidElement.textContent = wifiData.ssid || '--';
  }
  
  // Update IP Address
  const ipElement = document.getElementById('wifi-ip');
  if (ipElement) {
    ipElement.textContent = wifiData.ip || '--';
  }
  
  // Update Subnet Mask
  const netmaskElement = document.getElementById('wifi-netmask');
  if (netmaskElement) {
    netmaskElement.textContent = wifiData.netmask || '--';
  }
  
  // Update Gateway
  const gatewayElement = document.getElementById('wifi-gateway');
  if (gatewayElement) {
    gatewayElement.textContent = wifiData.gateway || '--';
  }
  
  // Update MAC Address
  const macElement = document.getElementById('wifi-mac');
  if (macElement) {
    macElement.textContent = wifiData.mac || '--';
  }

  // Update RSSI
  const rssiElement = document.getElementById('wifi-rssi');
  if (rssiElement) {
    rssiElement.textContent = (wifiData.rssi !== undefined && wifiData.rssi !== -999)
      ? `${wifiData.rssi} dBm`
      : '--';
  }

  // Update TX power display and sync slider
  if (wifiData.tx_power !== undefined) {
    const txPowerEl = document.getElementById('wifi-tx-power');
    if (txPowerEl) txPowerEl.textContent = `${wifiData.tx_power} dBm`;

    const slider = document.getElementById('wifi-tx-power-slider');
    const preview = document.getElementById('wifi-tx-power-preview');
    if (slider) slider.value = wifiData.tx_power;
    if (preview) preview.textContent = `${wifiData.tx_power} dBm`;
  }
}

function setWifiTxPower() {
  const slider = document.getElementById('wifi-tx-power-slider');
  if (!slider) return;
  const dbm = parseInt(slider.value, 10);

  fetch('/api/wifi_power', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ tx_power: dbm })
  })
    .then(res => res.json())
    .then(data => {
      const txPowerEl = document.getElementById('wifi-tx-power');
      if (txPowerEl) txPowerEl.textContent = `${data.tx_power} dBm`;
      const preview = document.getElementById('wifi-tx-power-preview');
      if (preview) preview.textContent = `${data.tx_power} dBm`;
    })
    .catch(err => console.error('Failed to set TX power:', err));
}

function capitalizeFirst(str) {
  return str.charAt(0).toUpperCase() + str.slice(1);
}

// System info functions
function loadSystemInfo() {
  fetch("/api/system_info")
    .then(res => res.json())
    .then(data => {
      // Update bridge status in header
      updateBridgeStatus(data);
      
      // Update detailed memory info in Bridge section
      if (data.memory) {
        const freeMemoryKB = Math.round(data.memory.free / 1024);
        const totalMemoryKB = Math.round(data.memory.total / 1024);
        
        const freeMemoryEl = document.getElementById("free-memory");
        const totalMemoryEl = document.getElementById("total-memory");
        
        if (freeMemoryEl) freeMemoryEl.textContent = `${freeMemoryKB} KB`;
        if (totalMemoryEl) totalMemoryEl.textContent = `${totalMemoryKB} KB`;
      }

      // Update version information in Bridge section
      updateVersionInfo(data);
    })
    .catch(err => {
      console.error('Failed to load system info:', err);
      // Show offline status if system info fails
      const connectionStatus = document.getElementById('connection-status');
      if (connectionStatus) {
        connectionStatus.style.color = 'var(--danger)';
      }
    });
}

function updateVersionInfo(data) {
  // Update firmware version (legacy version field)
  const firmwareVersionEl = document.getElementById("firmware-version");
  if (firmwareVersionEl && data.version) {
    firmwareVersionEl.textContent = data.version;
  }

  // Show version next to the header title
  const appVersionEl = document.getElementById("app-version");
  if (appVersionEl && data.version) {
    appVersionEl.textContent = `(v${data.version})`;
  }

  // Update git version
  const gitVersionEl = document.getElementById("git-version");
  if (gitVersionEl && data.git_version) {
    gitVersionEl.textContent = data.git_version;
  }

  // Update project name
  const projectNameEl = document.getElementById("project-name");
  if (projectNameEl && data.project) {
    projectNameEl.textContent = data.project;
  }

  // Update ESP-IDF version
  const idfVersionEl = document.getElementById("idf-version");
  if (idfVersionEl && data.idf_version) {
    idfVersionEl.textContent = data.idf_version;
  }

  // Update build date
  const buildDateEl = document.getElementById("build-date-full");
  if (buildDateEl && data.build_date) {
    buildDateEl.textContent = data.build_date;
  }

  // Update build time
  const buildTimeEl = document.getElementById("build-time");
  if (buildTimeEl && data.build_time) {
    buildTimeEl.textContent = data.build_time;
  }

  // Update the header build date status (short format)
  const headerBuildDateEl = document.getElementById("build-date");
  if (headerBuildDateEl && data.build_date) {
    headerBuildDateEl.textContent = data.build_date;
  }
}

// MQTT Configuration functions
function loadMqttStatus() {
  fetch("/mqtt/status")
    .then(res => res.json())
    .then(data => {
      const statusEl = document.getElementById("mqtt-state");
      if (statusEl) {
        statusEl.textContent = data.state;
        statusEl.className = `status-value ${data.state}`;
      }
      
      // Populate form if configured
      if (data.configured) {
        document.getElementById("broker-host").value = data.broker_host || '';
        document.getElementById("broker-port").value = data.broker_port || 1883;
        document.getElementById("username").value = data.username || '';
        //document.getElementById("use-ssl").checked = data.use_ssl || false;
        // Don't populate password for security
      }
      
      // Show error if any
      if (data.last_error) {
        showMqttError(data.last_error);
      }
    })
    .catch(err => {
      console.error('Failed to load MQTT status:', err);
      const statusEl = document.getElementById("mqtt-state");
      if (statusEl) {
        statusEl.textContent = 'error';
        statusEl.className = 'status-value network_error';
      }
    });
}

function showMqttError(message) {
  const errorEl = document.getElementById("mqtt-error");
  if (errorEl) {
    errorEl.textContent = message;
    errorEl.style.display = 'block';
    setTimeout(() => {
      errorEl.style.display = 'none';
    }, 5000);
  }
}

function togglePassword() {
  const passwordInput = document.getElementById("password");
  const toggleBtn = document.querySelector(".toggle-password");
  
  if (passwordInput.type === "password") {
    passwordInput.type = "text";
    toggleBtn.textContent = "🙈";
  } else {
    passwordInput.type = "password";
    toggleBtn.textContent = "👁️";
  }
}

function clearCredentials() {
  if (!confirm("Are you sure you want to clear MQTT credentials? This will disconnect the bridge from MQTT.")) {
    return;
  }
  
  fetch("/mqtt/clear", {
    method: "POST",
    headers: { "Content-Type": "application/json" }
  })
  .then(res => res.json())
  .then(data => {
    if (data.success) {
      showToast("MQTT credentials cleared successfully", 'success');
      document.getElementById("mqtt-config-form").reset();
      loadMqttStatus();
    } else {
      showMqttError("Failed to clear credentials");
    }
  })
  .catch(err => {
    console.error('Failed to clear credentials:', err);
    showMqttError("Network error clearing credentials");
  });
}

function testConnection() {
  const form = document.getElementById("mqtt-config-form");
  const formData = new FormData(form);
  
  const config = {
    broker_host: formData.get("broker_host"),
    broker_port: parseInt(formData.get("broker_port")),
    username: formData.get("username"),
    password: formData.get("password"),
    use_ssl: formData.has("use_ssl")
  };
  
  // Basic validation
  if (!config.broker_host || !config.username || !config.password) {
    showMqttError("Please fill in all required fields");
    return;
  }
  
  const button = event.target;
  const originalText = button.innerHTML;
  button.disabled = true;
  button.innerHTML = '<span class="icon">⏳</span> Testing...';
  
  // Note: This would require a separate test endpoint
  // For now, we'll just save and see if it connects
  fetch("/mqtt/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(config)
  })
  .then(res => res.json())
  .then(data => {
    if (data.success) {
      showToast("Configuration saved. Check connection status above.", 'success');
      loadMqttStatus();
    } else {
      showMqttError(data.error || "Failed to save configuration");
    }
  })
  .catch(err => {
    console.error('Failed to test connection:', err);
    showMqttError("Network error testing connection");
  })
  .finally(() => {
    button.disabled = false;
    button.innerHTML = originalText;
  });
}

// Main initialization
document.addEventListener("DOMContentLoaded", function () {
  // Reset WebSocket state on page load
  currentWebSocket = null;
  isReconnecting = false;
  if (reconnectTimer) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
  
  // Initialize theme
  initializeTheme();

  // Restore log dock collapsed/expanded state
  initLogDock();

  // Initialize navigation
  initNavigation();
  
  // Initialize firmware upload
  initFirmwareUpload();
  
  // Start WebSocket connection for logs
  startLogSocket();
  
  // Load system information
  loadSystemInfo();
  
  // Load WiFi information
  loadWifiInfo();
  
  // Load MQTT status
  loadMqttStatus();
  
  // Load auto-provisioning state
  loadAutoProvisioningState();
  
  // Refresh system info every 5 seconds for real-time uptime display
  setInterval(loadSystemInfo, 5000);
  
  // Refresh WiFi info every 15 seconds
  setInterval(loadWifiInfo, 15000);
  
  // Refresh MQTT status every 10 seconds
  setInterval(loadMqttStatus, 10000);
  
  // Load nodes data
  loadNodes();

  // Load console commands
  fetch("/api/console_commands")
    .then(res => res.json())
    .then(data => {
      const container = document.getElementById("console-commands");
      container.innerHTML = '';
      
      if (data && data.length > 0) {
        data.forEach(cmd => {
          container.appendChild(createCommandElement(cmd));
        });
      }
    })
    .catch(err => {
      console.error('Failed to load commands:', err);
    });
});

// Event handlers
document.addEventListener("click", function (e) {
  // Handle node name editing
  if (e.target.classList.contains("node-name") && e.target.classList.contains("editable")) {
    startEditingNodeName(e.target);
  }
  
  // Handle accept button
  if (e.target.classList.contains("accept-btn")) {
    const input = e.target.closest('.node-name-container').querySelector('.node-name-input');
    acceptNameEdit(input);
  }
  
  // Handle discard button
  if (e.target.classList.contains("discard-btn")) {
    const input = e.target.closest('.node-name-container').querySelector('.node-name-input');
    discardNameEdit(input);
  }
});

// MQTT form submission handler
document.getElementById("mqtt-config-form").addEventListener("submit", function(e) {
  e.preventDefault();
  
  const formData = new FormData(e.target);
  const config = {
    broker_host: formData.get("broker_host"),
    broker_port: parseInt(formData.get("broker_port")),
    username: formData.get("username"),
    password: formData.get("password"),
    use_ssl: formData.has("use_ssl")
  };
  
  // Basic validation
  if (!config.broker_host || !config.username || !config.password) {
    showMqttError("Please fill in all required fields");
    return;
  }
  
  const submitBtn = e.target.querySelector('button[type="submit"]');
  const originalText = submitBtn.innerHTML;
  submitBtn.disabled = true;
  submitBtn.innerHTML = '<span class="icon">⏳</span> Saving...';
  
  fetch("/mqtt/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(config)
  })
  .then(res => res.json())
  .then(data => {
    if (data.success) {
      showToast("MQTT configuration saved successfully", 'success');
      loadMqttStatus();
    } else {
      showMqttError(data.error || "Failed to save configuration");
    }
  })
  .catch(err => {
    console.error('Failed to save MQTT config:', err);
    showMqttError("Network error saving configuration");
  })
  .finally(() => {
    submitBtn.disabled = false;
    submitBtn.innerHTML = originalText;
  });
});

// Firmware Upload Functionality
let selectedFirmwareFile = null;
let uploadInProgress = false;
let currentUploadXhr = null;

// Per-update-type metadata: expected file, size bounds, human copy.
const UPDATE_TYPES = {
  bundle: {
    url: '/api/ota/upload_bundle',
    file: 'update_bundle.bin',
    min: 200 * 1024, max: 2.4 * 1024 * 1024, magic: 'B2MU',
    btn: 'Update firmware + web',
    hint: 'One file from tools/make_update_bundle.py — writes the app then the web assets and reboots once.'
  },
  firmware: {
    url: '/api/ota/upload',
    file: 'BleMesh2Mqtt.bin',
    min: 32 * 1024, max: 2 * 1024 * 1024,
    btn: 'Update firmware',
    hint: 'Application code only. The device reboots into the new build; web interface is left as-is.'
  },
  storage: {
    url: '/api/storage/upload',
    file: 'storage.bin',
    min: 512, max: 256 * 1024,
    btn: 'Update web interface',
    hint: 'Dashboard assets only (LittleFS image). Takes effect on next page load, no reboot.'
  }
};

function initFirmwareUpload() {
  const fileInput = document.getElementById('firmware-file');
  const uploadArea = document.getElementById('upload-area');

  document.querySelectorAll('input[name="update-type"]').forEach(radio => {
    radio.addEventListener('change', updateUploadUI);
  });

  fileInput.addEventListener('change', e => {
    if (e.target.files.length > 0) handleFileSelection(e.target.files[0]);
  });

  uploadArea.addEventListener('click', e => {
    if (e.target.tagName !== 'BUTTON') fileInput.click();
  });
  uploadArea.addEventListener('dragover', e => { e.preventDefault(); uploadArea.classList.add('dragover'); });
  uploadArea.addEventListener('dragleave', e => { e.preventDefault(); uploadArea.classList.remove('dragover'); });
  uploadArea.addEventListener('drop', e => {
    e.preventDefault();
    uploadArea.classList.remove('dragover');
    if (e.dataTransfer.files.length > 0) handleFileSelection(e.dataTransfer.files[0]);
  });

  updateUploadUI();
  loadFirmwareInfo();
}

function handleFileSelection(file) {
  const type = getSelectedUpdateType();
  const spec = UPDATE_TYPES[type];
  hideFirmwareError();

  if (!file.name.toLowerCase().endsWith('.bin')) {
    showFirmwareError(`Expected a .bin file (${spec.file}).`);
    return;
  }
  if (file.size < spec.min || file.size > spec.max) {
    showFirmwareError(`${file.name} is ${formatFileSize(file.size)} — outside the expected range for "${spec.file}".`);
    return;
  }

  selectedFirmwareFile = file;
  document.getElementById('file-name').textContent = file.name;
  document.getElementById('file-size').textContent = formatFileSize(file.size);
  document.getElementById('file-info').hidden = false;
  document.getElementById('upload-area').hidden = true;
  document.getElementById('upload-btn').disabled = false;

  // For the combined bundle, sanity-check the 4-byte magic so a wrong file is
  // caught before it hits the device.
  if (spec.magic) {
    file.slice(0, 4).arrayBuffer()
      .then(buf => {
        const sig = String.fromCharCode(...new Uint8Array(buf));
        if (sig !== spec.magic) {
          showFirmwareError('That file is not an update bundle (bad signature). Build it with tools/make_update_bundle.py.');
          clearFile();
        }
      })
      .catch(() => {});
  }
}

function clearFile() {
  selectedFirmwareFile = null;
  document.getElementById('file-info').hidden = true;
  document.getElementById('upload-area').hidden = false;
  document.getElementById('upload-btn').disabled = true;
  document.getElementById('firmware-file').value = '';
}

function getSelectedUpdateType() {
  const radio = document.querySelector('input[name="update-type"]:checked');
  return radio && UPDATE_TYPES[radio.value] ? radio.value : 'bundle';
}

function updateUploadUI() {
  const spec = UPDATE_TYPES[getSelectedUpdateType()];
  document.getElementById('update-type-hint').textContent = spec.hint;
  document.getElementById('upload-text').innerHTML =
    `<strong>Drop <code>${spec.file}</code></strong> or click to choose`;
  document.getElementById('upload-btn-text').textContent = spec.btn;
  clearFile();
  hideFirmwareError();
}

function uploadFirmware() {
  if (!selectedFirmwareFile || uploadInProgress) return;

  if (!otaApiKey) {
    showFirmwareError('OTA API key not loaded — refresh the page and try again.');
    return;
  }

  const spec = UPDATE_TYPES[getSelectedUpdateType()];
  uploadInProgress = true;

  document.getElementById('upload-progress').hidden = false;
  document.getElementById('upload-btn').hidden = true;
  document.getElementById('cancel-btn').hidden = false;
  document.getElementById('upload-area').classList.add('disabled');
  hideFirmwareError();
  updateProgress('Starting…', 0);

  const xhr = new XMLHttpRequest();
  currentUploadXhr = xhr;

  xhr.upload.addEventListener('progress', e => {
    if (e.lengthComputable) {
      const percent = Math.round((e.loaded / e.total) * 100);
      updateProgress(`Uploading ${formatFileSize(e.loaded)} / ${formatFileSize(e.total)}`, percent);
    }
  });

  xhr.addEventListener('load', () => {
    let ok = false, msg = '';
    try {
      const response = JSON.parse(xhr.responseText);
      ok = xhr.status === 200 && response.success;
      msg = response.message || '';
    } catch (e) {
      msg = 'Invalid server response';
    }
    if (ok) {
      updateProgress('Done — device restarting…', 100);
      showToast(msg || 'Update applied. Device will restart.', 'success');
      document.getElementById('cancel-btn').hidden = true;
      setTimeout(resetUploadUI, 5000);
    } else {
      showFirmwareError(msg || `Upload failed (HTTP ${xhr.status})`);
      resetUploadUI();
    }
  });

  xhr.addEventListener('error', () => {
    showFirmwareError('Network error during upload');
    resetUploadUI();
  });
  xhr.addEventListener('abort', () => {
    showFirmwareError('Upload cancelled');
    resetUploadUI();
  });

  xhr.open('POST', spec.url);
  xhr.setRequestHeader('X-OTA-Key', otaApiKey);
  xhr.send(selectedFirmwareFile);
}

function cancelUpload() {
  if (uploadInProgress && currentUploadXhr) {
    // Cancel the XMLHttpRequest
    currentUploadXhr.abort();
    currentUploadXhr = null;
    
    // Abort the backend operation
    ota_manager_abort();
    
    showFirmwareError('Upload cancelled by user');
  }
  resetUploadUI();
}

function updateProgress(message, percent) {
  document.getElementById('progress-text').textContent = message;
  document.getElementById('progress-percent').textContent = `${percent}%`;
  document.getElementById('progress-fill').style.width = `${percent}%`;
  
  const details = document.getElementById('progress-details');
  if (selectedFirmwareFile) {
    details.textContent = `File: ${selectedFirmwareFile.name} (${formatFileSize(selectedFirmwareFile.size)})`;
  }
}

function resetUploadUI() {
  uploadInProgress = false;
  currentUploadXhr = null;
  document.getElementById('upload-progress').hidden = true;
  document.getElementById('upload-btn').hidden = false;
  document.getElementById('cancel-btn').hidden = true;
  document.getElementById('upload-area').classList.remove('disabled');
  clearFile();
}

function loadFirmwareInfo() {
  // Load current firmware version from system info
  fetch('/api/system_info')
    .then(res => res.json())
    .then(data => {
      // Display version from system info
      const version = data.version ? `v${data.version}` : 'Unknown';
      document.getElementById('current-version').textContent = version;
    })
    .catch(err => {
      console.error('Failed to load firmware info:', err);
      document.getElementById('current-version').textContent = 'Unknown';
    });

  // Load OTA API key
  fetch('/api/ota/status')
    .then(res => res.json())
    .then(data => {
      if (data.api_key) {
        otaApiKey = data.api_key; // Store the key globally
        document.getElementById('ota-api-key').textContent = data.api_key;
      }
    })
    .catch(err => {
      console.error('Failed to load OTA API key:', err);
      document.getElementById('ota-api-key').textContent = 'Error loading key';
    });
}

function copyOtaKey() {
  const key = document.getElementById('ota-api-key').textContent.trim();
  if (!key || key === '…' || key.length < 8) return;
  navigator.clipboard.writeText(key)
    .then(() => showToast('OTA API key copied', 'success'))
    .catch(() => showToast('Could not copy — select the key manually', 'warning'));
}

function showFirmwareError(message) {
  const errorDiv = document.getElementById('firmware-error');
  errorDiv.textContent = message;
  errorDiv.hidden = false;
}

function hideFirmwareError() {
  document.getElementById('firmware-error').hidden = true;
}

function formatFileSize(bytes) {
  if (bytes === 0) return '0 Bytes';
  const k = 1024;
  const sizes = ['Bytes', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

// Firmware upload initialization moved to main DOMContentLoaded handler above

// Theme Toggle Functions
// The dashboard is dark by default (bare :root). Light mode is opt-in and
// marked with data-theme="light" on <html>.
function applyTheme(theme) {
  const html = document.documentElement;
  if (theme === 'light') {
    html.setAttribute('data-theme', 'light');
  } else {
    html.removeAttribute('data-theme');
  }
  updateThemeIcon(theme);
}

function toggleTheme() {
  const isLight = document.documentElement.getAttribute('data-theme') === 'light';
  const newTheme = isLight ? 'dark' : 'light';
  applyTheme(newTheme);
  localStorage.setItem('theme', newTheme);
}

function updateThemeIcon(theme) {
  const icon = document.getElementById('theme-icon');
  if (icon) {
    icon.textContent = theme === 'light' ? '☀️' : '🌙';
  }
}

function initializeTheme() {
  applyTheme(localStorage.getItem('theme') || 'dark');
}

// WiFi Reset Function
function resetWifi() {
  if (confirm('Are you sure you want to reset WiFi credentials? The device will restart and enter configuration mode.')) {
    // Show loading state
    const button = event.target.closest('button');
    const originalText = button.innerHTML;
    button.innerHTML = '<span class="icon">⏳</span> Resetting...';
    button.disabled = true;
    
    fetch('/api/reset_wifi', {
      method: 'POST'
    })
    .then(response => {
      if (response.ok) {
        alert('WiFi credentials have been reset. The device will restart in configuration mode.');
      } else {
        throw new Error('Failed to reset WiFi');
      }
    })
    .catch(error => {
      console.error('Error resetting WiFi:', error);
      alert('Failed to reset WiFi credentials. Please try again.');
      
      // Restore button state
      button.innerHTML = originalText;
      button.disabled = false;
    });
  }
}

// Auto-provisioning functions
function loadAutoProvisioningState() {
  fetch('/api/auto_provisioning')
    .then(response => response.json())
    .then(data => {
      const toggle = document.getElementById('auto-provisioning-toggle');
      if (toggle) {
        toggle.checked = data.enable_auto_provisioning;
      }
    })
    .catch(error => {
      console.error('Error loading auto-provisioning state:', error);
    });
}

function toggleAutoProvisioning(enabled) {
  const toggle = document.getElementById('auto-provisioning-toggle');
  const originalState = toggle.checked;
  
  // Temporarily disable the toggle to prevent multiple requests
  toggle.disabled = true;
  
  fetch('/api/auto_provisioning', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({
      enable_auto_provisioning: enabled
    })
  })
  .then(response => {
    if (response.ok) {
      return response.json();
    } else {
      throw new Error('Failed to update auto-provisioning setting');
    }
  })
  .then(data => {
    // Update the toggle to reflect the actual state
    toggle.checked = data.enable_auto_provisioning;
    
    const statusMessage = enabled 
      ? 'Auto-provisioning enabled - new BLE Mesh devices will be automatically provisioned'
      : 'Auto-provisioning disabled - devices must be manually provisioned';
    
    showToast(statusMessage, 'success');
  })
  .catch(error => {
    console.error('Error updating auto-provisioning:', error);
    
    // Restore the original state
    toggle.checked = originalState;
    
    showToast('Failed to update auto-provisioning setting', 'error');
  })
  .finally(() => {
    toggle.disabled = false;
  });
}
