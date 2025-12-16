ARG DOCKER_TAG=release-v5.5
FROM espressif/idf:${DOCKER_TAG}

ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8

# remove official ESP-IDF
RUN rm -rf /opt/esp/idf

# Clone forked ESP-IDF repository
RUN git clone --recursive -b ble-mesh-fixes https://github.com/ludodefgh/esp-idf.git /opt/esp/idf

# Install ESP-IDF tools (compilers, debuggers, etc.)
RUN /opt/esp/python_env/idf5.5_py3.12_env/bin/python /opt/esp/idf/tools/idf_tools.py install

RUN apt-get update -y && apt-get install udev nodejs npm -y

RUN echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> ~/.bashrc

ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]

CMD ["/bin/bash", "-c"]