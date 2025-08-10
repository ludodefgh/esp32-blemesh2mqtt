#\!/bin/bash

set -e

echo "🚀 Setting up ESP-IDF development environment in devcontainer..."

# Install ESP-IDF tools
cd /workspace
echo "📦 Installing ESP-IDF tools..."
./esp-idf/install.sh

# Source the environment to set IDF_PATH
source esp-idf/export.sh

echo "✅ ESP-IDF setup complete\!"
echo ""
echo "📖 To use this environment in new terminals, run:"
echo "   source esp-idf/export.sh"
echo ""
echo "🔨 Build and flash commands:"
echo "   idf.py build"
echo "   idf.py -p /dev/ttyUSB0 flash monitor"
echo ""
echo "🌐 Web interface files are in main/littlefs/"
echo "📡 BLE Mesh and MQTT bridge ready for development\!"

# Create a convenience script
echo '#\!/bin/bash
source /workspace/esp-idf/export.sh
exec "$@"' > /home/esp/idf-env.sh
chmod +x /home/esp/idf-env.sh

# Add automatic sourcing to bashrc
echo "" >> /home/esp/.bashrc
echo "# Auto-source ESP-IDF environment" >> /home/esp/.bashrc
echo "source /workspace/esp-idf/export.sh" >> /home/esp/.bashrc

echo "🎉 Devcontainer setup complete\!"
EOF < /dev/null
