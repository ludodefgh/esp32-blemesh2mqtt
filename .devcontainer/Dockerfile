FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Install system dependencies
RUN apt-get update && apt-get install -y \
    apt-utils \
    bison \
    bzip2 \
    ca-certificates \
    ccache \
    check \
    curl \
    flex \
    git \
    git-lfs \
    gperf \
    lcov \
    libbsd-dev \
    libffi-dev \
    libglib2.0-0 \
    libncurses-dev \
    libpixman-1-0 \
    libsdl2-2.0-0 \
    libslirp0 \
    libusb-1.0-0-dev \
    make \
    ninja-build \
    python3 \
    python3-pip \
    python3-venv \
    ruby \
    unzip \
    wget \
    xz-utils \
    zip \
    sudo \
    udev \
    && apt-get autoremove -y \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/python python /usr/bin/python3 10

# Create a non-root user for development
ARG USERNAME=esp
ARG USER_UID=1001
ARG USER_GID=1001

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME

# Set up ESP-IDF environment variables
ENV IDF_TOOLS_PATH=/opt/esp
ENV IDF_PYTHON_ENV_PATH=/opt/esp/python_env

# Create ESP tools directory and set permissions
RUN mkdir -p $IDF_TOOLS_PATH && chown -R $USERNAME:$USERNAME $IDF_TOOLS_PATH

# Switch to non-root user
USER $USERNAME

# Set working directory
WORKDIR /workspace

# Enable ccache by default
ENV IDF_CCACHE_ENABLE=1

# Disable Python constraint checking since we'll install in container
ENV IDF_PYTHON_CHECK_CONSTRAINTS=no

# Add helper aliases
RUN echo 'alias get_idf=". /workspace/esp-idf/export.sh"' >> /home/$USERNAME/.bashrc \
    && echo 'alias idf="idf.py"' >> /home/$USERNAME/.bashrc \
    && echo 'alias monitor="idf.py monitor"' >> /home/$USERNAME/.bashrc

CMD ["/bin/bash"]
