FROM ubuntu:20.04

COPY . /src

WORKDIR /src

ENV CC gcc
ENV CONTAINER ubuntu:20.04
ENV BUILDTYPE release
ENV RUSTPROFILE minimal

RUN ci/container_scripts/install_deps.sh

RUN ci/container_scripts/install_extra_deps.sh

ENV PATH "/root/.cargo/bin:${PATH}"
RUN ci/container_scripts/build_and_install.sh

ENV PATH="/root/.shadow/bin:${PATH}"
ENTRYPOINT shadow
