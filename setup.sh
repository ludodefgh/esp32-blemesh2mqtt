#!/bin/bash

# ESP-IDF Sengled B11N1E Prototype Setup Script
# This script sets up the development environment with our custom ESP-IDF

set -e

echo "🚀 Setting up ESP-IDF Sengled B11N1E Prototype development environment..."

# Check if we're in the right directory
if [ ! -f "main/main.cpp" ]; then
    echo "❌ Error: Please run this script from the project root directory"
    exit 1
fi

# Check if submodules are initialized
if [ ! -f "esp-idf/README.md" ]; then
    echo "📦 Initializing git submodules..."
    git submodule update --init --recursive
fi

# Install ESP-IDF dependencies
echo "📋 Installing ESP-IDF dependencies..."
cd esp-idf
./install.sh

# Go back to project root
cd ..

echo "✅ Setup complete!"
echo ""
echo "📖 To use this environment, run the following in each new terminal:"
echo "   source esp-idf/export.sh"
echo ""
echo "🔨 Then you can build and flash:"
echo "   idf.py build"
echo "   idf.py -p /dev/ttyUSB0 flash monitor"
echo ""
echo "🌐 For more information, see the README.md file"