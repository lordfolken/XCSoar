FROM debian:bullseye

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y python3 vim ccache python3-pkgconfig gdb && apt-get clean
COPY ./provisioning/install-debian-packages.sh /root/install-debian-packages.sh
RUN /root/install-debian-packages.sh && apt-get clean
COPY ./provisioning/install-android-tools.sh /root/install-android-tools.sh
RUN /root/install-android-tools.sh && apt-get clean

COPY ./docker/bin/xcsoar-compile ./docker/bin/xcsoar-test /usr/local/bin/

WORKDIR /opt/xcsoar
COPY ./docker/README.md /opt/xcsoar/
