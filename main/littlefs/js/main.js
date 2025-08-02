function onSliderInput(uuid, el) {
  el.nextElementSibling.value = el.value;
  throttleSendLightness(uuid, el.value);
}

let throttleTimeout = null;
let lastSend = 0;
let pending = {};

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
  fetch("/set_lightness", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}&lightness=${encodeURIComponent(value)}`
  });
}

function unprovision(uuid) {
  fetch("/unprovision", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}`
  }).then(() => location.reload());
}

function sendMqttStatus(uuid) {
  fetch("/send_mqtt_status", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}`
  });
}

function sendMqttDiscovery(uuid) {
  fetch("/send_mqtt_discovery", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}`
  });
}

function provision(uuid) {
  fetch("/provision", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}`
  }).then(() => location.reload());
}

function sendBridgeMqttDiscovery() {
  fetch("/send_bridge_mqtt_discovery", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: ""
  });
}

function sendBridgeMqttStatus() {
  fetch("/send_bridge_mqtt_status", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: ""
  });
}

function restartBridge() {
  if (confirm("Are you sure you want to restart the ESP32? This will disconnect all clients.")) {
    fetch("/restart_bridge", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: ""
    });
  }
}

function resetWiFi() {
  // Use a longer, more specific message to avoid browser caching
  const message = "⚠️ RESET WiFi SETTINGS ⚠️\n\n" +
                  "This action will:\n" +
                  "• Clear all stored WiFi credentials\n" +
                  "• Restart the ESP32 device\n" +
                  "• Enter captive portal setup mode\n" +
                  "• Require you to reconfigure WiFi access\n\n" +
                  "Are you sure you want to proceed?";
                  
  if (confirm(message)) {
    fetch("/reset_wifi?" + Date.now(), {  // Add timestamp to prevent caching
      method: "POST",
      headers: { 
        "Content-Type": "application/x-www-form-urlencoded",
        "Cache-Control": "no-cache"
      },
      body: ""
    }).catch(err => {
      // Expected - device will restart and disconnect
      console.log("Reset request sent, device restarting...");
    });
  }
}

document.addEventListener("DOMContentLoaded", function () {
  fetch("/nodes.json")
    .then(res => res.json())
    .then(data => {
      const nodesContainer = document.getElementById("nodes");
      data.provisioned.forEach(node => {
      const el = document.createElement("div");
      el.className = "node";
      el.dataset.uuid = node.uuid;
      el.innerHTML = `
        <strong class="node-name">${node.name}</strong><br>
        <input type="text" class="name-input" placeholder="New name">
        <button class="rename-btn">Rename</button><br>
        <input type="range" min="0" max="65535" step="500" value="0"
          oninput="onSliderInput('${node.uuid}', this)">
        <output>0</output><br>
        <strong>UUID:</strong> ${node.uuid}<br>
        <strong>Address:</strong> ${node.unicast}<br>
        <button onclick="unprovision('${node.uuid}')">Unprovision</button>
        <button onclick="sendMqttStatus('${node.uuid}')">Send MQTT Status</button>
        <button onclick="sendMqttDiscovery('${node.uuid}')">Send MQTT Discovery</button>
      `;
      nodesContainer.appendChild(el);
    });

      const unprovContainer = document.getElementById("unprovisioned");
      data.unprovisioned.forEach(dev => {
        const el = document.createElement("div");
        el.className = "node";
        el.innerHTML = `
          <strong>UUID:</strong> ${dev.uuid}<br>
          <strong>RSSI:</strong> ${dev.rssi}<br>
          <button onclick="provision('${dev.uuid}')">Provision</button>
        `;
        unprovContainer.appendChild(el);
      });
    });

    fetch("/api/console_commands")
  .then(res => res.json())
  .then(data => {
    const container = document.getElementById("console-commands");
    data.forEach(cmd => {
      const div = document.createElement("div");
      div.innerHTML = `<strong>${cmd.name}</strong>: ${cmd.help}`;
      container.appendChild(div);
    });
  });
});

document.addEventListener("click", function (e) {
  if (e.target.classList.contains("rename-btn")) {
    const nodeEl = e.target.closest(".node");
    const uuid = nodeEl.dataset.uuid;
    const newName = nodeEl.querySelector(".name-input").value;

    fetch("/api/rename_node", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ uuid, name: newName })
    }).then(res => {
      if (res.ok) {
        nodeEl.querySelector(".node-name").textContent = newName;
      } else {
        alert("Failed to rename node.");
      }
    });
  }
});
// You can also dynamically load node info from /nodes.json here (future upgrade)
