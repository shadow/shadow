FROM ubuntu:18.04 AS build


COPY . /src

WORKDIR ./src

ENV CC gcc
ENV CONTAINER ubuntu:18.04
ENV BUILDTYPE debug
ENV RUSTPROFILE minimal

RUN ci/container_scripts/install_deps.sh

RUN ci/container_scripts/install_extra_deps.sh

#RUN ci/container_scripts/build_and_install.sh

#RUN ci/container_scripts/test.sh

CMD shadow --version