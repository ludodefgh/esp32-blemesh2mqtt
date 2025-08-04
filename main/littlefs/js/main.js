// Global state
let throttleTimeout = null;
let lastSend = 0;
let pending = {};
let logAutoScroll = true;

// Utility functions
function showToast(message, type = 'info') {
  console.log(`[${type.toUpperCase()}] ${message}`);
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
    
    fetch("/bridge/restart", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: ""
    }).then(() => {
      showToast('Bridge restart initiated', 'info');
      setTimeout(() => {
        window.location.reload();
      }, 5000);
    }).catch(err => {
      showToast('Bridge restarting...', 'info');
      setTimeout(() => {
        window.location.reload();
      }, 5000);
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
      <span class="node-name">${node.name}</span>
      <span class="node-status ${node.unicast ? 'online' : 'offline'}">
        ${node.unicast ? 'Online' : 'Offline'}
      </span>
    </div>
    
    <div class="node-info">
      <div class="info-item">
        <span class="info-label">UUID</span>
        <span class="info-value">${node.uuid}</span>
      </div>
    </div>
    <div class="node-info">
      <div class="info-item">
        <span class="info-label">Address</span>
        <span class="info-value">${node.unicast || 'Not assigned'}</span>
      </div>
    </div>
    
    <div class="rename-section">
      <input type="text" class="name-input" placeholder="New name" value="">
      <button class="btn btn-secondary btn-small rename-btn">Rename</button>
    </div>
    
    <div class="lightness-control">
      <span>💡</span>
      <input type="range" min="0" max="65535" step="500" value="0"
        onchange="onSliderInput('${node.uuid}', this)">
      <output>0</output>
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

function startLogSocket() {
  if (isReconnecting) return;
  
  const ws = new WebSocket("ws://" + location.host + "/ws/logs");
  const logOutput = document.getElementById("log-output");
  const autoScrollCheckbox = document.getElementById("auto-scroll");

  ws.onopen = () => {
    console.log("WebSocket connected");
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
    console.log("WebSocket closed:", event.code, event.reason);
    if (!isReconnecting) {
      isReconnecting = true;
      reconnectTimer = setTimeout(() => {
        startLogSocket();
      }, 3000); // Wait 3 seconds before reconnecting
    }
  };

  ws.onerror = (error) => {
    console.error("WebSocket error:", error);
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

// System info functions
function loadSystemInfo() {
  fetch("/api/system_info")
    .then(res => res.json())
    .then(data => {
      if (data.memory) {
        const freeMemoryKB = Math.round(data.memory.free / 1024);
        const totalMemoryKB = Math.round(data.memory.total / 1024);
        
        const freeMemoryEl = document.getElementById("free-memory");
        const totalMemoryEl = document.getElementById("total-memory");
        
        if (freeMemoryEl) freeMemoryEl.textContent = `${freeMemoryKB} KB`;
        if (totalMemoryEl) totalMemoryEl.textContent = `${totalMemoryKB} KB`;
      }
    })
    .catch(err => {
      console.error('Failed to load system info:', err);
    });
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
        document.getElementById("use-ssl").checked = data.use_ssl || false;
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
  // Start WebSocket connection for logs
  startLogSocket();
  
  // Load system information
  loadSystemInfo();
  
  // Load MQTT status
  loadMqttStatus();
  
  // Refresh system info every 30 seconds
  setInterval(loadSystemInfo, 30000);
  
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
  if (e.target.classList.contains("rename-btn")) {
    const nodeEl = e.target.closest(".node");
    const uuid = nodeEl.dataset.uuid;
    const nameInput = nodeEl.querySelector(".name-input");
    const newName = nameInput.value.trim();
    
    if (!newName) {
      showToast('Please enter a name', 'warning');
      return;
    }

    const button = e.target;
    button.disabled = true;
    button.textContent = 'Renaming...';

    fetch("/node/rename", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ uuid, name: newName })
    }).then(res => {
      if (res.ok) {
        nodeEl.querySelector(".node-name").textContent = newName;
        nameInput.value = '';
        showToast('Node renamed successfully', 'success');
      } else {
        showToast('Failed to rename node', 'error');
      }
    }).catch(err => {
      showToast('Network error renaming node', 'error');
    }).finally(() => {
      button.disabled = false;
      button.textContent = 'Rename';
    });
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

function initFirmwareUpload() {
  const fileInput = document.getElementById('firmware-file');
  const uploadArea = document.getElementById('upload-area');
  
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
      if (file.name.endsWith('.bin')) {
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
  if (!file.name.endsWith('.bin')) {
    showFirmwareError('Please select a .bin firmware file');
    return;
  }
  
  if (file.size < 1024) {
    showFirmwareError('Firmware file appears to be too small');
    return;
  }
  
  if (file.size > 4 * 1024 * 1024) {
    showFirmwareError('Firmware file is too large (max 4MB)');
    return;
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

function uploadFirmware() {
  if (!selectedFirmwareFile || uploadInProgress) {
    return;
  }
  
  uploadInProgress = true;
  
  // Show progress UI
  document.getElementById('upload-progress').style.display = 'block';
  document.getElementById('upload-btn').style.display = 'none';
  document.getElementById('cancel-btn').style.display = 'inline-flex';
  document.getElementById('upload-area').classList.add('disabled');
  
  // Update progress
  updateProgress('Starting upload...', 0);
  
  // Create FormData and upload
  const formData = new FormData();
  formData.append('firmware', selectedFirmwareFile);
  
  const xhr = new XMLHttpRequest();
  
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
  
  xhr.open('POST', '/api/ota/upload');
  // Add authentication header
  xhr.setRequestHeader('X-OTA-Key', 'ota_secure_key_2024');
  xhr.send(selectedFirmwareFile);
}

function cancelUpload() {
  if (uploadInProgress) {
    // This would cancel the XMLHttpRequest if we stored it globally
    showFirmwareError('Upload cancellation not implemented');
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
      // For now, show a placeholder version
      // In a real implementation, you'd add version info to the system_info endpoint
      document.getElementById('current-version').textContent = 'v1.0.0';
    })
    .catch(err => {
      console.error('Failed to load firmware info:', err);
      document.getElementById('current-version').textContent = 'Unknown';
    });
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

// Initialize firmware upload when page loads
document.addEventListener('DOMContentLoaded', function() {
  initFirmwareUpload();
});
