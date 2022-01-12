FROM ubuntu:14.04

RUN apt-get update
RUN apt-get install --no-install-recommends -y build-essential python2.7 python2.7-dev python2.7-dbg libdb5.3-dev mysql-client libmysqlclient-dev gdb mercurial
WORKDIR /asss
COPY . .
RUN sed 's@^\(PYTHON :=\).*$@\1 /usr/bin/python2.7@' src/system.mk.trusty.dist > src/system.mk
RUN cd src && make

WORKDIR /zone
RUN cp -ar /asss/dist/* ./
RUN mkdir bin
RUN chmod 600 scrty scrty1
RUN cp /asss/build/asss /asss/src/core/backtrace /asss/build/dbtool \
/asss/so/enc_cont_1.6.0_libc2.11.1_64bit.so \
/asss/build/database.so /asss/build/funky.so /asss/build/peer.so /asss/build/pymod.so /asss/build/scoring.so /asss/build/turf.so \
/asss/src/py/exceptlogging.py /asss/src/py/exec.py /asss/src/py/fg_turf.py /asss/src/py/fg_wz.py /asss/src/py/fm_password.py \
/asss/src/pymod/optparser.py /asss/src/py/watchgreen.py ./bin/
RUN cd bin && ln -s enc_cont_1.6.0_libc2.11.1_64bit.so enc_cont.so

EXPOSE 5000

CMD bin/asss
