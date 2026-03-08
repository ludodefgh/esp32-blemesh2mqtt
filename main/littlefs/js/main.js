// Global state
let throttleTimeout = null;
let lastSend = 0;
let pending = {};
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
  if (counter) {
    counter.textContent = `${count} node${count !== 1 ? 's' : ''}`;
  }
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

// Lightness control functions
function onSliderInput(uuid, el) {
  const output = el.parentElement.querySelector('output');
  if (output) {
    output.value = el.value;
  }
  throttleSendLightness(uuid, el.value);
}

function throttleSendLightness(uuid, value) {
  pending = { uuid, value };
  if (throttleTimeout) return;

  const wait = Math.max(0, 200 - (Date.now() - lastSend));
  throttleTimeout = setTimeout(() => {
    sendLightness(pending.uuid, pending.value);
    lastSend = Date.now();
    throttleTimeout = null;
  }, wait);
}

function sendLightness(uuid, value) {
  fetch("/node/set_lightness", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}&lightness=${encodeURIComponent(value)}`
  }).then(response => {
    if (response.ok) {
      showToast(`Lightness set to ${value}`, 'success');
    } else {
      showToast('Failed to set lightness', 'error');
    }
  }).catch(err => {
    showToast('Network error setting lightness', 'error');
  });
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

function refreshDevices() {
  const button = event.target;
  const originalText = button.innerHTML;
  button.disabled = true;
  button.innerHTML = '<span class="icon">⏳</span> Refreshing...';
  
  setTimeout(() => {
    location.reload();
  }, 1000);
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
    
    <div class="lightness-control">
      <span>💡</span>
      <input type="range" min="0" max="${node.max_lightness || 65535}" step="500" value="${node.hsl_l || 0}"
        onchange="onSliderInput('${node.uuid}', this)">
      <output>${node.hsl_l || 0}</output>
    </div>
    
    <div class="controls">
      <button class="btn btn-primary btn-small" onclick="sendMqttStatus('${node.uuid}')">
        <span class="icon">📊</span>
        MQTT Status
      </button>
      <button class="btn btn-primary btn-small" onclick="sendMqttDiscovery('${node.uuid}')">
        <span class="icon">📡</span>
        MQTT Discovery
      </button>
      <button class="btn btn-danger btn-small" onclick="unprovision('${node.uuid}')">
        <span class="icon">🗑️</span>
        Unprovision
      </button>
    </div>
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
  fetch("/nodes.json")
    .then(res => res.json())
    .then(data => {
      const nodesContainer = document.getElementById("nodes");
      const unprovisionedContainer = document.getElementById("unprovisioned");
      
      // Clear containers
      nodesContainer.innerHTML = '';
      unprovisionedContainer.innerHTML = '';
      
      // Render provisioned nodes
      if (data.provisioned && data.provisioned.length > 0) {
        data.provisioned.forEach(node => {
          nodesContainer.appendChild(createNodeElement(node));
        });
        toggleEmptyState('nodes', 'no-nodes', true);
      } else {
        toggleEmptyState('nodes', 'no-nodes', false);
      }
      
      // Update node count
      updateNodeCount(data.provisioned ? data.provisioned.length : 0);
      
      // Render unprovisioned devices
      if (data.unprovisioned && data.unprovisioned.length > 0) {
        data.unprovisioned.forEach(device => {
          unprovisionedContainer.appendChild(createDeviceElement(device));
        });
        toggleEmptyState('unprovisioned', 'no-devices', true);
      } else {
        toggleEmptyState('unprovisioned', 'no-devices', false);
      }
    })
    .catch(err => {
      console.error('Failed to load nodes:', err);
      showToast('Failed to load device data', 'error');
    });

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

function initFirmwareUpload() {
  const fileInput = document.getElementById('firmware-file');
  const uploadArea = document.getElementById('upload-area');
  const radioButtons = document.querySelectorAll('input[name="update-type"]');
  
  // Update UI when upload type changes
  radioButtons.forEach(radio => {
    radio.addEventListener('change', function() {
      updateUploadUI();
    });
  });
  
  // File input change handler
  fileInput.addEventListener('change', function(e) {
    if (e.target.files.length > 0) {
      handleFileSelection(e.target.files[0]);
    }
  });
  
  // Drag and drop handlers
  uploadArea.addEventListener('dragover', function(e) {
    e.preventDefault();
    uploadArea.classList.add('dragover');
  });
  
  uploadArea.addEventListener('dragleave', function(e) {
    e.preventDefault();
    uploadArea.classList.remove('dragover');
  });
  
  uploadArea.addEventListener('drop', function(e) {
    e.preventDefault();
    uploadArea.classList.remove('dragover');
    
    if (e.dataTransfer.files.length > 0) {
      const file = e.dataTransfer.files[0];
      const updateType = getSelectedUpdateType();
      const expectedExtension = updateType === 'storage' ? '.bin' : '.bin';
      
      if (file.name.endsWith(expectedExtension)) {
        handleFileSelection(file);
      } else {
        showFirmwareError('Please select a .bin firmware file');
      }
    }
  });
  
  // Load current firmware version
  loadFirmwareInfo();
}

function handleFileSelection(file) {
  const updateType = getSelectedUpdateType();
  
  if (!file.name.endsWith('.bin')) {
    const fileType = updateType === 'storage' ? 'storage' : 'firmware';
    showFirmwareError(`Please select a .bin ${fileType} file`);
    return;
  }
  
  // Validate file based on update type
  if (updateType === 'storage') {
    if (file.size < 512) {
      showFirmwareError('Storage file appears to be too small');
      return;
    }
    
    if (file.size > 256 * 1024) { // 256KB max for storage partition
      showFirmwareError('Storage file is too large (max 256KB)');
      return;
    }
    
    // Validate storage file name
    if (!file.name.toLowerCase().includes('storage')) {
      showFirmwareError('Storage file should be named "storage.bin"');
      return;
    }
  } else {
    // Firmware validation
    if (file.size < 1024) {
      showFirmwareError('Firmware file appears to be too small');
      return;
    }
    
    if (file.size > 4 * 1024 * 1024) {
      showFirmwareError('Firmware file is too large (max 4MB)');
      return;
    }
  }
  
  selectedFirmwareFile = file;
  
  // Show file info
  document.getElementById('file-name').textContent = file.name;
  document.getElementById('file-size').textContent = formatFileSize(file.size);
  document.getElementById('file-info').style.display = 'block';
  document.getElementById('upload-btn').disabled = false;
  
  // Hide upload area
  document.getElementById('upload-area').style.display = 'none';
  
  hideFirmwareError();
}

function clearFile() {
  selectedFirmwareFile = null;
  document.getElementById('file-info').style.display = 'none';
  document.getElementById('upload-area').style.display = 'block';
  document.getElementById('upload-btn').disabled = true;
  document.getElementById('firmware-file').value = '';
  hideFirmwareError();
}

// Helper functions for upload type management
function getSelectedUpdateType() {
  const radio = document.querySelector('input[name="update-type"]:checked');
  return radio ? radio.value : 'firmware';
}

function updateUploadUI() {
  const updateType = getSelectedUpdateType();
  const uploadText = document.getElementById('upload-text');
  const fileInput = document.getElementById('firmware-file');
  const uploadBtnText = document.getElementById('upload-btn-text');
  
  if (updateType === 'storage') {
    uploadText.innerHTML = '<strong>Select storage.bin file</strong><br>or drag and drop here';
    fileInput.accept = '.bin';
    uploadBtnText.textContent = 'Upload Storage';
  } else {
    uploadText.innerHTML = '<strong>Select firmware file (.bin)</strong><br>or drag and drop here';
    fileInput.accept = '.bin';
    uploadBtnText.textContent = 'Upload Firmware';
  }
  
  // Clear selected file when switching types
  clearFile();
}

function uploadFirmware() {
  if (!selectedFirmwareFile || uploadInProgress) {
    return;
  }
  
  const updateType = getSelectedUpdateType();
  uploadInProgress = true;
  
  // Show progress UI
  document.getElementById('upload-progress').style.display = 'block';
  document.getElementById('upload-btn').style.display = 'none';
  document.getElementById('cancel-btn').style.display = 'inline-flex';
  document.getElementById('upload-area').classList.add('disabled');
  
  // Update progress
  const progressText = updateType === 'storage' ? 'Starting storage upload...' : 'Starting firmware upload...';
  updateProgress(progressText, 0);
  
  // Create FormData and upload
  const formData = new FormData();
  const fieldName = updateType === 'storage' ? 'storage' : 'firmware';
  formData.append(fieldName, selectedFirmwareFile);
  
  const xhr = new XMLHttpRequest();
  currentUploadXhr = xhr;
  
  xhr.upload.addEventListener('progress', function(e) {
    if (e.lengthComputable) {
      const percent = Math.round((e.loaded / e.total) * 100);
      updateProgress(`Uploading firmware... ${formatFileSize(e.loaded)} / ${formatFileSize(e.total)}`, percent);
    }
  });
  
  xhr.addEventListener('load', function() {
    if (xhr.status === 200) {
      try {
        const response = JSON.parse(xhr.responseText);
        if (response.success) {
          updateProgress('Upload successful! Device restarting...', 100);
          showToast('Firmware uploaded successfully. Device will restart.', 'success');
          
          // Hide cancel button since upload is complete
          document.getElementById('cancel-btn').style.display = 'none';
          
          // Reset UI after restart delay
          setTimeout(() => {
            resetUploadUI();
          }, 5000);
        } else {
          throw new Error(response.message || 'Upload failed');
        }
      } catch (e) {
        throw new Error('Invalid server response');
      }
    } else {
      throw new Error(`Upload failed: ${xhr.status} ${xhr.statusText}`);
    }
  });
  
  xhr.addEventListener('error', function() {
    showFirmwareError('Network error during upload');
    resetUploadUI();
  });
  
  xhr.addEventListener('abort', function() {
    showFirmwareError('Upload cancelled');
    resetUploadUI();
  });
  
  // Set upload endpoint based on update type
  const uploadUrl = updateType === 'storage' ? '/api/storage/upload' : '/api/ota/upload';
  xhr.open('POST', uploadUrl);

  // Add authentication header with the API key from the server
  if (!otaApiKey) {
    showFirmwareError('OTA API key not loaded. Please refresh the page.');
    resetUploadUI();
    return;
  }
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
  document.getElementById('upload-progress').style.display = 'none';
  document.getElementById('upload-btn').style.display = 'inline-flex';
  document.getElementById('cancel-btn').style.display = 'none';
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
  const key = document.getElementById('ota-api-key').textContent;
  if (key && key !== 'Loading...' && key !== 'Error loading key') {
    navigator.clipboard.writeText(key).then(() => {
      // Visual feedback
      const el = document.getElementById('ota-api-key');
      const originalText = el.textContent;
      el.textContent = '✓ Copied!';
      setTimeout(() => {
        el.textContent = originalText;
      }, 2000);
    }).catch(err => {
      console.error('Failed to copy:', err);
      alert('Failed to copy key. Please copy manually: ' + key);
    });
  }
}

function showFirmwareError(message) {
  const errorDiv = document.getElementById('firmware-error');
  errorDiv.textContent = message;
  errorDiv.style.display = 'block';
}

function hideFirmwareError() {
  document.getElementById('firmware-error').style.display = 'none';
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
function toggleTheme() {
  const html = document.documentElement;
  const currentTheme = html.getAttribute('data-theme');
  const newTheme = currentTheme === 'dark' ? 'light' : 'dark';
  
  // Apply theme
  if (newTheme === 'dark') {
    html.setAttribute('data-theme', 'dark');
  } else {
    html.removeAttribute('data-theme');
  }
  
  // Update icon
  updateThemeIcon(newTheme);
  
  // Save preference
  localStorage.setItem('theme', newTheme);
}

function updateThemeIcon(theme) {
  const icon = document.getElementById('theme-icon');
  if (icon) {
    icon.textContent = theme === 'dark' ? '🌙' : '☀️';
  }
}

function initializeTheme() {
  // Check for saved theme preference or default to 'light'
  const savedTheme = localStorage.getItem('theme') || 'light';
  
  // Apply saved theme
  const html = document.documentElement;
  if (savedTheme === 'dark') {
    html.setAttribute('data-theme', 'dark');
  } else {
    html.removeAttribute('data-theme');
  }
  
  // Update icon
  updateThemeIcon(savedTheme);
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
