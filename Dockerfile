FROM ubuntu:18.04 AS build

COPY . /src

WORKDIR /src

ENV CC gcc
ENV CONTAINER ubuntu:18.04
ENV BUILDTYPE debug
ENV RUSTPROFILE minimal

RUN ci/container_scripts/install_deps.sh

RUN ci/container_scripts/install_extra_deps.sh

ENV PATH "/root/.cargo/bin:${PATH}"
RUN ci/container_scripts/build_and_install.sh

CMD /root/.shadow/bin/shadow --version
