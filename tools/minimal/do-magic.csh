#!/bin/csh

setenv TARGET amd64
setenv MAKEOBJDIRPREFIX /tmp/${TARGET}-objdir
setenv DESTDIR /tmp/${TARGET}-minimal

./tools/minimal/build_minimal_world.csh
./tools/minimal/distribute_minimal_world.csh
./tools/minimal/make-minimal-memstick.sh ${DESTDIR} /tmp.minimal.disk
