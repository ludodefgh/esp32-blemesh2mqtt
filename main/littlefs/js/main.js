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

function provision(uuid) {
  fetch("/provision", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: `uuid=${encodeURIComponent(uuid)}`
  }).then(() => location.reload());
}

document.addEventListener("DOMContentLoaded", function () {
  fetch("/nodes.json")
    .then(res => res.json())
    .then(data => {
      const nodesContainer = document.getElementById("nodes");
      data.provisioned.forEach(node => {
        const el = document.createElement("div");
        el.className = "node";
        el.innerHTML = `
          <strong>${node.name}</strong><br>
          <input type="range" min="0" max="65535" step="500" value="0"
            oninput="onSliderInput('${node.uuid}', this)">
          <output>0</output><br>
          <button onclick="unprovision('${node.uuid}')">Unprovision</button>
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
});

// You can also dynamically load node info from /nodes.json here (future upgrade)
