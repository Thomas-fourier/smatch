FROM debian:trixie

COPY . /smatch

RUN apt-get update && apt-get -y upgrade
RUN apt-get -y install make gcc g++ llvm-dev perl musl-dev libxml2-dev \
	libsqlite3-dev libglib2.0-dev libssl-dev gtk+3.0-dev bison flex \
	bc libelf-dev git

RUN cd /smatch; make clean; make -j $NPROC
ENV PATH="$PATH:/smatch:/smatch/smatch_scripts"


WORKDIR /data/linux


